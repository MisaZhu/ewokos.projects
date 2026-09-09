/*
 * notepadqq, ported to EwokOS.
 *
 * Upstream: https://github.com/notepadqq/notepadqq (GPL-3.0).
 *
 * Upstream's editor core is CodeMirror running inside QtWebEngine (an
 * embedded Chromium reached over a web socket) - and this Qt build has no
 * WebEngine, no network, no QProcess and no plugin loading.  So this is a
 * source-level rewrite rather than a source-level port: the signature
 * notepadqq shape - the tabbed multi-document window with "new 1" untitled
 * naming, the bottom search/replace bar with regex and case toggles, the
 * per-language syntax highlighting with CodeMirror's default colors, and
 * the Ln/Col + length + EOL + encoding status bar readouts - is reproduced
 * on plain QtWidgets (QPlainTextEdit + QSyntaxHighlighter).
 */

#include "mainwindow.h"

#include <QApplication>

#include <qt/ewokosqt.h>

int main(int argc, char *argv[])
{
    ewokosQtInit();

    QApplication app(argc, argv);
    app.setApplicationName("Notepadqq");

    MainWindow win;
    win.show();

    // Upstream accepts file arguments; keep that.
    for (int i = 1; i < argc; ++i)
        win.openPath(QString::fromLocal8Bit(argv[i]));

    return app.exec();
}
