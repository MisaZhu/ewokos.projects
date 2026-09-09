#ifndef PCMANFM_MAINWINDOW_H
#define PCMANFM_MAINWINDOW_H

/*
 * The PCManFM-Qt main window: menu bar (File/Edit/View/Go/Tools/Help),
 * navigation toolbar with a path bar, the Places / directory-tree side pane
 * behind a splitter, and a QTabWidget of FolderViews - the same shape as
 * upstream's PCManFM::MainWindow, minus what has no substrate on EwokOS
 * (trash, mounted volumes, desktop preferences, dbus single-instancing).
 */

#include <QMainWindow>
#include <QStringList>

#include "folderview.h"

class QAction;
class QActionGroup;
class QComboBox;
class QFileSystemModel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QModelIndex;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTreeView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QStringList &paths, QWidget *parent = nullptr);

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createSidePane();

    FolderView *addTab(const QString &path);
    FolderView *currentView() const;
    void closeTab(int index);

    void onPathEntered();
    void onPlaceClicked(QListWidgetItem *item);
    void onTreeClicked(const QModelIndex &index);
    void onTabChanged(int index);
    void onViewPathChanged(const QString &path);
    void showContextMenu(const QPoint &globalPos, const QStringList &sel);
    void updateNavActions();
    void updateStatusBar();

    void doCut();
    void doCopy();
    void doPaste();
    void doDelete();
    void doRename();
    void createFolder();
    void createFile();
    void openTerminal();
    void about();

    QFileSystemModel *model_;     // files+dirs, shared by every tab
    QFileSystemModel *dirModel_;  // dirs only: side-pane tree and completer

    QTabWidget *tabs_;
    QLineEdit *pathEdit_;
    QSplitter *splitter_;
    QComboBox *sideCombo_;
    QStackedWidget *sideStack_;
    QListWidget *placesList_;
    QTreeView *dirTree_;

    QAction *actBack_;
    QAction *actForward_;
    QAction *actUp_;
    QAction *actNewTab_;
    QAction *actCloseTab_;
    QAction *actNewFolder_;
    QAction *actNewFile_;
    QAction *actQuit_;
    QAction *actCut_;
    QAction *actCopy_;
    QAction *actPaste_;
    QAction *actDelete_;
    QAction *actRename_;
    QAction *actSelectAll_;
    QAction *actInvertSel_;
    QAction *actIconView_;
    QAction *actCompactView_;
    QAction *actDetailedView_;
    QActionGroup *viewModeGroup_;
    QAction *actShowHidden_;
    QAction *actReload_;
    QAction *actHome_;
    QAction *actRoot_;
    QAction *actTerminal_;
    QAction *actAbout_;
};

#endif // PCMANFM_MAINWINDOW_H
