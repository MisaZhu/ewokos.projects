/*
 * ThumbnailBar - see thumbnailbar.h for the mapping to upstream's
 * DkFilePreview.
 */

#include "thumbnailbar.h"

#include <QImageReader>
#include <QPainter>
#include <QPixmap>
#include <QTimer>

static const int kThumbSide = 64;

ThumbnailBar::ThumbnailBar(QWidget *parent)
    : QListWidget(parent)
{
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(false);
    setIconSize(QSize(kThumbSide, kThumbSide));
    setUniformItemSizes(true);
    setSpacing(2);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFixedHeight(kThumbSide + 22);

    // Match the viewport's dark canvas.
    QPalette pal = palette();
    pal.setColor(QPalette::Base, QColor(48, 48, 48));
    setPalette(pal);

    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        emit thumbnailClicked(row(item));
    });

    loadTimer_ = new QTimer(this);
    loadTimer_->setInterval(0);
    connect(loadTimer_, &QTimer::timeout, this, &ThumbnailBar::loadNextThumbnail);
}

void ThumbnailBar::setFiles(const QString &dir, const QStringList &files)
{
    dir_ = dir;
    clear();

    // Placeholder icon so every item has the final size from the start and
    // the strip does not reflow while thumbnails trickle in.
    QPixmap blank(kThumbSide, kThumbSide);
    blank.fill(QColor(64, 64, 64));
    const QIcon placeholder(blank);

    for (const QString &name : files) {
        QListWidgetItem *item = new QListWidgetItem(placeholder, QString(), this);
        item->setToolTip(name);
        item->setSizeHint(QSize(kThumbSide + 8, kThumbSide + 8));
    }

    nextLoad_ = 0;
    if (count() > 0)
        loadTimer_->start();
    else
        loadTimer_->stop();
}

void ThumbnailBar::setCurrent(int index)
{
    if (index < 0 || index >= count()) {
        clearSelection();
        return;
    }
    setCurrentRow(index);
    scrollToItem(item(index), QAbstractItemView::EnsureVisible);
}

void ThumbnailBar::loadNextThumbnail()
{
    if (nextLoad_ >= count()) {
        loadTimer_->stop();
        return;
    }

    QListWidgetItem *it = item(nextLoad_);
    ++nextLoad_;

    // Decode straight to thumbnail size; for JPEG the reader downscales
    // during decode, which is what keeps big photo folders affordable.
    QImageReader reader(dir_ + "/" + it->toolTip());
    reader.setAutoTransform(true);
    QSize sz = reader.size();
    if (sz.isValid())
        reader.setScaledSize(sz.scaled(kThumbSide, kThumbSide, Qt::KeepAspectRatio));
    const QImage img = reader.read();
    if (img.isNull())
        return;  // leave the placeholder; the viewer will show the error

    // Centre inside the square cell so portrait and landscape line up.
    QPixmap cell(kThumbSide, kThumbSide);
    cell.fill(Qt::transparent);
    QPainter p(&cell);
    p.drawImage(QPoint((kThumbSide - img.width()) / 2,
                       (kThumbSide - img.height()) / 2), img);
    p.end();
    it->setIcon(QIcon(cell));
}
