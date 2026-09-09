/*
 * FolderView - one tab's worth of file browsing.  See folderview.h.
 */

#include "folderview.h"
#include "fileops.h"

#include <QDir>
#include <QFileSystemModel>
#include <QItemSelectionModel>
#include <QListView>
#include <QStackedWidget>
#include <QTreeView>
#include <QVBoxLayout>

FolderView::FolderView(QFileSystemModel *model, const QString &path, QWidget *parent)
    : QWidget(parent), model_(model), mode_(IconMode)
{
    iconView_ = new QListView(this);
    iconView_->setViewMode(QListView::IconMode);
    iconView_->setResizeMode(QListView::Adjust);
    iconView_->setWrapping(true);
    iconView_->setWordWrap(true);
    iconView_->setUniformItemSizes(true);
    iconView_->setIconSize(QSize(48, 48));
    iconView_->setGridSize(QSize(96, 88));
    iconView_->setSpacing(4);
    // QListView's IconMode default is Snap+free movement, which lets items be
    // dragged into holes and reorders the model view of a directory; a file
    // manager grid is static.
    iconView_->setMovement(QListView::Static);

    compactView_ = new QListView(this);
    compactView_->setViewMode(QListView::ListMode);
    compactView_->setResizeMode(QListView::Adjust);
    compactView_->setWrapping(true);
    compactView_->setUniformItemSizes(true);
    compactView_->setIconSize(QSize(16, 16));

    detailView_ = new QTreeView(this);
    detailView_->setRootIsDecorated(false);
    detailView_->setItemsExpandable(false);
    detailView_->setExpandsOnDoubleClick(false);
    detailView_->setSortingEnabled(true);
    detailView_->sortByColumn(0, Qt::AscendingOrder);
    detailView_->setAllColumnsShowFocus(true);
    detailView_->setIconSize(QSize(16, 16));

    // One selection model across the three views: mode switches keep the
    // selection, and selectedPaths() has a single source of truth.
    selection_ = new QItemSelectionModel(model_, this);

    const QList<QAbstractItemView *> views = {iconView_, compactView_, detailView_};
    for (QAbstractItemView *v : views) {
        v->setModel(model_);
        v->setSelectionModel(selection_);
        v->setSelectionMode(QAbstractItemView::ExtendedSelection);
        v->setEditTriggers(QAbstractItemView::NoEditTriggers);
        v->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(v, &QAbstractItemView::activated, this, &FolderView::onActivated);
        connect(v, &QWidget::customContextMenuRequested, this, &FolderView::onContextMenu);
    }
    connect(selection_, &QItemSelectionModel::selectionChanged,
            this, [this]() { emit selectionChanged(); });

    stack_ = new QStackedWidget(this);
    stack_->addWidget(iconView_);     // index == IconMode
    stack_->addWidget(compactView_);  // index == CompactMode
    stack_->addWidget(detailView_);   // index == DetailedMode

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack_);

    changePath(path);
}

QAbstractItemView *FolderView::currentItemView() const
{
    return static_cast<QAbstractItemView *>(stack_->currentWidget());
}

void FolderView::changePath(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    // setRootPath both anchors the model and moves its directory watcher, so
    // the current folder refreshes when something else changes it.
    const QModelIndex root = model_->setRootPath(clean);
    if (!root.isValid())
        return;
    path_ = clean;
    iconView_->setRootIndex(root);
    compactView_->setRootIndex(root);
    detailView_->setRootIndex(root);
    selection_->clear();
    emit pathChanged(path_);
}

void FolderView::setPath(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    if (clean == path_ || !QDir(clean).exists())
        return;
    back_.append(path_);
    forward_.clear();
    changePath(clean);
}

void FolderView::setViewMode(ViewMode mode)
{
    mode_ = mode;
    stack_->setCurrentIndex(static_cast<int>(mode));
}

QStringList FolderView::selectedPaths() const
{
    QStringList paths;
    // Detailed mode selects whole rows; keep column 0 only.
    const QModelIndexList rows = selection_->selectedIndexes();
    for (const QModelIndex &idx : rows) {
        if (idx.column() == 0)
            paths << model_->filePath(idx);
    }
    return paths;
}

void FolderView::selectAll()
{
    currentItemView()->selectAll();
}

void FolderView::invertSelection()
{
    const QModelIndex root = currentItemView()->rootIndex();
    const int rows = model_->rowCount(root);
    for (int i = 0; i < rows; ++i) {
        selection_->select(model_->index(i, 0, root),
                           QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
    }
}

bool FolderView::canGoUp() const
{
    return path_ != "/";
}

void FolderView::goBack()
{
    if (back_.isEmpty())
        return;
    forward_.prepend(path_);
    changePath(back_.takeLast());
}

void FolderView::goForward()
{
    if (forward_.isEmpty())
        return;
    back_.append(path_);
    changePath(forward_.takeFirst());
}

void FolderView::goUp()
{
    QDir dir(path_);
    if (dir.cdUp())
        setPath(dir.absolutePath());
}

void FolderView::refresh()
{
    // QFileSystemModel has no public refresh; bouncing the root path off "/"
    // makes it re-read the directory.
    const QString keep = path_;
    model_->setRootPath(QString());
    changePath(keep);
}

void FolderView::onActivated(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    const QString path = model_->filePath(index);
    if (model_->isDir(index))
        setPath(path);
    else
        emit fileActivated(path);
}

void FolderView::onContextMenu(const QPoint &pos)
{
    QAbstractItemView *view = currentItemView();
    const QModelIndex idx = view->indexAt(pos);
    QStringList sel;
    if (idx.isValid()) {
        // Right-clicking outside the selection retargets it, as upstream does.
        if (!selection_->isSelected(idx))
            selection_->select(idx, QItemSelectionModel::ClearAndSelect |
                                        QItemSelectionModel::Rows);
        sel = selectedPaths();
    } else {
        selection_->clear();
    }
    emit contextMenuRequested(view->viewport()->mapToGlobal(pos), sel);
}
