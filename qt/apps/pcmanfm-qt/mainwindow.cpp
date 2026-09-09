/*
 * MainWindow - the PCManFM-Qt shell.  See mainwindow.h for the mapping to
 * the upstream window.
 */

#include "mainwindow.h"
#include "fileops.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>

static QString homePath()
{
    const QString home = QDir::homePath();
    return (!home.isEmpty() && QDir(home).exists()) ? home : QStringLiteral("/");
}

static QString tabTitle(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? QStringLiteral("/") : name;
}

MainWindow::MainWindow(const QStringList &paths, QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("PCManFM-Qt");
    resize(760, 480);

    model_ = new QFileSystemModel(this);
    model_->setRootPath("/");
    model_->setReadOnly(false);
    model_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);

    dirModel_ = new QFileSystemModel(this);
    dirModel_->setRootPath("/");
    dirModel_->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);

    createActions();
    createToolBar();
    createSidePane();

    tabs_ = new QTabWidget(this);
    tabs_->setTabsClosable(true);
    tabs_->setDocumentMode(true);
    connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);

    splitter_->addWidget(tabs_);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes(QList<int>() << 160 << 600);
    setCentralWidget(splitter_);

    createMenus();
    statusBar();  // instantiate before the first update

    // The model row counts the status bar reports arrive asynchronously.
    connect(model_, &QFileSystemModel::directoryLoaded,
            this, [this](const QString &) { updateStatusBar(); });

    QStringList open = paths;
    if (open.isEmpty())
        open << homePath();
    for (const QString &p : open)
        addTab(QDir(p).exists() ? QDir(p).absolutePath() : homePath());
}

// ---- construction ------------------------------------------------------------

void MainWindow::createActions()
{
    QStyle *st = style();

    actNewTab_   = new QAction(st->standardIcon(QStyle::SP_FileDialogNewFolder),
                               tr("New Tab"), this);
    actNewTab_->setShortcut(QKeySequence("Ctrl+T"));
    connect(actNewTab_, &QAction::triggered, this, [this]() {
        addTab(currentView() ? currentView()->path() : homePath());
    });

    actCloseTab_ = new QAction(tr("Close Tab"), this);
    actCloseTab_->setShortcut(QKeySequence("Ctrl+W"));
    connect(actCloseTab_, &QAction::triggered, this, [this]() {
        closeTab(tabs_->currentIndex());
    });

    actNewFolder_ = new QAction(tr("Folder"), this);
    actNewFolder_->setShortcut(QKeySequence("Ctrl+Shift+N"));
    connect(actNewFolder_, &QAction::triggered, this, &MainWindow::createFolder);

    actNewFile_ = new QAction(tr("Blank File"), this);
    connect(actNewFile_, &QAction::triggered, this, &MainWindow::createFile);

    actQuit_ = new QAction(tr("Quit"), this);
    actQuit_->setShortcut(QKeySequence("Ctrl+Q"));
    connect(actQuit_, &QAction::triggered, this, &MainWindow::close);

    actCut_ = new QAction(tr("Cut"), this);
    actCut_->setShortcut(QKeySequence::Cut);
    connect(actCut_, &QAction::triggered, this, &MainWindow::doCut);

    actCopy_ = new QAction(tr("Copy"), this);
    actCopy_->setShortcut(QKeySequence::Copy);
    connect(actCopy_, &QAction::triggered, this, &MainWindow::doCopy);

    actPaste_ = new QAction(tr("Paste"), this);
    actPaste_->setShortcut(QKeySequence::Paste);
    connect(actPaste_, &QAction::triggered, this, &MainWindow::doPaste);

    actDelete_ = new QAction(tr("Delete"), this);
    actDelete_->setShortcut(QKeySequence::Delete);
    connect(actDelete_, &QAction::triggered, this, &MainWindow::doDelete);

    actRename_ = new QAction(tr("Rename..."), this);
    actRename_->setShortcut(QKeySequence("F2"));
    connect(actRename_, &QAction::triggered, this, &MainWindow::doRename);

    actSelectAll_ = new QAction(tr("Select All"), this);
    actSelectAll_->setShortcut(QKeySequence::SelectAll);
    connect(actSelectAll_, &QAction::triggered, this, [this]() {
        if (currentView()) currentView()->selectAll();
    });

    actInvertSel_ = new QAction(tr("Invert Selection"), this);
    actInvertSel_->setShortcut(QKeySequence("Ctrl+I"));
    connect(actInvertSel_, &QAction::triggered, this, [this]() {
        if (currentView()) currentView()->invertSelection();
    });

    viewModeGroup_ = new QActionGroup(this);
    actIconView_     = new QAction(tr("Icon View"), viewModeGroup_);
    actCompactView_  = new QAction(tr("Compact View"), viewModeGroup_);
    actDetailedView_ = new QAction(tr("Detailed List View"), viewModeGroup_);
    actIconView_->setShortcut(QKeySequence("Ctrl+1"));
    actCompactView_->setShortcut(QKeySequence("Ctrl+2"));
    actDetailedView_->setShortcut(QKeySequence("Ctrl+3"));
    const QList<QPair<QAction *, FolderView::ViewMode>> modes = {
        {actIconView_, FolderView::IconMode},
        {actCompactView_, FolderView::CompactMode},
        {actDetailedView_, FolderView::DetailedMode},
    };
    for (const auto &m : modes) {
        m.first->setCheckable(true);
        FolderView::ViewMode mode = m.second;
        connect(m.first, &QAction::triggered, this, [this, mode]() {
            if (currentView()) currentView()->setViewMode(mode);
        });
    }
    actIconView_->setChecked(true);

    actShowHidden_ = new QAction(tr("Show Hidden"), this);
    actShowHidden_->setCheckable(true);
    actShowHidden_->setShortcut(QKeySequence("Ctrl+H"));
    connect(actShowHidden_, &QAction::triggered, this, [this](bool on) {
        QDir::Filters f = QDir::AllEntries | QDir::NoDotAndDotDot;
        if (on)
            f |= QDir::Hidden;
        model_->setFilter(f);
    });

    actReload_ = new QAction(st->standardIcon(QStyle::SP_BrowserReload),
                             tr("Reload"), this);
    actReload_->setShortcut(QKeySequence("F5"));
    connect(actReload_, &QAction::triggered, this, [this]() {
        if (currentView()) currentView()->refresh();
    });

    actBack_ = new QAction(st->standardIcon(QStyle::SP_ArrowBack),
                           tr("Previous Folder"), this);
    actBack_->setShortcut(QKeySequence("Alt+Left"));
    connect(actBack_, &QAction::triggered, this, [this]() {
        if (currentView()) currentView()->goBack();
    });

    actForward_ = new QAction(st->standardIcon(QStyle::SP_ArrowForward),
                              tr("Next Folder"), this);
    actForward_->setShortcut(QKeySequence("Alt+Right"));
    connect(actForward_, &QAction::triggered, this, [this]() {
        if (currentView()) currentView()->goForward();
    });

    actUp_ = new QAction(st->standardIcon(QStyle::SP_ArrowUp),
                         tr("Parent Folder"), this);
    actUp_->setShortcut(QKeySequence("Alt+Up"));
    connect(actUp_, &QAction::triggered, this, [this]() {
        if (currentView()) currentView()->goUp();
    });

    actHome_ = new QAction(st->standardIcon(QStyle::SP_DirHomeIcon),
                           tr("Home"), this);
    connect(actHome_, &QAction::triggered, this, [this]() {
        if (currentView()) currentView()->setPath(homePath());
    });

    actRoot_ = new QAction(st->standardIcon(QStyle::SP_DriveHDIcon),
                           tr("Root"), this);
    connect(actRoot_, &QAction::triggered, this, [this]() {
        if (currentView()) currentView()->setPath("/");
    });

    actTerminal_ = new QAction(tr("Open Terminal"), this);
    actTerminal_->setShortcut(QKeySequence("F4"));
    connect(actTerminal_, &QAction::triggered, this, &MainWindow::openTerminal);

    actAbout_ = new QAction(tr("About"), this);
    connect(actAbout_, &QAction::triggered, this, &MainWindow::about);
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(actNewTab_);
    QMenu *createNew = file->addMenu(tr("Create &New"));
    createNew->addAction(actNewFolder_);
    createNew->addAction(actNewFile_);
    file->addSeparator();
    file->addAction(actCloseTab_);
    file->addAction(actQuit_);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(actCut_);
    edit->addAction(actCopy_);
    edit->addAction(actPaste_);
    edit->addAction(actDelete_);
    edit->addAction(actRename_);
    edit->addSeparator();
    edit->addAction(actSelectAll_);
    edit->addAction(actInvertSel_);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addAction(actIconView_);
    view->addAction(actCompactView_);
    view->addAction(actDetailedView_);
    view->addSeparator();
    view->addAction(actShowHidden_);
    view->addAction(actReload_);

    QMenu *go = menuBar()->addMenu(tr("&Go"));
    go->addAction(actBack_);
    go->addAction(actForward_);
    go->addAction(actUp_);
    go->addSeparator();
    go->addAction(actHome_);
    go->addAction(actRoot_);

    QMenu *tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(actTerminal_);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(actAbout_);
}

void MainWindow::createToolBar()
{
    QToolBar *bar = addToolBar(tr("Navigation"));
    bar->setMovable(false);
    bar->addAction(actNewTab_);
    bar->addSeparator();
    bar->addAction(actBack_);
    bar->addAction(actForward_);
    bar->addAction(actUp_);
    bar->addAction(actHome_);
    bar->addAction(actReload_);

    pathEdit_ = new QLineEdit(this);
    QCompleter *completer = new QCompleter(dirModel_, this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    pathEdit_->setCompleter(completer);
    connect(pathEdit_, &QLineEdit::returnPressed, this, &MainWindow::onPathEntered);
    bar->addWidget(pathEdit_);
}

void MainWindow::createSidePane()
{
    splitter_ = new QSplitter(Qt::Horizontal, this);

    QWidget *pane = new QWidget(splitter_);
    QVBoxLayout *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    sideCombo_ = new QComboBox(pane);
    sideCombo_->addItem(tr("Places"));
    sideCombo_->addItem(tr("Directory Tree"));
    layout->addWidget(sideCombo_);

    sideStack_ = new QStackedWidget(pane);
    layout->addWidget(sideStack_);
    connect(sideCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            sideStack_, &QStackedWidget::setCurrentIndex);

    placesList_ = new QListWidget(sideStack_);
    QStyle *st = style();
    struct Place { QString name; QString path; QStyle::StandardPixmap icon; };
    const QList<Place> places = {
        {tr("Home"),         homePath(), QStyle::SP_DirHomeIcon},
        {tr("Root"),         "/",        QStyle::SP_DriveHDIcon},
        {tr("Applications"), "/apps",    QStyle::SP_DirIcon},
        {tr("System"),       "/usr",     QStyle::SP_DirIcon},
        {tr("Temporary"),    "/tmp",     QStyle::SP_DirIcon},
    };
    for (const Place &p : places) {
        if (!QDir(p.path).exists())
            continue;
        QListWidgetItem *item = new QListWidgetItem(st->standardIcon(p.icon),
                                                    p.name, placesList_);
        item->setData(Qt::UserRole, p.path);
    }
    connect(placesList_, &QListWidget::itemClicked,
            this, &MainWindow::onPlaceClicked);
    sideStack_->addWidget(placesList_);

    dirTree_ = new QTreeView(sideStack_);
    dirTree_->setModel(dirModel_);
    dirTree_->setRootIndex(dirModel_->index("/"));
    dirTree_->setHeaderHidden(true);
    // Only the name column; size/type/date are noise in a tree pane.
    for (int col = 1; col < dirModel_->columnCount(); ++col)
        dirTree_->setColumnHidden(col, true);
    connect(dirTree_, &QTreeView::clicked, this, &MainWindow::onTreeClicked);
    sideStack_->addWidget(dirTree_);

    splitter_->addWidget(pane);
}

// ---- tabs ----------------------------------------------------------------------

FolderView *MainWindow::addTab(const QString &path)
{
    FolderView *view = new FolderView(model_, path, tabs_);
    connect(view, &FolderView::pathChanged, this, &MainWindow::onViewPathChanged);
    connect(view, &FolderView::selectionChanged, this, &MainWindow::updateStatusBar);
    connect(view, &FolderView::fileActivated, this, [this](const QString &p) {
        FileOps::openFile(p, this);
    });
    connect(view, &FolderView::contextMenuRequested,
            this, &MainWindow::showContextMenu);

    const int idx = tabs_->addTab(view, tabTitle(view->path()));
    tabs_->setCurrentIndex(idx);
    return view;
}

FolderView *MainWindow::currentView() const
{
    return qobject_cast<FolderView *>(tabs_->currentWidget());
}

void MainWindow::closeTab(int index)
{
    // Upstream closes the window with its last tab.
    if (tabs_->count() <= 1) {
        close();
        return;
    }
    QWidget *page = tabs_->widget(index);
    tabs_->removeTab(index);
    page->deleteLater();
}

// ---- navigation & UI sync -------------------------------------------------------

void MainWindow::onPathEntered()
{
    const QString path = pathEdit_->text().trimmed();
    if (path.isEmpty() || !currentView())
        return;
    if (QDir(path).exists())
        currentView()->setPath(path);
    else
        QMessageBox::warning(this, tr("Error"),
                             tr("Folder %1 does not exist.").arg(path));
}

void MainWindow::onPlaceClicked(QListWidgetItem *item)
{
    if (currentView())
        currentView()->setPath(item->data(Qt::UserRole).toString());
}

void MainWindow::onTreeClicked(const QModelIndex &index)
{
    if (currentView())
        currentView()->setPath(dirModel_->filePath(index));
}

void MainWindow::onTabChanged(int index)
{
    Q_UNUSED(index);
    FolderView *view = currentView();
    if (!view)
        return;
    pathEdit_->setText(view->path());
    switch (view->viewMode()) {
    case FolderView::IconMode:     actIconView_->setChecked(true); break;
    case FolderView::CompactMode:  actCompactView_->setChecked(true); break;
    case FolderView::DetailedMode: actDetailedView_->setChecked(true); break;
    }
    setWindowTitle(QString("%1 - PCManFM-Qt").arg(view->path()));
    updateNavActions();
    updateStatusBar();
}

void MainWindow::onViewPathChanged(const QString &path)
{
    FolderView *view = qobject_cast<FolderView *>(sender());
    if (!view)
        return;
    const int idx = tabs_->indexOf(view);
    if (idx >= 0)
        tabs_->setTabText(idx, tabTitle(path));
    if (view == currentView()) {
        pathEdit_->setText(path);
        setWindowTitle(QString("%1 - PCManFM-Qt").arg(path));
        updateNavActions();
        updateStatusBar();
    }
}

void MainWindow::updateNavActions()
{
    FolderView *view = currentView();
    actBack_->setEnabled(view && view->canGoBack());
    actForward_->setEnabled(view && view->canGoForward());
    actUp_->setEnabled(view && view->canGoUp());
}

void MainWindow::updateStatusBar()
{
    FolderView *view = currentView();
    if (!view)
        return;
    const int total = model_->rowCount(model_->index(view->path()));
    const QStringList sel = view->selectedPaths();
    QString text = tr("%1 items").arg(total);
    if (!sel.isEmpty()) {
        qint64 bytes = 0;
        for (const QString &p : sel) {
            QFileInfo info(p);
            if (info.isFile())
                bytes += info.size();
        }
        text = tr("%1 items selected (%2)").arg(sel.count())
                   .arg(FileOps::humanSize(bytes)) + "  |  " + text;
    }
    statusBar()->showMessage(text);
}

// ---- file operations --------------------------------------------------------------

void MainWindow::doCut()
{
    if (currentView() && !currentView()->selectedPaths().isEmpty())
        FileOps::setClipboard(currentView()->selectedPaths(), true);
}

void MainWindow::doCopy()
{
    if (currentView() && !currentView()->selectedPaths().isEmpty())
        FileOps::setClipboard(currentView()->selectedPaths(), false);
}

void MainWindow::doPaste()
{
    FolderView *view = currentView();
    if (!view || FileOps::clipboardEmpty())
        return;
    const QStringList paths = FileOps::clipboardPaths();
    if (FileOps::clipboardIsCut()) {
        if (FileOps::movePaths(paths, view->path(), this))
            FileOps::clearClipboard();
    } else {
        FileOps::copyPaths(paths, view->path(), this);
    }
}

void MainWindow::doDelete()
{
    FolderView *view = currentView();
    if (!view)
        return;
    const QStringList sel = view->selectedPaths();
    if (sel.isEmpty())
        return;
    const QString what = sel.count() == 1
        ? QFileInfo(sel.first()).fileName()
        : tr("the %1 selected items").arg(sel.count());
    if (QMessageBox::question(this, tr("Delete"),
                              tr("Really delete %1?").arg(what))
        != QMessageBox::Yes)
        return;
    FileOps::deletePaths(sel, this);
}

void MainWindow::doRename()
{
    FolderView *view = currentView();
    if (!view)
        return;
    const QStringList sel = view->selectedPaths();
    if (sel.count() != 1)
        return;
    QFileInfo info(sel.first());
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename File"),
                                               tr("New name:"), QLineEdit::Normal,
                                               info.fileName(), &ok);
    if (!ok || name.isEmpty() || name == info.fileName())
        return;
    if (!QFile::rename(sel.first(), info.absolutePath() + "/" + name))
        QMessageBox::warning(this, tr("Error"),
                             tr("Cannot rename %1.").arg(info.fileName()));
}

void MainWindow::createFolder()
{
    FolderView *view = currentView();
    if (!view)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Create Folder"),
                                               tr("Folder name:"), QLineEdit::Normal,
                                               tr("New Folder"), &ok);
    if (!ok || name.isEmpty())
        return;
    if (!QDir(view->path()).mkdir(name))
        QMessageBox::warning(this, tr("Error"),
                             tr("Cannot create folder %1.").arg(name));
}

void MainWindow::createFile()
{
    FolderView *view = currentView();
    if (!view)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Create File"),
                                               tr("File name:"), QLineEdit::Normal,
                                               tr("new file.txt"), &ok);
    if (!ok || name.isEmpty())
        return;
    QFile f(view->path() + "/" + name);
    if (f.exists() || !f.open(QIODevice::WriteOnly))
        QMessageBox::warning(this, tr("Error"),
                             tr("Cannot create file %1.").arg(name));
}

void MainWindow::openTerminal()
{
    FileOps::launch("/apps/xterm/xterm", "/apps/xterm/xterm");
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About"),
        tr("<b>PCManFM-Qt for EwokOS</b><br><br>"
           "A file manager modelled on "
           "<a href=\"https://github.com/lxqt/pcmanfm-qt\">PCManFM-Qt</a> "
           "(LXQt, LGPL-2.1), rebuilt on plain QtWidgets for the EwokOS "
           "Qt 5.15 port."));
}

// ---- context menus -----------------------------------------------------------------

void MainWindow::showContextMenu(const QPoint &globalPos, const QStringList &sel)
{
    FolderView *view = currentView();
    if (!view)
        return;

    QMenu menu(this);
    if (sel.isEmpty()) {
        // view background
        QMenu *createNew = menu.addMenu(tr("Create New"));
        createNew->addAction(actNewFolder_);
        createNew->addAction(actNewFile_);
        menu.addSeparator();
        QAction *paste = menu.addAction(tr("Paste"), this, &MainWindow::doPaste);
        paste->setEnabled(!FileOps::clipboardEmpty());
        menu.addSeparator();
        menu.addAction(actSelectAll_);
        menu.addSeparator();
        menu.addAction(tr("Properties"), this, [this, view]() {
            FileOps::showProperties(view->path(), this);
        });
    } else {
        const bool single = sel.count() == 1;
        const bool isDir = single && QFileInfo(sel.first()).isDir();
        menu.addAction(tr("Open"), this, [this, view, sel, isDir]() {
            if (isDir)
                view->setPath(sel.first());
            else
                for (const QString &p : sel)
                    FileOps::openFile(p, this);
        });
        if (isDir) {
            menu.addAction(tr("Open in New Tab"), this, [this, sel]() {
                addTab(sel.first());
            });
        }
        menu.addSeparator();
        menu.addAction(actCut_);
        menu.addAction(actCopy_);
        if (isDir) {
            QAction *pasteInto = menu.addAction(tr("Paste Into Folder"),
                                                this, [this, sel]() {
                const QStringList paths = FileOps::clipboardPaths();
                if (FileOps::clipboardIsCut()) {
                    if (FileOps::movePaths(paths, sel.first(), this))
                        FileOps::clearClipboard();
                } else {
                    FileOps::copyPaths(paths, sel.first(), this);
                }
            });
            pasteInto->setEnabled(!FileOps::clipboardEmpty());
        }
        menu.addSeparator();
        if (single)
            menu.addAction(actRename_);
        menu.addAction(actDelete_);
        menu.addSeparator();
        menu.addAction(tr("Properties"), this, [this, sel]() {
            FileOps::showProperties(sel.first(), this);
        });
    }
    menu.exec(globalPos);
}
