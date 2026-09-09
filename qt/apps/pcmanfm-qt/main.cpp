/*
 * PCManFM-Qt, ported to EwokOS.
 *
 * Upstream: https://github.com/lxqt/pcmanfm-qt (LGPL-2.1).
 *
 * The upstream program sits on libfm-qt, which in turn needs glib/gio,
 * menu-cache and an XDG desktop around it - none of which exist here.  So
 * this is a source-level rewrite rather than a source-level port: the main
 * window, its menus, the side pane (Places / directory tree), the tabbed
 * folder views with their three view modes and the file operations are
 * reproduced on plain QtWidgets + QFileSystemModel, and the libfm-qt layer
 * (launching files, "open terminal") is replaced by the native EwokOS
 * mechanisms - /usr/system/filetypes.json plus fork/proc_exec, the same way
 * xfinder does it.
 */

#include "mainwindow.h"

#include <QApplication>

#include <qt/ewokosqt.h>

int main(int argc, char *argv[])
{
    ewokosQtInit();

    QApplication app(argc, argv);
    app.setApplicationName("pcmanfm-qt");

    // Upstream accepts paths on the command line and opens one tab each;
    // keep that, defaulting to $HOME like upstream's "pcmanfm-qt" with no
    // arguments does.
    QStringList paths;
    for (int i = 1; i < argc; ++i)
        paths << QString::fromLocal8Bit(argv[i]);

    MainWindow win(paths);
    win.show();
    return app.exec();
}
