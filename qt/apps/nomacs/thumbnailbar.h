#ifndef NOMACS_THUMBNAILBAR_H
#define NOMACS_THUMBNAILBAR_H

/*
 * The preview strip along the bottom - nomacs' DkFilePreview on a
 * QListWidget in horizontal icon mode.  Upstream renders thumbnails on
 * worker threads; here they are decoded lazily on a zero-interval timer,
 * one per tick, so a large folder never blocks the event loop for more
 * than one image.
 */

#include <QListWidget>
#include <QStringList>

class QTimer;

class ThumbnailBar : public QListWidget {
    Q_OBJECT
public:
    explicit ThumbnailBar(QWidget *parent = nullptr);

    // Rebuild for a directory listing (names relative to dir).
    void setFiles(const QString &dir, const QStringList &files);
    void setCurrent(int index);

signals:
    void thumbnailClicked(int index);

private:
    void loadNextThumbnail();

    QString dir_;
    QTimer *loadTimer_;
    int nextLoad_ = 0;      // first item that still has no thumbnail
};

#endif // NOMACS_THUMBNAILBAR_H
