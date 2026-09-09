/*
    EwokOS stub for QxtGlobalShortcut.

    The real qxt implementation registers system-wide hotkeys through
    X11/Windows/Carbon native event filters, none of which exist on the
    EwokOS QPA.  qterminal only uses it for the drop-down-mode show/hide
    hotkey, so a shortcut that never fires keeps everything else working:
    setShortcut() succeeds and remembers the sequence, activated() is
    simply never emitted.
*/

#ifndef QXTGLOBALSHORTCUT_H
#define QXTGLOBALSHORTCUT_H

#include <QObject>
#include <QKeySequence>

class QxtGlobalShortcut : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled)
    Q_PROPERTY(QKeySequence shortcut READ shortcut WRITE setShortcut)

public:
    explicit QxtGlobalShortcut(QObject* parent = nullptr)
        : QObject(parent), m_enabled(true) {}
    explicit QxtGlobalShortcut(const QKeySequence& shortcut, QObject* parent = nullptr)
        : QObject(parent), m_enabled(true), m_shortcut(shortcut) {}
    ~QxtGlobalShortcut() override = default;

    QKeySequence shortcut() const { return m_shortcut; }
    bool setShortcut(const QKeySequence& shortcut)
    {
        m_shortcut = shortcut;
        return true;
    }

    bool isEnabled() const { return m_enabled; }

public Q_SLOTS:
    void setEnabled(bool enabled = true) { m_enabled = enabled; }
    void setDisabled(bool disabled = true) { m_enabled = !disabled; }

Q_SIGNALS:
    void activated();

private:
    bool m_enabled;
    QKeySequence m_shortcut;
};

#endif // QXTGLOBALSHORTCUT_H
