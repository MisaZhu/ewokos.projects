#ifndef QEWOKOSEVENTDISPATCHER_H
#define QEWOKOSEVENTDISPATCHER_H

#include <QtCore/private/qeventdispatcher_unix_p.h>

QT_BEGIN_NAMESPACE

/*
 * QEventDispatcherUNIX plus the window server's event queue.
 *
 * Everything Qt already does well is reused as it stands - QTimerInfoList for
 * timers, poll() over QSocketNotifier's file descriptors, QThreadPipe for
 * cross-thread wakeup - and the only thing added is x_poll_event(), drained at
 * the top of every pass.  That is why this derives from QEventDispatcherUNIX
 * rather than from QAbstractEventDispatcher: reimplementing the timer and
 * notifier machinery to add one non-blocking call would be a lot of code that
 * already exists and is already correct.
 *
 * What cannot be reused is the blocking.  See processEvents().
 */
class EwokosEventDispatcherPrivate : public QEventDispatcherUNIXPrivate
{
public:
    /* Drains what the window server has pending and hands each event to the
       window it belongs to.  Returns the number taken.  Never blocks. */
    int pumpXEvents();
};

class EwokosEventDispatcher : public QEventDispatcherUNIX
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(EwokosEventDispatcher)

public:
    explicit EwokosEventDispatcher(QObject *parent = nullptr);
    ~EwokosEventDispatcher() override;

    bool processEvents(QEventLoop::ProcessEventsFlags flags) override;
};

QT_END_NAMESPACE

#endif // QEWOKOSEVENTDISPATCHER_H
