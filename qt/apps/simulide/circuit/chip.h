/*
 * SimulIDE, ported to EwokOS - the base of every IC-shaped component.
 *
 * Upstream: src/gui/circuitwidget/chip.h.  A Chip is a Component whose pinout is
 * not written in C++ but read out of a package description, which is what lets
 * one class serve a DIP8 logic gate and a DIP40 microcontroller: the body size,
 * the pin count, the labels, which pins are inverted and which the device does
 * not use all come from data.  The AVR in avr/ and every gate in components/
 * derive from this and only supply behaviour.
 *
 * Two deviations.
 *
 * The package file is streamed, not parsed into a document.  Upstream's
 * Chip::initChip() hands the file to QDomDocument, and QDomDocument is in
 * libQt5Xml - which this Qt build does not have (the headers are installed, so
 * the mistake compiles and fails at link).  QXmlStreamReader is in QtCore and
 * reads the same file; the format is shallow enough - <package width= height=
 * pins=> with <pin> children - that streaming costs nothing.
 *
 * There is a built-in DIP fallback.  Upstream ships its package files in
 * data/packages/ and a Chip without them is a box with no pins.  This port has
 * to run before those files are installed, and a pinless AVR is not merely
 * cosmetic - the simulator has no terminals to attach ports to.  So a package
 * name that is not found on disk but does end in a pin count ("dip28", "sdip18")
 * gets a standard DIP laid out in code, with the numbering a datasheet uses: 1
 * at the top left, down the left side, then up the right.  The subclass still
 * renames the pins afterwards, so what the fallback loses is only the per-pin
 * types the file would have carried.
 */

#ifndef SIMU_CHIP_H
#define SIMU_CHIP_H

#include "component.h"

class Chip : public Component
{
    Q_OBJECT

    // Upstream's spellings, underscores and all: these are attribute names in a
    // .simu file, so they are not free to be tidied up.
    //
    // The ORDER is load-bearing.  Circuit's reflection walks the properties in
    // declaration order in both directions, so Package is applied before
    // Logic_Symbol on load - setPackage() picks the plain path and reloads, then
    // setLogicSymbol() swaps to the _LS variant and reloads again.  The other way
    // round the LS choice would be overwritten by the plain path, because
    // setPackage() rebuilds m_pkgeFile from the package name and does not know a
    // logic symbol was asked for.
    Q_PROPERTY( QString Package     READ package     WRITE setPackage     USER true )
    Q_PROPERTY( bool    Logic_Symbol READ logicSymbol WRITE setLogicSymbol USER true )

    public:
        Chip( QObject *parent, const QString &type, const QString &id );
        ~Chip();

        // ---- the package ---------------------------------------------------

        // Loads <name>.package out of the data directory and rebuilds every pin
        // from it.  Rebuilding throws away the wires attached to the old pins,
        // which is upstream's behaviour and the reason switching a chip between
        // its IC and logic symbols is a destructive edit.
        void setPackage( const QString &name );
        QString package() const { return m_package; }

        // The file that was actually read, or would be: "<data>/packages/<name>.package".
        QString packageFile() const { return m_pkgeFile; }

        // Whether the last initChip() succeeded.  A subclass checks this before
        // wiring its pins to anything, because a missing package file leaves it
        // with the fallback pinout rather than the one it expected.
        bool initOk() const { return m_error == 0; }
        int error() const { return m_error; }

        int numChipPins() const { return m_numpins; }
        int chipWidth() const { return m_width; }
        int chipHeight() const { return m_height; }

        // ---- the logic symbol ----------------------------------------------
        // SimulIDE ships two drawings of the same device: the IC, a dark body
        // with the datasheet's pin names, and the logic symbol, a white body with
        // the gate's function drawn on it and bubbles on the inverted pins.  They
        // are two package files, <name>.package and <name>_LS.package, and
        // switching is a reload.

        bool logicSymbol() const { return m_isLS; }
        virtual void setLogicSymbol( bool ls );

        // ---- pins ------------------------------------------------------------

        // Renames a pin's label without touching its id.  The AVR uses it to put
        // the port bit names on a generic DIP pinout; setPinName() would change
        // the id a .simu names the pin by, so it is not offered here.
        void setPinLabel( int num, const QString &label );

        Pin *chipPin( int num ) const;

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    protected:
        // Reads m_pkgeFile and builds the pins.  Virtual because the AVR has to
        // follow it with its own per-device pinout edits, and a gate has to attach
        // its inputs to the pins the file just made.
        virtual bool initChip();

        // One <pin> out of the package file, at the position it comes in.  The
        // index is the document order, which is what upstream's `pos-1` is and
        // what a subclass counts on when it says "pin 14 is RESET".
        virtual void addChipPin( const QString &id, const QString &type,
                                 const QString &label, int xpos, int ypos, int angle );

        // The DIP fallback described in the header.  `pins` is the count the
        // package name ended with.
        void buildDipFallback( int pins );

        // The body colour, so a subclass can darken an unprogrammed MCU or tint
        // a chip that is running.
        QColor m_icColor;
        QColor m_lsColor;

        int m_numpins;
        int m_width;
        int m_height;

        int m_error;

        bool m_isLS;

        QString m_package;
        QString m_pkgeFile;
};

#endif // SIMU_CHIP_H
