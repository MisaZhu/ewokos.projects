#include "qewokosintegration.h"
#include "qewokosbackingstore.h"
#include "qewokoseventdispatcher.h"
#include "qewokosfontdatabase.h"
#include "qewokosscreen.h"
#include "qewokoswindow.h"

#include <qpa/qwindowsysteminterface.h>

#include <QtCore/qstringlist.h>
#include <QtCore/qdebug.h>

QT_BEGIN_NAMESPACE

EwokosIntegration *EwokosIntegration::s_instance = nullptr;

EwokosIntegration::EwokosIntegration(const QStringList &paramList)
    : m_screen(nullptr)
    , m_fontDatabase(nullptr)
{
    Q_UNUSED(paramList);

    /* x_init() memsets the context, resolves /dev/x into dev_fsinfo and pulls the
       theme.  It does not open anything and cannot fail, so there is no error
       path here - a machine with no window server shows up later, as an
       xwin_open() that returns null. */
    x_init(&m_x, this);

    if (s_instance)
        qWarning("ewokos: a second integration was created; window and event "
                 "routing will follow the newest one");
    s_instance = this;
}

EwokosIntegration::~EwokosIntegration()
{
    if (s_instance == this)
        s_instance = nullptr;

    /* linuxfb tears its screens down the same way.  An integration is destroyed
       when QGuiApplication goes, and by then the screen list it was registered
       in is gone with it, so there is nothing left to notify. */
    delete m_screen;
    m_screen = nullptr;

    delete m_fontDatabase;
    m_fontDatabase = nullptr;
}

bool EwokosIntegration::hasCapability(Capability cap) const
{
    switch (cap) {
    case MultipleWindows:
        /* Menus, dialogs and tooltips are separate xwin windows here - every
           QWindow gets its own xwin_open().  Claiming this is what stops Qt from
           drawing popups into the parent's surface.  The base says no, so this
           one has to be asked for explicitly. */
    case NonFullScreenWindows:
        /* xwin_open() takes a position and a size and the WM honors both; only
           X_AUTO_FULL_SCREEN overrides it, and that is an environment opt-in. */
    case RasterGLSurface:
        /* Matches minimal and linuxfb: it says the raster path may be used with a
           GL surface, and costs nothing when GL is compiled out as it is here. */
        return true;

    case OpenGL:
    case ThreadedOpenGL:
    case ThreadedPixmaps:
    case BufferQueueingOpenGL:
    case AllGLFunctionsQueryable:
    case OpenGLOnRasterSurface:
    case SharedGraphicsCache:
        /* configure was given -no-opengl -no-eglfs -no-gbm -no-kms, and the whole
           stack is the software raster engine.  The base would already answer no
           to all of these, but saying so here means a reader of this file sees
           the whole GL story in one place rather than having to go and check what
           QPlatformIntegration::hasCapability() defaults to. */
        return false;

    default:
        break;
    }
    /* Everything else is the base's answer, which in Qt 5.15 is a closed list -
       NonFullScreenWindows, NativeWidgets, WindowManagement,
       TopStackedNativeChildWindows and WindowActivation are yes, and every other
       capability, including ApplicationState, WindowMasks, ForeignWindows and
       SyncState, is no.  WindowActivation matters here because
       EwokosWindow::requestActivateWindow() is implemented. */
    return QPlatformIntegration::hasCapability(cap);
}

QPlatformWindow *EwokosIntegration::createPlatformWindow(QWindow *window) const
{
    /* The xwin itself is not opened here.  QWindowPrivate::create() calls this,
       asserts the result, and only then calls initialize() - and
       EwokosWindow::initialize() is where the geometry has been resolved and the
       window can actually be asked for.  See qewokoswindow.cpp. */
    return new EwokosWindow(window);
}

QPlatformBackingStore *EwokosIntegration::createPlatformBackingStore(QWindow *window) const
{
    return new EwokosBackingStore(window);
}

QAbstractEventDispatcher *EwokosIntegration::createEventDispatcher() const
{
    /* EwokosEventDispatcher, not QEventDispatcherUNIX directly: the base blocks in
       poll() on a timeout that can be "until the next timer, or effectively
       forever", and x events arrive on a path with no wakeup at all - the
       blocking read inside libx parks on x->evt_node with vfs_block(), which
       takes no timeout, and the server is not a thread that could write the
       QThreadPipe.  The derived class keeps everything the base does correctly
       and only takes the blocking away. */
    return new EwokosEventDispatcher;
}

QPlatformFontDatabase *EwokosIntegration::fontDatabase() const
{
    /* Lazy rather than in the constructor: QFontDatabase may never be touched at
       all by an application that only draws with pixmaps, and populating it
       means walking /usr/system/fonts and parsing every TrueType header. */
    if (!m_fontDatabase)
        m_fontDatabase = new EwokosFontDatabase;
    return m_fontDatabase;
}

void EwokosIntegration::initialize()
{
    /* Display 0 is the one xwin composites onto by default; x_get_display_num()
       can report more, but a Qt screen per x display would need the WM to agree
       on which window lives where, and it does not track that.  One screen is
       also what every other GUI in this tree presents. */
    m_screen = new EwokosScreen(0);

    QWindowSystemInterface::handleScreenAdded(m_screen);

    QPlatformIntegration::initialize();
}

QT_END_NAMESPACE
