/*
 * Qt Widgets on EwokOS.
 *
 * Not a showcase so much as a smoke test with a user interface: every widget and
 * every event path here exercises something the platform plugin has to get right,
 * and each one is called out where it is used.  If this window opens, takes
 * typing, scrolls, resizes, ticks a timer and closes on the second attempt, the
 * QPA plugin is doing its job.
 */

#include <QtWidgets/qapplication.h>
#include <QtWidgets/qgridlayout.h>
#include <QtWidgets/qlabel.h>
#include <QtWidgets/qlineedit.h>
#include <QtWidgets/qlistwidget.h>
#include <QtWidgets/qmainwindow.h>
#include <QtWidgets/qmenu.h>
#include <QtWidgets/qmenubar.h>
#include <QtWidgets/qpushbutton.h>
#include <QtWidgets/qstatusbar.h>
#include <QtWidgets/qtoolbar.h>
#include <QtWidgets/qwidget.h>

#include <QtCore/qdatetime.h>
#include <QtCore/qtimer.h>

/* QCloseEvent lives in qevent.h - there is no qcloseevent.h in QtGui, and the
   whole QEvent hierarchy shares that one header. */
#include <QtGui/qevent.h>

#include <qt/ewokosqt.h>

class DemoWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit DemoWindow(QWidget *parent = nullptr);

protected:
    /* The close veto is the reason this is overridden at all.  xwin's server
       closes a window the moment the WM asks, without consulting the client -
       xwin_event_handle() calls xwin_close() on XEVT_WIN_CLOSE - so the plugin
       intercepts that event and routes it here as a QCloseEvent instead.  Refusing
       the first one proves the interception works; accepting the second one proves
       the window really does go away when Qt says so. */
    void closeEvent(QCloseEvent *event) override;

private slots:
    void addEntry();
    void tick();

private:
    QLineEdit *m_entry;
    QListWidget *m_list;
    QLabel *m_clock;
    QLabel *m_clicks;
    QTimer m_timer;
    int m_ticks;
    int m_clickCount;
    int m_closeAttempts;
};

DemoWindow::DemoWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_entry(nullptr)
    , m_list(nullptr)
    , m_clock(nullptr)
    , m_clicks(nullptr)
    , m_ticks(0)
    , m_clickCount(0)
    , m_closeAttempts(0)
{
    setWindowTitle(QStringLiteral("Qt on EwokOS"));

    /* Menus and the toolbar are separate top-level xwin windows once they pop up,
       which is why the integration claims MultipleWindows. */
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("&Add entry"), this, &DemoWindow::addEntry);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("E&xit"), this, &QWidget::close);

    QToolBar *bar = addToolBar(QStringLiteral("main"));
    bar->addAction(QStringLiteral("Add"), this, &DemoWindow::addEntry);

    QWidget *central = new QWidget(this);
    QGridLayout *grid = new QGridLayout(central);

    /* A QLineEdit is the strictest test of the keyboard path: it needs key events
       with correct text, correct shift handling and working Backspace, which is
       where EwokOS's key_code/value split and its non-boolean shift field would
       show up first. */
    m_entry = new QLineEdit(central);
    m_entry->setPlaceholderText(QStringLiteral("type here, then press Enter"));
    grid->addWidget(m_entry, 0, 0, 1, 2);

    QPushButton *addButton = new QPushButton(QStringLiteral("Add"), central);
    grid->addWidget(addButton, 1, 0);
    connect(addButton, &QPushButton::clicked, this, &DemoWindow::addEntry);
    connect(m_entry, &QLineEdit::returnPressed, this, &DemoWindow::addEntry);

    /* A QListWidget is the strictest test of the mouse path: it needs press,
       release, move, drag-selection and - because it enters rename on a double
       click - the MouseButtonDblClick the server never sends and the plugin has
       to synthesize. */
    m_list = new QListWidget(central);
    m_list->addItem(QStringLiteral("scroll me"));
    m_list->addItem(QStringLiteral("double-click me to rename"));
    for (int i = 0; i < 40; ++i)
        m_list->addItem(QStringLiteral("filler row %1").arg(i));
    grid->addWidget(m_list, 2, 0, 1, 2);

    m_clicks = new QLabel(QStringLiteral("clicks: 0"), central);
    grid->addWidget(m_clicks, 3, 0);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *) {
        m_clicks->setText(QStringLiteral("clicks: %1").arg(++m_clickCount));
    });

    /* A timer proves the dispatcher's QTimerInfoList path is live.  Without it a
       window would still draw and still take input, so this is the part of the
       port that would otherwise look fine and not be. */
    m_clock = new QLabel(central);
    grid->addWidget(m_clock, 3, 1);
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &DemoWindow::tick);
    m_timer.start();
    tick();

    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("close me once: I will refuse"));

    /* Resizing this window goes to the server as a staged geometry request and
       comes back as XEVT_WIN_RESIZE, at which point the plugin grows its surface.
       A layout that can be squeezed exercises that path in both directions. */
    resize(420, 360);
}

void DemoWindow::addEntry()
{
    const QString text = m_entry->text();
    if (text.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("nothing to add"), 2000);
        return;
    }
    m_list->insertItem(0, text);
    m_list->setCurrentRow(0);
    m_entry->clear();
    statusBar()->showMessage(QStringLiteral("added \"%1\"").arg(text), 2000);
}

void DemoWindow::tick()
{
    ++m_ticks;
    m_clock->setText(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"))
                     + QStringLiteral("  (%1)").arg(m_ticks));
}

void DemoWindow::closeEvent(QCloseEvent *event)
{
    if (++m_closeAttempts == 1) {
        event->ignore();
        statusBar()->showMessage(QStringLiteral(
            "close refused - the WM asked, Qt said no, the window stayed"));
        return;
    }
    event->accept();
}

int main(int argc, char *argv[])
{
    /* Before the QApplication: this is what registers the statically linked
       platform plugin and selects it.  See include/qt/ewokosqt.h. */
    ewokosQtInit();

    QApplication app(argc, argv);

    DemoWindow window;
    window.show();

    return app.exec();
}

#include "main.moc"
