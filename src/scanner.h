/*
 *  Copyright (C) 2009, 2012, 2015 John Schember <john@nachtimwald.com>
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

#ifndef _SCANNER_H
#define	_SCANNER_H

#include "command.h"
#include "trayitemmanager.h"

#include <QList>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>

#include <sys/types.h>

class TrayItemManager;

class ProcessId {
    public:
        ProcessId(const QString &command, pid_t pid, const TrayItemConfig &config, uint timeout, bool checkNormality, const QRegularExpression &windowName);
        ProcessId(const ProcessId &obj);
        ProcessId& operator=(const ProcessId &obj);

   pubic:
        QString command;
        pid_t pid;
        TrayItemConfig config;
        QElapsedTimer etimer;
        uint timeout;
        bool checkNormality;
        QRegularExpression windowName;
};

// Launches commands and looks for the window ids they create.

class Scanner : public QObject {
    Q_OBJECT

public:
    Scanner(TrayItemManager *manager);
    ~Scanner();
    void enqueueSearch(const QRegularExpression &windowName, uint maxTime, bool checkNormality, const TrayItemConfig &config);
    void enqueueLaunch(const QString &command, const QStringList &arguments, const QRegularExpression &windowName, uint maxTime, bool checkNormality, const TrayItemConfig &config);
    bool isRunning();

private slots:
    void check();

signals:
    void windowFound(Window, const TrayItemConfig &);
    void stopping();

private:
    void enqueue(const QString &command, const QStringList &arguments, const QRegularExpression &windowName, const TrayItemConfig &config, uint maxTime, bool checkNormality);

    TrayItemManager *m_manager;
    QTimer *m_timer;
    QList<ProcessId> m_processes;
};

#endif	/* _SCANNER_H */
