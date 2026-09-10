/***************************************************************************
 *   Copyright (C) 2006 by Vladimir Kuznetsov                              *
 *   vovanec@gmail.com                                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include <QApplication>
#include <QtGlobal>
/* EwokOS: register the statically linked "ewokos" QPA platform plugin and
   select it before any QApplication is built; without this Qt falls back to
   the "minimal" platform compiled into libQt5Gui.a and aborts at startup.
   See include/qt/ewokosqt.h. */
#include <qt/ewokosqt.h>
/* EwokOS: properties.h no longer drags in the monolithic <QtCore> (see the
   note there), so spell out what this file actually uses. */
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

#include <cassert>
#include <cstdio>
#include <getopt.h>
#include <cstdlib>
#include <unistd.h>
#include <errno.h>  /* EwokOS libc has no <cerrno>; errno lives here */
#include <string.h>
#ifdef HAVE_QDBUS
    #include <QtDBus/QtDBus>
    #include <unistd.h>
    #include "processadaptor.h"
#endif


#include "mainwindow.h"
#include "qterminalapp.h"
#include "qterminalutils.h"
#include "terminalconfig.h"

/* ===== TEMPORARY DIAGNOSTIC - submenu inoperability investigation ==========
 * Logs every event the popup QMenus receive, so the Qt side of the handoff can
 * be read next to the [QPA] and [XS] traces on the serial console.  Set to 0 to
 * compile out; remove once the defect is fixed. */
#define EWOK_MENU_TRACE 1
#if EWOK_MENU_TRACE
#include <QAction>
#include <QEvent>
#include <QMenu>
#include <QMouseEvent>
#include <cstdio>

class MenuTrace : public QObject
{
public:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        QMenu *m = qobject_cast<QMenu *>(o);
        if (!m)
            return false;
        switch (e->type()) {
        case QEvent::Enter:
            fprintf(stdout, "[MT] %-18s ENTER\n", name(m));
            break;
        case QEvent::Leave:
            fprintf(stdout, "[MT] %-18s LEAVE   act=%s\n", name(m), act(m->activeAction()));
            break;
        case QEvent::MouseMove: {
            QMouseEvent *me = static_cast<QMouseEvent *>(e);
            QAction *at = m->actionAt(me->pos());
            fprintf(stdout, "[MT] %-18s MOVE pos=(%d,%d) g=(%d,%d) at=%s sub=%d "
                            "act=%s vis=%d um=%d\n",
                    name(m), me->x(), me->y(), me->globalX(), me->globalY(),
                    act(at), (at && at->menu()) ? 1 : 0, act(m->activeAction()),
                    (int)m->isVisible(), (int)m->underMouse());
            break;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease: {
            QMouseEvent *me = static_cast<QMouseEvent *>(e);
            fprintf(stdout, "[MT] %-18s %s pos=(%d,%d) act=%s\n", name(m),
                    e->type() == QEvent::MouseButtonPress ? "PRESS  " : "RELEASE",
                    me->x(), me->y(), act(m->activeAction()));
            break;
        }
        case QEvent::Timer:
            fprintf(stdout, "[MT] %-18s TIMER act=%s vis=%d\n", name(m),
                    act(m->activeAction()), (int)m->isVisible());
            break;
        case QEvent::Show:
            fprintf(stdout, "[MT] %-18s SHOW  act=%s\n", name(m), act(m->activeAction()));
            break;
        case QEvent::Hide:
            fprintf(stdout, "[MT] %-18s HIDE  act=%s\n", name(m), act(m->activeAction()));
            break;
        case QEvent::WindowActivate:
        case QEvent::WindowDeactivate:
            fprintf(stdout, "[MT] %-18s %s\n", name(m),
                    e->type() == QEvent::WindowActivate ? "WIN-ACTIVATE" : "WIN-DEACTIVATE");
            break;
        default:
            break;
        }
        fflush(stdout);
        return false;
    }

private:
    /* Rotating pools: several of these are arguments of one fprintf, so a
       single static buffer would be overwritten before it was read. */
    static const char *name(const QMenu *m)
    {
        static char pool[4][48];
        static int slot = 0;
        char *buf = pool[slot++ & 3];
        QString t = m->objectName().isEmpty() ? m->title() : m->objectName();
        snprintf(buf, 48, "%s@%p", qPrintable(t), (const void *)m);
        return buf;
    }

    static const char *act(const QAction *a)
    {
        static char pool[4][40];
        static int slot = 0;
        char *buf = pool[slot++ & 3];
        if (!a)
            snprintf(buf, 40, "null");
        else
            snprintf(buf, 40, "%s", qPrintable(a->text()));
        return buf;
    }
};
#endif

#define out

const char* const short_options = "vhw:e:dp:";

const struct option long_options[] = {
    {"version", 0, nullptr, 'v'},
    {"help",    0, nullptr, 'h'},
    {"workdir", 1, nullptr, 'w'},
    {"execute", 1, nullptr, 'e'},
    {"drop",    0, nullptr, 'd'},
    {"profile", 1, nullptr, 'p'},
    {nullptr,   0, nullptr,  0}
};

QTerminalApp * QTerminalApp::m_instance = nullptr;

[[ noreturn ]] void print_usage_and_exit(int code)
{
    printf("QTerminal %s\n", QTERMINAL_VERSION);
    puts("Usage: qterminal [OPTION]...\n");
    puts("  -d,  --drop               Start in \"dropdown mode\" (like Yakuake or Tilda)");
    puts("  -e,  --execute <command>  Execute command instead of shell");
    puts("  -h,  --help               Print this help");
    puts("  -p,  --profile <name>     Load profile from ~/.config/<name>.conf");
    puts("  -v,  --version            Prints application version and exits");
    puts("  -w,  --workdir <dir>      Start session with specified work directory");
    puts("\nHomepage: <https://github.com/lxqt/qterminal>");
    puts("Report bugs to <https://github.com/lxqt/qterminal/issues>");
    exit(code);
}

[[ noreturn ]] void print_version_and_exit(int code=0)
{
    printf("%s\n", QTERMINAL_VERSION);
    exit(code);
}

void parse_args(int argc, char* argv[], QString& workdir, QStringList & shell_command, out bool& dropMode)
{
    int next_option;
    dropMode = false;
    do{
        next_option = getopt_long(argc, argv, short_options, long_options, nullptr);
        switch(next_option)
        {
            case 'h':
                print_usage_and_exit(0);
                break;
            case 'w':
                workdir = QString::fromLocal8Bit(optarg);
                break;
            case 'e':
                shell_command << parse_command(QString::fromLocal8Bit(optarg));
                // #15 "Raw" -e params
                // Passing "raw" params (like konsole -e mcedit /tmp/tmp.txt") is more preferable - then I can call QString("qterminal -e ") + cmd_line in other programs
                while (optind < argc)
                {
                    //printf("arg: %d - %s\n", optind, argv[optind]);
                    shell_command << QString::fromLocal8Bit(argv[optind++]);
                }
                break;
            case 'd':
                dropMode = true;
                break;
            case 'p':
                Properties::Instance(QString::fromLocal8Bit(optarg));
                break;
            case '?':
                print_usage_and_exit(1);
                break;
            case 'v':
                print_version_and_exit();
                break;
        }
    }
    while(next_option != -1);
}

int main(int argc, char *argv[])
{
    ewokosQtInit();

    if (!qEnvironmentVariableIsEmpty("XPC_SERVICE_NAME")) {
        // On macOS, if qterminal.app is spawned by launchd (e.g., from Finder
        // or use `open qterminal.app`, $PWD is set to /. Workaround that by
        // go to $HOME first.
        if (chdir(QDir::homePath().toLatin1().data())) {
            qDebug() << "Failed to chdir to $HOME" << QDir::homePath() << strerror(errno);
        }

        // also initializes $LANG
        QString systemLocaleName(QLocale().name());
        systemLocaleName.append(QLatin1String(".UTF-8"));
        qputenv("LANG", systemLocaleName.toLatin1());
    }

    QApplication::setApplicationName(QStringLiteral("qterminal"));
    QApplication::setApplicationVersion(QStringLiteral(QTERMINAL_VERSION));
    QApplication::setOrganizationDomain(QStringLiteral("qterminal.org"));
    QApplication::setDesktopFileName(QLatin1String("qterminal.desktop"));
    // Warning: do not change settings format. It can screw bookmarks later.
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QTerminalApp *app = QTerminalApp::Instance(argc, argv);
    #ifdef HAVE_QDBUS
        app->registerOnDbus();
    #endif

    QString workdir;
    QStringList shell_command;
    bool dropMode;
    parse_args(argc, argv, workdir, shell_command, dropMode);

    Properties::Instance()->migrate_settings();
    Properties::Instance()->loadSettings();

    if (workdir.isEmpty())
        workdir = QDir::currentPath();
    app->setWorkingDirectory(workdir);

    const QSettings settings;
    const QFileInfo customStyle = QFileInfo(
        QFileInfo(settings.fileName()).canonicalPath() +
        QStringLiteral("/style.qss")
    );
    if (customStyle.isFile() && customStyle.isReadable())
    {
        QFile style(customStyle.canonicalFilePath());
        style.open(QFile::ReadOnly);
        QString styleString = QLatin1String(style.readAll());
        app->setStyleSheet(styleString);
    }

    // icons
    /* setup our custom icon theme if there is no system theme (OS X, Windows) */
    QCoreApplication::instance()->setAttribute(Qt::AA_UseHighDpiPixmaps); //Fix for High-DPI systems
    if (QIcon::themeName().isEmpty())
        QIcon::setThemeName(QStringLiteral("QTerminal"));

    // translations

    // install the translations built-into Qt itself
    QTranslator qtTranslator;
    qtTranslator.load(QStringLiteral("qt_") + QLocale::system().name(), QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    app->installTranslator(&qtTranslator);

    QTranslator translator;
    QString fname = QString::fromLatin1("qterminal_%1.qm").arg(QLocale::system().name().left(5));
#ifdef TRANSLATIONS_DIR
    //qDebug() << "TRANSLATIONS_DIR: Loading translation file" << fname << "from dir" << TRANSLATIONS_DIR;
    /*qDebug() << "load success:" <<*/ translator.load(fname, QString::fromUtf8(TRANSLATIONS_DIR), QStringLiteral("_"));
#endif
#ifdef APPLE_BUNDLE
    QDir translations_dir = QDir(QApplication::applicationDirPath());
    translations_dir.cdUp();
    if (translations_dir.cd(QStringLiteral("Resources/translations"))) {
        //qDebug() << "APPLE_BUNDLE: Loading translator file" << fname << "from dir" << translations_dir.path();
        /*qDebug() << "load success:" <<*/ translator.load(fname, translations_dir.path(), QStringLiteral("_"));
    } /*else {
        qWarning() << "Unable to find \"Resources/translations\" dir in" << translations_dir.path();
    }*/
#endif
    app->installTranslator(&translator);

    TerminalConfig initConfig = TerminalConfig(workdir, shell_command);
    app->newWindow(dropMode, initConfig);

#if EWOK_MENU_TRACE
    static MenuTrace menuTrace;
    app->installEventFilter(&menuTrace);
#endif

    int ret = app->exec();
    delete Properties::Instance();
    app->cleanup();

    return ret;
}

MainWindow *QTerminalApp::newWindow(bool dropMode, TerminalConfig &cfg)
{
    MainWindow *window = nullptr;
    if (dropMode)
    {
        window = new MainWindow(cfg, dropMode);
        if (Properties::Instance()->dropShowOnStart)
            window->show();
    }
    else
    {
        window = new MainWindow(cfg, dropMode);
        if (Properties::Instance()->windowMaximized)
            window->setWindowState(Qt::WindowMaximized);
        window->show();
    }
    return window;
}

QTerminalApp *QTerminalApp::Instance()
{
    assert(m_instance != nullptr);
    return m_instance;
}

QTerminalApp *QTerminalApp::Instance(int &argc, char **argv)
{
    assert(m_instance == nullptr);
    m_instance = new QTerminalApp(argc, argv);
    return m_instance;
}

QTerminalApp::QTerminalApp(int &argc, char **argv)
    :QApplication(argc, argv)
{
}

QString &QTerminalApp::getWorkingDirectory()
{
    return m_workDir;
}

void QTerminalApp::setWorkingDirectory(const QString &wd)
{
    m_workDir = wd;
}

void QTerminalApp::cleanup() {
    delete m_instance;
    m_instance = nullptr;
}


void QTerminalApp::addWindow(MainWindow *window)
{
    m_windowList.append(window);
}

void QTerminalApp::removeWindow(MainWindow *window)
{
    m_windowList.removeOne(window);
}

QList<MainWindow *> QTerminalApp::getWindowList()
{
    return m_windowList;
}

#ifdef HAVE_QDBUS
void QTerminalApp::registerOnDbus()
{
    if (!QDBusConnection::sessionBus().isConnected())
    {
        fprintf(stderr, "Cannot connect to the D-Bus session bus.\n"
                "To start it, run:\n"
                "\teval `dbus-launch --auto-syntax`\n");
        return;
    }
    QString serviceName = QStringLiteral("org.lxqt.QTerminal-%1").arg(getpid());
    if (!QDBusConnection::sessionBus().registerService(serviceName))
    {
        fprintf(stderr, "%s\n", qPrintable(QDBusConnection::sessionBus().lastError().message()));
        return;
    }
    new ProcessAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/"), this);
}

QList<QDBusObjectPath> QTerminalApp::getWindows()
{
    QList<QDBusObjectPath> windows;
    for (MainWindow *wnd : qAsConst(m_windowList))
    {
        windows.push_back(wnd->getDbusPath());
    }
    return windows;
}

QDBusObjectPath QTerminalApp::newWindow(const QHash<QString,QVariant> &termArgs)
{
    TerminalConfig cfg = TerminalConfig::fromDbus(termArgs);
    MainWindow *wnd = newWindow(false, cfg);
    assert(wnd != nullptr);
    return wnd->getDbusPath();
}

QDBusObjectPath QTerminalApp::getActiveWindow()
{
    QWidget *aw = activeWindow();
    if (aw == nullptr)
        return QDBusObjectPath("/");
    return qobject_cast<MainWindow*>(aw)->getDbusPath();
}

bool QTerminalApp::isDropMode() {
  if (m_windowList.count() == 0) {
    return false;
  }
  MainWindow *wnd = m_windowList.at(0);
  return wnd->dropMode();
}

bool QTerminalApp::toggleDropdown() {
  if (m_windowList.count() == 0) {
    return false;
  }
  MainWindow *wnd = m_windowList.at(0);
  if (!wnd->dropMode()) {
    return false;
  }
  wnd->showHide();
  return true;
}


#endif

