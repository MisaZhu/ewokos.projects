/*
 * nomacs, ported to EwokOS.
 *
 * Upstream: https://github.com/nomacs/nomacs (GPL-3.0).
 *
 * The upstream program sits on exiv2 (metadata), optional OpenCV (RAW /
 * TIFF / image manipulation) and quazip (zip browsing) - none of which
 * exist here, and the EwokOS Qt build has no QProcess, no network and no
 * plugin loading.  So this is a source-level rewrite rather than a
 * source-level port: the signature nomacs shape - a frameless-feeling
 * viewport with wheel zoom and drag pan, folder-wise prev/next browsing,
 * the thumbnail preview strip, rotate/flip, slideshow and fullscreen -
 * is reproduced on plain QtWidgets + QImageReader, restricted to the
 * formats this Qt tree decodes (png/jpeg/bmp/ppm/pgm/pbm/xbm/xpm).
 */

#include "mainwindow.h"

#include <QApplication>

#include <qt/ewokosqt.h>

int main(int argc, char *argv[])
{
    ewokosQtInit();

    QApplication app(argc, argv);
    app.setApplicationName("nomacs");

    // Upstream accepts one file or directory argument; keep that.
    QString path;
    if (argc > 1)
        path = QString::fromLocal8Bit(argv[1]);

    MainWindow win(path);
    win.show();
    return app.exec();
}
