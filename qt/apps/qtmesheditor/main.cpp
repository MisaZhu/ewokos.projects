/*
 * QtMeshEditor, ported to EwokOS.
 *
 * Upstream: https://github.com/fernandotonon/QtMeshEditor (GPL-3.0).
 *
 * See mainwindow.h for why this is a QtWidgets rewrite rather than a
 * source-level port of the upstream Ogre3D/Assimp tree.
 */

#include "mainwindow.h"

#include <QApplication>

#include <qt/ewokosqt.h>

int main(int argc, char *argv[])
{
    ewokosQtInit();

    QApplication app(argc, argv);
    app.setApplicationName("qtmesheditor");

    // Upstream loads a mesh named on the command line; keep the first.
    QString path;
    if (argc > 1)
        path = QString::fromLocal8Bit(argv[1]);

    MainWindow win(path);
    win.show();
    return app.exec();
}
