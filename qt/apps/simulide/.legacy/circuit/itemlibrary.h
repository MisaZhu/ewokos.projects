/*
 * SimulIDE, ported to EwokOS - the item library.
 *
 * Upstream: src/gui/circuitwidget/itemlibrary.h.  A table from a type name to the
 * function that builds one, plus the category the component selector files it
 * under and the blurb the property panel shows.  Every component registers itself
 * here from a single function in its own translation unit rather than by static
 * initialiser, because this tree is built -fno-use-cxa-atexit and the C++ runtime
 * here does not run .init_array for a statically linked Qt - a global
 * self-registering object would simply never register.
 *
 * One addition upstream does not have: aliases.  Upstream's own spelling of a type
 * has drifted between releases, and the examples that ship with it carry both.  A
 * rail is "Rail" in some files and "Fixed Voltage" in others; earth is "Ground"
 * and "Ground (0 V)".  Rather than pick one and fail to load the other half of the
 * example folder, the library maps the older names onto the current one.  The
 * mapping is only used on the way in: a component always reports its own
 * canonical type, so what gets written back is the current spelling.
 */

#ifndef SIMU_ITEMLIBRARY_H
#define SIMU_ITEMLIBRARY_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "component.h"

// One row of the table.  Deliberately a plain struct rather than a QObject: it
// holds no state that could change, and keeping it out of the meta-object system
// means no moc pass for this file.
struct LibraryItem
{
    QString type;
    QString category;
    QString help;
    createItemPtr createFn;
};

class ItemLibrary
{
    public:
        // Built once, on first use, and never deleted: it outlives every sheet.
        static ItemLibrary* self();

        // Registers a type.  Re-registering a name replaces the entry, so a
        // component can be overridden without editing the library.
        void addItem( const QString &type, const QString &category,
                      const QString &help, createItemPtr fn );

        // `from` is accepted wherever `to` is expected, on reading only.
        void addAlias( const QString &from, const QString &to );

        // Resolves aliases, so a caller never has to know about them.
        QString resolve( const QString &type ) const;

        LibraryItem* libraryItem( const QString &type );
        createItemPtr createFn( const QString &type );

        const QList<LibraryItem*> &items() const { return m_items; }

        // The categories that have at least one item, in the order the items were
        // registered, which is the order the selector shows them in.
        QStringList categories() const;
        QList<LibraryItem*> itemsIn( const QString &category ) const;

    private:
        ItemLibrary();

        QList<LibraryItem*> m_items;
        QHash<QString,LibraryItem*> m_map;
        QHash<QString,QString> m_alias;
};

#endif // SIMU_ITEMLIBRARY_H
