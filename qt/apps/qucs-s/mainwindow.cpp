/*
 * Qucs-S, ported to EwokOS - the application window.
 *
 * See mainwindow.h for the mapping onto upstream's qucs/qucs.cpp.
 */

#include "mainwindow.h"

#include "compdialog.h"
#include "components.h"
#include "misc.h"
#include "schematic.h"
#include "view.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <math.h>

// Where the shipped examples live in the rootfs - the same shape simulide uses
// for its own data, and the Makefile defines it.
#ifndef QUCS_EXAMPLES_DIR
#define QUCS_EXAMPLES_DIR "/apps/qucs-s/res/examples"
#endif

static const char *kVersion = "1.0-ewok";

// ---- toolbar icons ----------------------------------------------------------

enum {
    IcoNew = 0, IcoOpen, IcoSave,
    IcoSelect, IcoWire, IcoLabel, IcoDiagram,
    IcoRotate, IcoMirror, IcoDelete, IcoActive,
    IcoSimulate, IcoOpPoint, IcoNetlist, IcoAbout
};

// This Qt build has no icon theme to borrow from - the platform plugin offers
// none and there is no QIconLoader behind it - so every picture the window
// shows is drawn here, the way components.cpp draws the symbols.  All of them
// are laid out on a 22 x 22 grid and scaled to whatever size is asked for.
static QIcon toolIcon(int what, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(size / 22.0, size / 22.0);
    const QColor ink(0, 0, 130);
    p.setPen(QPen(ink, 1.6));
    p.setBrush(Qt::NoBrush);

    switch (what) {
    case IcoNew:
        p.drawPolyline(QPolygonF() << QPointF(5, 2) << QPointF(14, 2) << QPointF(18, 6)
                       << QPointF(18, 20) << QPointF(5, 20) << QPointF(5, 2));
        p.drawPolyline(QPolygonF() << QPointF(14, 2) << QPointF(14, 6) << QPointF(18, 6));
        break;

    case IcoOpen:
        p.drawPolyline(QPolygonF() << QPointF(2, 6) << QPointF(9, 6) << QPointF(11, 8)
                       << QPointF(20, 8) << QPointF(20, 18) << QPointF(2, 18)
                       << QPointF(2, 6));
        break;

    case IcoSave:
        p.drawRect(QRectF(3, 3, 16, 16));
        p.drawRect(QRectF(7, 3, 8, 6));
        p.drawRect(QRectF(7, 13, 8, 6));
        break;

    case IcoSelect:
        p.setBrush(ink);
        p.drawPolygon(QPolygonF() << QPointF(5, 2) << QPointF(5, 17) << QPointF(9, 13)
                      << QPointF(12, 19) << QPointF(14.5, 18) << QPointF(11, 12)
                      << QPointF(16, 12));
        break;

    case IcoWire:
        p.drawPolyline(QPolygonF() << QPointF(3, 17) << QPointF(9, 17) << QPointF(9, 6)
                       << QPointF(19, 6));
        p.setBrush(ink);
        p.drawEllipse(QPointF(3, 17), 1.8, 1.8);
        p.drawEllipse(QPointF(19, 6), 1.8, 1.8);
        break;

    case IcoLabel:
        p.drawLine(QPointF(2, 15), QPointF(20, 15));
        p.drawRect(QRectF(6, 4, 10, 8));
        p.setPen(QPen(ink, 1.1));
        p.drawLine(QPointF(8, 7), QPointF(14, 7));
        p.drawLine(QPointF(8, 9.5), QPointF(12, 9.5));
        break;

    case IcoDiagram:
        p.drawPolyline(QPolygonF() << QPointF(3, 3) << QPointF(3, 18) << QPointF(20, 18));
        p.setPen(QPen(QColor(180, 30, 30), 1.6));
        p.drawPolyline(QPolygonF() << QPointF(4, 15) << QPointF(8, 14) << QPointF(11, 9)
                       << QPointF(14, 6) << QPointF(19, 5));
        break;

    case IcoRotate:
        p.drawArc(QRectF(4, 4, 14, 14), 60 * 16, 250 * 16);
        p.setBrush(ink);
        p.drawPolygon(QPolygonF() << QPointF(10.5, 2.5) << QPointF(15.5, 5.5)
                      << QPointF(10, 8));
        break;

    case IcoMirror:
        p.setPen(QPen(ink, 1.2, Qt::DashLine));
        p.drawLine(QPointF(11, 2), QPointF(11, 20));
        p.setPen(QPen(ink, 1.6));
        p.drawPolygon(QPolygonF() << QPointF(9, 6) << QPointF(9, 16) << QPointF(3, 11));
        p.drawPolygon(QPolygonF() << QPointF(13, 6) << QPointF(13, 16) << QPointF(19, 11));
        break;

    case IcoDelete:
        p.drawLine(QPointF(5, 5), QPointF(17, 17));
        p.drawLine(QPointF(17, 5), QPointF(5, 17));
        break;

    case IcoActive:
        p.drawEllipse(QRectF(4, 4, 14, 14));
        p.drawLine(QPointF(11, 11), QPointF(11, 18));      // a switch thrown
        p.drawLine(QPointF(11, 4), QPointF(11, 7));
        break;

    case IcoSimulate: {
        QPolygonF wave;
        for (int i = 0; i <= 24; i++)
            wave << QPointF(2.0 + i * 0.75, 11.0 - 6.0 * sin(i * M_PI / 12.0));
        p.drawPolyline(wave);
        break;
    }

    case IcoOpPoint:
        for (int i = 0; i < 4; i++) {
            const double y = 4.5 + i * 4.2;
            p.drawLine(QPointF(6, y), QPointF(19, y));
            p.setBrush(ink);
            p.drawEllipse(QPointF(4, y), 1.4, 1.4);
            p.setBrush(Qt::NoBrush);
        }
        break;

    case IcoNetlist:
        p.drawRect(QRectF(4, 2, 14, 18));
        p.setPen(QPen(ink, 1.1));
        for (int i = 0; i < 4; i++)
            p.drawLine(QPointF(7, 6 + i * 3.4), QPointF(15, 6 + i * 3.4));
        break;

    default:                                               // IcoAbout
        p.drawEllipse(QRectF(3, 3, 16, 16));
        p.drawLine(QPointF(11, 10), QPointF(11, 16));
        p.setBrush(ink);
        p.drawEllipse(QPointF(11, 7), 1.2, 1.2);
        break;
    }

    p.end();
    return QIcon(pm);
}

// ---- the read-only listings -------------------------------------------------

// A read-only text window, for the netlist and the operating point.  Both are
// plain listings, and a QPlainTextEdit in a dialog is the least machinery that
// scrolls, selects and copies one.
static void showTextDialog(QWidget *parent, const QString &title, const QString &text)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.resize(600, 460);

    QVBoxLayout *l = new QVBoxLayout(&dlg);
    QPlainTextEdit *edit = new QPlainTextEdit(&dlg);
    edit->setReadOnly(true);
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont f = edit->font();
    f.setStyleHint(QFont::TypeWriter);          // whatever fixed font there is
    edit->setFont(f);
    edit->setPlainText(text);
    l->addWidget(edit);

    QPushButton *close = new QPushButton(QObject::tr("Close"), &dlg);
    QObject::connect(close, SIGNAL(clicked()), &dlg, SLOT(accept()));
    QHBoxLayout *bl = new QHBoxLayout;
    bl->addStretch(1);
    bl->addWidget(close);
    l->addLayout(bl);

    dlg.exec();
}

static const char *kindName(int kind)
{
    switch (kind) {
    case ElResistor:      return "R";
    case ElCapacitor:     return "C";
    case ElInductor:      return "L";
    case ElVoltageSource: return "V";
    case ElCurrentSource: return "I";
    case ElDiode:         return "D";
    case ElBjt:           return "Q";
    default:              return "X";
    }
}

static const char *ctrlName(int ctrl)
{
    switch (ctrl) {
    case CtrlVoltage: return "VCVS/VCCS";
    case CtrlCurrent: return "CCVS/CCCS";
    default:          return 0;
    }
}

// ---- construction -----------------------------------------------------------

MainWindow::MainWindow(const QString &path, QWidget *parent)
    : QMainWindow(parent), m_tabs(0), m_lib(0), m_msg(0), m_libDock(0),
      m_msgDock(0), m_posLabel(0), m_viewMenu(0),
      m_actNew(0), m_actOpen(0), m_actSave(0), m_actSaveAs(0), m_actClose(0),
      m_actQuit(0), m_actSelectAll(0), m_actDelete(0), m_actRotate(0),
      m_actMirror(0), m_actActive(0), m_actProps(0), m_actSimulate(0),
      m_actOpPoint(0), m_actNetlist(0), m_actAbout(0)
{
    setWindowTitle(tr("Qucs-S"));
    setMinimumSize(680, 480);
    resize(960, 680);

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);
    connect(m_tabs, SIGNAL(currentChanged(int)), this, SLOT(tabChanged(int)));
    connect(m_tabs, SIGNAL(tabCloseRequested(int)), this, SLOT(tabCloseRequested(int)));

    buildActions();
    buildMenus();
    buildToolBar();
    buildDocks();

    m_posLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_posLabel);

    // Upstream opens whatever the project names; here a file on the command
    // line, and a fresh sheet when there is none.
    if (path.isEmpty() || !loadFile(path))
        newSheet();

    updateActions();
    message(tr("Qucs-S %1 - a Qt Widgets rewrite of https://github.com/ra3xdh/qucs_s\n"
               "for EwokOS.  Draw a circuit, put a simulation block on the sheet\n"
               "and press F2.").arg(QLatin1String(kVersion)));
}

MainWindow::~MainWindow()
{
    // The sheets are children of the tab widget, so Qt owns the widgets; only
    // the records around them are this window's.
    qDeleteAll(m_sheets);
    m_sheets.clear();
}

// ---- actions, menus, toolbar, docks -----------------------------------------

void MainWindow::buildActions()
{
    m_actNew = new QAction(toolIcon(IcoNew, 22), tr("&New schematic"), this);
    m_actNew->setShortcut(QKeySequence::New);
    connect(m_actNew, SIGNAL(triggered()), this, SLOT(newSheet()));

    m_actOpen = new QAction(toolIcon(IcoOpen, 22), tr("&Open..."), this);
    m_actOpen->setShortcut(QKeySequence::Open);
    connect(m_actOpen, SIGNAL(triggered()), this, SLOT(openSheet()));

    m_actSave = new QAction(toolIcon(IcoSave, 22), tr("&Save"), this);
    m_actSave->setShortcut(QKeySequence::Save);
    connect(m_actSave, SIGNAL(triggered()), this, SLOT(saveSheet()));

    m_actSaveAs = new QAction(tr("Save &as..."), this);
    m_actSaveAs->setShortcut(QKeySequence::SaveAs);
    connect(m_actSaveAs, SIGNAL(triggered()), this, SLOT(saveSheetAs()));

    m_actClose = new QAction(tr("&Close schematic"), this);
    m_actClose->setShortcut(QKeySequence::Close);
    connect(m_actClose, SIGNAL(triggered()), this, SLOT(closeSheet()));

    m_actQuit = new QAction(tr("&Quit"), this);
    m_actQuit->setShortcut(QKeySequence::Quit);
    connect(m_actQuit, SIGNAL(triggered()), this, SLOT(close()));

    // The four mouse modes.  Checkable and kept in step with the sheet by
    // viewToolChanged(); ToolPlace is armed from the component library and has
    // no button of its own, so none of them is checked while it is on.
    struct ToolDef { int ico; const char *text; int tool; const char *tip; };
    static const ToolDef kTools[] = {
        { IcoSelect,  "Pointer",   SchematicView::ToolSelect,
          "Select and move elements (Esc)" },
        { IcoWire,    "Wire",      SchematicView::ToolWire,
          "Draw a wire: click for the start and for every bend (W)" },
        { IcoLabel,   "Net label", SchematicView::ToolLabel,
          "Click a wire to name the net it carries (L)" },
        { IcoDiagram, "Diagram",   SchematicView::ToolDiagram,
          "Drag out a diagram, then double-click it to choose what it plots (D)" },
    };
    for (int i = 0; i < (int)(sizeof(kTools) / sizeof(kTools[0])); i++) {
        QAction *a = new QAction(toolIcon(kTools[i].ico, 22),
                                 tr(kTools[i].text), this);
        a->setCheckable(true);
        a->setData(kTools[i].tool);
        a->setToolTip(tr(kTools[i].tip));
        a->setStatusTip(tr(kTools[i].tip));
        connect(a, SIGNAL(triggered()), this, SLOT(toolTriggered()));
        m_toolActs.append(a);
    }
    m_toolActs.at(0)->setChecked(true);
    // Plain letters would be eaten by the sheet, which handles R, M and the
    // arrows itself; W/L/D are free and are what upstream's toolbar hints at.
    m_toolActs.at(1)->setShortcut(QKeySequence(Qt::Key_W));
    m_toolActs.at(2)->setShortcut(QKeySequence(Qt::Key_L));
    m_toolActs.at(3)->setShortcut(QKeySequence(Qt::Key_D));
    m_toolActs.at(0)->setShortcut(QKeySequence(Qt::Key_Escape));

    m_actSelectAll = new QAction(tr("Select &all"), this);
    m_actSelectAll->setShortcut(QKeySequence::SelectAll);
    connect(m_actSelectAll, SIGNAL(triggered()), this, SLOT(doSelectAll()));

    m_actDelete = new QAction(toolIcon(IcoDelete, 22), tr("&Delete"), this);
    m_actDelete->setShortcut(QKeySequence::Delete);
    m_actDelete->setToolTip(tr("Delete what is selected (Del)"));
    connect(m_actDelete, SIGNAL(triggered()), this, SLOT(doDelete()));

    m_actRotate = new QAction(toolIcon(IcoRotate, 22), tr("&Rotate"), this);
    m_actRotate->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_R));
    m_actRotate->setToolTip(tr("Turn the selection 90 degrees (R)"));
    connect(m_actRotate, SIGNAL(triggered()), this, SLOT(doRotate()));

    m_actMirror = new QAction(toolIcon(IcoMirror, 22), tr("&Mirror"), this);
    m_actMirror->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_M));
    m_actMirror->setToolTip(tr("Flip the selection left to right (M)"));
    connect(m_actMirror, SIGNAL(triggered()), this, SLOT(doMirror()));

    m_actActive = new QAction(toolIcon(IcoActive, 22), tr("&Active"), this);
    m_actActive->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_T));
    m_actActive->setToolTip(tr("Take the selection into the netlist, or out of it (T)"));
    connect(m_actActive, SIGNAL(triggered()), this, SLOT(doActive()));

    m_actProps = new QAction(tr("&Properties..."), this);
    m_actProps->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_P));
    m_actProps->setToolTip(tr("Edit what is selected (double-click it)"));
    connect(m_actProps, SIGNAL(triggered()), this, SLOT(doProperties()));

    m_actSimulate = new QAction(toolIcon(IcoSimulate, 22), tr("&Simulate"), this);
    m_actSimulate->setShortcut(QKeySequence(Qt::Key_F2));
    m_actSimulate->setToolTip(tr("Run the active simulation block (F2)"));
    connect(m_actSimulate, SIGNAL(triggered()), this, SLOT(simulate()));

    m_actOpPoint = new QAction(toolIcon(IcoOpPoint, 22), tr("DC &operating point"), this);
    m_actOpPoint->setToolTip(tr("Solve the circuit at t=0 and list the node voltages"));
    connect(m_actOpPoint, SIGNAL(triggered()), this, SLOT(showOperatingPoint()));

    m_actNetlist = new QAction(toolIcon(IcoNetlist, 22), tr("View &netlist"), this);
    m_actNetlist->setToolTip(tr("What the simulator is handed, as SPICE-ish text"));
    connect(m_actNetlist, SIGNAL(triggered()), this, SLOT(showNetlist()));

    m_actAbout = new QAction(toolIcon(IcoAbout, 22), tr("&About Qucs-S"), this);
    connect(m_actAbout, SIGNAL(triggered()), this, SLOT(about()));
}

void MainWindow::buildExamplesMenu(QMenu *file)
{
    const QDir dir(QLatin1String(QUCS_EXAMPLES_DIR));
    const QStringList found = dir.entryList(QStringList() << "*.sch",
                                            QDir::Files, QDir::Name);
    if (found.isEmpty())
        return;                    // no examples installed: no submenu either

    QMenu *sub = file->addMenu(tr("&Examples"));
    sub->setStatusTip(tr("Schematics shipped with this port, in %1")
                      .arg(QLatin1String(QUCS_EXAMPLES_DIR)));
    for (int i = 0; i < found.size(); i++) {
        QAction *a = sub->addAction(found.at(i).left(found.at(i).size() - 4));
        a->setData(dir.filePath(found.at(i)));
        a->setStatusTip(dir.filePath(found.at(i)));
        connect(a, SIGNAL(triggered()), this, SLOT(openExample()));
    }
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_actNew);
    file->addAction(m_actOpen);
    buildExamplesMenu(file);
    file->addAction(m_actSave);
    file->addAction(m_actSaveAs);
    file->addAction(m_actClose);
    file->addSeparator();
    file->addAction(m_actQuit);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_actSelectAll);
    edit->addAction(m_actDelete);
    edit->addSeparator();
    edit->addAction(m_actRotate);
    edit->addAction(m_actMirror);
    edit->addAction(m_actActive);
    edit->addSeparator();
    edit->addAction(m_actProps);

    // Filled in once the docks exist, since a dock's own toggle action is what
    // a menu entry should be - it then tracks the dock being closed by its
    // button too.
    m_viewMenu = menuBar()->addMenu(tr("&View"));

    QMenu *sim = menuBar()->addMenu(tr("&Simulation"));
    sim->addAction(m_actSimulate);
    sim->addAction(m_actOpPoint);
    sim->addAction(m_actNetlist);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_actAbout);
}

void MainWindow::buildToolBar()
{
    QToolBar *tb = addToolBar(tr("Main"));
    tb->setObjectName(QLatin1String("mainToolBar"));
    tb->setIconSize(QSize(22, 22));
    tb->addAction(m_actNew);
    tb->addAction(m_actOpen);
    tb->addAction(m_actSave);
    tb->addSeparator();
    for (int i = 0; i < m_toolActs.size(); i++)
        tb->addAction(m_toolActs.at(i));
    tb->addSeparator();
    tb->addAction(m_actRotate);
    tb->addAction(m_actMirror);
    tb->addAction(m_actDelete);
    tb->addAction(m_actActive);
    tb->addSeparator();
    tb->addAction(m_actSimulate);
    tb->addAction(m_actOpPoint);
    tb->addAction(m_actNetlist);
}

void MainWindow::buildDocks()
{
    // ---- the component library ---------------------------------------------
    // Upstream's is a list of icons per category; a tree keeps the categories
    // foldable, which matters with the whole catalogue on one screen.
    m_lib = new QTreeWidget(this);
    m_lib->setHeaderHidden(true);
    m_lib->setIconSize(QSize(26, 26));
    m_lib->setRootIsDecorated(true);
    const QStringList cats = compCategories();
    for (int i = 0; i < cats.size(); i++) {
        QTreeWidgetItem *cat = new QTreeWidgetItem(m_lib, QStringList(cats.at(i)));
        cat->setFlags(Qt::ItemIsEnabled);          // a heading, not a component
        QFont f = cat->font(0);
        f.setBold(true);
        cat->setFont(0, f);
        const QList<const CompDef *> defs = compsInCategory(cats.at(i));
        for (int k = 0; k < defs.size(); k++) {
            const CompDef *d = defs.at(k);
            QTreeWidgetItem *item = new QTreeWidgetItem(cat,
                    QStringList(QString(QLatin1String(d->label))));
            item->setIcon(0, compIcon(QLatin1String(d->type)));
            item->setData(0, Qt::UserRole, QString(QLatin1String(d->type)));
            QString tip = QString(QLatin1String("<b>%1</b> (%2)<br>")
                                  .arg(QLatin1String(d->label))
                                  .arg(QLatin1String(d->type)));
            for (int n = 0; n < d->nparams; n++) {
                if (n)
                    tip += "<br>";
                tip += QString("%1: %2").arg(QLatin1String(d->params[n].name))
                       .arg(QLatin1String(d->params[n].desc));
            }
            tip += tr("<br><br>Click to place it on the schematic.");
            item->setToolTip(0, tip);
        }
        cat->setExpanded(true);
    }
    connect(m_lib, SIGNAL(itemClicked(QTreeWidgetItem *, int)),
            this, SLOT(placeFromLibrary()));
    connect(m_lib, SIGNAL(itemDoubleClicked(QTreeWidgetItem *, int)),
            this, SLOT(placeFromLibrary()));

    m_libDock = new QDockWidget(tr("Component library"), this);
    m_libDock->setObjectName(QLatin1String("libraryDock"));
    m_libDock->setWidget(m_lib);
    m_libDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, m_libDock);

    // ---- the messages -------------------------------------------------------
    m_msg = new QPlainTextEdit(this);
    m_msg->setReadOnly(true);
    m_msg->setMaximumBlockCount(1000);
    m_msgDock = new QDockWidget(tr("Messages"), this);
    m_msgDock->setObjectName(QLatin1String("messageDock"));
    m_msgDock->setWidget(m_msg);
    m_msgDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, m_msgDock);

    // Now that they exist, the View menu can carry their own toggle actions.
    m_viewMenu->addAction(m_libDock->toggleViewAction());
    m_viewMenu->addAction(m_msgDock->toggleViewAction());
    m_viewMenu->addSeparator();
    for (int i = 0; i < m_toolActs.size(); i++)
        m_viewMenu->addAction(m_toolActs.at(i));
}

// ---- the open sheets --------------------------------------------------------

SheetTab *MainWindow::tabAt(int index) const
{
    return (index >= 0 && index < m_sheets.size()) ? m_sheets.at(index) : 0;
}

SheetTab *MainWindow::currentTab() const
{
    return tabAt(m_tabs->currentIndex());
}

void MainWindow::addSheet(Schematic *sch)
{
    SheetTab *t = new SheetTab;
    t->sch = sch;

    t->view = new SchematicView;
    // The document is parented to the widget that draws it, so closing a tab
    // takes the view, the sheet and this record's widgets away in one delete
    // and the window never has to track them itself.
    sch->setParent(t->view);
    t->view->setSchematic(sch);

    t->scroll = new QScrollArea;
    t->scroll->setFrameShape(QFrame::NoFrame);
    t->scroll->setBackgroundRole(QPalette::Dark);
    t->scroll->setWidget(t->view);

    // Into the list before the tab: addTab() answers with currentChanged(),
    // and tabChanged() reads the sheet at that index.
    m_sheets.append(t);
    m_tabs->addTab(t->scroll, sch->displayName());
    connectView(t);
    m_tabs->setCurrentIndex(m_sheets.size() - 1);
    t->view->setFocus();

    updateWindowTitle();
    updateActions();
}

void MainWindow::connectView(SheetTab *t)
{
    connect(t->view, SIGNAL(edited()), this, SLOT(viewEdited()));
    connect(t->view, SIGNAL(selectionChanged()), this, SLOT(updateActions()));
    connect(t->view, SIGNAL(toolChanged(int)), this, SLOT(viewToolChanged(int)));
    connect(t->view, SIGNAL(statusMessage(QString)), this, SLOT(viewStatus(QString)));
    connect(t->view, SIGNAL(editComponent(int)), this, SLOT(editComponent(int)));
    connect(t->view, SIGNAL(editDiagram(int)), this, SLOT(editDiagram(int)));
}

QString MainWindow::startDir() const
{
    if (!m_lastDir.isEmpty() && QDir(m_lastDir).exists())
        return m_lastDir;
    const QString home = QDir::homePath();
    return (!home.isEmpty() && QDir(home).exists()) ? home : QLatin1String("/");
}

bool MainWindow::loadFile(const QString &path)
{
    Schematic *sch = new Schematic;
    QString err;
    if (!sch->load(path, &err)) {
        delete sch;
        message(tr("Could not open %1: %2").arg(path, err));
        QMessageBox::warning(this, tr("Open"),
                             tr("Could not open %1:\n%2").arg(path, err));
        return false;
    }
    m_lastDir = QFileInfo(path).absolutePath();
    addSheet(sch);
    message(tr("Opened %1: %2 components, %3 wires, %4 diagrams.")
            .arg(path)
            .arg(sch->components().size())
            .arg(sch->wires().size())
            .arg(sch->diagrams().size()));
    return true;
}

void MainWindow::newSheet()
{
    Schematic *sch = new Schematic;
    // Blank, except for one thing: in Qucs an analysis is a component placed on
    // the sheet, so a sheet without one cannot be simulated at all and F2 would
    // only say so.  Upstream opens a template document instead.
    Component sim(QLatin1String("DC"), QPoint(40, 40));
    sim.name = sch->uniqueName(QLatin1String("DC"));
    sch->addComponent(sim);
    sch->setModified(false);

    addSheet(sch);
    message(tr("New schematic.  Draw the circuit, put a diagram on it and "
               "press F2."));
}

void MainWindow::openSheet()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open schematic"),
            startDir(), tr("Qucs schematic (*.sch);;All files (*)"));
    if (path.isEmpty())
        return;
    // Opening the same file twice is a slip far more often than a wish, so it
    // goes to the tab that already holds it instead of adding another.
    for (int i = 0; i < m_sheets.size(); i++) {
        if (m_sheets.at(i)->sch->filePath() == path) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
    loadFile(path);
}

void MainWindow::openExample()
{
    QAction *a = qobject_cast<QAction *>(sender());
    if (!a || !loadFile(a->data().toString()))
        return;
    // The examples are part of the read-only rootfs.  Leave the dialog starting
    // somewhere a file can actually be written to, and say that saving one asks
    // for a name rather than writing over the copy that ships.
    m_lastDir = QString();
    message(tr("%1 is one of the shipped examples: it is opened read-only, and "
               "Save asks where to put your copy.").arg(a->text()));
}

bool MainWindow::writeSheet(SheetTab *t, const QString &path)
{
    if (!t)
        return false;
    QString err;
    if (!t->sch->save(path, &err)) {
        message(tr("Saving %1 failed: %2").arg(path, err));
        QMessageBox::warning(this, tr("Save"),
                             tr("Could not save %1:\n%2").arg(path, err));
        return false;
    }
    m_lastDir = QFileInfo(path).absolutePath();
    const int i = m_sheets.indexOf(t);
    if (i >= 0)
        m_tabs->setTabText(i, t->sch->displayName());
    if (i == m_tabs->currentIndex())
        updateWindowTitle();
    message(tr("Saved %1.").arg(path));
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 4000);
    return true;
}

bool MainWindow::askFileName(SheetTab *t)
{
    if (!t)
        return false;
    QString start = startDir();
    if (!start.endsWith(QLatin1Char('/')))
        start += QLatin1Char('/');
    start += t->sch->displayName();

    QString path = QFileDialog::getSaveFileName(this, tr("Save schematic as"), start,
                                                tr("Qucs schematic (*.sch)"));
    if (path.isEmpty())
        return false;                        // backed out: the sheet stays open
    // Honour the extension the filter promised, so "Save as" and a later
    // "Open" agree on what a schematic is called.
    if (!path.endsWith(QLatin1String(".sch"), Qt::CaseInsensitive))
        path += QLatin1String(".sch");
    return writeSheet(t, path);
}

void MainWindow::saveSheet()
{
    SheetTab *t = currentTab();
    if (!t)
        return;
    const QString path = t->sch->filePath();
    // A sheet with no file yet, and one still pointing into the read-only
    // examples directory, both have to be given a name first.
    if (path.isEmpty() || path.startsWith(QLatin1String(QUCS_EXAMPLES_DIR)))
        askFileName(t);
    else
        writeSheet(t, path);
}

void MainWindow::saveSheetAs()
{
    askFileName(currentTab());
}

bool MainWindow::askSave(SheetTab *t)
{
    if (!t || !t->sch->isModified())
        return true;

    const QMessageBox::StandardButton b = QMessageBox::question(this, tr("Qucs-S"),
            tr("%1 has been changed.\nSave the changes?").arg(t->sch->displayName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
    if (b == QMessageBox::Cancel || b == QMessageBox::NoButton)
        return false;
    if (b == QMessageBox::Discard)
        return true;

    const QString path = t->sch->filePath();
    if (path.isEmpty() || path.startsWith(QLatin1String(QUCS_EXAMPLES_DIR)))
        return askFileName(t);
    return writeSheet(t, path);
}

void MainWindow::closeSheet()
{
    tabCloseRequested(m_tabs->currentIndex());
}

void MainWindow::tabCloseRequested(int index)
{
    SheetTab *t = tabAt(index);
    if (!t)
        return;
    if (!askSave(t))
        return;

    // Out of the list before out of the tab bar: removeTab() answers with
    // currentChanged(), and the two must not disagree about which sheet is
    // which while it runs.
    m_sheets.removeAt(index);
    m_tabs->removeTab(index);                // the page itself is not deleted

    delete t->scroll;                        // and with it the view and the sheet
    delete t;

    updateWindowTitle();
    updateActions();
    if (m_sheets.isEmpty())
        newSheet();                          // the window always holds one sheet
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    for (int i = 0; i < m_sheets.size(); i++) {
        if (!askSave(m_sheets.at(i))) {
            e->ignore();
            return;
        }
    }
    e->accept();
}

// ---- title, actions, status -------------------------------------------------

void MainWindow::updateWindowTitle()
{
    SheetTab *t = currentTab();
    if (!t) {
        setWindowTitle(tr("Qucs-S"));
        return;
    }
    // The mark goes on the tab too, so the sheets that still want saving are
    // visible without switching to them.
    const QString mark = t->sch->isModified() ? QLatin1String(" *") : QString();
    setWindowTitle(tr("%1%2 - Qucs-S").arg(t->sch->displayName(), mark));
    m_tabs->setTabText(m_tabs->currentIndex(), t->sch->displayName() + mark);
}

void MainWindow::updateActions()
{
    SheetTab *t = currentTab();
    const bool any = (t != 0);
    const int n = any ? t->view->selectedCount() : 0;

    m_actSave->setEnabled(any);
    m_actSaveAs->setEnabled(any);
    m_actClose->setEnabled(any);
    m_actSelectAll->setEnabled(any);
    m_actSimulate->setEnabled(any);
    m_actOpPoint->setEnabled(any);
    m_actNetlist->setEnabled(any);

    m_actDelete->setEnabled(n > 0);
    m_actRotate->setEnabled(n > 0);
    m_actMirror->setEnabled(n > 0);
    m_actActive->setEnabled(n > 0);
    // Properties opens a different dialog for a component and for a diagram, so
    // it is on when either is selected.  A wire has none - its label is the net
    // label tool's business.
    m_actProps->setEnabled(any && (t->view->firstSelectedComponent() >= 0
                                   || t->view->firstSelectedDiagram() >= 0));
}

void MainWindow::message(const QString &text)
{
    m_msg->appendPlainText(text);
}

void MainWindow::viewStatus(const QString &text)
{
    if (text.isEmpty())
        statusBar()->clearMessage();
    else
        statusBar()->showMessage(text);
}

void MainWindow::viewEdited()
{
    SheetTab *t = currentTab();
    if (!t)
        return;
    // setModified() answers with changed() only the first time, which is what
    // the view's own edits already asked for.
    t->sch->setModified(true);

    // A result belongs to the sheet exactly as it was when it was run, and the
    // sheet has just stopped being that: the diagrams go back to empty frames
    // rather than plotting numbers that no longer match what is drawn.
    t->view->setSimResult(0);
    t->result = SimResult();
    t->netlist.clear();
    t->hasResult = false;

    updateWindowTitle();
    updateActions();
}

void MainWindow::viewToolChanged(int tool)
{
    // ToolPlace is armed from the component library and has no button, so this
    // leaves the whole group unchecked while the cursor carries a component.
    for (int i = 0; i < m_toolActs.size(); i++)
        m_toolActs.at(i)->setChecked(m_toolActs.at(i)->data().toInt() == tool);
}

void MainWindow::tabChanged(int index)
{
    SheetTab *t = tabAt(index);
    if (!t)
        return;
    updateWindowTitle();
    updateActions();
    // Each sheet keeps the mouse mode it was left in, so the toolbar has to
    // follow the sheet rather than the other way round.
    viewToolChanged(t->view->tool());
}

void MainWindow::toolTriggered()
{
    QAction *a = qobject_cast<QAction *>(sender());
    SheetTab *t = currentTab();
    if (!a || !t)
        return;
    t->view->setTool(a->data().toInt());
    // setTool() stays quiet when the mode is already the one asked for, and the
    // action still needs the group settled - a shortcut on a sheet that is
    // already wiring would otherwise leave another button checked.
    viewToolChanged(a->data().toInt());
}

void MainWindow::placeFromLibrary()
{
    QTreeWidgetItem *item = m_lib->currentItem();
    SheetTab *t = currentTab();
    if (!item || !t)
        return;
    const QString type = item->data(0, Qt::UserRole).toString();
    if (type.isEmpty() || !compDef(type))
        return;                              // a category heading carries none

    t->view->setPlaceType(type);
    t->view->setTool(SchematicView::ToolPlace);
    viewToolChanged(SchematicView::ToolPlace);
    t->view->setFocus();
    viewStatus(tr("%1 follows the cursor: click to place it, Esc to put it down.")
               .arg(item->text(0)));
}

// ---- the property dialogs ---------------------------------------------------

void MainWindow::editComponent(int index)
{
    SheetTab *t = currentTab();
    if (!t)
        return;
    QList<Component> &comps = t->sch->components();
    if (index < 0 || index >= comps.size())
        return;

    ComponentDialog dlg(comps.at(index), t->sch, index, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const Component c = dlg.result();
    comps[index] = c;
    t->sch->touch();
    // The value on the sheet may be the thing that changed, and a netlist built
    // before it is no longer the circuit that is drawn.
    viewEdited();
    message(tr("%1 (%2) changed.").arg(c.name, c.type));
}

void MainWindow::editDiagram(int index)
{
    SheetTab *t = currentTab();
    if (!t)
        return;
    QList<Diagram> &diags = t->sch->diagrams();
    if (index < 0 || index >= diags.size())
        return;

    const SimResult *res = t->hasResult ? &t->result
                                        : static_cast<const SimResult *>(0);
    DiagramDialog dlg(diags.at(index), t->sch, res, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    diags[index] = dlg.result();
    // Choosing what a diagram plots is not a change to the circuit: the result
    // in hand still describes it, so this touches the sheet without throwing
    // the curves away the way viewEdited() does.
    t->sch->touch();
    updateWindowTitle();
}

// ---- the Edit actions, forwarded to the sheet in front ----------------------

void MainWindow::doSelectAll()
{
    SheetTab *t = currentTab();
    if (t)
        t->view->selectAll();
}

void MainWindow::doDelete()
{
    SheetTab *t = currentTab();
    if (t)
        t->view->deleteSelection();
}

void MainWindow::doRotate()
{
    SheetTab *t = currentTab();
    if (t)
        t->view->rotateSelection();
}

void MainWindow::doMirror()
{
    SheetTab *t = currentTab();
    if (t)
        t->view->mirrorSelection();
}

void MainWindow::doActive()
{
    SheetTab *t = currentTab();
    if (t)
        t->view->toggleActive();
}

void MainWindow::doProperties()
{
    SheetTab *t = currentTab();
    if (!t)
        return;
    const int c = t->view->firstSelectedComponent();
    if (c >= 0) {
        editComponent(c);
        return;
    }
    const int d = t->view->firstSelectedDiagram();
    if (d >= 0)
        editDiagram(d);
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About Qucs-S"), tr(
        "<h3>Qucs-S %1 for EwokOS</h3>"
        "<p>A rewrite of <a href=\"https://github.com/ra3xdh/qucs_s\">Qucs-S</a> "
        "- Quite Universal Circuit Simulator with SPICE - on plain Qt Widgets.</p>"
        "<p>Upstream runs its analyses in an external kernel, ngspice or Xyce or "
        "its own qucsator, through QProcess, and draws its plots with QtCharts. "
        "Neither exists here, so the analysis is in the program: modified nodal "
        "analysis with a dense LU solve, Newton-Raphson for the junctions and "
        "trapezoidal integration for the transient run.</p>"
        "<p>Elements: R, C, L, ground, DC and AC voltage and current sources, a "
        "pulse source, a diode, a bipolar transistor, the four controlled "
        "sources and a current probe.  Analyses: DC sweep, AC sweep and "
        "transient, each of them a component placed on the schematic the way "
        "Qucs does it.</p>"
        "<p>Upstream is GPL-2.0, and so is this port.</p>")
        .arg(QLatin1String(kVersion)));
}

// ---- simulation -------------------------------------------------------------

void MainWindow::simulate()
{
    SheetTab *t = currentTab();
    if (!t)
        return;

    // In Qucs the analysis to run is read off the sheet: the simulation blocks
    // are components, and the active one of them is what F2 runs.
    if (t->sch->simComponents().isEmpty()) {
        const QString why = tr("This schematic holds no simulation block.\n\n"
                               "An analysis is a component in Qucs: place one "
                               "from the Simulations group of the component "
                               "library and run it again.");
        message(why);
        QMessageBox::information(this, tr("Simulate"), why);
        return;
    }

    const Component sim = t->sch->components().at(t->sch->activeSim());
    message(QString("--- %1 (%2) on %3 ---")
            .arg(sim.name).arg(sim.type).arg(t->sch->displayName()));

    // Whatever the last run left is about a circuit that no longer exists.
    t->view->setSimResult(0);
    t->result = SimResult();
    t->netlist.clear();
    t->hasResult = false;

    if (!buildNetlist(*t->sch, &t->netlist)) {
        message(t->netlist.error);
        // runSimulation() never got to fold the warnings into the result, so
        // they are echoed here instead of being lost with the failed netlist.
        for (int i = 0; i < t->netlist.warnings.size(); i++)
            message(tr("warning: %1").arg(t->netlist.warnings.at(i)));
        updateActions();
        return;
    }

    SimParams p;
    QString perr;
    if (!parseSimParams(sim, &p, &perr)) {
        message(perr);
        updateActions();
        return;
    }

    runSimulation(t->netlist, p, &t->result);

    // A sweep that stopped early still holds the points it did converge, and a
    // diagram showing those says more than an empty frame - so the result is
    // published whenever there is anything to plot, error or no error.
    t->hasResult = !t->result.sweep.isEmpty();
    t->view->setSimResult(t->hasResult ? &t->result : 0);
    reportResult();
    t->view->update();
    updateActions();
}

void MainWindow::reportResult()
{
    SheetTab *t = currentTab();
    if (!t)
        return;
    const SimResult &r = t->result;

    if (!r.error.isEmpty())
        message(r.error);
    for (int i = 0; i < r.notes.size(); i++)
        message(tr("note: %1").arg(r.notes.at(i)));

    if (r.sweep.isEmpty()) {
        if (r.error.isEmpty())
            message(tr("The run produced no points."));
        return;
    }

    message(tr("%1 points, %2 = %3 .. %4 %5")
            .arg(r.sweep.size())
            .arg(r.xName)
            .arg(axisNum(r.sweep.first()))
            .arg(axisNum(r.sweep.last()))
            .arg(r.xUnit));
    message(tr("variables: %1").arg(r.varNames().join(QLatin1String(", "))));

    // A diagram placed before the first run asks for nothing yet and would stay
    // an empty frame.  Upstream opens its diagram dialog here; this port makes
    // the choice once - the first two variables - so that F2 on a fresh sheet
    // shows a curve instead of asking a question.
    const QStringList names = r.varNames();
    QList<Diagram> &diags = t->sch->diagrams();
    bool filled = false;
    for (int i = 0; i < diags.size(); i++) {
        Diagram &d = diags[i];
        if (!d.vars.isEmpty())
            continue;
        for (int k = 0; k < names.size() && k < 2; k++)
            d.vars.append(names.at(k));
        if (d.vars.isEmpty())
            continue;
        if (r.kind == QLatin1String("AC")) {
            // An AC sweep spans decades and the interesting quantity is the
            // magnitude, so the axes get the settings every textbook bode plot
            // has rather than a linear plot of a real part.
            d.plot = Diagram::Magnitude;
            d.logX = true;
            d.logY = true;
        }
        message(tr("Diagram %1 now plots %2.")
                .arg(i + 1).arg(d.vars.join(QLatin1String(", "))));
        filled = true;
    }
    // Only when a diagram actually changed: filling one is an edit to the
    // document, and the result must survive it.
    if (filled)
        t->sch->setModified(true);
}

void MainWindow::showOperatingPoint()
{
    SheetTab *t = currentTab();
    if (!t)
        return;

    Netlist nl;
    if (!buildNetlist(*t->sch, &nl)) {
        message(nl.error);
        QMessageBox::warning(this, tr("DC operating point"), nl.error);
        return;
    }

    OpPoint op;
    QString err;
    if (!operatingPoint(nl, 0.0, &op, &err)) {
        message(err);
        QMessageBox::warning(this, tr("DC operating point"), err);
        return;
    }

    // What upstream's "DC operating point" dialog lists after a .OP run, from
    // the same solution the transient run starts from.
    QString text = tr("DC operating point of %1\n\n").arg(t->sch->displayName());
    text += tr("node        voltage\n");
    for (int n = 1; n < nl.nodeCount(); n++) {
        if (n >= op.nodeV.size())
            break;
        text += QString("%1 %2 V\n").arg(nl.nodeNames.at(n), -11)
                .arg(num2str(op.nodeV.at(n)));
    }

    text += tr("\nelement     current\n");
    bool any = false;
    for (int i = 0; i < nl.elems.size(); i++) {
        const Element &e = nl.elems.at(i);
        if (!e.reportsCurrent || i >= op.elemI.size())
            continue;
        text += QString("%1 %2 A\n").arg(e.name, -11).arg(num2str(op.elemI.at(i)));
        any = true;
    }
    if (!any)
        text += tr("(nothing to report: put a current probe or a voltage "
                   "source in the circuit)\n");

    text += tr("\nA current reads positive from the element's first terminal "
               "to its second,\nso a source that is delivering power reads "
               "negative.\n");
    showTextDialog(this, tr("DC operating point"), text);
}

void MainWindow::showNetlist()
{
    SheetTab *t = currentTab();
    if (!t)
        return;
    // The return is ignored on purpose: a netlist that could not be finished is
    // exactly what the user wants to look at, and it carries its own error.
    Netlist nl;
    buildNetlist(*t->sch, &nl);
    showTextDialog(this, tr("Netlist of %1").arg(t->sch->displayName()),
                   netlistText(nl));
}

// How an independent source behaves over time, in SPICE's own words.
static QString waveText(const Element &e)
{
    if (e.wave == WaveSine)
        return QString("SIN(0 %1 %2 %3)")
               .arg(num2str(e.p[0]))          // amplitude
               .arg(num2str(e.p[2]))          // frequency
               .arg(num2str(e.p[3]));         // damping
    if (e.wave == WavePulse)
        // SPICE orders a pulse low high delay rise fall width period, which is
        // not the order the properties carry: there the width comes first.
        return QString("PULSE(%1 %2 %3 %4 %5 %6 %7)")
               .arg(num2str(e.p[0])).arg(num2str(e.p[1])).arg(num2str(e.p[2]))
               .arg(num2str(e.p[3])).arg(num2str(e.p[5])).arg(num2str(e.p[4]))
               .arg(num2str(e.p[6]));
    return QString("DC %1").arg(num2str(e.value));
}

QString MainWindow::netlistText(const Netlist &nl) const
{
    QString out = tr("* the netlist the simulator solves, for %1\n")
                  .arg(currentTab() ? currentTab()->sch->displayName() : QString());
    if (!nl.error.isEmpty())
        out += QString("* ERROR: %1\n").arg(nl.error);
    for (int i = 0; i < nl.warnings.size(); i++)
        out += tr("* warning: %1\n").arg(nl.warnings.at(i));
    if (!nl.error.isEmpty() || !nl.warnings.isEmpty())
        out += QLatin1String("*\n");

    out += tr("* nodes, %1 of them; 0 is ground and has no unknown\n")
           .arg(nl.nodeCount());
    for (int n = 0; n < nl.nodeCount(); n++) {
        out += QString("*   %1  %2").arg(n).arg(nl.nodeNames.at(n));
        const QList<QPoint> pts = (n < nl.nodePoints.size())
                                  ? nl.nodePoints.at(n) : QList<QPoint>();
        for (int k = 0; k < pts.size() && k < 6; k++)
            out += QString(" (%1,%2)").arg(pts.at(k).x()).arg(pts.at(k).y());
        if (pts.size() > 6)
            out += QLatin1String(" ...");
        out += QLatin1Char('\n');
    }

    out += tr("*\n* elements, %1 of them: <kind><name> <nodes> <model>\n")
           .arg(nl.elems.size());
    for (int i = 0; i < nl.elems.size(); i++) {
        const Element &e = nl.elems.at(i);
        QString line = QString("%1%2").arg(QLatin1String(kindName(e.kind))).arg(e.name);

        const int last = (e.kind == ElBjt) ? 3 : 2;
        for (int k = 0; k < last; k++)
            line += QString(" %1").arg(e.node[k] < 0 ? 0 : e.node[k]);

        if (e.kind == ElResistor) {
            line += QString("  %1").arg(num2str(e.value));
        } else if (e.kind == ElCapacitor) {
            line += QString("  %1").arg(num2str(e.value));
            if (e.p[0] != 0.0)
                line += tr(" IC=%1").arg(num2str(e.p[0]));
        } else if (e.kind == ElInductor) {
            line += QString("  %1").arg(num2str(e.value));
            if (e.p[0] != 0.0)
                line += tr(" IC=%1").arg(num2str(e.p[0]));
        } else if (e.kind == ElVoltageSource || e.kind == ElCurrentSource) {
            if (e.ctrl == CtrlNone) {
                line += QString("  %1").arg(waveText(e));
            } else {
                line += QString("  %1").arg(QLatin1String(ctrlName(e.ctrl)));
                if (e.ctrl == CtrlVoltage)
                    line += tr(" by v(%1)-v(%2)").arg(e.node[2]).arg(e.node[3]);
                else
                    line += tr(" by the current of %1")
                            .arg(e.ctrlName.isEmpty() ? tr("(none named)") : e.ctrlName);
                line += tr(" gain=%1").arg(num2str(e.value));
            }
        } else if (e.kind == ElDiode) {
            line += tr("  Is=%1 N=%2").arg(num2str(e.value)).arg(num2str(e.p[0]));
            if (e.p[2] > 0.0)
                line += tr(" Cj0=%1 Vj=%2 M=%3").arg(num2str(e.p[2]))
                        .arg(num2str(e.p[3])).arg(num2str(e.p[4]));
        } else if (e.kind == ElBjt) {
            line += tr("  %1 Is=%2 Bf=%3 Br=%4")
                    .arg(e.p[3] > 0.0 ? tr("pnp") : tr("npn"))
                    .arg(num2str(e.p[0])).arg(num2str(e.p[1])).arg(num2str(e.p[2]));
        }

        if (e.reportsCurrent)
            line += tr("   ; %1.I is available to a diagram").arg(e.name);
        out += line + QLatin1Char('\n');
    }

    return out;
}
