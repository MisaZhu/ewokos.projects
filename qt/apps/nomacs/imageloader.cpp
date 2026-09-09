/*
 * ImageLoader - see imageloader.h for the mapping to upstream's
 * DkImageLoader.
 */

#include "imageloader.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QTransform>

QStringList ImageLoader::nameFilters()
{
    // The formats compiled into this Qt tree (qtgui-config.h): png, jpeg,
    // bmp, ppm/pgm/pbm, xbm, xpm.  No gif/tiff/webp plugins exist here.
    static const QStringList filters = {
        "*.png", "*.jpg", "*.jpeg", "*.bmp",
        "*.ppm", "*.pgm", "*.pbm", "*.xbm", "*.xpm",
    };
    return filters;
}

bool ImageLoader::isDisplayable(const QString &path)
{
    const QString ext = "*." + QFileInfo(path).suffix().toLower();
    return nameFilters().contains(ext);
}

ImageLoader::ImageLoader(QObject *parent)
    : QObject(parent)
{
}

void ImageLoader::scanDir(const QString &dir)
{
    dir_ = dir;
    files_ = QDir(dir).entryList(nameFilters(),
                                 QDir::Files | QDir::Readable, QDir::Name);
    emit folderChanged();
}

bool ImageLoader::load(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.isDir()) {
        scanDir(fi.absoluteFilePath());
        index_ = files_.isEmpty() ? -1 : 0;
    } else {
        scanDir(fi.absolutePath());
        index_ = files_.indexOf(fi.fileName());
        if (index_ < 0 && !files_.isEmpty())
            index_ = 0;
    }
    return loadCurrent();
}

void ImageLoader::reload()
{
    const QString keep = (index_ >= 0 && index_ < files_.size())
                             ? files_.at(index_) : QString();
    scanDir(dir_);
    index_ = keep.isEmpty() ? -1 : files_.indexOf(keep);
    if (index_ < 0 && !files_.isEmpty())
        index_ = 0;
    loadCurrent();
}

bool ImageLoader::loadCurrent()
{
    if (index_ < 0 || index_ >= files_.size()) {
        image_ = QImage();
        fileSize_ = 0;
        emit imageChanged();
        return false;
    }
    const QString path = filePath();
    QImageReader reader(path);
    reader.setAutoTransform(true);  // honour the JPEG orientation tag
    image_ = reader.read();
    fileSize_ = QFileInfo(path).size();
    emit imageChanged();
    return !image_.isNull();
}

QString ImageLoader::filePath() const
{
    if (index_ < 0 || index_ >= files_.size())
        return QString();
    return dir_ + "/" + files_.at(index_);
}

// ---- navigation ---------------------------------------------------------

bool ImageLoader::next()
{
    if (files_.isEmpty())
        return false;
    index_ = (index_ + 1) % files_.size();
    return loadCurrent();
}

bool ImageLoader::previous()
{
    if (files_.isEmpty())
        return false;
    index_ = (index_ + files_.size() - 1) % files_.size();
    return loadCurrent();
}

bool ImageLoader::first()
{
    if (files_.isEmpty())
        return false;
    index_ = 0;
    return loadCurrent();
}

bool ImageLoader::last()
{
    if (files_.isEmpty())
        return false;
    index_ = files_.size() - 1;
    return loadCurrent();
}

bool ImageLoader::jumpTo(int index)
{
    if (index < 0 || index >= files_.size())
        return false;
    index_ = index;
    return loadCurrent();
}

// ---- edits ----------------------------------------------------------------

void ImageLoader::rotate(int degrees)
{
    if (image_.isNull())
        return;
    QTransform t;
    t.rotate(degrees);
    image_ = image_.transformed(t, Qt::SmoothTransformation);
    emit imageChanged();
}

void ImageLoader::flip(bool horizontal)
{
    if (image_.isNull())
        return;
    image_ = image_.mirrored(horizontal, !horizontal);
    emit imageChanged();
}

bool ImageLoader::save(const QString &path)
{
    if (image_.isNull())
        return false;
    QImageWriter writer(path);
    if (!writer.write(image_))
        return false;
    // Saving into the browsed directory (the usual case) changes its
    // listing; rescan so the strip and the counter stay truthful.
    if (QFileInfo(path).absolutePath() == dir_) {
        const QString cur = files_.value(index_);
        scanDir(dir_);
        index_ = files_.indexOf(QFileInfo(path).fileName());
        if (index_ < 0)
            index_ = qMax(0, files_.indexOf(cur));
        fileSize_ = QFileInfo(path).size();
        emit imageChanged();
    }
    return true;
}
