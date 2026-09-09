#ifndef QEWOKOSFONTDATABASE_H
#define QEWOKOSFONTDATABASE_H

#include <QtFontDatabaseSupport/private/qfreetypefontdatabase_p.h>

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
 */
class EwokosFontDatabase : public QFreeTypeFontDatabase
{
public:
    void populateFontDatabase() override;
};

QT_END_NAMESPACE

#endif // QEWOKOSFONTDATABASE_H
