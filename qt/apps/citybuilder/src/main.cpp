/*
 * CityBuilderEngine, ported to EwokOS.
 *
 * Upstream: https://github.com/Vinorcola/CityBuilderEngine - a Qt 5.15 /
 * C++14 2D city-builder, itself a reimplementation of Zeus - Master of
 * Olympus.
 *
 * Two EwokOS-specific differences from upstream's main():
 *
 *   - ewokosQtInit() must run before QApplication so the statically linked
 *     "ewokos" QPA platform plugin is registered (see <qt/ewokosqt.h>).
 *
 *   - the asset directory is the installed location in the rootfs, not the
 *     upstream working-copy relative "assets/zeus".  The Makefile copies res/
 *     under apps/citybuilder/ at install time and passes its path here as
 *     CBE_ASSETS_DIR; the #ifndef keeps the upstream default for a native
 *     build straight out of the source tree.
 */

#include <QApplication>

#include <qt/ewokosqt.h>

#include "src/ui/MainWindow.hpp"

#ifndef CBE_ASSETS_DIR
#define CBE_ASSETS_DIR "assets/zeus"
#endif

int main(int argc, char* argv[])
{
    ewokosQtInit();

    QApplication application(argc, argv);

    auto window(new MainWindow(CBE_ASSETS_DIR));
    window->loadMap(CBE_ASSETS_DIR "/maps/testing-with-houses.yaml");
    window->showMaximized();

    return application.exec();
}
