/*
 * SimulIDE, ported to EwokOS - see component.h.
 */

#include <QAction>
#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include <QTransform>

#include "component.h"

#include "circuit.h"
#include "connector.h"
#include "pin.h"

// Keeps a rotation in [0,360) so a component turned four times reads 0 again
// rather than 360, which is what the file would otherwise accumulate.  Written
// as a loop rather than with fmod() so this file needs no math header.
static qreal normAngle( qreal a )
{
    while( a <    0 ) a += 360.0;
    while( a >= 360 ) a -= 360.0;
    return a;
}

// Upstream's multUnits, verbatim.  Index 4 is the space - bare units - which
// is what setValue() renormalises into and what a component starts at.
static const char kMultUnits[] = "TGMk munp";
static const int  kMultCount   = 9;   // strlen of the above

// "µ" is U+00B5 in some of upstream's own example files and the Greek mu
// U+03BC in others, and neither is in the table, so both are folded onto the
// 'u' that is.  Without this a file reading Capacitance="100" Unit=" µF" loads
// as 100 farads.
static QChar foldMicro( QChar c )
{
    if( c == QChar( 0x00B5 ) || c == QChar( 0x03BC ) ) return QLatin1Char( 'u' );
    return c;
}

Component::Component( QObject *parent, const QString &type, const QString &id )
          : QObject( parent )
          , QGraphicsItem()
          , eElement( id )
          , m_value( 0 )
          , m_unit( " " )
          , m_mult( " " )
          , m_unitMult( 1 )
          , m_Hflip( 1 )
          , m_Vflip( 1 )
          , m_idLabel( id )
          , m_type( type )
          , m_color( Qt::white )
          , m_showId( false )
          , m_showVal( false )
          , m_labelx( 0 )
          , m_labely( -24 )
          , m_labelrot( 0 )
          , m_valLabelx( 0 )
          , m_valLabely( 20 )
          , m_valLabRot( 0 )
{
    setObjectName( id );

    setFlag( QGraphicsItem::ItemIsSelectable, true );
    setFlag( QGraphicsItem::ItemIsMovable, true );
    setFlag( QGraphicsItem::ItemSendsGeometryChanges, true );

    // Above the wires, below nothing: a symbol must cover the wire that runs
    // under it, or the sheet reads as spaghetti.
    setZValue( 2 );
}

Component::~Component()
{
    // Catches the components that are deleted without going through remove() -
    // a circuit being cleared, or a parent window closing.  remComponent() is
    // idempotent, so a component that was already removed costs one lookup.
    Circuit *circ = circuit();
    if( circ ) circ->remComponent( this );
}

QRectF Component::boundingRect() const
{
    // m_area is the contract: everything a component draws, including its own
    // text, has to fit inside it.  A component that draws outside gets clipped
    // rather than repainting its neighbours, which is the failure that shows up
    // first when someone forgets.
    return m_area.adjusted( -2, -2, 2, 2 );
}

void Component::paint( QPainter *painter, const QStyleOptionGraphicsItem*, QWidget* )
{
    // The selection marker goes first so that the pen it leaves behind is not the
    // one a subclass inherits.  Upstream washes a selected symbol dark grey
    // instead; this port frames it, because a wash hides the very symbol being
    // selected and a dashed box one unit outside m_area leaves it readable.
    if( isSelected() )
    {
        painter->setPen( QPen( QColor( 255, 170, 60 ), 1, Qt::DashLine ) );
        painter->setBrush( Qt::NoBrush );
        painter->drawRect( m_area.adjusted( -1, -1, 1, 1 ) );
    }

    // Upstream's base pen and brush, which a subclass inherits rather than
    // re-declares - Chip::paint draws its body with nothing but a call up here,
    // and so can anything else that is happy with a black outline over m_color.
    //
    // The brush matters as much as the pen.  The constructor puts symbols above
    // the wires, and a symbol with no brush does not cover the wire running under
    // it, so every crossing on the sheet reads as a junction.  m_color is white
    // by default for that reason, and a subclass that wants a body of its own
    // colour sets it, the way Chip and Node do.
    painter->setPen( QPen( QColor( 0, 0, 0 ), 1.5, Qt::SolidLine,
                           Qt::RoundCap, Qt::RoundJoin ) );
    painter->setBrush( m_color );
}

// ---- names ------------------------------------------------------------------

void Component::setId( const QString &id )
{
    if( m_elmId == id ) return;

    eElement::setId( id );
    setObjectName( id );

    // A pin's id is this name with the pin's own appended, and a Connector in
    // the file refers to pins by that id, so the two have to move together.
    for( Pin *pin : m_pin )
        if( pin ) pin->updatePinId();
}

void Component::setIdLabel( const QString &id )
{
    m_idLabel = id;

    // Labels are drawn by the scene, not by this item, so redrawing this item
    // would not move a pixel.
    if( QGraphicsScene *sc = scene() ) sc->update();
}

void Component::setShowId( bool show )
{
    if( m_showId == show ) return;

    m_showId = show;
    if( QGraphicsScene *sc = scene() ) sc->update();
}

void Component::setShowVal( bool show )
{
    if( m_showVal == show ) return;

    m_showVal = show;
    if( QGraphicsScene *sc = scene() ) sc->update();
}

QString Component::idLabelText() const
{
    return m_idLabel;
}

QString Component::valLabelText() const
{
    // A space is the default base unit, meaning "this component has none", and
    // such a component draws no value label.
    if( m_unit.trimmed().isEmpty() ) return QString();

    // Six significant digits, which is QString::number's default and turns the
    // 4.7000000000000002 a renormalisation can leave behind back into 4.7.
    return QString::number( m_value, 'g', 6 ) + m_mult + m_unit;
}

// ---- units and value ---------------------------------------------------------

void Component::setUnit( const QString &un )
{
    // Only the prefix is taken from the argument; the base unit belongs to the
    // component and is set once, in its constructor.  This is what makes a
    // " kΩ" in the file read as kilo-ohms and not as kilo-whatever.
    QString u = un;
    u.remove( QLatin1Char( ' ' ) );

    if( !u.isEmpty() )
    {
        const QChar mul = foldMicro( u.at( 0 ) );

        double unitMult = 1e12;   // the table starts at Tera

        for( int i = 0; i < kMultCount; i++ )
        {
            if( mul == QLatin1Char( kMultUnits[i] ) )
            {
                m_unitMult = unitMult;
                m_mult     = QString( mul );
                if( m_mult != " " ) m_mult.prepend( " " );
                return;
            }
            unitMult /= 1000;
        }
    }

    // An unrecognised prefix is bare units rather than an error: upstream's
    // own files carry "Ω" with no prefix, and rejecting them would lose the
    // value that comes with them.
    m_unitMult = 1;
    m_mult     = " ";
}

void Component::setValue( double val )
{
    if( qAbs( val ) < 1e-12 )
    {
        m_value = 0;
        m_mult  = " ";
        return;
    }

    // `val` arrives expressed in the current unit, so it goes to base units
    // first and then the prefix is picked to put the displayed number back in
    // [1,1000).  Both loops are bounded: upstream runs them until the value
    // fits, which walks off the end of the table on a 1e-300 or a 1e300.
    val *= m_unitMult;

    int index = 4;      // bare units
    m_unitMult = 1;

    while( qAbs( val ) >= 1000 && index > 0 )
    {
        index--;
        m_unitMult *= 1000;
        val /= 1000;
    }
    while( qAbs( val ) < 1 && index < kMultCount-1 )
    {
        index++;
        m_unitMult /= 1000;
        val *= 1000;
    }

    m_mult = QString( QLatin1Char( kMultUnits[index] ) );
    if( m_mult != " " ) m_mult.prepend( " " );

    m_value = val;
}

// ---- geometry ----------------------------------------------------------------

void Component::setHflip( int hf )
{
    hf = ( hf < 0 ) ? -1 : 1;
    if( hf == m_Hflip ) return;

    m_Hflip = hf;
    setflip();
}

void Component::setVflip( int vf )
{
    vf = ( vf < 0 ) ? -1 : 1;
    if( vf == m_Vflip ) return;

    m_Vflip = vf;
    setflip();
}

void Component::setflip()
{
    // QGraphicsItem keeps position, rotation and scale as separate cached values
    // and composes them itself, so mirroring is a negative scale and the rotation
    // survives untouched.  The pins are child items and come with it, which is the
    // whole reason for doing it this way rather than mirroring the drawing.
    //
    // Through setTransform() and not setScale(): Qt 5's scale is a single qreal,
    // so it cannot mirror one axis.  That is safe here because the item's
    // transform is the FIRST thing computedFullTransform() applies - it takes it
    // as the base and post-multiplies translate(origin), rotate(rotation),
    // scale(scale) onto it - so the mirror happens in the symbol's own
    // coordinates and the rotation still turns the mirrored symbol.  The
    // consequence to remember is that a subclass which wants a transform of its
    // own has to compose the flip into it rather than call setTransform().
    QGraphicsItem::setTransform( QTransform::fromScale( m_Hflip, m_Vflip ) );

    if( QGraphicsScene *sc = scene() ) sc->update();
}

void Component::setGridX( int x ) { setPos( x, pos().y() ); }
void Component::setGridY( int y ) { setPos( pos().x(), y ); }

void Component::rotateCW()
{
    QGraphicsItem::setRotation( normAngle( rotation()+90 ) );
    emit moved();
}

void Component::rotateCCW()
{
    QGraphicsItem::setRotation( normAngle( rotation()-90 ) );
    emit moved();
}

void Component::rotateHalf()
{
    QGraphicsItem::setRotation( normAngle( rotation()+180 ) );
    emit moved();
}

void Component::H_flip() { setHflip( -m_Hflip ); emit moved(); }
void Component::V_flip() { setVflip( -m_Vflip ); emit moved(); }

QVariant Component::itemChange( GraphicsItemChange change, const QVariant &value )
{
    if( change == ItemPositionChange )
    {
        Circuit *circ = circuit();

        // Only while the user is dragging, and never while a file is being read:
        // Circuit::snapping() already folds m_loading in, so a position coming
        // out of a .simu is taken as written.  Upstream's own examples are not
        // all on this port's lattice, and re-snapping them on load would move
        // every symbol by a few pixels for no reason.
        if( circ && circ->snapping() )
        {
            QPointF p = value.toPointF();
            p.setX( qRound( p.x()/(qreal)GRID )*GRID );
            p.setY( qRound( p.y()/(qreal)GRID )*GRID );
            return p;
        }
        return value;
    }
    if( change == ItemPositionHasChanged )
    {
        emit moved();
        return value;
    }
    return QGraphicsItem::itemChange( change, value );
}

Circuit *Component::circuit() const
{
    return qobject_cast<Circuit*>( scene() );
}

// ---- pins --------------------------------------------------------------------

Pin *Component::addPin( int x, int y, const QString &name, int angle, int length )
{
    const int index = m_pin.size();

    Pin *pin = new Pin( x, y, name, angle, this, index );
    pin->setLength( length );

    m_pin.append( pin );

    // The eElement half: what the matrix sees.  setNumPins() keeps the two
    // lists the same length, which is the invariant stamp() relies on.
    setNumPins( m_pin.size() );
    m_epin[index] = pin;

    return pin;
}

void Component::setNumPins( int n )
{
    m_pin.resize( n );
    eElement::setNumEpins( n );
}

void Component::initPins()
{
    // Child items, so they follow the symbol through rotation and flip.  Not
    // selectable or movable in their own right: a drag that starts on a
    // terminal moves the symbol, and a wire is started by CircuitView asking
    // the scene what pin is under the cursor, not by the pin handling the
    // press itself.
    for( Pin *pin : m_pin )
    {
        if( !pin ) continue;

        pin->setParentItem( this );
        pin->setFlag( QGraphicsItem::ItemIsSelectable, false );
        pin->setFlag( QGraphicsItem::ItemIsMovable, false );
    }
}

Pin *Component::getPin( const QString &pinId ) const
{
    for( Pin *pin : m_pin )
        if( pin && pin->pinId() == pinId ) return pin;

    return 0l;
}

void Component::dropPins( int from )
{
    if( from < 0 ) from = 0;
    if( from >= m_pin.size() ) return;

    Circuit *circ = circuit();

    for( int i = from; i < m_pin.size(); i++ )
    {
        Pin *pin = m_pin[i];
        if( !pin ) continue;

        // The wire goes first.  Connector::remove() clears both of its pins'
        // back-pointers before scheduling its own deletion, so ~Pin then sees a
        // null m_con and does not reach into an object that is on its way out.
        if( Connector *con = pin->connector() ) con->remove();

        if( circ ) circ->removePin( pin->pinId() );

        if( QGraphicsScene *sc = pin->scene() ) sc->removeItem( pin );

        delete pin;
    }

    while( m_pin.size() > from ) m_pin.removeLast();
    setNumPins( m_pin.size() );

    // A join names its pins by index, and the indices just vacated are the ones
    // the caller is about to reuse.  updateNodes() skips a pair that points past
    // the end of the pin list, but it cannot tell a stale index from a live one,
    // so a pair left here would silently solder two of the new pins together -
    // and the symptom is a short that moves when the package changes.
    for( int i = m_joined.size()-1; i >= 0; i-- )
        if( m_joined[i].first >= from || m_joined[i].second >= from )
            m_joined.removeAt( i );
}

void Component::joinPins( int a, int b )
{
    if( a == b ) return;
    if( a < 0 || b < 0 || a >= m_pin.size() || b >= m_pin.size() ) return;

    m_joined.append( qMakePair( a, b ) );
}

void Component::pinConnected( Pin*, bool )
{
    // Most components have nothing to do here.  Node overrides it, and so do
    // the parts whose terminal count decides their behaviour.
}

// ---- editing -----------------------------------------------------------------

void Component::slotProperties()
{
    Circuit *circ = circuit();
    if( circ ) circ->showProperties( this );
}

void Component::slotRemove()
{
    remove();
}

void Component::remove()
{
    Circuit *circ = circuit();
    if( circ ) circ->remComponent( this );

    // Off the sheet now rather than at the next event-loop turn, so nothing can
    // find it in between; ~Component's own remComponent() call is then a no-op.
    if( QGraphicsScene *sc = scene() ) sc->removeItem( this );

    deleteLater();
}

// ---- mouse -------------------------------------------------------------------
//
// There are no mouse handlers here on purpose.  ItemIsMovable makes
// QGraphicsItem do the dragging, including dragging everything else that is
// selected, and itemChange() below is where the snapping and the notification
// happen - which is the one place that sees every position change rather than
// only the ones that started on this item.

void Component::contextMenuEvent( QGraphicsSceneContextMenuEvent *event )
{
    QMenu menu;

    contextMenu( event, &menu );

    if( !menu.isEmpty() ) menu.exec( event->screenPos() );

    event->accept();
}

// Upstream's menu, in upstream's order: the actions a user reaches for are at
// the top and the geometry ones are flat rather than nested, because the
// rotation submenu costs a click on every use.
void Component::contextMenu( QGraphicsSceneContextMenuEvent*, QMenu *menu )
{
    Circuit *circ = circuit();

    QAction *act = menu->addAction( tr( "Copy" ) + "\tCtrl+C" );
    act->setEnabled( circ != 0l );
    connect( act, SIGNAL( triggered() ), this, SLOT( slotCopy() ) );

    act = menu->addAction( tr( "Remove" ) + "\tDel" );
    connect( act, SIGNAL( triggered() ), this, SLOT( slotRemove() ) );

    act = menu->addAction( tr( "Properties" ) );
    act->setEnabled( circ != 0l );
    connect( act, SIGNAL( triggered() ), this, SLOT( slotProperties() ) );

    menu->addSeparator();

    act = menu->addAction( tr( "Rotate Clockwise" ) );
    connect( act, SIGNAL( triggered() ), this, SLOT( rotateCW() ) );

    act = menu->addAction( tr( "Rotate Counter Clockwise" ) );
    connect( act, SIGNAL( triggered() ), this, SLOT( rotateCCW() ) );

    act = menu->addAction( tr( "Rotate 180 Degrees" ) );
    connect( act, SIGNAL( triggered() ), this, SLOT( rotateHalf() ) );

    menu->addSeparator();

    act = menu->addAction( tr( "Horizontal Flip" ) );
    connect( act, SIGNAL( triggered() ), this, SLOT( H_flip() ) );

    act = menu->addAction( tr( "Vertical Flip" ) );
    connect( act, SIGNAL( triggered() ), this, SLOT( V_flip() ) );
}

// ---- copy ---------------------------------------------------------------------
//
// There is no write() or read() in this class.  Circuit serialises a component
// by reflecting over its meta-object, and copying is the same reflection with a
// new instance at the far end, so a component that adds a Q_PROPERTY is saved,
// loaded, copied and shown in the panel without anything else being touched.

void Component::slotCopy()
{
    Circuit *circ = circuit();
    if( circ ) circ->copyComponent( this );
}
