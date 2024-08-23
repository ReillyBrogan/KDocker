# KDocker

KDocker will help you dock most applications to the system tray. Just point and
click!

All you need to do is start KDocker and select an application using the mouse
and the application gets docked into the system tray.


## Desktop Environment Behavior

Each desktop environment has slightly different behavior in how they handle the
system try. Depending on which desktop environment you're using KDocker might
work differently than you expect. The behavior of the system try is defined by
the desktop environment and KDocker has no control over these differences in
behavior.

Desktop | Left Click  | Right Click            | Double Click | Scroll Wheel
------- | ----------- | ---------------------- | ------------ | ------------
KDE     | show / hide | opens menu             | show / hide  | nothing
Gnome   | opens menu  | opens menu             | show / hide  | nothing
Mate    | opens menu  | opens Mate applet menu | opens menu   | nothing
LXDE    | show / hide | opens menu             | show / hide  | show / hide
XFCE    | show / hide | opens menu             | show / hide  | nothing

### Notes

Gnome, not all distros install the system tray plugin by default.


## DBus Interface

A DBus interface is available at `com.kdocker.KDocker/manage` and allows
performing various actions with KDocker. The interface can be used to script
working with KDocker.

### Docking methods

Two versions of every method are provided with one being a simple command
and the other supporting extended attributes for fine tuned control. DBus
does not allow default values to multiple versions of the methods with
different options is required.

Method           | input                                                                        | output
---------------- | ---------------------------------------------------------------------------- | ------
dockWindowTitle  | (s pattern)                                                                  | ()
dockWindowTitle  | (s pattern, u timeout, b checkNormality, a{ss} windowConfig)                 | ()
dockLaunchApp    | (s app, as args, s pattern)                                                  | ()
dockLaunchApp    | (s app, as args, s pattern, u timeout, b checkNormality, a{ss} windowConfig) | ()
dockWindowId     | (u windowId)                                                                 | (b found)
dockWindowId     | (u windowId, a{ss} windowConfig)                                             | (b found)
dockPid          | (i pid)                                                                      | (b found)
dockPid          | (i pid, b checkNormality, a{ss} windowConfig)                                | (b found)
dockSelectWindow | ()                                                                           | ()
dockSelectWindow | (b checkNormality, a{ss} windowConfig)                                       | ()
dockFocused      | ()                                                                           | ()
dockFocused      | (a{ss} windowConfig)                                                         | ()

### pattern

Pattern is a PCRE regular expression.

If `pattern` matching on the window name is not wanted with `dockLaunchApp` use `""` as the value.

#### windowConfig

Tray item options correspond to the options seen in the `options` menu. It is a list of
dictionary references. Keys and value are both strings.

Key                | value
------------------ | -----
icon               | file path
attention-icon     | file path
iconify-focus-lost | true / false
iconify-minimized  | true / false
iconify-obscured   | true / false
notify-time        | seconds
quiet              | true / false
skip-pager         | true / false
sticky             | true / false
skip-taskbar       | true / false

Invalid keys are ignored.

Entries in the dictionary are options and only need to be provided if desired. However,
DBus does not allow empty dictionary parameters. If no settings are desired either send
one with the default value or send a key that isn't valid with any value. E.g. `{ "a", "b" }`.

### Docked window management

Method       | input        | output
------------ | ------------ | ------
listWindows  | ()           | (a{us} windows)
closeWindow  | (u windowId) | (b found)
undockWindow | (u windowId) | (b found)
showWindow   | (u windowId) | (b found)
hideWindow   | (u windowId) | (b found)
undockAll    | ()           | ()

### Behavior

Method      | input | output
----------- | ----- | ------
keepRunning | ()    | ()
quit        | ()    | ()


## Website

https://github.com/user-none/KDocker


## Building from source

KDocker requires Qt 6. Unlike the name implies, it does not use any libraires
from KDE nor does KDE need to be installed.

Build dependencies for Ubuntu 24.04

- build-essential
- cmake
- qt6-base-dev
- libx11-dev
- libxcb1-dev
- libxmu-dev

Building

1. `mkdir build`
2. `cd build`
3. `cmake ..`
4. `make`

*IMPORTANT*: Close all previous instances of KDocker that are running before running
a new build. KDocker is a single instance application.
