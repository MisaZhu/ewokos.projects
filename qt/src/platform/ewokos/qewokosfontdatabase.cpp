#include "qewokosfontdatabase.h"

#include <QtCore/qdebug.h>
#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>

QT_BEGIN_NAMESPACE

/* X_SYSTEM_PATH "/fonts", spelled out rather than concatenated so that the path
   this scans can be read off without opening libx's header. */
static const char EWOK_FONT_DIR[] = "/usr/system/fonts";

void EwokosFontDatabase::populateFontDatabase()
{
    /* QT_QPA_FONTDIR is the variable Qt itself uses to relocate the font
       directory on an embedded target, so it keeps that meaning here: colon
       separated, and when it is set it replaces the default rather than adding
       to it.  A deployment that ships its own fonts next to the app can point
       at them without this file changing. */
    QStringList dirs;
    const QByteArray env = qgetenv("QT_QPA_FONTDIR");
    if (!env.isEmpty())
        dirs = QString::fromLocal8Bit(env).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (dirs.isEmpty())
        dirs << QLatin1String(EWOK_FONT_DIR);

    /* The extensions QFreeTypeFontDatabase::populateFontDatabase() accepts.
       Listed here rather than inherited because the base scans its own directory
       and cannot be pointed at another one. */
    static const QStringList filters = QStringList()
            << QLatin1String("*.ttf")
            << QLatin1String("*.ttc")
            << QLatin1String("*.otf")
            << QLatin1String("*.pfa")
            << QLatin1String("*.pfb");

    int found = 0;
    for (int i = 0; i < dirs.size(); ++i) {
        const QDir dir(dirs.at(i));
        if (!dir.exists()) {
            qWarning("ewokos: font directory %s does not exist", qPrintable(dirs.at(i)));
            continue;
        }
        const QList<QFileInfo> files = dir.entryInfoList(filters, QDir::Files);
        for (int j = 0; j < files.size(); ++j) {
            /* Empty fontData plus a path is the "load it from disk" form; that is
               exactly how the base class registers the files it finds. */
            addTTFile(QByteArray(), QFile::encodeName(files.at(j).absoluteFilePath()));
            ++found;
        }
    }

    /* No font at all means every QTextLayout falls back to an empty font engine
       and the application draws nothing but backgrounds - which looks like a
       rendering bug and is not one.  Say so once, at the point where it is
       still obvious which directory was empty. */
    if (found == 0)
        qWarning("ewokos: no fonts found in %s; text will not render. "
                 "Set QT_QPA_FONTDIR to a directory holding .ttf files.",
                 qPrintable(dirs.join(QLatin1Char(':'))));
}

QT_END_NAMESPACE
