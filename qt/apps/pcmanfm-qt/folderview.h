#ifndef PCMANFM_FOLDERVIEW_H
#define PCMANFM_FOLDERVIEW_H

/*
 * One tab of the main window: upstream's Fm::FolderView with its three view
 * modes (icon / compact / detailed list) rebuilt as a QStackedWidget over two
 * QListViews and a QTreeView, all reading the one shared QFileSystemModel and
 * sharing a single QItemSelectionModel so switching modes keeps the
 * selection.  Per-tab back/forward history lives here too, as it does in
 * upstream's TabPage.
 */

#include <QWidget>
#include <QStringList>

class QFileSystemModel;
class QListView;
class QTreeView;
class QStackedWidget;
class QAbstractItemView;
class QItemSelectionModel;
class QModelIndex;

class FolderView : public QWidget {
    Q_OBJECT
public:
    enum ViewMode { IconMode, CompactMode, DetailedMode };

    FolderView(QFileSystemModel *model, const QString &path, QWidget *parent = nullptr);

    QString path() const { return path_; }
    void setPath(const QString &path);

    ViewMode viewMode() const { return mode_; }
    void setViewMode(ViewMode mode);

    QStringList selectedPaths() const;
    void selectAll();
    void invertSelection();

    bool canGoBack() const    { return !back_.isEmpty(); }
    bool canGoForward() const { return !forward_.isEmpty(); }
    bool canGoUp() const;
    void goBack();
    void goForward();
    void goUp();
    void refresh();

signals:
    void pathChanged(const QString &path);
    void selectionChanged();
    // A file (not a folder) was activated; folders navigate in-place.
    void fileActivated(const QString &path);
    // Right-click; sel is empty for the view background.
    void contextMenuRequested(const QPoint &globalPos, const QStringList &sel);

private:
    void changePath(const QString &path);       // no history bookkeeping
    void onActivated(const QModelIndex &index);
    void onContextMenu(const QPoint &pos);
    QAbstractItemView *currentItemView() const;

    QFileSystemModel *model_;
    QListView *iconView_;
    QListView *compactView_;
    QTreeView *detailView_;
    QStackedWidget *stack_;
    QItemSelectionModel *selection_;

    QString path_;
    QStringList back_;
    QStringList forward_;
    ViewMode mode_;
};

#endif // PCMANFM_FOLDERVIEW_H
