#include "qewokoseventdispatcher.h"
#include "qewokosintegration.h"
#include "qewokoswindow.h"

#include <qpa/qwindowsysteminterface.h>

#include <QtCore/private/qthread_p.h>
#include <QtCore/qdebug.h>
#include <QtGui/qguiapplication.h>

#include <x/xwin.h>

#include <unistd.h>

QT_BEGIN_NAMESPACE

/*
 * How many server events one pass may take.
 *
 * A busy server - a drag with the mouse moving - can keep x_poll_event()
 * returning faster than Qt can consume, and an unbounded drain would keep a
 * single processEvents() call from ever returning.  That is not a theoretical
 * concern: QEventLoop::processEvents() is expected to come back so timers can
 * run and QCoreApplication::sendPostedEvents() can drain, and a pass that does
 * not return starves both.  64 is well past what a frame's worth of mouse motion
 * produces.
 */
static const int EWOK_MAX_EVENTS_PER_PASS = 64;

/*
 * The idle polling interval, in milliseconds, and the single knob this dispatcher
 * adds to Qt's event loop.
 *
 * Why an interval is needed at all: x events have no wakeup.  The window server
 * pushes to x->evt_node and the only reader of that node is this thread, so a
 * pass that blocks in poll() with no timeout blocks until a Qt timer fires or
 * another thread writes the QThreadPipe - neither of which the server does.
 * QEventDispatcherUNIX can afford an indefinite wait because QThreadPipe gives
 * any other thread a way to end it; here nothing does.
 *
 * What it costs: an idle application wakes this often, and each wake is one
 * non-blocking round trip to /dev/x.  20 ms is 50 of those a second, which is
 * under the frame budget for input latency and small enough not to matter next
 * to what xserverd itself does.  x_run() uses 100 ms between its own blocking
 * reads, so this is the more responsive end of what the tree already accepts.
 *
 * QT_EWOKOS_POLL_CAP overrides it, in milliseconds, clamped to [1,1000]: below 1
 * the loop degenerates into a spin and above 1000 input feels broken.
 */
static const int EWOK_DEFAULT_POLL_CAP_MS = 20;

static int ewokosPollCapMs()
{
    bool ok = false;
    const int v = qEnvironmentVariableIntValue("QT_EWOKOS_POLL_CAP", &ok);
    if (ok && v >= 1 && v <= 1000)
        return v;
    return EWOK_DEFAULT_POLL_CAP_MS;
}

/*
 * One server event to one window.
 *
 * The close request is intercepted here and nowhere else, and the reason is that
 * xwin_dispatch_event() routes XEVT_WIN to xwin_event_handle(), which calls
 * xwin_close() on XEVT_WIN_CLOSE without consulting anybody - and xwin_close()
 * then calls x_terminate() when the window it closed was the main one.  Qt's
 * whole position is that the application receives a QCloseEvent and may refuse
 * it: a document editor with unsaved changes has to be able to say no.  So the
 * decision goes to Qt first, and the window is torn down by Qt deleting the
 * platform window - QWindowPrivate::destroy() -> ~EwokosWindow() ->
 * EwokosWindow::teardown(), which sets m_closing so that closeThunk() - the veto
 * libx consults - lets that particular xwin_close() through.
 *
 * Anything else is handed to libx's own router, which does the registry lookup,
 * the XEVT_WIN split and the prompt_win guard.  Duplicating that here would mean
 * drifting from it.
 */
static void routeXEvent(x_t *x, xevent_t *ev)
{
    if (ev->type == XEVT_WIN && ev->value.window.event == XEVT_WIN_CLOSE) {
        xwin_t *xwin = xwin_find_by_handle(ev->win);
        EwokosWindow *w = xwin ? static_cast<EwokosWindow *>(xwin->data) : nullptr;
        if (w) {
            w->handleCloseRequest();
            return;
        }
        /* No EwokosWindow behind the handle - a window libx owns that Qt does
           not, which happens if an application mixes the two.  Let libx close
           it the way it always would. */
    }
    xwin_dispatch_event(x, ev);
}

int EwokosEventDispatcherPrivate::pumpXEvents()
{
    EwokosIntegration *integration = EwokosIntegration::instance();
    if (!integration)
        return 0;

    x_t *x = integration->xContext();
    xevent_t ev;
    int taken = 0;

    while (taken < EWOK_MAX_EVENTS_PER_PASS && x_poll_event(x, &ev) == 0) {
        ++taken;
        routeXEvent(x, &ev);
    }

    /* xwin_close() sets x->terminated when the window it closed was the main
       one; that is how a plain libx application learns to exit.  Qt normally
       learns the same thing from quitOnLastWindowClosed(), and the two agree on
       every path that goes through EwokosWindow::teardown().  This covers the one
       that does not: something outside Qt took the main window away - the WM
       killing the app, a session shutdown - leaving a process with no main
       window and no way to get one back. */
    if (x->terminated)
        QGuiApplication::quit();

    return taken;
}

EwokosEventDispatcher::EwokosEventDispatcher(QObject *parent)
    : QEventDispatcherUNIX(*new EwokosEventDispatcherPrivate, parent)
{
}

EwokosEventDispatcher::~EwokosEventDispatcher()
{
}

bool EwokosEventDispatcher::processEvents(QEventLoop::ProcessEventsFlags flags)
{
    Q_D(EwokosEventDispatcher);

    /* Same first line as the base: interrupt() is a one-shot request to make this
       pass come back, and processEvents() is the only place it is consumed. */
    d->interrupt.storeRelaxed(0);

    /* 1. The window server, non-blocking and first.  It is the only source that
          can change what is on screen, and nothing else in this pass can see its
          events - they are not file descriptors and they are not posted Qt
          events, they are a queue in libx that only x_poll_event() drains. */
    int nevents = d->pumpXEvents();

    /* 2. What those produced on the Qt side.  EwokosWindow delivers through
          QWindowSystemInterface with queued delivery everywhere except window
          creation, because its resize and repaint paths run while libx holds
          painting_lock - so a pump that moved the mouse or resized a window has
          not yet reached a single widget.  This is the step that delivers it,
          and it is what makes the repaint happen. */
    if (QWindowSystemInterface::sendWindowSystemEvents(flags))
        ++nevents;

    /* The base's own condition for "this pass is allowed to wait", spelled out
       here because the sleep below has to obey it too: a thread that has been
       told not to wait (canWaitLocked) or that has been interrupted must not be
       put to sleep by anything. */
    QThreadData *threadData = d->threadData.loadRelaxed();
    const bool mayWait = (flags & QEventLoop::WaitForMoreEvents)
                         && threadData->canWaitLocked()
                         && !d->interrupt.loadRelaxed();

    /* Emitted before the base call rather than around the sleep, because the base
       emits awake() as its own first act.  That puts the pair in the order Qt
       documents - aboutToBlock(), then awake() - across every pass, idle or not. */
    if (mayWait && nevents == 0)
        emit aboutToBlock();

    /* 3. Everything Qt owns: posted events, timers, socket notifiers.

          WaitForMoreEvents is stripped, and it has to be.  With it set the base
          works out a poll() timeout of "until the next timer, or effectively
          forever if there is none" and blocks for it - which is safe for it,
          because QThreadPipe gives any other thread a way to end that block, and
          fatal here, because the window server is not another thread and reaches
          this process through a queue nobody polls.  Stripped, canWait is false,
          the timeout is zero and poll() returns at once. */
    if (QEventDispatcherUNIX::processEvents(flags & ~QEventLoop::WaitForMoreEvents))
        nevents = 1;

    /* 3b. Publish presents the flip could not.  With fps_async on, xwin_repaint()
          renders into ws_g and then flips ws_g onto the handoff buffer the
          server composites - but only while the server has consumed the previous
          submission.  A frame that loses that race is not dropped: libx parks it
          as present_pending and the contract is that the event loop calls
          xwin_retry_pending_presents() once per pass to publish it as soon as
          the server lets go.  x_run() honours that; this dispatcher replaces
          x_run(), so the call has to live here.  Without it the skipped frame
          sits in ws_g forever: the screen keeps the pre-resize picture with
          black unpainted bands no matter how often Qt redraws, which is exactly
          what happened on every window resize.  No-op when nothing is pending. */
    xwin_retry_pending_presents();

    /* 4. An idle pass.  Without this the loop above spins: processEvents()
          returns false, QEventLoop calls it again immediately, and the CPU is
          burned polling /dev/x 64 times a millisecond instead of 50 times a
          second.  The price is that input latency is bounded by the cap - and by
          nothing else, since a server event arriving during the sleep is not seen
          until it ends.

          interrupt() from another thread does not cut this short: it writes the
          QThreadPipe, which the sleep is not watching.  The cap bounds the delay
          to at most one interval, which is why the clamp in ewokosPollCapMs()
          refuses anything above a second. */
    if (mayWait && nevents == 0)
        usleep((unsigned)(ewokosPollCapMs() * 1000));

    return nevents > 0;
}

QT_END_NAMESPACE

#include "moc_qewokoseventdispatcher.cpp"
