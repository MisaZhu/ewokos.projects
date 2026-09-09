/*
 * XFile Manager, ported to EwokOS.
 *
 * Upstream: https://github.com/arshc0der/XFile_Manager (MIT, see LICENSE in
 * this directory).
 *
 * TEMPORARY DEBUG BUILD: stage traces go to the kernel console (klog -> UART)
 * so the last stage before a fault is visible even when the process dies.
 */

#include "mainwindow.h"

#include <QApplication>

#include <qt/ewokosqt.h>
#include <ewoksys/klog.h>

__attribute__((constructor)) static void xfm_ctor_trace(void)
{
    klog("[xfm] init_array ctor\n");
}

int main(int argc, char *argv[])
{
    klog("[xfm] main enter\n");
    ewokosQtInit();
    klog("[xfm] ewokosQtInit done\n");
    Q_INIT_RESOURCE(XFile_Manager_Resources);
    klog("[xfm] resource done\n");

    QApplication a(argc, argv);
    klog("[xfm] QApplication done\n");
    MainWindow w;
    klog("[xfm] MainWindow done\n");
    w.show();
    klog("[xfm] show done\n");
    return a.exec();
}
