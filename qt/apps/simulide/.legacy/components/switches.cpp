/*
 * SimulIDE, ported to EwokOS - see switches.h.
 */

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

#include "switches.h"

#include "circuit.h"
#include "pin.h"
#include "twopin.h"

#include "simulator.h"

// Rows are this far apart, which is two lattice cells: far enough that the pin
// squares of adjacent poles do not touch, close enough that an eight pole switch
// still fits in a window.
static const int kPoleSpacing = 16;

// Tells the Simulator the sheet changed, so the next step rebuilds the matrix
// rather than re-solving the factorisation it already has.  A switch does not
// change the topology - the nodes are the same either way - but eNode's
// classification of itself as single or not is only recomputed in initialize(),
// which createMatrix() calls, and createMatrix() runs only on a changed circuit.
// Skipping this leaves a contact that has just opened still driving a node that
// nothing drives any more.
static void circChanged()
{
    if( Simulator *sim = Simulator::self() ) sim->setCircChanged();
}

// ---- Switch ----------------------------------------------------------------------

Switch::Switch( QObject *parent, const QString &type, const QString &id )
      : Component( parent, type, id )
      , m_thrown()
      , m_poles( 1 )
      , m_dt( false )
      , m_normClose( false )
      , m_key()
{
    buildPins();

    setLabelX( -16 );
    setLabelY( -24 );
    setLabelRot( 0 );
}

void Switch::setPoles( int p )
{
    if( p < 1 ) p = 1;
    if( p > 8 ) p = 8;

    if( p == m_poles ) return;

    m_poles = p;

    buildPins();
}

void Switch::setDT( bool dt )
{
    if( m_dt == dt ) return;

    m_dt = dt;

    buildPins();
}

void Switch::buildPins()
{
    Circuit *circ = circuit();

    // Whole teardown rather than a patch: a switch going from one pole to two, or
    // from single to double throw, has terminals in places the old ones were not,
    // and a wire left hanging off a pin that no longer means what it did is worse
    // than one that was taken off.  Upstream does the same when its package
    // changes.
    dropPins( 0 );
    m_thrown.clear();

    const int sp = kPoleSpacing;

    // Rows are centred on the symbol, so an odd pole count leaves the middle one
    // on the lattice and an even one leaves the centre between two of them.
    const int top = -( m_poles-1 )*sp/2;

    for( int i = 0; i < m_poles; i++ )
    {
        const int y = top + i*sp;

        // A single pole single throw keeps upstream's names, because that is the
        // shape nearly every .simu carries and the names are what a connector in
        // the file refers to.  Anything larger has no upstream equivalent to be
        // compatible with, so it gets indexed names that say which pole and which
        // throw they are.
        const bool plain = ( m_poles == 1 && !m_dt );

        addPin( -16, y, plain ? QString( "lPin" )
                              : QString( "pinP" ) + QString::number( i ), 180 );

        if( m_dt )
        {
            addPin( 16, y-5, plain ? QString( "rPinA" )
                                   : QString( "pinT%1a" ).arg( i ), 0 );
            addPin( 16, y+5, plain ? QString( "rPinB" )
                                   : QString( "pinT%1b" ).arg( i ), 0 );
        }
        else
            addPin( 16, y, plain ? QString( "rPin" )
                                 : QString( "pinT" ) + QString::number( i ), 0 );

        m_thrown.append( false );
    }

    // Both terminals of a contact present a finite admittance whichever way the
    // switch is, which is what keeps a node carrying only open switches out of
    // the singular case described in the header.
    for( int i = 0; i < numPins(); i++ )
        if( Pin *p = pin( i ) ) p->setIsAdmit( true );

    initPins();

    const int ext = m_dt ? 12 : 8;
    setArea( QRectF( -8, top-ext, 16, ( m_poles-1 )*sp + 2*ext ) );

    // Circuit::addComponent() registered the pins that existed when this
    // component was created, once.  These were made afterwards, and a pin that is
    // not in the sheet's map is a pin no connector in a file can find - so the
    // component that makes them has to register them itself.  In the constructor
    // circuit() is still null and there is nothing to register with.
    if( circ )
    {
        for( int i = 0; i < numPins(); i++ )
            if( Pin *p = pin( i ) ) circ->addPin( p );

        circ->updateNodes();
    }
}

bool Switch::isThrown( int pole ) const
{
    if( pole < 0 || pole >= m_thrown.size() ) return false;

    return m_thrown[pole];
}

void Switch::setThrown( int pole, bool thrown )
{
    if( pole < 0 || pole >= m_thrown.size() ) return;
    if( m_thrown[pole] == thrown ) return;

    m_thrown[pole] = thrown;

    circChanged();
    update();
}

bool Switch::isConducting( int pole ) const
{
    // Thrown XOR normally-closed: a switch that rests closed opens when it is
    // thrown, and one that rests open closes.
    return isThrown( pole ) != m_normClose;
}

void Switch::toggle()
{
    // Every pole together.  Poles is one switch with several decks, not several
    // switches in one body - that is SwitchDip - so throwing one and not the
    // others is not a state this class can be in.
    const bool now = !isThrown( 0 );

    for( int i = 0; i < m_thrown.size(); i++ )
        if( m_thrown[i] != now )
        {
            m_thrown[i] = now;
            update();
        }

    circChanged();
}

bool Switch::keyPressed( const QString &key )
{
    // An empty Key claims nothing.  Without this test every switch on the sheet
    // would answer to the empty string and the first keystroke would throw all of
    // them, which is the failure that makes a Key property look broken rather
    // than merely unset.
    if( m_key.isEmpty() ) return false;
    if( key.compare( m_key, Qt::CaseInsensitive ) != 0 ) return false;

    toggle();

    return true;
}

void Switch::stamp()
{
    const int perPole = m_dt ? 3 : 2;

    for( int i = 0; i < m_poles; i++ )
    {
        const int base = i*perPole;

        Pin *pole = pin( base );

        if( m_dt )
        {
            // A changeover: the common is on one throw and off the other, and
            // both are stamped, because the one it is off still presents its
            // 1e12 ohm to its node and leaving that row empty is the singular
            // matrix the header describes.
            Pin *a = pin( base+1 );
            Pin *b = pin( base+2 );

            const bool onB = isThrown( i ) ? !m_normClose : m_normClose;

            stampConductance( pole, onB ? b : a, SW_CLOSED_ADMIT );
            stampConductance( pole, onB ? a : b, SW_OPEN_ADMIT );
        }
        else
        {
            const bool on = isConducting( i );

            stampConductance( pole, pin( base+1 ),
                              on ? SW_CLOSED_ADMIT : SW_OPEN_ADMIT );
        }
    }
}

void Switch::mousePressEvent( QGraphicsSceneMouseEvent *event )
{
    if( event->button() == Qt::LeftButton ) toggle();

    // The base has to see the press: it is what starts the drag, and a switch
    // that could be thrown but not moved would read as a hang.
    Component::mousePressEvent( event );
}

void Switch::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                    QWidget *widget )
{
    Component::paint( painter, option, widget );

    painter->drawRoundedRect( m_area, 2, 2 );

    const int sp = kPoleSpacing;
    const int top = -( m_poles-1 )*sp/2;

    for( int i = 0; i < m_poles; i++ )
    {
        const qreal y = top + i*sp;

        if( m_dt )
        {
            const qreal ya = y-5, yb = y+5;

            const bool onB = isThrown( i ) ? !m_normClose : m_normClose;

            painter->drawLine( QPointF( -8, y ), QPointF( 0, y ) );
            painter->drawLine( QPointF( 0, y ), QPointF( 8, onB ? yb : ya ) );

            // The throw that is not selected still gets its pad, so both are
            // visible and the symbol reads as a changeover rather than as a
            // switch with one terminal missing.
            painter->drawEllipse( QPointF( 8, onB ? ya : yb ), 1.3, 1.3 );
        }
        else if( isConducting( i ) )
        {
            painter->drawLine( QPointF( -8, y ), QPointF( 8, y ) );
        }
        else
        {
            painter->drawLine( QPointF( -8, y ), QPointF( 6, y-6 ) );
            painter->drawEllipse( QPointF( 8, y ), 1.3, 1.3 );
        }
    }
}

// ---- Push ------------------------------------------------------------------------

Push::Push( QObject *parent, const QString &type, const QString &id )
    : Switch( parent, type, id )
{
}

void Push::mousePressEvent( QGraphicsSceneMouseEvent *event )
{
    if( event->button() == Qt::LeftButton )
    {
        for( int i = 0; i < m_thrown.size(); i++ )
            if( !m_thrown[i] )
            {
                m_thrown[i] = true;
                update();
            }
        circChanged();
    }

    Component::mousePressEvent( event );
}

void Push::mouseReleaseEvent( QGraphicsSceneMouseEvent *event )
{
    if( event->button() == Qt::LeftButton )
    {
        for( int i = 0; i < m_thrown.size(); i++ )
            if( m_thrown[i] )
            {
                m_thrown[i] = false;
                update();
            }
        circChanged();
    }

    // Without this the drag the press began is never ended, and the symbol
    // follows the cursor with the button up.
    Component::mouseReleaseEvent( event );
}

void Push::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                  QWidget *widget )
{
    Switch::paint( painter, option, widget );

    // The cap, which is the only thing distinguishing a momentary push from a
    // latching switch of the same pole count.  Drawn over the body rather than
    // instead of it, so the contacts underneath still show which way it is
    // resting.
    painter->setBrush( isThrown( 0 ) ? QColor( 120, 200, 120 )
                                     : QColor( 200, 200, 200 ) );

    const int sp = kPoleSpacing;
    const int top = -( m_poles-1 )*sp/2;

    painter->drawRoundedRect( QRectF( -5, top-5, 10, ( m_poles-1 )*sp + 10 ), 3, 3 );

    painter->setBrush( m_color );
}

// ---- SwitchDip -------------------------------------------------------------------

SwitchDip::SwitchDip( QObject *parent, const QString &type, const QString &id )
         : Component( parent, type, id )
         , m_state()
         , m_size( 0 )
{
    setSize( 8 );

    setLabelX( -24 );
    setLabelY( -40 );
    setLabelRot( 0 );
}

int SwitchDip::segmentAt( const QPointF &local ) const
{
    // The body runs from y = -28 down size cells of eight, and segment i occupies
    // the cell that starts at -28 + 8*i, so the index is the cell the point falls
    // in rather than the nearest pin - the pins are eight apart and a click
    // between two of them belongs to one of them.
    if( m_size <= 0 ) return -1;

    const int i = (int)( ( local.y() + 28 )/8 );

    if( i < 0 || i >= m_size ) return -1;

    return i;
}

void SwitchDip::setSize( int s )
{
    if( s <= 0 ) s = 8;
    if( s == m_size ) return;

    if( s < m_size )
    {
        // Shrinking keeps the wires on the segments that survive, which is the
        // whole reason dropPins() takes an index.
        dropPins( 2*s );

        while( m_state.size() > s ) m_state.removeLast();
    }
    else
        addSegments( s - m_size );

    m_size = s;

    setArea( QRectF( -10, -28, 20, m_size*8 ) );

    if( Circuit *circ = circuit() ) circ->updateNodes();
}

void SwitchDip::addSegments( int count )
{
    Circuit *circ = circuit();

    const int first = m_size;

    for( int i = first; i < first+count; i++ )
    {
        const QString base = "switch" + QString::number( i ) + "-ePin";
        const int y = -24 + 8*i;

        Pin *a = addPin( -16, y, base + QString::number( 2*i   ), 180 );
        Pin *b = addPin(  16, y, base + QString::number( 2*i+1 ),   0 );

        if( a ) a->setIsAdmit( true );
        if( b ) b->setIsAdmit( true );

        // Made after Circuit::addComponent() ran, so this component has to put
        // them in the sheet's pin map itself.
        if( circ )
        {
            if( a ) circ->addPin( a );
            if( b ) circ->addPin( b );
        }

        m_state.append( false );
    }

    initPins();
}

bool SwitchDip::isClosed( int i ) const
{
    if( i < 0 || i >= m_state.size() ) return false;

    return m_state[i];
}

void SwitchDip::setClosed( int i, bool on )
{
    if( i < 0 || i >= m_state.size() ) return;
    if( m_state[i] == on ) return;

    m_state[i] = on;

    circChanged();
    update();
}

void SwitchDip::stamp()
{
    for( int i = 0; i < m_size; i++ )
        stampConductance( pin( 2*i ), pin( 2*i+1 ),
                          isClosed( i ) ? SW_CLOSED_ADMIT : SW_OPEN_ADMIT );
}

void SwitchDip::mousePressEvent( QGraphicsSceneMouseEvent *event )
{
    if( event->button() == Qt::LeftButton )
    {
        // event->pos() is in this item's own frame, which is the frame the
        // segments are laid out in, so the flip and the rotation the symbol is
        // carrying have already been undone by the time it gets here.
        const int i = segmentAt( event->pos() );

        if( i >= 0 ) setClosed( i, !isClosed( i ) );
    }

    Component::mousePressEvent( event );
}

void SwitchDip::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                       QWidget *widget )
{
    Component::paint( painter, option, widget );

    painter->drawRoundedRect( m_area, 2, 2 );

    for( int i = 0; i < m_size; i++ )
    {
        const qreal y = -24 + 8*i;

        painter->drawLine( QPointF( -10, y ), QPointF( -4, y ) );
        painter->drawLine( QPointF(   4, y ), QPointF( 10, y ) );

        if( isClosed( i ) )
        {
            painter->drawLine( QPointF( -4, y ), QPointF( 4, y ) );

            // The slider, in the position a real DIP pack's is when the segment
            // is on, so a bank of eight can be read at a glance rather than one
            // contact at a time.
            painter->setBrush( QColor( 120, 200, 120 ) );
            painter->drawRect( QRectF( -3, y-2.5, 6, 5 ) );
            painter->setBrush( m_color );
        }
        else
            painter->drawLine( QPointF( -4, y ), QPointF( 3, y-3 ) );
    }
}

// ---- RelaySPST -------------------------------------------------------------------

RelaySPST::RelaySPST( QObject *parent, const QString &type, const QString &id )
         : Component( parent, type, id )
         , m_coilAdmit( 1/100.0 )
         , m_Ion( 0.02 )
         , m_Ioff( 0.01 )
         , m_poles( 1 )
         , m_dt( false )
         , m_normClose( false )
         , m_closed( false )
{
    buildPins();

    setBaseUnit( "Ω" );
    setRcoil( 100 );
    setShowVal( true );

    setLabelX( -24 );
    setLabelY( -40 );
    setLabelRot( 0 );

    setValLabelX( 12 );
    setValLabelY( 0 );
    setValLabRot( 0 );
}

void RelaySPST::setPoles( int p )
{
    if( p < 1 ) p = 1;
    if( p > 8 ) p = 8;

    if( p == m_poles ) return;

    m_poles = p;

    buildPins();
}

void RelaySPST::setDT( bool dt )
{
    if( m_dt == dt ) return;

    m_dt = dt;

    buildPins();
}

void RelaySPST::setRcoil( double r )
{
    setValue( r );

    // Floored rather than allowed to reach zero: the coil is stamped as a
    // conductance of 1/R, and a zero would put an infinity on the diagonal of a
    // matrix that otherwise looks perfectly well formed.
    double ohms = getmultValue();
    if( ohms < RES_FLOOR ) ohms = RES_FLOOR;

    m_coilAdmit = 1/ohms;

    update();
}

void RelaySPST::setUnit( const QString &un )
{
    Component::setUnit( un );

    double ohms = getmultValue();
    if( ohms < RES_FLOOR ) ohms = RES_FLOOR;

    m_coilAdmit = 1/ohms;
}

void RelaySPST::buildPins()
{
    Circuit *circ = circuit();

    dropPins( 0 );

    const int sp = kPoleSpacing;

    // The coil sits under the contacts and its terminals point down, so the rows
    // are stacked upwards from a fixed bottom one: the coil's own geometry does
    // not move as poles are added, which is what keeps the symbol from growing in
    // both directions at once.
    const int bottomRow = -14;
    const int top = bottomRow - ( m_poles-1 )*sp;

    addPin( -8, 20, "lPin", 90 );
    addPin(  8, 20, "rPin", 90 );

    for( int i = 0; i < m_poles; i++ )
    {
        const int y = top + i*sp;

        addPin( -16, y, QString( "pinP" ) + QString::number( i ), 180 );

        if( m_dt )
        {
            addPin( 16, y-5, QString( "pinT%1a" ).arg( i ), 0 );
            addPin( 16, y+5, QString( "pinT%1b" ).arg( i ), 0 );
        }
        else
            addPin( 16, y, QString( "pinT" ) + QString::number( i ), 0 );
    }

    for( int i = 0; i < numPins(); i++ )
        if( Pin *p = pin( i ) ) p->setIsAdmit( true );

    initPins();

    const int ext = m_dt ? 12 : 8;

    // The body has to cover the contact rows at the top and the coil at the
    // bottom, and the coil's own rectangle is fixed, so the height is the
    // distance between the two rather than a function of the pole count alone.
    setArea( QRectF( -9, top-ext, 18, 12 - ( top-ext ) ) );

    if( circ )
    {
        for( int i = 0; i < numPins(); i++ )
            if( Pin *p = pin( i ) ) circ->addPin( p );

        circ->updateNodes();
    }
}

double RelaySPST::coilCurrent() const
{
    Pin *l = pin( 0 );
    Pin *r = pin( 1 );

    if( !l || !r ) return 0;

    // The coil is a pure resistance, so its current is Ohm's law on the two
    // terminal voltages just solved.  Read on demand rather than kept: there is
    // no per-step list a resistance is on, so nothing would ever refresh it.
    return ( l->getVolt() - r->getVolt() )*m_coilAdmit;
}

void RelaySPST::setClosed( bool on )
{
    if( m_closed == on ) return;

    m_closed = on;

    circChanged();
    update();
}

void RelaySPST::stamp()
{
    // The coil first: it is what the current is read back from, and it is a
    // resistance in its own right whether or not the contacts move.
    stampConductance( pin( 0 ), pin( 1 ), m_coilAdmit );

    const bool on = ( m_closed != m_normClose );
    const int perPole = m_dt ? 3 : 2;

    for( int i = 0; i < m_poles; i++ )
    {
        const int base = 2 + i*perPole;

        Pin *pole = pin( base );

        if( m_dt )
        {
            Pin *a = pin( base+1 );
            Pin *b = pin( base+2 );

            stampConductance( pole, on ? b : a, SW_CLOSED_ADMIT );
            stampConductance( pole, on ? a : b, SW_OPEN_ADMIT );
        }
        else
            stampConductance( pole, pin( base+1 ),
                              on ? SW_CLOSED_ADMIT : SW_OPEN_ADMIT );
    }
}

void RelaySPST::updateStep()
{
    const double i = qAbs( coilCurrent() );

    // Ordered here rather than in the setters.  IOn below IOff is a mistake the
    // property panel will happily accept, and taken literally it makes the two
    // thresholds overlap so that no current satisfies "off" without also
    // satisfying "on" - which is a relay that chatters once per step, at a
    // million steps a second, re-factorising the matrix every time.  Taking them
    // in the order that makes sense costs two comparisons.
    const double on  = qMax( m_Ion, m_Ioff );
    const double off = qMin( m_Ion, m_Ioff );

    if( !m_closed )
    {
        if( i >= on ) setClosed( true );
    }
    else if( i <= off ) setClosed( false );
}

void RelaySPST::resetStep()
{
    setClosed( false );
}

void RelaySPST::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                       QWidget *widget )
{
    Component::paint( painter, option, widget );

    painter->drawRoundedRect( m_area, 2, 2 );

    const int sp = kPoleSpacing;
    const int top = -14 - ( m_poles-1 )*sp;
    const bool on = ( m_closed != m_normClose );

    // ---- the contacts --------------------------------------------------------
    for( int i = 0; i < m_poles; i++ )
    {
        const qreal y = top + i*sp;

        if( m_dt )
        {
            const qreal ya = y-5, yb = y+5;

            painter->drawLine( QPointF( -8, y ), QPointF( 0, y ) );
            painter->drawLine( QPointF( 0, y ), QPointF( 8, on ? yb : ya ) );
            painter->drawEllipse( QPointF( 8, on ? ya : yb ), 1.3, 1.3 );
        }
        else if( on )
            painter->drawLine( QPointF( -8, y ), QPointF( 8, y ) );
        else
        {
            painter->drawLine( QPointF( -8, y ), QPointF( 6, y-6 ) );
            painter->drawEllipse( QPointF( 8, y ), 1.3, 1.3 );
        }
    }

    // ---- the armature --------------------------------------------------------
    // The dashed link between coil and contacts, which is what says the two move
    // together.  Without it the symbol reads as a coil that happens to be drawn
    // under some unrelated switches.
    painter->setPen( QPen( painter->pen().color(), 1, Qt::DashLine ) );
    painter->drawLine( QPointF( 0, 2 ), QPointF( 0, top+ ( m_poles-1 )*sp ) );

    QPen solid = painter->pen();
    solid.setStyle( Qt::SolidLine );
    painter->setPen( solid );

    // ---- the coil ------------------------------------------------------------
    painter->drawRect( QRectF( -9, 2, 18, 10 ) );
    painter->drawLine( QPointF( -3, 2 ), QPointF( -3, 12 ) );
    painter->drawLine( QPointF(  3, 2 ), QPointF(  3, 12 ) );
}

// ---- factories -------------------------------------------------------------------

Component* createSwitch( QObject *parent, const QString &type, const QString &id )
{ return new Switch( parent, type, id ); }

Component* createPush( QObject *parent, const QString &type, const QString &id )
{ return new Push( parent, type, id ); }

Component* createSwitchDip( QObject *parent, const QString &type, const QString &id )
{ return new SwitchDip( parent, type, id ); }

Component* createRelaySPST( QObject *parent, const QString &type, const QString &id )
{ return new RelaySPST( parent, type, id ); }
