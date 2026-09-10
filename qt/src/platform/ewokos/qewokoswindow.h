#ifndef QEWOKOSWINDOW_H
#define QEWOKOSWINDOW_H

#include <qpa/qplatformwindow.h>
#include <QtGui/qimage.h>
#include <QtGui/qregion.h>

#include <x/xwin.h>

QT_BEGIN_NAMESPACE

/*
 * One QPlatformWindow per xwin_t.
 *
 * Painting model.  xwin gives a client a shared-memory canvas (ws_g) that the
 * compositor reads directly - there is no private snapshot any more - so
 * writing into it while the compositor samples it is a visible race.  libx
 * brackets the safe window with xwin_repaint(): it sets xinfo->painting, calls
 * on_repaint, and only then publishes the frame.  Qt has no place to hook that
 * bracket - QPlatformBackingStore::beginPaint/endPaint run outside it and flush
 * runs before it - so this class keeps its own QImage, lets Qt paint into that,
 * and copies into the canvas from inside on_repaint where the bracket is
 * already held.
 *
 * That costs one copy per flush, which is what every other GUI in this tree
 * pays (doom, macemu, SDL2's ewokos backend).  The zero-copy alternative is to
 * wrap ws_g in a QImage and set on_repaint to nothing; it is one flag away
 * (claim xinfo->painting in beginPaint, release in endPaint) but needs libx to
 * publish that claim as an API before it is safe, because painting is also
 * written by xwin_repaint itself under painting_lock.
 *
 * Event model.  The dispatcher polls the server and routes events here; see
 * qewokoseventdispatcher.cpp for why XEVT_WIN_CLOSE is intercepted before it
 * can reach xwin_event_handle().
 */
class EwokosWindow : public QPlatformWindow
{
public:
    explicit EwokosWindow(QWindow *window);
    ~EwokosWindow() override;

    void initialize() override;

    void setGeometry(const QRect &rect) override;
    QRect geometry() const override;
    void setVisible(bool visible) override;
    void setWindowTitle(const QString &title) override;
    void setWindowState(Qt::WindowStates state) override;
    void setOpacity(qreal level) override;

    /* The format Qt asked for, which for this platform is also the format it
       has.

       QPlatformWindow::format() returns a default-constructed QSurfaceFormat,
       whose alphaBufferSize is -1, and QWindow::format() forwards to it as soon
       as a platform window exists.  QWidget meanwhile writes
       WA_TranslucentBackground into QWindowPrivate::requestedFormat -
       QWidgetPrivate::create_sys() before the window is created, and
       updateIsTranslucent() on every later change.  So QWindow::format()
       reported alpha both before creation and after it only if the QPA passed
       the request through, and without this override it did not: the alpha bit
       was invisible to anything downstream, updateAlpha() included, which is
       what left a window translucent only through setWindowOpacity().

       Nothing here resolves or negotiates a format - the surface is a plain
       ARGB32 QImage - so requested is actual and passing it back is the truth.
       It also makes QWindow::format() stop changing meaning across creation. */
    QSurfaceFormat format() const override;

    void propagateSizeHints() override;
    void requestActivateWindow() override;
    void raise() override;
    void lower() override;
    WId winId() const override;
    bool isExposed() const override;
    bool setKeyboardGrabEnabled(bool grab) override;
    bool setMouseGrabEnabled(bool grab) override;

    /* The xwin this window owns.  Null before initialize() and after teardown(). */
    xwin_t *xwin() const { return m_xwin; }

    /* --- used by EwokosBackingStore --- */

    /* The QImage Qt paints into.  Always the size of the canvas, so paintDevice()
       never has to be recreated around a resize. */
    QImage *paintSurface() { return &m_surface; }

    /* Records what flush() changed, so on_repaint copies only those rows. */
    void setDirtyRegion(const QRegion &region);

    /* Publishes the frame.  Blocks until the server has composited it, which is
       exactly what a QPA flush is supposed to mean for a software surface. */
    void present();

    /* --- used by EwokosEventDispatcher --- */

    /* One XEVT_MOUSE or XEVT_IM event for this window. */
    void handleInputEvent(xevent_t *ev);

    /* The server asked for the window to be closed.  Hands the decision to Qt;
       if the application accepts it, QWindow::destroy() deletes this platform
       window and the teardown happens in the destructor. */
    void handleCloseRequest();

    /* The workspace canvas changed size (server rebuild, or WM resize/max). */
    void handleResize();

private:
    /* libx callbacks are plain C function pointers, so every one of them is a
       thunk that recovers `this` from xwin->data. */
    static void repaintThunk(xwin_t *xwin, graph_t *g);
    static void eventThunk(xwin_t *xwin, xevent_t *ev);
    static void resizeThunk(xwin_t *xwin);
    static void moveThunk(xwin_t *xwin);
    static void focusThunk(xwin_t *xwin);
    static void unfocusThunk(xwin_t *xwin);
    static bool closeThunk(xwin_t *xwin);

    void repaintInto(graph_t *g);
    void resurface(const QSize &size);

    /* Publishes the window's translucency to the server.

       xwin_open() zeroes xinfo, so xinfo->alpha starts off and the compositor
       blits every pixel of the canvas as opaque - whatever alpha Qt painted
       into m_surface is simply discarded.  This is what turns that around: it
       re-reads the two things that can ask for translucency (an alpha-capable
       surface format, which is what QWidget's WA_TranslucentBackground ends up
       as, and a window opacity below 1) and flips xinfo->alpha to match.  The
       format half only reads a real value because format() above passes the
       request through instead of inheriting the base's empty one.

       Re-reading rather than being told is load-bearing for the opacity half:
       QWidget::setWindowOpacity() before the window exists only lands in
       QWindowPrivate::opacity, and QWindowPrivate::create() never replays it to
       the platform window, so QPlatformWindow::setOpacity() is not called for
       it.  initialize() is the only place that value can be picked up.  Called
       again from present(), which is where a WA_TranslucentBackground set after
       creation shows up. */
    void updateAlpha();

    /* Closes and frees the xwin, drops every callback and clears the painting
       state.  Idempotent, and called from the destructor.

       This is not an override: QPlatformWindow in Qt 5.15 has no destroy()
       virtual - QWindowPrivate::destroy() simply deletes the platform window,
       so the destructor is the teardown hook.  Keeping it a named function
       rather than inlining it into ~EwokosWindow() is what lets the destructor
       be reached twice (once from a failed initialize(), once from QWindow
       teardown) without repeating the ordering constraints documented below. */
    void teardown();
    void handleMouseEvent(xevent_t *ev);
    void handleImEvent(xevent_t *ev);

    /* Where the server actually put the window.  xwin_open() clamps the request
       to the desktop space and X_AUTO_FULL_SCREEN can maximize it behind the
       client's back, so xinfo->wsr - not the geometry Qt asked for - is what has
       to be read back and what mouse coordinates are relative to. */
    QRect serverGeometry() const;

    /* Second press of a pair.  See m_doubleClickArmed. */
    bool isDoubleClick(Qt::MouseButton button, const QPoint &local) const;

    xwin_t *m_xwin;
    QImage m_surface;
    QRegion m_dirty;
    bool m_closing;
    bool m_exposed;

    /* Translucency state, both halves of what updateAlpha() derives.  The
       factor is the window-wide opacity as 0..256 fixed point, applied while
       copying m_surface into the canvas; m_alpha mirrors xinfo->alpha and is
       what keeps repaintInto() from converting pixels the server would then
       blit as opaque anyway. */
    int m_opacityFactor;
    bool m_alpha;

    /* Mouse state the server does not carry: xwin reports one button per event
       and never a button mask, but QMouseEvent wants both. */
    Qt::MouseButtons m_buttons;
    Qt::KeyboardModifiers m_modifiers;

    /* Double-click synthesis.  xserverd emits DOWN, [MOVE|DRAG...], UP and then
       a redundant CLICK; MOUSE_STATE_DOUBLE_CLICK exists in the enum but the
       server never sets it (grep xinput.c).  Qt will not synthesize the second
       click itself, so without this QTextEdit never selects a word on
       double-click and QListView never enters rename. */
    qint64 m_lastPressTime;
    QPoint m_lastPressLocal;
    Qt::MouseButton m_lastPressButton;
    bool m_doubleClickArmed;
};

QT_END_NAMESPACE

#endif // QEWOKOSWINDOW_H
