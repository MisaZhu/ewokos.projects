#ifndef EWOKOSQT_H
#define EWOKOSQT_H

/*
 * Bringing up Qt on EwokOS.
 *
 * Call ewokosQtInit() once, before constructing a QGuiApplication,
 * QApplication or QWidget.  It registers the statically linked "ewokos" QPA
 * platform plugin and points QT_QPA_PLATFORM at it if the application has not
 * already chosen a platform.
 *
 * It exists because the usual two alternatives do not work here:
 *
 *   Q_IMPORT_PLUGIN(EwokosIntegrationPlugin)  expands to a namespace-scope
 *       object with a non-trivial constructor.  EwokOS links with -nostartfiles,
 *       so .init_array never runs and the object is never constructed.  See the
 *       long comment in src/platform/ewokos/main.cpp.
 *
 *   QT_QPA_DEFAULT_PLATFORM_NAME              was compiled into libQt5Gui.a as
 *       "minimal", before this plugin existed.  An application redefining the
 *       macro does not reach a library that was built with the other value.
 *
 * An application that needs a different platform - or none - can set
 * QT_QPA_PLATFORM itself before calling this and the value will be left alone.
 */
void ewokosQtInit();

#endif /* EWOKOSQT_H */
