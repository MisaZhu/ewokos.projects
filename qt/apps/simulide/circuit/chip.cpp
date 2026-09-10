/*
 * SimulIDE, ported to EwokOS - see chip.h.
 */

#include <QFile>
#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include <QXmlStreamReader>

#include "chip.h"

#include "circuit.h"
#include "pin.h"

// One side of the body is 8 units per pin, which is the lattice step and also
// the length of a pin's lead, so neighbouring pins' leads cannot overlap.
#define CHIP_CELL 8

// Where a package file lives, and how the logic-symbol variant is named.  Both
// are upstream's: data/packages/<name>.package and <name>_LS.package.
static const char kPkgDir[]     = "/packages/";
static const char kPkgExt[]     = ".package";
static const char kPkgLsExt[]   = "_LS.package";

// Trailing digits of a package name, which is the pin count for the families
// SimulIDE names that way: dip8, dip14, dip28, dip40, sdip18, qfp32.  Zero when
// the name does not end in a number, which is what tells initChip() that no
// fallback is possible and the chip genuinely has no pinout.
static int trailingDigits( const QString &name )
{
    int i = name.size();
    while( i > 0 && name.at( i-1 ).isDigit() ) --i;

    if( i == name.size() ) return 0;

    bool ok = false;
    const int n = name.mid( i ).toInt( &ok );

    return ok ? n : 0;
}

Chip::Chip( QObject *parent, const QString &type, const QString &id )
     : Component( parent, type, id )
     , m_icColor( 50, 50, 70 )
     , m_lsColor( 255, 255, 255 )
     , m_numpins( 0 )
     , m_width( 0 )
     , m_height( 0 )
     , m_error( 0 )
     , m_isLS( false )
{
    // Upstream's colours for the two drawings.  m_color is what Component::paint
    // fills the body with, and initChip() picks between them.
    m_color = m_icColor;

    // The label goes above the body rather than at Component's default, which
    // assumes a symbol drawn about its origin: a chip's origin is its top-left
    // corner, so the default would put the name across the first row of pins.
    setLabelX( 0 );
    setLabelY( -20 );
    setLabelRot( 0 );
}

Chip::~Chip()
{
    // The pins are QObject children of the component and are destroyed with it;
    // Circuit::remComponent() has already taken them out of the sheet's pin map.
}

// ---- the package ---------------------------------------------------------------

void Chip::setPackage( const QString &name )
{
    m_package = name;

    m_pkgeFile = QLatin1String( SIMULIDE_DATA_DIR )
               + QLatin1String( kPkgDir ) + name + QLatin1String( kPkgExt );

    initChip();
}

void Chip::setLogicSymbol( bool ls )
{
    if( m_isLS == ls ) return;
    if( m_package.isEmpty() ) return;

    // Upstream swaps the suffix on the stored path.  Rebuilding the path from the
    // package name says the same thing with no string surgery, and cannot get out
    // of step with whatever setPackage() decided the directory was.
    m_pkgeFile = QLatin1String( SIMULIDE_DATA_DIR ) + QLatin1String( kPkgDir )
               + m_package
               + ( ls ? QLatin1String( kPkgLsExt ) : QLatin1String( kPkgExt ) );

    if( initChip() )
        if( QGraphicsScene *sc = scene() ) sc->update();
}

// One <pin> out of the package file, held until the body size is known: a pin's
// x depends on the width, which is an attribute of the root rather than of the
// pin, so the whole file has to be read before anything can be placed.
struct PinDef
{
    QString id;
    QString type;
    QString label;
    QString side;
    int     pos;
};

bool Chip::initChip()
{
    m_error = 0;

    QList<PinDef> defs;

    int width = 0, height = 0, pins = 0;
    bool sawRoot = false;

    QFile file( m_pkgeFile );

    if( !m_pkgeFile.isEmpty() && file.open( QIODevice::ReadOnly | QIODevice::Text ) )
    {
        QXmlStreamReader xml( &file );

        while( !xml.atEnd() )
        {
            xml.readNext();

            if( !xml.isStartElement() ) continue;

            if( xml.name() == QLatin1String( "package" ) )
            {
                sawRoot = true;

                const QXmlStreamAttributes a = xml.attributes();
                width  = a.value( "width" ).toInt();
                height = a.value( "height" ).toInt();
                pins   = a.value( "pins" ).toInt();
            }
            else if( sawRoot && xml.name() == QLatin1String( "pin" ) )
            {
                const QXmlStreamAttributes a = xml.attributes();

                PinDef d;
                d.id    = a.value( "id" ).toString();
                d.type  = a.value( "type" ).toString();
                d.label = a.value( "label" ).toString();
                d.side  = a.value( "side" ).toString();
                d.pos   = a.value( "pos" ).toInt();

                defs.append( d );
            }
        }

        if( xml.hasError() ) m_error = 2;

        file.close();

        if( !sawRoot ) m_error = 2;
    }
    else
    {
        m_error = 1;
    }

    clearPins();

    // ---- fall back to a DIP laid out in code -------------------------------
    // Only when there was no usable file.  A subclass that has just been built
    // still has to have somewhere to put its ports, and a chip with no pins is
    // not a degraded drawing - it is a component the simulator cannot attach
    // anything to.
    if( m_error != 0 )
    {
        const int n = trailingDigits( m_package );

        if( n >= 2 )
        {
            buildDipFallback( n );
            m_error = 0;
        }
        return m_error == 0;
    }

    if( defs.isEmpty() )
    {
        m_error = 3;
        return false;
    }

    // The count the file declares and the count it actually carries can
    // disagree.  Upstream resizes to the declared one and leaves the rest null,
    // and that is the more useful of the two: a subclass walks the device's
    // datasheet pin numbers, so numChipPins() has to report the whole device and
    // Component::pin() hand back null for a position the truncated file omitted
    // rather than let the loop stop short.  A file that declares fewer than it
    // carries is malformed, so there the pins present win.
    m_numpins = qMax( pins, defs.size() );
    m_width   = width;
    m_height  = height;

    m_isLS = m_pkgeFile.endsWith( QLatin1String( kPkgLsExt ) );

    m_color = m_isLS ? m_lsColor : m_icColor;

    setArea( QRectF( 0, 0, CHIP_CELL*m_width, CHIP_CELL*m_height ) );

    // A chip is always named: on a busy sheet "U1" is how you say which one, and
    // the label is drawn above the body where it cannot cover a pin.
    setShowId( true );

    for( const PinDef &d : defs )
    {
        int xpos = 0, ypos = 0, angle = 0;

        if( d.side == QLatin1String( "left" ) )
        {
            xpos = -CHIP_CELL;
            ypos = CHIP_CELL*d.pos;
            angle = 180;
        }
        else if( d.side == QLatin1String( "top" ) )
        {
            xpos = CHIP_CELL*d.pos;
            ypos = -CHIP_CELL;
            angle = 90;
        }
        else if( d.side == QLatin1String( "right" ) )
        {
            xpos = CHIP_CELL*m_width + CHIP_CELL;
            ypos = CHIP_CELL*d.pos;
            angle = 0;
        }
        else if( d.side == QLatin1String( "bottom" ) )
        {
            xpos = CHIP_CELL*d.pos;
            ypos = CHIP_CELL*m_height + CHIP_CELL;
            angle = 270;
        }

        addChipPin( d.id, d.type, d.label, xpos, ypos, angle );
    }

    // A rebuild while the chip is on a sheet has to leave the sheet able to find
    // the new pins and with a node graph that matches them.  On first
    // construction there is no sheet yet and Circuit::addComponent() does both.
    if( Circuit *circ = circuit() )
    {
        for( Pin *pin : m_pin )
            if( pin ) circ->addPin( pin );

        circ->updateNodes();
    }

    return true;
}

void Chip::buildDipFallback( int pins )
{
    const int leftCount  = ( pins + 1 ) / 2;
    const int rightCount = pins - leftCount;

    m_numpins = pins;
    m_width   = 3;
    m_height  = qMax( leftCount, rightCount );

    m_isLS = m_pkgeFile.endsWith( QLatin1String( kPkgLsExt ) );

    m_color = m_isLS ? m_lsColor : m_icColor;

    setArea( QRectF( 0, 0, CHIP_CELL*m_width, CHIP_CELL*m_height ) );
    setShowId( true );

    // Pin 1 at the top left, down the left side, then up the right - the
    // numbering every DIP datasheet uses and the one a user counts pins by.
    for( int i = 1; i <= leftCount; i++ )
    {
        const QString num = QString::number( i );
        addChipPin( num, QString(), num, -CHIP_CELL, CHIP_CELL*(i-1), 180 );
    }

    for( int i = pins; i > leftCount; i-- )
    {
        const QString num = QString::number( i );
        addChipPin( num, QString(), num,
                    CHIP_CELL*m_width + CHIP_CELL, CHIP_CELL*(pins-i), 0 );
    }

    if( Circuit *circ = circuit() )
    {
        for( Pin *pin : m_pin )
            if( pin ) circ->addPin( pin );

        circ->updateNodes();
    }
}

void Chip::addChipPin( const QString &id, const QString &type,
                       const QString &label, int xpos, int ypos, int angle )
{
    // Component::addPin appends, so the index is the document order - upstream's
    // `pos-1`, and what a subclass means when it says pin 14.
    Pin *pin = addPin( xpos, ypos, id, angle );
    if( !pin ) return;

    pin->setLabelText( label );

    if( type == QLatin1String( "inverted" ) )
        pin->setInverted( true );
    else if( type == QLatin1String( "unused" ) )
        pin->setUnused( true );
    else if( type == QLatin1String( "null" ) )
    {
        // Declared so the body has the right number of positions, but there is
        // nothing behind it: no square, no label, and no wire can be attached.
        pin->setVisible( false );
        pin->setLabelText( QString() );
    }

    // Black on the white logic symbol, near-white on the dark IC body.
    pin->setLabelColor( m_isLS ? QColor( 0, 0, 0 ) : QColor( 250, 250, 200 ) );
}

// ---- pins -----------------------------------------------------------------------

void Chip::setPinLabel( int num, const QString &label )
{
    if( Pin *pin = chipPin( num ) ) pin->setLabelText( label );
}

Pin* Chip::chipPin( int num ) const
{
    return pin( num );
}

// ---- painting -------------------------------------------------------------------

void Chip::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                  QWidget *widget )
{
    Component::paint( painter, option, widget );

    painter->drawRoundedRect( m_area, 1, 1 );

    if( m_isLS ) return;

    // The notch at the top centre, which is what makes pin 1 findable on a
    // physical package and so on the drawing of one.  -2880 is -180 degrees in
    // the sixteenths of a degree drawArc() takes, so it is a half circle opening
    // downwards into the body.
    painter->setPen( QColor( 170, 170, 150 ) );
    painter->drawArc( int( m_area.width() )/2 - 6, -4, 8, 8, 0, -2880 );
}
