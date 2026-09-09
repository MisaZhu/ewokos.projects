#ifndef QEWOKOSINTEGRATION_H
#define QEWOKOSINTEGRATION_H

#include <qpa/qplatformintegration.h>

#include <x/x.h>

QT_BEGIN_NAMESPACE

class EwokosScreen;

/*
 * One integration per process, owning the single libx context every window
 * shares.
 *
 * libx's model is one x_t per application, opened against /dev/x, holding the
 * main window, the prompt window and a queue of events pushed locally with
 * x_push_event().  Qt's model is one integration per QGuiApplication.  The two
 * line up, so the x_t lives here rather than per window: windows reach it
 * through instance()->xContext(), and the dispatcher reaches it the same way to
 * pump events.
 *
 * initialize() rather than the constructor is where the screen appears, because
 * handleScreenAdded() posts a QScreen into QGuiApplication's screen list and
 * that list is not usable until the integration is installed - which is what
 * initialize() means.  linuxfb does it in the same place.
 */
class EwokosIntegration : public QPlatformIntegration
{
public:
    explicit EwokosIntegration(const QStringList &paramList);
    ~EwokosIntegration() override;

    bool hasCapability(Capability cap) const override;

    QPlatformWindow *createPlatformWindow(QWindow *window) const override;
    QPlatformBackingStore *createPlatformBackingStore(QWindow *window) const override;
    QAbstractEventDispatcher *createEventDispatcher() const override;

    /* An accessor, not a factory - QPlatformIntegration has no
       createPlatformFontDatabase(), and QFontDatabase may call this more than
       once, so the database has to outlive the call.  minimal caches it in a
       mutable member for exactly that reason; same here. */
    QPlatformFontDatabase *fontDatabase() const override;

    void initialize() override;

    /* The libx context.  Never null after the constructor, but not necessarily
       usable: x_init() resolves /dev/x and leaves dev_fsinfo empty when the
       window server is not running, and every window creation checks that. */
    x_t *xContext() { return &m_x; }

    /* Set in the constructor, cleared in the destructor.  There is only ever one
       integration, so a plain static is what the rest of the plugin uses rather
       than reaching for QGuiApplicationPrivate::platformIntegration(). */
    static EwokosIntegration *instance() { return s_instance; }

private:
    x_t m_x;
    EwokosScreen *m_screen;
    mutable QPlatformFontDatabase *m_fontDatabase;

    static EwokosIntegration *s_instance;
};

QT_END_NAMESPACE

#endif // QEWOKOSINTEGRATION_H
