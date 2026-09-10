#include "qewokoswindow.h"
#include "qewokosintegration.h"
#include "qewokoskeymap.h"

#include <qpa/qwindowsysteminterface.h>

#include <QtCore/qdatetime.h>
#include <QtCore/qdebug.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qpainter.h>
#include <QtGui/qstylehints.h>
#include <QtGui/qsurfaceformat.h>

#include <mouse/mouse.h>

#include <stdlib.h>
#include <string.h>

QT_BEGIN_NAMESPACE

/* ===== TEMPORARY DIAGNOSTIC - menuprobe submenu investigation =============
 * Set EWOK_QPA_TRACE to 1 to compile the tracing back in.  Everything goes to
 * stdout so it interleaves with the probe application's own trace on the serial
 * console.  Not a permanent addition: the window list is an unbounded static
 * and the mouse trace is far too chatty for production.  The submenu breakage
 * this was chasing is fixed - the platform now honours the popup mouse grab
 * (see setMouseGrabEnabled) - so it stays off. */
#define EWOK_QPA_TRACE 0
#if EWOK_QPA_TRACE
#include <QtCore/qlist.h>
#include <stdarg.h>
#include <stdio.h>

static QList<EwokosWindow *> ewokTraceWindows;

static void ewokTrace(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fputs("[QPA] ", stdout);
    fputs(buf, stdout);
    fflush(stdout);
}

static const char *ewokId(EwokosWindow *w)
{
    static char pool[4][128];
    static int slot = 0;
    char *b = pool[slot++ & 3];
    if (!w || !w->window())
        return "<null>";
    snprintf(b, 128, "%s@%p", qPrintable(w->window()->title()), (const void *)w);
    return b;
}

/* The whole window set with both sides of the geometry handoff and the two
 * server flags the input routing depends on.  This is the line that answers
 * "does the popup have a real xwin, is it visible, and is it focused".
 * serverGeometry() is private, so wsr is read off the shared struct - the same
 * thing that getter does. */
static void ewokDumpWindows(const char *why)
{
    ewokTrace("---- window set @ %s ----\n", why);
    for (int i = 0; i < ewokTraceWindows.size(); ++i) {
        EwokosWindow *w = ewokTraceWindows.at(i);
        const QRect q = w->geometry();
        xwin_t *xw = w->xwin();
        if (!xw || !xw->xinfo) {
            ewokTrace("  [%d] %s  NO XWIN  qt=%d,%d %dx%d\n",
                      i, ewokId(w), q.x(), q.y(), q.width(), q.height());
            continue;
        }
        const xinfo_t *xi = xw->xinfo;
        ewokTrace("  [%d] %s xwin=%p handle=%llx style=0x%x vis=%d foc=%d "
                  "srv=%d,%d %dx%d qt=%d,%d %dx%d sync=%d\n",
                  i, ewokId(w), (const void *)xw,
                  (unsigned long long)xi->win, (unsigned)xi->style,
                  (int)xi->visible, (int)xi->focused,
                  xi->wsr.x, xi->wsr.y, xi->wsr.w, xi->wsr.h,
                  q.x(), q.y(), q.width(), q.height(),
                  (int)(q.x() == xi->wsr.x && q.y() == xi->wsr.y &&
                        q.width() == xi->wsr.w && q.height() == xi->wsr.h));
    }
}
#endif /* EWOK_QPA_TRACE */

/* xwin_open() rejects w <= 0 or h <= 0, and QWindow's geometry before an
   application positions it is Qt's "let the platform decide" sentinel rather
   than a usable size.  These are what initialGeometry() substitutes. */
static const int EWOK_DEFAULT_WIDTH = 640;
static const int EWOK_DEFAULT_HEIGHT = 480;

/* QStyleHints can be null this early in startup, and the value it would report
   comes from QPlatformIntegration::styleHint(MouseDoubleClickInterval), whose
   default is 400 ms. */
static int ewokosDoubleClickInterval()
{
    QStyleHints *hints = QGuiApplication::styleHints();
    return hints ? hints->mouseDoubleClickInterval() : 400;
}

/*
 * Premultiplied to straight alpha.
 *
 * Qt paints m_surface as Format_ARGB32, which is premultiplied: every channel
 * is already scaled by its own alpha.  xwin's compositor blends with straight
 * alpha - graph_blt_alpha hands the raw RGB and the raw alpha byte to
 * graph_blend_argb, which does dst*(255-a)/255 + src*a/255.  Feeding it
 * premultiplied pixels darkens everything that is not fully opaque, most
 * visibly the anti-aliased edge of every glyph.
 *
 * The conversion is a divide per channel, so it goes through 255*65536/a as a
 * multiply and a shift instead.  Index 0 is never read: a fully transparent
 * pixel returns before the table is consulted.
 */
struct EwokInverseAlpha
{
    uint32_t v[256];
    EwokInverseAlpha()
    {
        v[0] = 0;
        for (int a = 1; a < 256; ++a)
            v[a] = ((255u << 16) + (uint32_t)a / 2) / (uint32_t)a;
    }
};

static const EwokInverseAlpha &ewokInverseAlpha()
{
    static const EwokInverseAlpha table;
    return table;
}

/* One premultiplied ARGB32 pixel to one straight-alpha ARGB32 pixel, with the
   window-wide opacity folded in.  `factor` is that opacity as 0..256 fixed
   point, so 256 means "leave the alpha alone" and the pass costs nothing but
   the un-premultiply.

   Clamping each channel to its own alpha before the multiply is both what
   premultiplied means and what bounds it: a <= 255 gives k <= 255<<16, so the
   product stays inside 32 bits. */
static inline uint32_t ewokStraightAlpha(QRgb p, uint32_t factor)
{
    const uint32_t a = qAlpha(p);
    if (a == 0)
        return 0;

    const uint32_t na = (a * factor) >> 8;
    if (na == 0)
        return 0;
    /* Fully opaque: nothing to un-premultiply, and na == a when factor is 256,
       so this is the identity for the pixels that make up most of a window. */
    if (a == 255)
        return (na << 24) | (p & 0x00ffffffu);

    const uint32_t k = ewokInverseAlpha().v[a];
    uint32_t r = qRed(p);
    uint32_t g = qGreen(p);
    uint32_t b = qBlue(p);
    if (r > a) r = a;
    if (g > a) g = a;
    if (b > a) b = a;
    return (na << 24) | (((r * k) >> 16) << 16)
                    | (((g * k) >> 16) << 8)
                    | ((b * k) >> 16);
}

/*
 * Qt window flags to an xwin style mask.
 *
 * xwin's decoration model is coarser than Qt's: there is a frame, there is a
 * title inside that frame, and there is whether the client may resize.  Qt's
 * CustomizeWindowHint is the switch that says "the flags that follow are the
 * whole story", so every test below is gated on it - without that gate a plain
 * Qt::Widget or Qt::Dialog would lose its title bar, because neither sets
 * WindowTitleHint.
 */
static int ewokosWindowStyle(Qt::WindowFlags flags)
{
    int style = XWIN_STYLE_NORMAL;
    const int type = static_cast<int>(flags & Qt::WindowType_Mask);
    const bool customized = (flags & Qt::CustomizeWindowHint);

    /* Menus, combo dropdowns, tooltips: transient surfaces the WM must not
       decorate at all.  On xwin the frame carries the title, the buttons and
       the resize corner, and none of those may appear on a popup. */
    if (type == Qt::Popup || type == Qt::ToolTip || type == Qt::SplashScreen)
        style |= XWIN_STYLE_NO_FRAME;

    if (flags & Qt::FramelessWindowHint)
        style |= XWIN_STYLE_NO_FRAME;
    if (customized && !(flags & Qt::WindowTitleHint))
        style |= XWIN_STYLE_NO_TITLE;
    /* xwin's only way to say "this window's size is not the user's to change"
       is NO_RESIZE, and Qt's only way to say it is to withhold the maximize
       button. */
    if (customized && !(flags & Qt::WindowMaximizeButtonHint))
        style |= XWIN_STYLE_NO_RESIZE;
    if (flags & Qt::WindowStaysOnTopHint)
        style |= XWIN_STYLE_SYSTOP;

    /* Qt's own popup handling still does the transient behaviour - grabbing
       the mouse, closing on an outside click. */
    return style;
}

EwokosWindow::EwokosWindow(QWindow *window)
    : QPlatformWindow(window)
    , m_xwin(nullptr)
    , m_closing(false)
    , m_exposed(false)
    , m_opacityFactor(256)
    , m_alpha(false)
    , m_buttons(Qt::NoButton)
    , m_modifiers(Qt::NoModifier)
    , m_lastPressTime(0)
    , m_lastPressButton(Qt::NoButton)
    , m_doubleClickArmed(false)
{
}

EwokosWindow::~EwokosWindow()
{
    /* QWindowPrivate::destroy() deletes the platform window, so this is the
       teardown path Qt itself uses.  A QPlatformWindow can also be deleted with
       the xwin still open if initialize() failed halfway, and teardown() is
       idempotent, so there is no ordering to get right here. */
    teardown();
}

void EwokosWindow::initialize()
{
    /* QPlatformWindow::initialize() is empty and the rect this object was
       constructed with is QWindow::geometry() verbatim, which for a top-level
       window nobody positioned is invalid.  initialGeometry() is the helper
       that resolves it: default size, clamped to the screen the window ended
       up on, honoring an explicit position if one was set. */
    QRect rect = initialGeometry(window(), geometry(),
                                 EWOK_DEFAULT_WIDTH, EWOK_DEFAULT_HEIGHT);
    QPlatformWindow::setGeometry(rect);
#if EWOK_QPA_TRACE
    ewokTrace("initialize %s flags=0x%llx style=0x%x want=%d,%d %dx%d "
              "(qwindow geometry was %d,%d %dx%d)\n",
              ewokId(this), (unsigned long long)window()->flags(),
              ewokosWindowStyle(window()->flags()),
              rect.x(), rect.y(), rect.width(), rect.height(),
              geometry().x(), geometry().y(),
              geometry().width(), geometry().height());
    ewokTraceWindows.append(this);
#endif

    EwokosIntegration *integration = EwokosIntegration::instance();
    x_t *x = integration ? integration->xContext() : nullptr;
    if (!x) {
        qWarning("ewokos: no x_t context; window \"%s\" will not be created",
                 qPrintable(window()->title()));
        return;
    }

    /* The title has to be right the first time: libx offers no setter, so the
       only place it is written is here (see setWindowTitle for what changing it
       later costs).  XWIN_TITLE_MAX is 32 including the NUL, so xwin_open()
       truncates - which is the server's business, not ours to pre-truncate. */
    const QByteArray title = window()->title().toUtf8();
    m_xwin = xwin_open(x, -1, rect.x(), rect.y(), rect.width(), rect.height(),
                       title.constData(), ewokosWindowStyle(window()->flags()));
    if (!m_xwin) {
        qWarning("ewokos: xwin_open failed for %dx%d+%d+%d \"%s\"",
                 rect.width(), rect.height(), rect.x(), rect.y(),
                 title.constData());
#if EWOK_QPA_TRACE
        ewokTrace("!! xwin_open RETURNED NULL for %s - Qt will still believe "
                  "this window exists\n", ewokId(this));
        ewokDumpWindows("xwin_open failed");
#endif
        return;
    }

    /* data first: every thunk recovers `this` from it, and xwin_open() has
       already published the window to the server, so an event can in principle
       arrive before the next line. */
    m_xwin->data = this;
    m_xwin->on_repaint = repaintThunk;
    m_xwin->on_event = eventThunk;
    m_xwin->on_resize = resizeThunk;
    m_xwin->on_move = moveThunk;
    m_xwin->on_focus = focusThunk;
    m_xwin->on_unfocus = unfocusThunk;
    m_xwin->on_close = closeThunk;

    /* libx's x_run() does this once, after the main window exists, and Qt never
       calls x_run() - the dispatcher replaces it.  Without the call xinfo->name
       stays empty, and it is what xserverd matches on when a window is looked
       up by name and what xcmd prints when it lists them, so the application
       would be anonymous to the server.  The value comes from the environment
       because x_exec() puts it there before exec'ing; x_set_app_name() rejects
       a null or empty name itself, so there is nothing to check here.

       Gated on main_win because x_set_app_name() always writes into
       x->main_win->xinfo, and xwin_open() above is what made this window the
       main one - for every later window the call would be a no-op that also
       could not succeed from anywhere else. */
    if (x->main_win == m_xwin)
        x_set_app_name(x, getenv("X_APP_NAME"));

    /* Before the first present, so the compositor never blits a frame of this
       window as opaque and then has to be told otherwise.  xwin_open() has just
       zeroed xinfo, and a fresh window has no cached blend on the display to
       invalidate, so this is the cheap case - the flag is simply right from the
       start. */
    updateAlpha();

    resurface(rect.size());

    /* The server is the authority on geometry from here on: xwin_open() clamps
       the request to the desktop space, and X_AUTO_FULL_SCREEN maximizes the
       window behind the client's back.  Adopt whatever it actually produced
       rather than reporting the request. */
    const QRect actual = serverGeometry();
    if (actual != rect) {
        QPlatformWindow::setGeometry(actual);
        resurface(actual.size());
    }

    QWindowSystemInterface::handleGeometryChange(window(), actual);

    /* Synchronous here and nowhere else.  This runs inside QWindow::create(),
       which is inside show(), and an application that paints straight after
       show() without entering the event loop has to find the window already
       exposed.  Everywhere else the delivery is queued, because the resize and
       repaint thunks run while libx holds painting_lock and a synchronously
       delivered expose event would make a widget repaint - which flushes -
       which calls xwin_repaint() - which takes that same non-recursive lock. */
    m_exposed = true;
    QWindowSystemInterface::handleExposeEvent<QWindowSystemInterface::SynchronousDelivery>(
        window(), QRegion(0, 0, actual.width(), actual.height()));
#if EWOK_QPA_TRACE
    ewokTrace("initialize done %s xwin=%p handle=%llx srv=%d,%d %dx%d "
              "vis=%d foc=%d\n",
              ewokId(this), (const void *)m_xwin,
              (unsigned long long)m_xwin->xinfo->win,
              actual.x(), actual.y(), actual.width(), actual.height(),
              (int)m_xwin->xinfo->visible, (int)m_xwin->xinfo->focused);
#endif
}

void EwokosWindow::teardown()
{
#if EWOK_QPA_TRACE
    /* before the m_xwin guard: a window whose xwin_open() failed still has to
       come off the trace list, or the next dump walks freed memory */
    ewokTrace("teardown %s xwin=%p\n", ewokId(this), (const void *)m_xwin);
    ewokTraceWindows.removeAll(this);
#endif
    if (!m_xwin)
        return;

    xwin_t *xwin = m_xwin;

    /* m_closing before xwin_close(): closeThunk() gates on it, so a stray
       server close request is refused while our own teardown is allowed
       through.  The other callbacks are dropped afterwards rather than before,
       because xwin_close() takes painting_lock and waits out any in-flight
       repaint - that repaint still needs repaintThunk to be valid.

       There is no base-class call to make: QPlatformWindow in 5.15 has no
       destroy() virtual, so unregistering this window from QWindow's list and
       from QGuiApplication's is entirely ~QPlatformWindow()'s job, and it runs
       on its own after ~EwokosWindow() has called this. */
    m_closing = true;
    xwin_close(xwin);

    xwin->data = nullptr;
    xwin->on_repaint = nullptr;
    xwin->on_event = nullptr;
    xwin->on_resize = nullptr;
    xwin->on_move = nullptr;
    xwin->on_focus = nullptr;
    xwin->on_unfocus = nullptr;
    xwin->on_close = nullptr;

    /* xwin_close() released the shm and the fd but left the struct; the
       registry entry is gone, so nothing else can find it. */
    xwin_destroy(xwin);
    m_xwin = nullptr;

    m_surface = QImage();
    m_dirty = QRegion();
    m_exposed = false;
    m_buttons = Qt::NoButton;
    /* The xwin that carried these is gone; a later initialize() opens one whose
       xinfo starts zeroed again. */
    m_alpha = false;
    m_opacityFactor = 256;
}

QRect EwokosWindow::serverGeometry() const
{
    if (!m_xwin || !m_xwin->xinfo)
        return QPlatformWindow::geometry();
    const grect_t &wsr = m_xwin->xinfo->wsr;
    return QRect(wsr.x, wsr.y, wsr.w, wsr.h);
}

void EwokosWindow::setGeometry(const QRect &rect)
{
    QPlatformWindow::setGeometry(rect);
    if (!m_xwin || !m_xwin->xinfo)
        return;

    /* Through the libx setters, never by writing xinfo->wsr: the server is the
       only writer of live geometry, and these stage the request into
       wsr_pending so the server applies it under its own lock together with the
       buffer rebuild that has to match it.  Writing wsr directly is exactly the
       tearing race that staging was introduced to remove.

       Size and position are separate calls because they are separate requests
       to the server, and asking for a resize that is already in effect would
       rebuild the canvas and throw away its contents for nothing. */
    const QRect current = serverGeometry();
#if EWOK_QPA_TRACE
    ewokTrace("setGeometry %s want=%d,%d %dx%d current=%d,%d %dx%d -> %s%s\n",
              ewokId(this), rect.x(), rect.y(), rect.width(), rect.height(),
              current.x(), current.y(), current.width(), current.height(),
              current.size() != rect.size() ? "RESIZE " : "",
              current.topLeft() != rect.topLeft() ? "MOVE" : "");
#endif
    if (current.size() != rect.size())
        xwin_resize_to(m_xwin, rect.width(), rect.height());
    if (current.topLeft() != rect.topLeft())
        xwin_move_to(m_xwin, rect.x(), rect.y());
}

QRect EwokosWindow::geometry() const
{
    return QPlatformWindow::geometry();
}

void EwokosWindow::setVisible(bool visible)
{
#if EWOK_QPA_TRACE
    const bool wasVisible = m_xwin && m_xwin->xinfo && m_xwin->xinfo->visible;
    ewokTrace("setVisible(%d) %s xwin=%p was=%d -> xwin_set_visible is %s\n",
              (int)visible, ewokId(this), (const void *)m_xwin, (int)wasVisible,
              (m_xwin && wasVisible == visible) ? "A NO-OP" : "issued");
#endif
    if (!m_xwin)
        return;

    xwin_set_visible(m_xwin, visible);
    m_exposed = visible;

    const QRect rect = serverGeometry();
#if EWOK_QPA_TRACE
    ewokTrace("setVisible done %s vis=%d foc=%d srv=%d,%d %dx%d\n",
              ewokId(this), (int)m_xwin->xinfo->visible,
              (int)m_xwin->xinfo->focused,
              rect.x(), rect.y(), rect.width(), rect.height());
    ewokDumpWindows(visible ? "after show" : "after hide");
#endif
    /* An empty region is how Qt says "not exposed".  Queued delivery: this can
       be reached from inside a server event, and the expose handler repaints. */
    QWindowSystemInterface::handleExposeEvent(
        window(), visible ? QRegion(0, 0, rect.width(), rect.height()) : QRegion());
}

void EwokosWindow::setWindowTitle(const QString &title)
{
    if (!m_xwin || !m_xwin->xinfo)
        return;

    /* libx has no title setter - xwin_open() writes this field with strncpy and
       is the only writer in the library.  xinfo is shared memory the WM reads
       to draw the decoration, and title is deliberately outside the geometry
       handoff (wsr_pending / state_pending / geom_pending) that the server
       applies under its lock, so writing it here cannot tear geometry. */
    const QByteArray utf8 = title.toUtf8();
    strncpy(m_xwin->xinfo->title, utf8.constData(), XWIN_TITLE_MAX - 1);
    m_xwin->xinfo->title[XWIN_TITLE_MAX - 1] = '\0';

    /* Ask for a repaint so the WM redraws the frame with the new title.  Heavier
       than the change warrants - there is no client-visible "refresh
       decoration only" request - but a title change is rare and a stale title
       bar is visible. */
    xwin_repaint_req(m_xwin);
}

void EwokosWindow::setWindowState(Qt::WindowStates state)
{
    if (!m_xwin)
        return;

    if (state & Qt::WindowFullScreen)
        xwin_fullscreen(m_xwin);
    else if (state & Qt::WindowMaximized)
        xwin_max(m_xwin);
    else if (state & Qt::WindowMinimized)
        /* xwin exposes no minimize-to-taskbar call to clients: XWIN_STATE_MIN
           exists in xinfo but only the WM ever sets it.  Hiding is the closest
           thing available, and Qt already treats a minimized window as one that
           should not be painting. */
        xwin_set_visible(m_xwin, false);

    /* WindowNoState needs nothing.  xwin_max() is a toggle - the server restores
       the pre-max geometry itself on the second XEVT_WIN_MAX - so there is no
       "unmaximize" for a client to issue. */
}

void EwokosWindow::setOpacity(qreal level)
{
    Q_UNUSED(level);
    /* QWindow::setOpacity() stores the value on itself before calling down
       here, so updateAlpha() reads it back off the QWindow.  Reading it rather
       than taking the argument is what also covers an opacity that was set
       before this platform window existed - see updateAlpha(). */
    updateAlpha();

    /* The factor is applied while copying m_surface into the canvas, so the
       frame already on screen is stale even though Qt's backing store did not
       change and no repaint is coming.  Mark everything and push one out;
       xwin_repaint() calls repaintThunk(), which is the only reader of both. */
    if (m_xwin && !m_surface.isNull()) {
        m_dirty = QRegion(0, 0, m_surface.width(), m_surface.height());
        present();
    }
}

QSurfaceFormat EwokosWindow::format() const
{
    /* QPlatformWindow's own returns QSurfaceFormat(), i.e. alphaBufferSize -1.
       See the header for why that made WA_TranslucentBackground undetectable. */
    return window() ? window()->requestedFormat() : QSurfaceFormat();
}

void EwokosWindow::propagateSizeHints()
{
    /* xwin has no size-hints protocol: no minimum, maximum, base size or resize
       increment can be published to the WM.  windowMinimumSize() and friends are
       therefore advisory only, enforced by Qt's own layout and not by the server.
       Deliberately not faked with XWIN_STYLE_NO_RESIZE, which would also stop a
       window that merely has a minimum size from being resized at all. */
}

void EwokosWindow::requestActivateWindow()
{
    if (!m_xwin)
        return;
    xwin_top(m_xwin);
    QWindowSystemInterface::handleWindowActivated(window());
}

void EwokosWindow::raise()
{
    if (m_xwin)
        xwin_top(m_xwin);
}

void EwokosWindow::lower()
{
    /* xwin_top() is the only stacking call libx publishes, and it raises.
       There is no client-side lower, so this is a no-op rather than a raise in
       disguise. */
}

WId EwokosWindow::winId() const
{
    /* xinfo->win is the handle the server echoes back in xevent_t::win and the
       key xwin_find_by_handle() resolves, so it is the only value here that
       means anything to the rest of the system. */
    return (m_xwin && m_xwin->xinfo) ? (WId)m_xwin->xinfo->win : 0;
}

bool EwokosWindow::isExposed() const
{
    return m_exposed;
}

bool EwokosWindow::setKeyboardGrabEnabled(bool grab)
{
    /* xserverd has no grab primitive: keys are routed to the focused window and
       to nothing else.  That is exactly what Qt wants a keyboard grab for -
       QMenu grabs on popup so the open menu, not the window under it, receives
       the keys - and on this platform the popup holds the server focus for as
       long as it is visible (xwin_set_visible(true) asks for focus, and the
       server re-homes it when the popup hides), so the grab is satisfied by
       construction.  Claiming it keeps Qt from falling back to paths that
       assume a grab-less platform and from warning on every single popup. */
    Q_UNUSED(grab);
    return true;
}

bool EwokosWindow::setMouseGrabEnabled(bool grab)
{
    /* xserverd does have a persistent pointer grab (XWIN_CNTL_GRAB_MOUSE, via
       xwin_grab_mouse): while it is held the server routes every mouse event
       to this window regardless of what the cursor is physically over, which
       is exactly the pointer-grab semantics Qt's popup menus are built on.

       QApplication grabs the mouse on the top-level popup when a menu opens
       (grabForPopup -> stealMouseGrab -> here) and QWidgetWindow::handleMouseEvent
       then redirects that raw global-coordinate stream to the topmost popup -
       the open submenu - through mapFromGlobal, and QMenu::mousePressEvent closes
       the whole cascade with hideUpToMenuBar() when a press lands outside it.

       Returning false here (the QPlatformWindow default) is what broke second
       level menus: popupGrabOk stayed false, so a submenu only received events
       while the cursor happened to sit over its own window, the outside-click
       dismiss and the replay path were skipped, and focus was left dangling when
       the cascade closed.  The grab is per-window and released by this same
       window when the last popup closes, so there is no state to track here.

       The window is already visible when Qt arms the grab (show_sys() runs
       before openPopup()), so xwin_grab_mouse() succeeds; its -1 - window gone,
       or somehow not visible - is reported back as a failed grab rather than
       claimed, so Qt does not assume routing that is not in effect. */
    if (!m_xwin)
        return false;
    return xwin_grab_mouse(m_xwin, grab) == 0;
}

void EwokosWindow::setDirtyRegion(const QRegion &region)
{
    m_dirty += region;
}

void EwokosWindow::updateAlpha()
{
    if (!m_xwin || !m_xwin->xinfo || !window())
        return;

    /* Both halves are re-derived rather than tracked from the calls that change
       them.  The format half has no notification to track at all: QWidget turns
       WA_TranslucentBackground into alphaBufferSize 8 on the QWindow's format
       (QWidgetPrivate::create) and updateIsTranslucent() can move it later,
       neither of which reaches a QPlatformWindow.  It reads a value worth
       looking at only because format() above forwards the request instead of
       inheriting QPlatformWindow's empty default.  The opacity half does have
       setOpacity(), but only once this object exists - see the header. */
    const qreal level = qBound(qreal(0), window()->opacity(), qreal(1));
    m_opacityFactor = qBound(0, qRound(level * 256.0), 256);

    /* An opacity below 1 needs the same server-side blend as per-pixel alpha:
       it is applied by scaling the alpha byte in repaintInto(), and the
       compositor only honors that byte on a window it was told about. */
    const bool want = level < 1.0 || window()->format().alphaBufferSize() > 0;
    if (want == m_alpha)
        return;
    m_alpha = want;

    /* xwin_set_alpha() publishes the flag and, on a real change, asks the server
       to refresh the display.  That refresh is not optional: the compositor
       caches whether this window's picture already sits blended on the display
       and, with the cache warm, copies only the fully opaque pixels - so a
       window that turns translucent mid-life would keep showing the opaque blit
       it got before.  A window that is still being created has no such cache
       yet, which is why the initialize() call costs nothing. */
    xwin_set_alpha(m_xwin, want);
}

void EwokosWindow::present()
{
    if (!m_xwin || !m_xwin->xinfo)
        return;

    /* Before the publish and outside it: this can turn into an UPDATE_INFO
       round trip, and xwin_repaint() holds painting_lock across the whole
       frame.  Cheap no-op on the overwhelming majority of frames - it only
       reaches the server when the application actually changed its mind about
       being translucent. */
    updateAlpha();

    /* This is the publish, and it blocks: xwin_repaint() takes painting_lock,
       marks the canvas mid-frame, calls repaintThunk() to copy our surface in,
       then hands the frame to the server and waits until the compositor has
       taken it.  A QPA flush on a software surface is supposed to mean "the
       pixels are with the window system", and this is the call that means it. */
    xwin_repaint(m_xwin);
}

void EwokosWindow::resurface(const QSize &size)
{
    if (size.isEmpty() || m_surface.size() == size)
        return;

    QImage next(size, QImage::Format_ARGB32);
    if (next.isNull())
        return;

    /* Keep what was there.  The server rebuilds the canvas on its own schedule
       - a WM resize fires on_resize before Qt has had a chance to repaint - and
       handing Qt a cleared surface would flash the window between the resize and
       the first paint that follows it. */
    if (m_surface.isNull())
        next.fill(Qt::transparent);
    else {
        QPainter painter(&next);
        painter.drawImage(0, 0, m_surface);
    }
    m_surface = next;

    /* The old contents no longer describe the whole canvas: rows that did not
       exist, and columns past the old width, have nothing in them. */
    m_dirty = QRegion(0, 0, size.width(), size.height());
}

void EwokosWindow::repaintInto(graph_t *g)
{
    if (!g || !g->buffer || m_surface.isNull())
        return;

    /* The canvas and our surface can disagree on size for one event: on_resize
       reads xinfo->wsr, which the server updates before the ws_g rebuild that
       matches it is published.  Clamp to the overlap instead of trusting either
       side - reading past the end of the shm here is not recoverable. */
    const int w = qMin(g->w, (int32_t)m_surface.width());
    const int h = qMin(g->h, (int32_t)m_surface.height());
    if (w <= 0 || h <= 0)
        return;

    QRegion region = m_dirty.isEmpty() ? QRegion(0, 0, w, h) : m_dirty;
    region &= QRegion(0, 0, w, h);

    /* QRegion's own begin()/end() rather than rects(): the latter is deprecated
       in 5.15 and builds a QVector just to walk it, while the iterator is a bare
       const QRect * into the region's rectangle array.  Iterating is also what
       keeps the cost proportional to the dirty area, which for a typical expose
       is a handful of rectangles. */
    const uint32_t factor = (uint32_t)m_opacityFactor;
    for (const QRect &r : region) {
        for (int y = r.top(); y <= r.bottom(); ++y) {
            /* constScanLine() accounts for the surface's bytesPerLine; the
               canvas is tightly packed at g->w pixels per row. */
            const QRgb *src = reinterpret_cast<const QRgb *>(m_surface.constScanLine(y)) + r.left();
            uint32_t *dst = g->buffer + (size_t)y * (size_t)g->w + (size_t)r.left();

            /* An opaque window's surface is byte-identical to the canvas - both
               are 0xAARRGGBB words and the compositor ignores the alpha byte -
               so a row is a memcpy.  A translucent one needs the conversion, and
               skipping it when the compositor is going to blit opaquely anyway
               is what keeps every non-translucent window in the tree on the fast
               path.  m_alpha rather than xinfo->alpha because updateAlpha() is
               the only writer of both and present() has just run it. */
            if (!m_alpha) {
                memcpy(dst, src, (size_t)r.width() * sizeof(uint32_t));
                continue;
            }
            const int n = r.width();
            for (int i = 0; i < n; ++i)
                dst[i] = ewokStraightAlpha(src[i], factor);
        }
    }

    m_dirty = QRegion();
}

bool EwokosWindow::isDoubleClick(Qt::MouseButton button, const QPoint &local) const
{
    if (!m_doubleClickArmed || button != m_lastPressButton)
        return false;
    if (QDateTime::currentMSecsSinceEpoch() - m_lastPressTime > ewokosDoubleClickInterval())
        return false;

    /* Qt publishes no double-click distance, so reuse the drag distance it does
       publish: it is the threshold Qt already uses everywhere else for "did the
       pointer move meaningfully", and reusing it means press-drag-release-press
       is not mistaken for a double click. */
    QStyleHints *hints = QGuiApplication::styleHints();
    const int threshold = hints ? qMax(4, hints->startDragDistance()) : 4;
    return (local - m_lastPressLocal).manhattanLength() <= threshold * 2;
}

void EwokosWindow::handleInputEvent(xevent_t *ev)
{
    if (!ev || !window())
        return;
    if (ev->type == XEVT_MOUSE)
        handleMouseEvent(ev);
    else if (ev->type == XEVT_IM)
        handleImEvent(ev);
}

void EwokosWindow::handleMouseEvent(xevent_t *ev)
{
    /* x/y are desktop-absolute and rx/ry are the motion delta - the field names
       suggest "root" and "relative to window" and both readings are wrong.
       SDL's ewokos backend subtracts wsr for exactly this reason, and so does
       every other client in the tree. */
    const QRect geo = serverGeometry();
    const QPoint global(ev->value.mouse.x, ev->value.mouse.y);
    const QPoint local(global.x() - geo.x(), global.y() - geo.y());
#if EWOK_QPA_TRACE
    /* One line per physical event: which xwin it landed on, and whether the
       two geometry views agree.  Qt redirects every mouse event to
       activePopupWidget() through mapFromGlobal(), which uses the cached
       QPlatformWindow::geometry() - so a desync shows up here as sync=0 and
       means the popup sees the click at the wrong place. */
    ewokTrace("mouse st=%d on %s global=(%d,%d) local=(%d,%d) srv=%d,%d %dx%d "
              "qt=%d,%d %dx%d sync=%d vis=%d foc=%d\n",
              (int)ev->state, ewokId(this), global.x(), global.y(),
              local.x(), local.y(),
              geo.x(), geo.y(), geo.width(), geo.height(),
              geometry().x(), geometry().y(),
              geometry().width(), geometry().height(),
              (int)(geo == geometry()),
              m_xwin && m_xwin->xinfo ? (int)m_xwin->xinfo->visible : -1,
              m_xwin && m_xwin->xinfo ? (int)m_xwin->xinfo->focused : -1);
#endif

    const int rawButton = ev->value.mouse.button;

    /* Scrolling arrives as a button code with no separate event type, and xwin
       reports a direction with no magnitude - so one event is one notch, and
       120 is Qt's one-notch delta. */
    if (rawButton >= MOUSE_BUTTON_SCROLL_UP && rawButton <= MOUSE_BUTTON_SCROLL_RIGHT) {
        QPoint angleDelta;
        switch (rawButton) {
        case MOUSE_BUTTON_SCROLL_UP:    angleDelta = QPoint(0, 120);   break;
        case MOUSE_BUTTON_SCROLL_DOWN:  angleDelta = QPoint(0, -120);  break;
        case MOUSE_BUTTON_SCROLL_LEFT:  angleDelta = QPoint(-120, 0);  break;
        case MOUSE_BUTTON_SCROLL_RIGHT: angleDelta = QPoint(120, 0);   break;
        default: break;
        }
        QWindowSystemInterface::handleWheelEvent(window(), QPointF(local), QPointF(global),
                                                 QPoint(), angleDelta, m_modifiers);
        return;
    }

    /* xwin reports the one button that changed and never a mask, but QMouseEvent
       wants both; the mask is latched here.  A press or release that names no
       button is taken as the primary one rather than dropped - xtouch fills the
       field, but a mouse event that somehow did not would otherwise swallow the
       click entirely. */
    Qt::MouseButton button = Qt::NoButton;
    switch (rawButton) {
    case MOUSE_BUTTON_LEFT:  button = Qt::LeftButton;   break;
    case MOUSE_BUTTON_MID:   button = Qt::MiddleButton; break;
    case MOUSE_BUTTON_RIGHT: button = Qt::RightButton;  break;
    default: break;
    }

    switch (ev->state) {
    case MOUSE_STATE_DOWN: {
        if (button == Qt::NoButton)
            button = Qt::LeftButton;
        m_buttons |= button;

        QEvent::Type type = QEvent::MouseButtonPress;
        if (isDoubleClick(button, local)) {
            /* Qt needs Press, Release, DblClick, Release - the second press of
               the pair becomes the DblClick, and the release that follows is
               delivered by the UP branch as an ordinary release. */
            type = QEvent::MouseButtonDblClick;
            m_doubleClickArmed = false;
        } else {
            m_doubleClickArmed = true;
            m_lastPressTime = QDateTime::currentMSecsSinceEpoch();
            m_lastPressLocal = local;
            m_lastPressButton = button;
        }
        QWindowSystemInterface::handleMouseEvent(window(), QPointF(local), QPointF(global),
                                                 m_buttons, button, type, m_modifiers);
        break;
    }
    case MOUSE_STATE_UP:
        if (button == Qt::NoButton)
            button = Qt::LeftButton;
        m_buttons &= ~button;
        QWindowSystemInterface::handleMouseEvent(window(), QPointF(local), QPointF(global),
                                                 m_buttons, button, QEvent::MouseButtonRelease,
                                                 m_modifiers);
        break;
    case MOUSE_STATE_MOVE:
    case MOUSE_STATE_DRAG:
        /* DRAG is "moved while a button is held".  The mask is already latched
           from the DOWN, so both are MouseMove and Qt tells them apart by the
           buttons it is handed. */
        QWindowSystemInterface::handleMouseEvent(window(), QPointF(local), QPointF(global),
                                                 m_buttons, Qt::NoButton, QEvent::MouseMove,
                                                 m_modifiers);
        break;
    case MOUSE_STATE_CLICK:
    case MOUSE_STATE_DOUBLE_CLICK:
        /* Deliberately dropped.  xserverd emits CLICK after the UP it already
           reported, so forwarding it would give the application a second
           press/release pair for one physical click; DOUBLE_CLICK is in the
           enum but the server never sets it, and the synthesis above is what
           produces Qt's MouseButtonDblClick instead. */
        break;
    default:
        break;
    }
}

void EwokosWindow::handleImEvent(xevent_t *ev)
{
    /* For XEVT_IM the press/release distinction is in ev->state, not in the im
       payload - which has no state field.  XIM_STATE_PRESS is 0, so the test has
       to be an equality against RELEASE and not a truthiness check. */
    const QEvent::Type type = (ev->state == XIM_STATE_RELEASE)
            ? QEvent::KeyRelease : QEvent::KeyPress;

    const EwokosKey key = ewokosTranslateKey(ev->value.im.key_code,
                                             ev->value.im.value,
                                             ev->value.im.shift,
                                             ev->value.im.ctrl != 0);
    if (key.key == 0)
        return;     /* unmappable code: dropping it beats inventing a key */

    m_modifiers = ewokosKeyModifiers(ev->value.im.key_code,
                                     ev->value.im.shift,
                                     ev->value.im.ctrl != 0);

    QWindowSystemInterface::handleKeyEvent(window(), type, key.key, m_modifiers, key.text);
}

void EwokosWindow::handleCloseRequest()
{
    if (!window())
        return;
    /* Queued.  The close event runs arbitrary application code - a "save
       changes?" dialog is the common case - and we are being called from inside
       the dispatcher's pump with an xevent_t still on its stack. */
    QWindowSystemInterface::handleCloseEvent(window());
}

void EwokosWindow::handleResize()
{
    if (!m_xwin || !m_xwin->xinfo || !window())
        return;

    const QRect rect = serverGeometry();
    /* The base setter, not the override: ours would push this size straight back
       to the server that just told us about it. */
    QPlatformWindow::setGeometry(rect);
    resurface(rect.size());

    /* Queued, and it has to be.  This runs from resizeThunk, which libx calls
       from inside x_get_graph() while painting_lock is held; a synchronously
       delivered expose event would repaint, flush, and re-enter xwin_repaint()
       on that same non-recursive mutex. */
    QWindowSystemInterface::handleGeometryChange(window(), rect);
    QWindowSystemInterface::handleExposeEvent(window(), QRegion(0, 0, rect.width(), rect.height()));
}

/* --- libx callbacks -------------------------------------------------------
 *
 * Plain C function pointers, so each one recovers `this` from xwin->data.  All
 * of them run on the thread that is pumping events, which for Qt is the GUI
 * thread - libx never calls back from a thread of its own.
 */

void EwokosWindow::repaintThunk(xwin_t *xwin, graph_t *g)
{
    EwokosWindow *self = static_cast<EwokosWindow *>(xwin->data);
    if (self)
        self->repaintInto(g);
}

void EwokosWindow::eventThunk(xwin_t *xwin, xevent_t *ev)
{
    EwokosWindow *self = static_cast<EwokosWindow *>(xwin->data);
    if (self)
        self->handleInputEvent(ev);
}

void EwokosWindow::resizeThunk(xwin_t *xwin)
{
    EwokosWindow *self = static_cast<EwokosWindow *>(xwin->data);
    if (self)
        self->handleResize();
}

void EwokosWindow::moveThunk(xwin_t *xwin)
{
    /* A move changes only the position, so handleResize() covers it: resurface()
       is a no-op when the size is unchanged and handleGeometryChange() carries
       the new position to Qt. */
    EwokosWindow *self = static_cast<EwokosWindow *>(xwin->data);
    if (self)
        self->handleResize();
}

void EwokosWindow::focusThunk(xwin_t *xwin)
{
    EwokosWindow *self = static_cast<EwokosWindow *>(xwin->data);
#if EWOK_QPA_TRACE
    ewokTrace("FOCUS  -> %s vis=%d\n", ewokId(self),
              xwin->xinfo ? (int)xwin->xinfo->visible : -1);
#endif
    if (self && self->window())
        QWindowSystemInterface::handleWindowActivated(self->window());
}

void EwokosWindow::unfocusThunk(xwin_t *xwin)
{
#if EWOK_QPA_TRACE
    EwokosWindow *self = static_cast<EwokosWindow *>(xwin->data);
    ewokTrace("UNFOCUS-> %s vis=%d (reported to Qt: nothing)\n", ewokId(self),
              xwin->xinfo ? (int)xwin->xinfo->visible : -1);
#endif
    /* Deliberately nothing.  handleWindowActivated(nullptr) does not mean
       "another of our windows is active now", it means "none of them is" -
       and with the ApplicationState capability undeclared that demotes the
       whole application to Qt::ApplicationInactive, whose ApplicationDeactivate
       event makes QApplication close every open popup.  Menus died the moment
       they opened: opening one focuses it, and the server unfocuses the main
       window first.  Focus moves are announced by the gaining window's FOCUS
       event, and an outside click closes popups through QApplication's own
       mouse-press path, so nothing needs this null report. */
    Q_UNUSED(xwin);
}

bool EwokosWindow::closeThunk(xwin_t *xwin)
{
    EwokosWindow *self = static_cast<EwokosWindow *>(xwin->data);
    /* Refuse unless we are the ones asking.  xwin_close() consults this before
       it tears anything down, so returning false is a real veto - and the
       dispatcher already intercepts the server's close request so that Qt gets
       to decide it.  This is the backstop for any path that reaches
       xwin_close() without going through teardown(). */
    return self ? self->m_closing : true;
}

QT_END_NAMESPACE
