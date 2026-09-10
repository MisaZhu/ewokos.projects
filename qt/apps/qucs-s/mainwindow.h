/*
 * Qucs-S, ported to EwokOS - the application window.
 *
 * Upstream: https://github.com/ra3xdh/qucs_s (GPL-2.0), whose qucs/qucs.cpp is
 * a QMainWindow with a component-library dock, a message dock, a toolbar of
 * the mouse modes, and a QTabWidget of documents; its "Simulate" action
 * writes a netlist, launches the kernel with QProcess and reads a .dat file
 * back.  The shape is kept - the docks, the toolbar, the tabs, the F2 - and
 * only the simulation step differs: simulator.cpp answers it in process, so
 * the result is on the sheet the moment the call returns.
 *
 * What is dropped is upstream's project container (.prj wrapping several
 * documents, a SPICE netlist tab, the equation/model editors): here a tab is
 * one .sch file, which is the part of a project this port needs.
 */

#ifndef QUCS_MAINWINDOW_H
#define QUCS_MAINWINDOW_H

#include <QList>
#include <QMainWindow>

#include "netlist.h"
#include "simulator.h"

class QAction;
class QCloseEvent;
class QDockWidget;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QScrollArea;
class QTabWidget;
class QTreeWidget;

class Schematic;
class SchematicView;

// One open schematic.  Heap allocated and held by pointer: a SchematicView
// borrows the SimResult of its sheet, so the record must not move when the
// list of open sheets grows.
struct SheetTab {
    Schematic *sch;
    SchematicView *view;
    QScrollArea *scroll;
    Netlist netlist;                         // of the last run, for the viewers
    SimResult result;
    bool hasResult;

    SheetTab() : sch(0), view(0), scroll(0), hasResult(false) {}
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // `path` is a schematic to open at start-up, as on the command line.
    explicit MainWindow(const QString &path = QString(), QWidget *parent = 0);
    ~MainWindow();

public slots:
    void newSheet();
    void openSheet();
    void saveSheet();
    void saveSheetAs();
    void closeSheet();
    void simulate();
    void showOperatingPoint();
    void showNetlist();

protected:
    void closeEvent(QCloseEvent *);

private slots:
    void tabChanged(int index);
    void tabCloseRequested(int index);
    void toolTriggered();
    void placeFromLibrary();
    void editComponent(int index);
    void editDiagram(int index);
    void viewEdited();
    void viewToolChanged(int tool);
    void viewStatus(const QString &text);
    // Which of the edit actions make sense, from the selection of the sheet in
    // front; also where selectionChanged() of every view lands.
    void updateActions();
    // One entry of File > Examples; the path is the action's data.
    void openExample();
    void about();

    // The Edit actions, forwarded to the sheet in front.  A slot each rather
    // than a direct action-to-view connection: one action connected to every
    // open view would run the edit on all of them, including the hidden ones.
    void doSelectAll();
    void doDelete();
    void doRotate();
    void doMirror();
    void doActive();
    void doProperties();

private:
    void buildActions();
    void buildMenus();
    void buildToolBar();
    void buildDocks();
    // The .sch files shipped in res/examples, one menu entry each.
    void buildExamplesMenu(QMenu *file);

    SheetTab *currentTab() const;
    SheetTab *tabAt(int index) const;
    void addSheet(Schematic *sch);
    void connectView(SheetTab *t);
    bool loadFile(const QString &path);

    void updateWindowTitle();
    void message(const QString &text);
    // What the last run produced, in the message dock, and the variables it
    // offers a diagram that has none yet.
    void reportResult();

    // Where the file dialogs start: the directory the last Open or Save landed
    // in, else the home directory, else the root.
    QString startDir() const;
    // Writes t to path and makes path the sheet's file; false, with the reason
    // in the message dock, when the write failed.
    bool writeSheet(SheetTab *t, const QString &path);
    // Asks for a name and writes t there; false when the user backed out or the
    // write failed.  A method of its own rather than the saveSheetAs() slot
    // because askSave() wants it for whichever sheet is being closed, which is
    // not necessarily the one in front.
    bool askFileName(SheetTab *t);
    bool askSave(SheetTab *t);

    // The netlist the simulator would solve, as SPICE-ish text - upstream's
    // "Simulation > View netlist", which shows the one it hands to the kernel.
    QString netlistText(const Netlist &nl) const;

    QTabWidget *m_tabs;
    QTreeWidget *m_lib;
    QPlainTextEdit *m_msg;
    QDockWidget *m_libDock;
    QDockWidget *m_msgDock;
    QLabel *m_posLabel;
    QMenu *m_viewMenu;                       // built before the docks it lists

    QList<SheetTab *> m_sheets;
    QString m_lastDir;                       // where Open and Save As start

    QAction *m_actNew;
    QAction *m_actOpen;
    QAction *m_actSave;
    QAction *m_actSaveAs;
    QAction *m_actClose;
    QAction *m_actQuit;

    QAction *m_actSelectAll;
    QAction *m_actDelete;
    QAction *m_actRotate;
    QAction *m_actMirror;
    QAction *m_actActive;
    QAction *m_actProps;

    QAction *m_actSimulate;
    QAction *m_actOpPoint;
    QAction *m_actNetlist;

    QAction *m_actAbout;

    QList<QAction *> m_toolActs;             // toolbar, in SchematicView::Tool order
};

#endif // QUCS_MAINWINDOW_H
