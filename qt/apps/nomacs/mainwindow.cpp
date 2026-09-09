/*
 * MainWindow - the nomacs shell.  See mainwindow.h for the mapping to the
 * upstream window.
 */

#include "mainwindow.h"
#include "thumbnailbar.h"
#include "viewport.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

static QString startPath()
{
    const QString home = QDir::homePath();
    return (!home.isEmpty() && QDir(home).exists()) ? home : QStringLiteral("/");
}

static QString dialogFilter()
{
    return QObject::tr("Images (%1)").arg(ImageLoader::nameFilters().join(' '));
}

MainWindow::MainWindow(const QString &path, QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("nomacs - Image Lounge");
    resize(760, 480);

    loader_ = new ImageLoader(this);
    connect(loader_, &ImageLoader::imageChanged, this, &MainWindow::onImageChanged);
    connect(loader_, &ImageLoader::folderChanged, this, &MainWindow::onFolderChanged);

    viewport_ = new Viewport(this);
    connect(viewport_, &Viewport::zoomChanged,
            this, [this](qreal) { updateStatusBar(); });
    connect(viewport_, &Viewport::nextRequested,
            this, [this]() { loader_->next(); });
    connect(viewport_, &Viewport::previousRequested,
            this, [this]() { loader_->previous(); });

    thumbs_ = new ThumbnailBar(this);
    connect(thumbs_, &ThumbnailBar::thumbnailClicked,
            this, [this](int index) { loader_->jumpTo(index); });

    // Viewport over strip, no margins - the strip reads as part of the
    // canvas the way upstream's preview panel does.
    QWidget *central = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(viewport_, 1);
    lay->addWidget(thumbs_, 0);
    setCentralWidget(central);

    slideshowTimer_ = new QTimer(this);
    slideshowTimer_->setInterval(3000);  // upstream's default display time
    connect(slideshowTimer_, &QTimer::timeout,
            this, [this]() { loader_->next(); });

    createActions();
    createToolBar();
    createMenus();

    statusFile_ = new QLabel(this);
    statusInfo_ = new QLabel(this);
    statusBar()->addWidget(statusFile_, 1);
    statusBar()->addPermanentWidget(statusInfo_);

    loader_->load(path.isEmpty() ? startPath() : path);
}

// ---- construction -----------------------------------------------------------

void MainWindow::createActions()
{
    QStyle *st = style();

    actOpen_ = new QAction(st->standardIcon(QStyle::SP_DialogOpenButton),
                           tr("Open..."), this);
    actOpen_->setShortcut(QKeySequence::Open);
    connect(actOpen_, &QAction::triggered, this, &MainWindow::openFile);

    actOpenFolder_ = new QAction(st->standardIcon(QStyle::SP_DirOpenIcon),
                                 tr("Open Folder..."), this);
    actOpenFolder_->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(actOpenFolder_, &QAction::triggered, this, &MainWindow::openFolder);

    actSaveAs_ = new QAction(st->standardIcon(QStyle::SP_DialogSaveButton),
                             tr("Save As..."), this);
    actSaveAs_->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(actSaveAs_, &QAction::triggered, this, &MainWindow::saveAs);

    actRename_ = new QAction(tr("Rename..."), this);
    actRename_->setShortcut(QKeySequence("F2"));
    connect(actRename_, &QAction::triggered, this, &MainWindow::renameFile);

    actDelete_ = new QAction(tr("Delete"), this);
    actDelete_->setShortcut(QKeySequence::Delete);
    connect(actDelete_, &QAction::triggered, this, &MainWindow::deleteFile);

    actReload_ = new QAction(tr("Reload"), this);
    actReload_->setShortcut(QKeySequence("F5"));
    connect(actReload_, &QAction::triggered, this, [this]() { loader_->reload(); });

    actQuit_ = new QAction(tr("Quit"), this);
    actQuit_->setShortcut(QKeySequence("Ctrl+Q"));
    connect(actQuit_, &QAction::triggered, this, &MainWindow::close);

    // Edit - the upstream shortcuts (r / Shift+R, Shift+H / Shift+V).
    actRotateCw_ = new QAction(tr("Rotate 90° Right"), this);
    actRotateCw_->setShortcut(QKeySequence("R"));
    connect(actRotateCw_, &QAction::triggered, this, [this]() { loader_->rotate(90); });

    actRotateCcw_ = new QAction(tr("Rotate 90° Left"), this);
    actRotateCcw_->setShortcut(QKeySequence("Shift+R"));
    connect(actRotateCcw_, &QAction::triggered, this, [this]() { loader_->rotate(-90); });

    actFlipH_ = new QAction(tr("Flip Horizontal"), this);
    actFlipH_->setShortcut(QKeySequence("Shift+H"));
    connect(actFlipH_, &QAction::triggered, this, [this]() { loader_->flip(true); });

    actFlipV_ = new QAction(tr("Flip Vertical"), this);
    actFlipV_->setShortcut(QKeySequence("Shift+V"));
    connect(actFlipV_, &QAction::triggered, this, [this]() { loader_->flip(false); });

    // Navigation.  Left/Right/Home/End arrive through keyPressEvent instead
    // of shortcuts so they keep working when a child widget has focus.
    actPrev_ = new QAction(st->standardIcon(QStyle::SP_ArrowBack),
                           tr("Previous Image"), this);
    connect(actPrev_, &QAction::triggered, this, [this]() { loader_->previous(); });

    actNext_ = new QAction(st->standardIcon(QStyle::SP_ArrowForward),
                           tr("Next Image"), this);
    connect(actNext_, &QAction::triggered, this, [this]() { loader_->next(); });

    actFirst_ = new QAction(tr("First Image"), this);
    connect(actFirst_, &QAction::triggered, this, [this]() { loader_->first(); });

    actLast_ = new QAction(tr("Last Image"), this);
    connect(actLast_, &QAction::triggered, this, [this]() { loader_->last(); });

    // View.
    actZoomIn_ = new QAction(tr("Zoom In"), this);
    actZoomIn_->setShortcut(QKeySequence::ZoomIn);
    connect(actZoomIn_, &QAction::triggered, this, [this]() { viewport_->zoomIn(); });

    actZoomOut_ = new QAction(tr("Zoom Out"), this);
    actZoomOut_->setShortcut(QKeySequence::ZoomOut);
    connect(actZoomOut_, &QAction::triggered, this, [this]() { viewport_->zoomOut(); });

    actZoomFit_ = new QAction(tr("Fit to Window"), this);
    actZoomFit_->setShortcut(QKeySequence("Ctrl+0"));
    connect(actZoomFit_, &QAction::triggered, this, [this]() { viewport_->zoomToFit(); });

    actZoom100_ = new QAction(tr("Actual Size"), this);
    actZoom100_->setShortcut(QKeySequence("Ctrl+1"));
    connect(actZoom100_, &QAction::triggered, this, [this]() { viewport_->zoomActualSize(); });

    actFullScreen_ = new QAction(tr("Full Screen"), this);
    actFullScreen_->setShortcut(QKeySequence("F11"));
    actFullScreen_->setCheckable(true);
    connect(actFullScreen_, &QAction::triggered, this, &MainWindow::toggleFullScreen);

    actSlideshow_ = new QAction(tr("Slideshow"), this);
    actSlideshow_->setShortcut(QKeySequence("F9"));
    actSlideshow_->setCheckable(true);
    connect(actSlideshow_, &QAction::triggered, this, &MainWindow::toggleSlideshow);

    actShowThumbs_ = new QAction(tr("Thumbnails"), this);
    actShowThumbs_->setShortcut(QKeySequence("T"));
    actShowThumbs_->setCheckable(true);
    actShowThumbs_->setChecked(true);
    connect(actShowThumbs_, &QAction::toggled,
            this, [this](bool on) { thumbs_->setVisible(on); });

    actAbout_ = new QAction(tr("About nomacs"), this);
    connect(actAbout_, &QAction::triggered, this, &MainWindow::about);
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(actOpen_);
    file->addAction(actOpenFolder_);
    file->addSeparator();
    file->addAction(actSaveAs_);
    file->addAction(actRename_);
    file->addAction(actDelete_);
    file->addSeparator();
    file->addAction(actReload_);
    file->addSeparator();
    file->addAction(actQuit_);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(actRotateCw_);
    edit->addAction(actRotateCcw_);
    edit->addSeparator();
    edit->addAction(actFlipH_);
    edit->addAction(actFlipV_);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addAction(actPrev_);
    view->addAction(actNext_);
    view->addAction(actFirst_);
    view->addAction(actLast_);
    view->addSeparator();
    view->addAction(actZoomIn_);
    view->addAction(actZoomOut_);
    view->addAction(actZoomFit_);
    view->addAction(actZoom100_);
    view->addSeparator();
    view->addAction(actShowThumbs_);
    view->addAction(actFullScreen_);
    view->addAction(actSlideshow_);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(actAbout_);
}

void MainWindow::createToolBar()
{
    QToolBar *tb = addToolBar(tr("Main"));
    tb->setMovable(false);
    tb->addAction(actOpen_);
    tb->addSeparator();
    tb->addAction(actPrev_);
    tb->addAction(actNext_);
    tb->addSeparator();
    tb->addAction(actRotateCcw_);
    tb->addAction(actRotateCw_);
}

// ---- loader plumbing ---------------------------------------------------------

void MainWindow::onImageChanged()
{
    viewport_->setImage(loader_->image());
    thumbs_->setCurrent(loader_->currentIndex());

    const QString name = QFileInfo(loader_->filePath()).fileName();
    setWindowTitle(name.isEmpty() ? QStringLiteral("nomacs - Image Lounge")
                                  : name + QStringLiteral(" - nomacs"));
    updateStatusBar();
}

void MainWindow::onFolderChanged()
{
    thumbs_->setFiles(loader_->dirPath(), loader_->files());
    thumbs_->setCurrent(loader_->currentIndex());
}

static QString humanSize(qint64 bytes)
{
    if (bytes >= 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    if (bytes >= 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " kB";
    return QString::number(bytes) + " B";
}

void MainWindow::updateStatusBar()
{
    if (loader_->count() == 0) {
        statusFile_->setText(tr("No images in %1").arg(loader_->dirPath()));
        statusInfo_->clear();
        return;
    }
    statusFile_->setText(QString("%1/%2  %3")
                             .arg(loader_->currentIndex() + 1)
                             .arg(loader_->count())
                             .arg(QFileInfo(loader_->filePath()).fileName()));
    const QImage &img = loader_->image();
    if (img.isNull()) {
        statusInfo_->setText(tr("cannot decode"));
        return;
    }
    statusInfo_->setText(QString("%1 x %2  %3  %4%")
                             .arg(img.width())
                             .arg(img.height())
                             .arg(humanSize(loader_->fileSize()))
                             .arg(qRound(viewport_->zoom() * 100)));
}

// ---- actions -----------------------------------------------------------------

void MainWindow::openFile()
{
    const QString start = loader_->dirPath().isEmpty() ? startPath()
                                                       : loader_->dirPath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Image"), start, dialogFilter());
    if (!path.isEmpty())
        loader_->load(path);
}

void MainWindow::openFolder()
{
    const QString start = loader_->dirPath().isEmpty() ? startPath()
                                                       : loader_->dirPath();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Open Folder"), start);
    if (!dir.isEmpty())
        loader_->load(dir);
}

void MainWindow::saveAs()
{
    if (loader_->image().isNull())
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save As"), loader_->filePath(), dialogFilter());
    if (path.isEmpty())
        return;
    if (!loader_->save(path))
        QMessageBox::warning(this, tr("Save As"),
                             tr("Could not write %1").arg(path));
}

void MainWindow::renameFile()
{
    const QString path = loader_->filePath();
    if (path.isEmpty())
        return;
    const QString oldName = QFileInfo(path).fileName();
    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, tr("Rename"), tr("New name:"), QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.isEmpty() || newName == oldName)
        return;
    const QString newPath = loader_->dirPath() + "/" + newName;
    if (!QFile::rename(path, newPath)) {
        QMessageBox::warning(this, tr("Rename"),
                             tr("Could not rename %1").arg(oldName));
        return;
    }
    loader_->load(newPath);
}

void MainWindow::deleteFile()
{
    const QString path = loader_->filePath();
    if (path.isEmpty())
        return;
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Delete"),
        tr("Delete %1?").arg(QFileInfo(path).fileName()),
        QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    if (!QFile::remove(path)) {
        QMessageBox::warning(this, tr("Delete"),
                             tr("Could not delete %1").arg(path));
        return;
    }
    loader_->reload();  // the successor at the same index becomes current
}

void MainWindow::toggleFullScreen()
{
    // Upstream also hides the chrome in fullscreen - the image gets the
    // whole screen, Esc (via keyPressEvent) brings the window back.
    if (isFullScreen()) {
        showNormal();
        menuBar()->show();
        statusBar()->show();
        if (actShowThumbs_->isChecked())
            thumbs_->show();
        actFullScreen_->setChecked(false);
    } else {
        menuBar()->hide();
        statusBar()->hide();
        thumbs_->hide();
        showFullScreen();
        actFullScreen_->setChecked(true);
    }
}

void MainWindow::toggleSlideshow()
{
    if (slideshowTimer_->isActive()) {
        slideshowTimer_->stop();
        actSlideshow_->setChecked(false);
    } else {
        slideshowTimer_->start();
        actSlideshow_->setChecked(true);
    }
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About nomacs"),
        tr("<b>nomacs - Image Lounge</b><br>"
           "EwokOS port of <a href=\"https://github.com/nomacs/nomacs\">"
           "nomacs</a> (GPL-3.0), rebuilt on plain QtWidgets.<br><br>"
           "Formats: png, jpeg, bmp, ppm, pgm, pbm, xbm, xpm."));
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // The nomacs keys: arrows / space / backspace browse, Esc leaves
    // fullscreen (or a running slideshow) before it means quit.
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Backspace:
        loader_->previous();
        return;
    case Qt::Key_Right:
    case Qt::Key_Space:
        loader_->next();
        return;
    case Qt::Key_Home:
        loader_->first();
        return;
    case Qt::Key_End:
        loader_->last();
        return;
    case Qt::Key_Escape:
        if (slideshowTimer_->isActive())
            toggleSlideshow();
        else if (isFullScreen())
            toggleFullScreen();
        else
            close();
        return;
    default:
        QMainWindow::keyPressEvent(event);
    }
}
