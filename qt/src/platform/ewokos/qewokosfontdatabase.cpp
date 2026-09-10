#include "qewokosfontdatabase.h"

#include <QtCore/qdebug.h>
#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>

#include <x/x.h>
#include <string.h>

QT_BEGIN_NAMESPACE

/* X_SYSTEM_PATH "/fonts", spelled out rather than concatenated so that the path
   this scans can be read off without opening libx's header. */
static const char EWOK_FONT_DIR[] = "/usr/system/fonts";

/* DEFAULT_SYSTEM_FONT from font/font.h, spelled out for the same reason as
   EWOK_FONT_DIR.  libfont's font_new(name, true) falls back to it when the
   theme names a font that is not installed; resolveThemeFont() does the same. */
static const char EWOK_DEFAULT_FONT[] = "system";

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
    QHash<QString, QStringList> scanned;
    for (int i = 0; i < dirs.size(); ++i) {
        const QDir dir(dirs.at(i));
        if (!dir.exists()) {
            qWarning("ewokos: font directory %s does not exist", qPrintable(dirs.at(i)));
            continue;
        }
        const QList<QFileInfo> files = dir.entryInfoList(filters, QDir::Files);
        for (int j = 0; j < files.size(); ++j) {
            /* Empty fontData plus a path is the "load it from disk" form; that is
               exactly how the base class registers the files it finds.  The
               families come back too, which saves resolveThemeFont() a second
               FreeType parse of the file the theme points at. */
            const QString path = files.at(j).absoluteFilePath();
            const QStringList families = addTTFile(QByteArray(), QFile::encodeName(path));
            if (!families.isEmpty()) {
                scanned.insert(path, families);
                for (int k = 0; k < families.size(); ++k)
                    m_registeredFamilies << families.at(k).toLower();
            }
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

    resolveThemeFont(scanned);
    m_populated = true;
}

/* The xwin theme names the UI font ("font" in theme.json) and its size
   ("font_size"); libfont resolves the name to /usr/system/fonts/<name>.ttf
   (font_file.c) and falls back to DEFAULT_SYSTEM_FONT when that file is not
   there.  Qt registers a font under the family from the TTF name table rather
   than the filename, so the theme name has to be resolved through the file:
   find it in what the scan registered - or register it - and take the family
   FreeType reported. */
void EwokosFontDatabase::resolveThemeFont(const QHash<QString, QStringList> &scanned)
{
    x_theme_t theme;
    if (x_get_theme(&theme) != 0 || theme.fontName[0] == 0)
        return;

    for (int attempt = 0; attempt < 2; ++attempt) {
        const char *name = attempt == 0 ? theme.fontName : EWOK_DEFAULT_FONT;
        if (attempt == 1 && strcmp(theme.fontName, EWOK_DEFAULT_FONT) == 0)
            break;
        const QString path = QLatin1String(EWOK_FONT_DIR) + QLatin1Char('/')
                + QLatin1String(name) + QLatin1String(".ttf");
        QStringList families = scanned.value(path);
        /* QT_QPA_FONTDIR can move the scan away from the system fonts, but the
           theme still means the system file.  qt_registerFont() replaces a
           handle registered twice, so registering a file the scan already saw
           is harmless. */
        if (families.isEmpty() && QFileInfo::exists(path))
            families = addTTFile(QByteArray(), QFile::encodeName(path));
        if (!families.isEmpty()) {
            m_themeFamily = families.first();
            m_themePixelSize = int(theme.fontSize);
            if (!m_registeredFamilies.contains(m_themeFamily.toLower()))
                m_registeredFamilies << m_themeFamily.toLower();
            return;
        }
    }
}

/* No platform theme here, so QGuiApplication's default font comes from this
   (initFontUnlocked() in qguiapplication.cpp).  It is asked before
   QFontDatabase's lazy initializeDb() has ever run, and initializeDb() skips
   population once any family exists - so the scan cannot be left to it and is
   forced from here instead. */
QFont EwokosFontDatabase::defaultFont() const
{
    if (!m_populated)
        const_cast<EwokosFontDatabase *>(this)->populateFontDatabase();
    if (m_themeFamily.isEmpty())
        return QPlatformFontDatabase::defaultFont();

    QFont font(m_themeFamily);
    /* Theme sizes are pixels - libfont hands them to FT_Set_Pixel_Sizes()
       unchanged - so pixelSize, not pointSize. */
    if (m_themePixelSize > 0)
        font.setPixelSize(m_themePixelSize);
    return font;
}

/* There is no fontconfig here, so nothing but this database stands between a
   family name and a face.  A name the scan registered matches before fallbacks
   are ever consulted - an application that asks for an installed font keeps
   getting it.  A name that is not installed ("Monospace", "Sans", a family
   from another platform) reaches the fallback list, and the base builds that
   list as every registered family in registration order: the request would be
   answered by whichever file sorts first, Arial, no matter what was asked for.
   The xwin theme font is what the rest of the system draws with, so an
   unresolvable request leads with it instead and only then degrades to the
   rest of the list. */
QStringList EwokosFontDatabase::fallbacksForFamily(const QString &family, QFont::Style style,
                                                   QFont::StyleHint styleHint, QChar::Script script) const
{
    QStringList fallbacks = QFreeTypeFontDatabase::fallbacksForFamily(family, style, styleHint, script);
    if (!m_themeFamily.isEmpty() && !m_registeredFamilies.contains(family.toLower())) {
        fallbacks.removeAll(m_themeFamily);
        fallbacks.prepend(m_themeFamily);
    }
    return fallbacks;
}

QT_END_NAMESPACE
