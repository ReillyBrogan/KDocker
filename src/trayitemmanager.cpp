/*
 *  Copyright (C) 2009, 2012, 2015 John Schember <john@nachtimwald.com>
 *  Copyright (C) 2004 Girish Ramakrishnan All Rights Reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#include <QCoreApplication>
#include <QByteArray>
#include <QMessageBox>
#include <QTextStream>

#include "constants.h"
#include "trayitemmanager.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <Xlib.h>
#include "xlibutil.h"

#define  ESC_key  9

int ignoreXErrors(Display *, XErrorEvent *) {
    return 0;
}

TrayItemManager::TrayItemManager() {
    m_scanner = new Scanner(this);
    connect(m_scanner, SIGNAL(windowFound(Window, TrayItemArgs)), this, SLOT(dockWindow(Window, TrayItemArgs)));
    connect(m_scanner, SIGNAL(stopping()), this, SLOT(checkCount()));
    // 'const' TrayItemArgs initializer
    m_initArgs.iBalloonTimeout = -1;
    for (int opt=0; opt < Option_MAX; opt++) {
        m_initArgs.opt[opt] = NOARG;  // unset all
    }
    m_grabInfo.qtimer = new QTimer;
    m_grabInfo.qloop  = new QEventLoop;
    m_grabInfo.isGrabbing = false;
    connect(m_grabInfo.qtimer, SIGNAL(timeout()), m_grabInfo.qloop, SLOT(quit()));
    connect(this, SIGNAL(quitMouseGrab()),        m_grabInfo.qloop, SLOT(quit()));

    // This will prevent x errors from being written to the console.
    // The isValidWindowId function in util.cpp will generate errors if the
    // window is not valid while it is checking.
    XSetErrorHandler(ignoreXErrors);

    // Create and start the even receiver. We're using our own receiver instead
    // of subclassing QAbstractNativeEventFilter because Qt 6 doesn't forward
    // events. It worked with Qt 5 for it's unknown why it doesn't with Qt 6.
    //
    // It's suspected it doesn't work because of how the xcb connection is
    // created. While testing, `xcb_connect(Null, 0)` did not generate events,
    // however, `XGetXCBConnection` does. Qt internally will use either one
    // based on the define `QT_CONFIG(xcb_xlib)`. It's suspected that in Qt 5
    // it is using `XGetXCBConnection` and Qt 6 is being built so it will use
    // `xcb_connect`. This is just a theory and was uncovered on Ubuntu 24.04.
    m_eventReceiver = new XcbEventReciver();
    connect(m_eventReceiver, &XcbEventReciver::xcbEvent, this, &TrayItemManager::handleXcbEvent);
    connect(m_eventReceiver, &XcbEventReciver::finished, m_eventReceiver, &QObject::deleteLater);
    m_eventReceiver->start();
}

TrayItemManager::~TrayItemManager() {
    while (!m_trayItems.isEmpty()) {
        TrayItem *t = m_trayItems.takeFirst();
        delete t;
    }
    delete m_grabInfo.qtimer;
    delete m_grabInfo.qloop;
    delete m_scanner;
    // Don't delete the m_eventReceiver because it will be deleted automatically
    // from a connection once it fully stops.
    m_eventReceiver->quit();
}

/* The X11 Event Filter. Pass on events to the TrayItems that we created. */
void TrayItemManager::handleXcbEvent(void *event) {
    static xcb_window_t dockedWindow = 0;  //     zero: event ignored (default) ...
                                           // non-zero: pass to TrayItem::xcbEventFilter
    switch (static_cast<xcb_generic_event_t *>(event)-> response_type & ~0x80) {
		case XCB_FOCUS_OUT:          // -> TrayItem::xcbEventFilter
            dockedWindow = static_cast<xcb_focus_out_event_t *>(event)-> event;
			break;

		case XCB_DESTROY_NOTIFY:     // -> TrayItem::xcbEventFilter
            dockedWindow = static_cast<xcb_destroy_notify_event_t *>(event)-> window;
			break;

		case XCB_UNMAP_NOTIFY:       // -> TrayItem::xcbEventFilter
            dockedWindow = static_cast<xcb_unmap_notify_event_t *>(event)-> window;
            break;

		case XCB_MAP_NOTIFY:         // -> TrayItem::xcbEventFilter
            dockedWindow = static_cast<xcb_map_notify_event_t *>(event)-> window;
			break;

        case XCB_VISIBILITY_NOTIFY:  // -> TrayItem::xcbEventFilter
            dockedWindow = static_cast<xcb_visibility_notify_event_t *>(event)-> window;
            break;

        case XCB_PROPERTY_NOTIFY:    // -> TrayItem::xcbEventFilter
            dockedWindow = static_cast<xcb_visibility_notify_event_t *>(event)-> window;
            break;

        case XCB_BUTTON_PRESS:
            if (m_grabInfo.isGrabbing) {
                m_grabInfo.isGrabbing = false;   // Cancel immediately

                m_grabInfo.button = static_cast<xcb_button_press_event_t *>(event)-> detail;
                m_grabInfo.window = static_cast<xcb_button_press_event_t *>(event)-> child;

                emit quitMouseGrab();            // Interrupt QTimer waiting for grab
            }
            break;

        case XCB_KEY_RELEASE:
            if (m_grabInfo.isGrabbing) {
                if (static_cast<xcb_key_release_event_t *>(event)-> detail == ESC_key)
                {
                    m_grabInfo.isGrabbing = false;

                    emit quitMouseGrab();        // Interrupt QTimer waiting for grab
                }
            }
    }

    if (dockedWindow) {
        // Pass on the event to the tray item with the associated window.
        QListIterator<TrayItem*> ti(m_trayItems);
        static TrayItem *t;

        while (ti.hasNext()) {
            t = ti.next();
            if (t-> dockedWindow() == static_cast<Window>(dockedWindow)) {
                t-> xcbEventFilter(static_cast<xcb_generic_event_t *>(event), dockedWindow);
            }
        }
    }

    free(event);
}

void TrayItemManager::processCommand(const QStringList &args) {
    enum PatternType
    {
        Normal,
        Regex,
        Wildcard
    };

    int option;
    pid_t pid = 0;
    Window window = 0;
    bool checkNormality = true;
    int maxTime = 5;
    QString windowNamePattern;
    PatternType patType = PatternType::Normal;
    QRegularExpression::PatternOptions patternOptions = QRegularExpression::CaseInsensitiveOption;
    TrayItemArgs settings = m_initArgs;
    // Turn the QStringList of arguments into something getopt can use.
    QList<QByteArray> bargs;

    Q_FOREACH(QString s, args) {
        bargs.append(s.toLocal8Bit());
    }
    int argc = bargs.count();
    // Use a const char * here and a const_cast later because it is faster.
    // Using char * will cause a deep copy.
    const char *argv[argc + 1];
    for (int i = 0; i < argc; i++) {
        argv[i] = bargs[i].data();
    }

    /* Options: a, h, u, v are all handled by the KDocker class because we
     * want them to print on the tty the instance was called from.
     */
    optind = 0; // initialise the getopt static
    while ((option = getopt(argc, const_cast<char **> (argv), Constants::OPTIONSTRING)) != -1) {
        switch (option) {
            case '?':
                checkCount();
                return;
            case 'b':
                checkNormality = false;
                break;
            case 'd':
                maxTime = atoi(optarg);
                break;
            case 'e':
                if (QString::fromLocal8Bit(optarg).compare("n") == 0) {
                    patType = PatternType::Normal;
                } else if (QString::fromLocal8Bit(optarg).compare("r") == 0) {
                    patType = PatternType::Regex;
                } else if (QString::fromLocal8Bit(optarg).compare("w") == 0) {
                    patType = PatternType::Wildcard;
                } else {
                    QMessageBox::critical(0, qApp->applicationName(), tr("Invalid name matting option: %1.\n\nChoices are: %2.").arg(optarg).arg("n, r, w"));
                    checkCount();
                    return;
                }
                break;
            case 'f':
                window = XLibUtil::activeWindow(XLibUtil::display());
                if (!window) {
                    QMessageBox::critical(0, qApp->applicationName(), tr("Cannot dock the active window because no window has focus."));
                    checkCount();
                    return;
                }
                break;
            case 'i':
                settings.sCustomIcon = QString::fromLocal8Bit(optarg);
                break;
            case 'I':
                settings.sAttentionIcon = QString::fromLocal8Bit(optarg);
                break;
            case 'j':
                patternOptions = QRegularExpression::NoPatternOption;
                break;
            case 'l':
                settings.opt[IconifyFocusLost] = true;
                break;
            case 'm':
                settings.opt[IconifyMinimized] = false;
                break;
            case 'n':
                windowNamePattern = QString::fromLocal8Bit(optarg);
                break;
            case 'o':
                settings.opt[IconifyObscured] = true;
                break;
            case 'p':
                settings.iBalloonTimeout = atoi(optarg) * 1000;   // convert to ms
                break;
            case 'q':
                settings.iBalloonTimeout = 0;   // same as '-p 0'
                break;
            case 'r':
                settings.opt[SkipPager] = true;
                break;
            case 's':
                settings.opt[Sticky] = true;
                break;
            case 't':
                settings.opt[SkipTaskbar] = true;
                break;
            case 'w':
                bool ok;
                window = static_cast<Window>(QString(optarg).toInt(&ok, 0));
                if (!XLibUtil::isValidWindowId(XLibUtil::display(), window)) {
                    QMessageBox::critical(0, qApp->applicationName(), tr("Invalid window id."));
                    checkCount();
                    return;
                }
                break;
            case 'x':
                pid = atol(optarg);
                break;
        } // switch (option)
    } // while (getopt)

    if (optind < argc || !windowNamePattern.isEmpty()) {
        // We are either launching an application and or matching by name.
        QString command;
        QStringList arguments;

        // Store the command and it's arguments if the user specified them.
        if (optind < argc) {
            command = argv[optind];
            for (int i = optind + 1; i < argc; i++) {
                arguments << QString::fromLocal8Bit(argv[i]);
            }
        }

        // Add the parameters the scanner should use to match. If a command was specified it will be started by the scanner.
        QRegularExpression windowName;
        switch (patType) {
            case PatternType::Normal:
                windowName.setPattern(QRegularExpression::escape(windowNamePattern));
                break;
            case PatternType::Regex:
                windowName.setPattern(windowNamePattern);
                break;
            case PatternType::Wildcard:
                windowName.setPattern(QRegularExpression::wildcardToRegularExpression(windowNamePattern));
                break;
        }
        windowName.setPatternOptions(patternOptions);
        m_scanner->enqueue(command, arguments, settings, maxTime, checkNormality, windowName);
        checkCount();
    } else {
        if (!window) {
            if (pid != 0) {
                window = XLibUtil::pidToWid(XLibUtil::display(), XLibUtil::appRootWindow(), checkNormality, pid, dockedWindows());
            } else {
                window = userSelectWindow(checkNormality);
            }
        }
        if (window) {
            dockWindow(window, settings);
        } else {
            // No window was selected or set.
            checkCount();
        }
    }
}

void TrayItemManager::dockWindow(Window window, const TrayItemArgs settings) {
    if (isWindowDocked(window)) {
        QMessageBox::information(0, qApp->applicationName(), tr("This window is already docked.\nClick on system tray icon to toggle docking."));
        checkCount();
        return;
    }

    TrayItem *ti = new TrayItem(window, settings);

    connect(ti, SIGNAL(selectAnother()), this, SLOT(selectAndIconify()));
    connect(ti, SIGNAL(dead(TrayItem*)), this, SLOT(remove(TrayItem*)));
    connect(ti, SIGNAL(undock(TrayItem*)), this, SLOT(undock(TrayItem*)));
    connect(ti, SIGNAL(undockAll()), this, SLOT(undockAll()));
    connect(ti, SIGNAL(about()), this, SLOT(about()));

    ti->showWindow();

    m_trayItems.append(ti);
}

Window TrayItemManager::userSelectWindow(bool checkNormality) {
    QTextStream out(stdout);
    out << tr("Select the application/window to dock with the left mouse button.") << Qt::endl;
    out << tr("Click any other mouse button to abort.") << Qt::endl;

    QString error;
    Window window = XLibUtil::selectWindow(XLibUtil::display(), m_grabInfo, error);
    if (!window) {
        if (error != QString()) {
            QMessageBox::critical(0, qApp->applicationName(), error);
        }
        checkCount();
        return 0;
    }

    if (checkNormality) {
        if (!XLibUtil::isNormalWindow(XLibUtil::display(), window)) {
            if (QMessageBox::warning(0, qApp->applicationName(), tr("The window you are attempting to dock does not seem to be a normal window."), QMessageBox::Abort | QMessageBox::Ignore) == QMessageBox::Abort) {
                checkCount();
                return 0;
            }
        }
    }

    return window;
}

void TrayItemManager::remove(TrayItem *trayItem) {
    m_trayItems.removeAll(trayItem);
    trayItem->deleteLater();

    checkCount();
}

void TrayItemManager::undock(TrayItem *trayItem) {
    trayItem->restoreWindow();
    trayItem->setSkipTaskbar(false);
    trayItem->doSkipTaskbar();
    remove(trayItem);
}

void TrayItemManager::undockAll() {

    Q_FOREACH(TrayItem *ti, m_trayItems) {
        undock(ti);
    }
}

void TrayItemManager::about() {
    QMessageBox aboutBox;
    aboutBox.setIconPixmap(QPixmap(":/images/kdocker.png"));
    aboutBox.setWindowTitle(tr("About %1 - %2").arg(qApp->applicationName()).arg(qApp->applicationVersion()));
    aboutBox.setText(Constants::ABOUT_MESSAGE);
    aboutBox.setInformativeText(tr("See %1 for more information.").arg("<a href=\"https://github.com/user-none/KDocker\">https://github.com/user-none/KDocker</a>"));
    aboutBox.setStandardButtons(QMessageBox::Ok);
    aboutBox.exec();
}

void TrayItemManager::selectAndIconify() {
    Window window = userSelectWindow(true);

    if (window) {
        dockWindow(window, m_initArgs);
    }
}

void TrayItemManager::checkCount() {
    if (m_trayItems.isEmpty() && !m_scanner->isRunning()) {
        qApp->quit();
    }
}

QList<Window> TrayItemManager::dockedWindows() {
    QList<Window> windows;

    QListIterator<TrayItem*> ti(m_trayItems);
    while (ti.hasNext()) {
        windows.append(ti.next()->dockedWindow());
    }

    return windows;
}

bool TrayItemManager::isWindowDocked(Window window) {
    return dockedWindows().contains(window);
}
