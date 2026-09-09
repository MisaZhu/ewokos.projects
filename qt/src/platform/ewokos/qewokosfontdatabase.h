#ifndef QEWOKOSFONTDATABASE_H
#define QEWOKOSFONTDATABASE_H

#include <QtFontDatabaseSupport/private/qfreetypefontdatabase_p.h>

#include <QtCore/qhash.h>
#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>

QT_BEGIN_NAMESPACE

/*
 * QFreeTypeFontDatabase with EwokOS's font directory.
 *
 * The base class does the whole job already - it renders through the FreeType
 * engine that this build was configured against (-system-freetype) and
 * addTTFile() registers a font file by path.  What it cannot do is find them:
 * its populateFontDatabase() scans a single compiled-in directory (QT_QPA_FONTDIR
 * or the mkspec default), and neither is where EwokOS keeps fonts.  Everything
 * in this tree reads them from /usr/system/fonts - that is X_SYSTEM_PATH "/fonts"
 * in libx's own header - so that is what is scanned here.
 *
 * defaultFont() is the second half of the same story.  This plugin provides no
 * platform theme, so QGuiApplication takes its default font from the platform
 * font database (initFontUnlocked() in qguiapplication.cpp), and the base
 * answers "Helvetica" - which does not exist here and degrades to whichever
 * family the matcher happens to hit first.  xwin's theme already picks the UI
 * font; this override hands Qt the same one.
 */
class EwokosFontDatabase : public QFreeTypeFontDatabase
{
public:
    void populateFontDatabase() override;
    QFont defaultFont() const override;

private:
    void resolveThemeFont(const QHash<QString, QStringList> &scanned);

    /* The family Qt registered for the xwin theme's font file, and the theme's
       pixel size.  The family stays empty when the theme font is missing or
       unreadable, which is what sends defaultFont() down the fallback path. */
    QString m_themeFamily;
    int m_themePixelSize = 0;
    bool m_populated = false;
};

QT_END_NAMESPACE

#endif // QEWOKOSFONTDATABASE_H
