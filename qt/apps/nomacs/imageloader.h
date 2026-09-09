#ifndef NOMACS_IMAGELOADER_H
#define NOMACS_IMAGELOADER_H

/*
 * The folder-browsing core of nomacs (upstream's DkImageLoader): a sorted
 * list of the displayable files in one directory plus a current index, with
 * prev/next that wrap around like upstream's "loop images" default.  No
 * background threads - EwokOS Qt is single-threaded through the QPA plugin,
 * so images are decoded synchronously on demand.
 */

#include <QImage>
#include <QObject>
#include <QStringList>

class ImageLoader : public QObject {
    Q_OBJECT
public:
    explicit ImageLoader(QObject *parent = nullptr);

    // Extensions this Qt build decodes, as QDir name filters ("*.png"...).
    static QStringList nameFilters();
    static bool isDisplayable(const QString &path);

    // Point the loader at a file (its directory is scanned, the file becomes
    // current) or at a directory (the first image becomes current).
    bool load(const QString &path);
    void reload();          // rescan the directory, keep the current file

    bool next();            // wraps at either end
    bool previous();
    bool first();
    bool last();
    bool jumpTo(int index);

    const QImage &image() const { return image_; }
    QString filePath() const;
    QString dirPath() const { return dir_; }
    int currentIndex() const { return index_; }
    int count() const { return files_.size(); }
    const QStringList &files() const { return files_; }
    qint64 fileSize() const { return fileSize_; }

    // In-place edits; the caller saves explicitly (upstream keeps an edit
    // history, here the loaded QImage is simply replaced).
    void rotate(int degrees);
    void flip(bool horizontal);
    bool save(const QString &path);

signals:
    // Emitted whenever the current image changed - navigation, reload or
    // an edit.  The window rebuilds title/status/viewport from the loader.
    void imageChanged();
    // The directory list changed (new scan); the thumbnail bar rebuilds.
    void folderChanged();

private:
    void scanDir(const QString &dir);
    bool loadCurrent();

    QString dir_;           // directory being browsed
    QStringList files_;     // file names (not paths), sorted by name
    int index_ = -1;        // position in files_, -1 when empty
    QImage image_;          // the decoded current image
    qint64 fileSize_ = 0;   // on-disk size of the current file
};

#endif // NOMACS_IMAGELOADER_H
