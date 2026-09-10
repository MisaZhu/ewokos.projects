/*
 * SimulIDE, ported to EwokOS - see passive.h.
 */

#include <QAction>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

#include "passive.h"

#include "circuit.h"
#include "pin.h"

// ---------------------------------------------------------------------------
// Resistor
// ---------------------------------------------------------------------------

Resistor::Resistor( QObject *parent, const QString &type, const QString &id )
         : TwoPin( parent, type, id )
{
    setBaseUnit( "Ω" );

    // Upstream's default, out of eResistor's constructor rather than Resistor's,
    // which is where it actually lives.
    setResist( 100 );

    // Upstream's label offsets.  They are not cosmetic: an example circuit with
    // symbols placed to suit them reads badly with labels anywhere else, and the
    // two triples round-trip through a .simu as labelx/labely/labelrot and
    // valLabelx/valLabely/valLabRot.
    setLabelX( -12 );
    setLabelY( -20 );
    setLabelRot( 0 );

    setValLabelX( -16 );
    setValLabelY( 6 );
    setValLabRot( 0 );
    setShowVal( true );
}

void Resistor::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                      QWidget *widget )
{
    Component::paint( painter, option, widget );

    // The IEC rectangle rather than the zig-zag, which is what upstream draws.
    // Twenty-one by eight inside an area of twenty-two by nine, so the body sits
    // clear of the selection box the base has just drawn.
    painter->drawRect( QRectF( -10.5, -4, 21, 8 ) );
}

Component* createResistor( QObject *parent, const QString &type, const QString &id )
{
    return new Resistor( parent, type, id );
}

// ---------------------------------------------------------------------------
// CapacitorBase
// ---------------------------------------------------------------------------

CapacitorBase::CapacitorBase( QObject *parent, const QString &type, const QString &id )
              : TwoPin( parent, type, id )
              , m_cap( 0 )
              , m_volt( 0 )
{
    // A taller area than a resistor's, because the plates are drawn either side of
    // the origin, and leads of twelve rather than eight so the terminals stand
    // clear of them.
    setArea( QRectF( -10, -10, 20, 20 ) );

    if( Pin *a = lPin() ) a->setLength( 12 );
    if( Pin *b = rPin() ) b->setLength( 12 );

    setBaseUnit( "F" );

    // Upstream's eCapacitor default of 10 uF.
    setCapac( 1e-5 );

    setValLabelX( -16 );
    setValLabelY( 8 );
    setValLabRot( 0 );
    setShowVal( true );

    setLabelX( -16 );
    setLabelY( -24 );
    setLabelRot( 0 );
}

void CapacitorBase::setCapac( double c )
{
    if( c < CAP_FLOOR ) c = CAP_FLOOR;

    // In this order, and the farads come out of getmultValue() rather than from
    // the argument: setValue() picks the prefix that puts the displayed number in
    // [1,1000), so what the element wants is the product it leaves behind and not
    // the number that went in.
    setValue( c );
    setFarads( getmultValue() );
}

void CapacitorBase::setFarads( double c )
{
    if( c < CAP_FLOOR ) c = CAP_FLOOR;
    if( c == m_cap ) return;

    m_cap = c;

    // The companion conductance, G = C/dt.  Recomputed here and in resetStep()
    // and nowhere else, which are the only two events that can move it; stamp()
    // runs once per pass and must not rewrite a value that has not changed, or
    // eNode::stampAdmitance()'s tolerance guard is defeated and the matrix is
    // refactored every step of every run.
    setOhms( dt()/m_cap );
}

void CapacitorBase::setUnit( const QString &un )
{
    // Component's, not TwoPin's.  TwoPin's follows the prefix change with
    // setOhms(getmultValue()), which for a capacitor would set its resistance to
    // its capacitance - 10 uF becoming a 1e-5 ohm short.
    Component::setUnit( un );

    setFarads( getmultValue() );
}

void CapacitorBase::stamp()
{
    // Conductance first, and the source only if the conductance went in.  A
    // capacitor with one lead unwired is an open circuit, and stamping -Is onto
    // whichever end is live would be a current sink with nothing feeding it,
    // dragging that node towards whatever the source can push against it.
    if( !stampPair( m_admit ) ) return;

    // Is = G*v_prev, from b to a, so that the branch as a whole carries
    // i = G*v - Is = C*(v - v_prev)/dt.  stampCurrent() adds to the node's right
    // hand side, positive meaning into the node, hence +Is at a and -Is at b.
    const double isrc = m_volt*m_admit;

    lPin()->stampCurrent(  isrc );
    rPin()->stampCurrent( -isrc );
}

void CapacitorBase::updateStep()
{
    // Deliberately not stamp(), which is what eElement::updateStep() does by
    // default.  The matrix is zeroed before the next round of stamps, so anything
    // written from here would be wiped before it was ever solved.  All this has
    // to do is remember the voltage the solve just produced, for the source that
    // stamp() builds next time round.
    m_volt = pinVolt();
}

void CapacitorBase::resetStep()
{
    // dt can change under a capacitor: a .simu carries its own simuRate and it is
    // applied after the components have been built, so a capacitance computed
    // against one timestep can be run at another.  The Simulator calls this on
    // every element of its reactive list from setSimuRate() and from startSim(),
    // which is what makes recomputing here sufficient.
    setOhms( dt()/m_cap );

    // And Start means from rest, so the memory term goes with it.
    m_volt = 0;
}

// ---------------------------------------------------------------------------
// Capacitor
// ---------------------------------------------------------------------------

Capacitor::Capacitor( QObject *parent, const QString &type, const QString &id )
          : CapacitorBase( parent, type, id )
{
}

void Capacitor::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                       QWidget *widget )
{
    Component::paint( painter, option, widget );

    QPen pen = painter->pen();
    pen.setWidth( 3 );
    painter->setPen( pen );

    // Two plates of equal length, because this one is not polarised - the
    // difference from elCapacitor is the whole of what the two symbols say.
    painter->drawLine( QPointF( -3, -6 ), QPointF( -3, 6 ) );
    painter->drawLine( QPointF(  3, -6 ), QPointF(  3, 6 ) );
}

Component* createCapacitor( QObject *parent, const QString &type, const QString &id )
{
    return new Capacitor( parent, type, id );
}

// ---------------------------------------------------------------------------
// elCapacitor
// ---------------------------------------------------------------------------

elCapacitor::elCapacitor( QObject *parent, const QString &type, const QString &id )
            : CapacitorBase( parent, type, id )
            , m_reversed( false )
            , m_counter( 0 )
{
}

void elCapacitor::updateStep()
{
    // The integration is the base's; this half only watches the sign of what came
    // out of it.
    CapacitorBase::updateStep();

    const bool back = ( m_volt < -1e-6 );

    if( back )
    {
        // Five steps of it before saying so.  One step of reverse voltage is what
        // an AC signal and what a switching transient both look like, and flagging
        // either would put a part into its fault colours while it is working
        // normally.  Capped rather than reset once it fires, so a part left
        // backwards for an hour does not wrap its counter round.
        if( ++m_counter < 5 ) return;
        m_counter = 5;
    }
    else
    {
        m_counter = 0;
    }

    if( m_reversed == back ) return;

    m_reversed = back;

    // Only on the transition.  This runs at simuRate, so an unconditional
    // repaint would be a million a second for a change that happens twice.
    update();
}

void elCapacitor::resetStep()
{
    CapacitorBase::resetStep();

    // Upstream's initialize(), which is the same event under another name: a run
    // starting again has no history, so neither the fault nor the debounce that
    // was on the way to reporting it survives it.
    if( !m_reversed && !m_counter ) return;

    m_reversed = false;
    m_counter  = 0;
    update();
}

void elCapacitor::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                         QWidget *widget )
{
    Component::paint( painter, option, widget );

    QPen pen = painter->pen();
    pen.setWidth( 3 );

    // Red and thinner while the part is backwards, which is the whole of what the
    // flag above is for.
    if( m_reversed )
    {
        pen.setColor( QColor( 255, 100, 100 ) );
        pen.setWidth( 2 );
    }
    painter->setPen( pen );

    // The straight plate at x=3 is the positive one, and the bracket at x=-3 is
    // the curved negative plate of the standard symbol, drawn as the two arms and
    // the short vertical that read as a curve at this size.
    painter->drawLine( QPointF(  3, -7 ), QPointF(  3,  7 ) );
    painter->drawLine( QPointF( -3, -7 ), QPointF(  3, -7 ) );
    painter->drawLine( QPointF( -3,  7 ), QPointF(  3,  7 ) );
    painter->drawLine( QPointF( -3, -3 ), QPointF( -3,  3 ) );
}

Component* createElCapacitor( QObject *parent, const QString &type, const QString &id )
{
    return new elCapacitor( parent, type, id );
}

// ---------------------------------------------------------------------------
// ResistorDip
// ---------------------------------------------------------------------------

ResistorDip::ResistorDip( QObject *parent, const QString &type, const QString &id )
            : Component( parent, type, id )
            , m_size( 0 )
            , m_resist( 100 )
            , m_admit( 1/100.0 )
{
    // Zero first, so that setSize(8) below reads as growth from nothing rather
    // than as a resize of a pack that already has segments in it.
    setSize( 8 );

    setBaseUnit( "Ω" );
    setResist( 100 );

    // Upstream's, including the quarter turn on the value label, which runs down
    // the side of a pack that is eight segments tall.
    setValLabelX( 5 );
    setValLabelY( -26 );
    setValLabRot( 90 );
    setShowVal( true );

    setLabelX( -24 );
    setLabelY( -40 );
    setLabelRot( 0 );
}

void ResistorDip::setSize( int s )
{
    // Upstream's guard: a pack of zero segments is not a thing, and a .simu that
    // carries Size="0" gets the default rather than a symbol with no terminals.
    if( s <= 0 ) s = 8;
    if( s == m_size ) return;

    // Growing and shrinking are separate operations and neither rebuilds the whole
    // symbol, which is the reason dropPins() takes an index rather than only
    // clearing: a pack resized from eight segments to six has to keep the wires on
    // the four that survive, and a teardown followed by a rebuild would take them
    // all off.
    if( s < m_size ) dropPins( 2*s );
    else             addSegments( s - m_size );

    m_size = s;

    // Upstream sets m_area to QRect(-8,-26,16,size*8-4) and then draws
    // boundingRect(), which is that grown by two on every side.  The result is
    // stored here instead, so that m_area means what this port says it means -
    // everything drawn fits inside it - and the body comes out where upstream's
    // does: twenty wide, centred on the span of the pins, two units past the
    // first and last of them at each end.
    setArea( QRectF( -10, -28, 20, m_size*8 ) );
    setValLabelY( -26 );

    // The node graph now holds pins that no longer exist, or none for pins that
    // do.  updateNodes() rebuilds it whole and pauses the simulation itself while
    // it does, because a step in flight would be walking the eNodes being freed.
    if( Circuit *circ = circuit() ) circ->updateNodes();
}

void ResistorDip::addSegments( int count )
{
    Circuit *circ = circuit();

    const int first = m_size;

    for( int i = first; i < first+count; i++ )
    {
        // Upstream's pin names, which is how a .simu connector refers to one
        // segment of a pack.  addPin() prefixes them with the component's own id,
        // so the names here are the part after the first hyphen and no more.
        const QString base = "resistor" + QString::number( i ) + "-ePin";

        const int y = -24 + 8*i;

        Pin *a = addPin( -16, y, base + QString::number( 2*i   ), 180 );
        Pin *b = addPin(  16, y, base + QString::number( 2*i+1 ),   0 );

        // Both halves of what makes a terminal take part in the solve; see the
        // comment in TwoPin's constructor for the failure that follows from
        // leaving either out.
        if( a ) a->setIsAdmit( true );
        if( b ) b->setIsAdmit( true );

        // And the sheet's pin map, which Component::addPin() cannot do because a
        // component's constructor runs before it is on a sheet.  Without this a
        // segment added by a Size edit is invisible to a connector naming it.
        if( circ )
        {
            if( a ) circ->addPin( a );
            if( b ) circ->addPin( b );
        }
    }

    // Reparents the new pins as graphics items so they follow the symbol through
    // rotation and flip.  Idempotent for the ones already done.
    initPins();
}

void ResistorDip::applyOhms()
{
    double r = getmultValue();
    if( qAbs( r ) < RES_FLOOR ) r = RES_FLOOR;

    if( r == m_resist ) return;

    m_resist = r;
    m_admit  = 1/r;
}

void ResistorDip::setResist( double r )
{
    if( r < RES_FLOOR ) r = RES_FLOOR;

    setValue( r );
    applyOhms();
}

void ResistorDip::setUnit( const QString &un )
{
    Component::setUnit( un );
    applyOhms();
}

void ResistorDip::stamp()
{
    // One stamp per segment, each of which declines on its own if either of its
    // ends is unwired - so a pack with three of its eight segments connected
    // contributes three conductances and not eight.
    for( int i = 0; i < m_size; i++ )
        stampConductance( pin( 2*i ), pin( 2*i+1 ), m_admit );
}

void ResistorDip::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                         QWidget *widget )
{
    Component::paint( painter, option, widget );

    // Upstream calls drawRoundRect(), which is Qt 4 and is not in Qt 5; this is
    // the same call under the name it has now.
    painter->drawRoundedRect( m_area, 2, 2 );
}

Component* createResistorDip( QObject *parent, const QString &type, const QString &id )
{
    return new ResistorDip( parent, type, id );
}

// ---------------------------------------------------------------------------
// Potentiometer
// ---------------------------------------------------------------------------

Potentiometer::Potentiometer( QObject *parent, const QString &type, const QString &id )
              : Component( parent, type, id )
              , m_pos( 500 )
              , m_resist( 1000 )
              , m_admitA( 1/500.0 )
              , m_admitB( 1/500.0 )
{
    // Upstream's, plus 1.5 units of height so that the wiper arrow it draws below
    // the body is inside the area.  Upstream's own area stops at y=8 and its arrow
    // reaches y=9, which its boundingRect happens to cover; this port's contract
    // is that m_area covers it, so m_area is the one that moves.
    setArea( QRectF( -12, -4.5, 24, 14 ) );

    addPin( -16,  0, "PinA", 180 );
    // Upstream declares the wiper 270, which in its frame is a pin below the
    // symbol with its lead pointing up into it.  This port's angles run the other
    // way vertically - see leadDir() in circuit/pin.cpp - so the same terminal, in
    // the same place, pointing the same way, is 90.
    addPin(   0, 16, "PinM",  90 );
    addPin(  16,  0, "PinB",   0 );

    for( int i = 0; i < numPins(); i++ )
        if( Pin *p = pin( i ) ) p->setIsAdmit( true );

    initPins();

    setBaseUnit( "Ω" );
    setResist( 1000 );

    setLabelX( -16 );
    setLabelY( -40 );
    setLabelRot( 0 );

    setValLabelX( 10 );
    setValLabelY( -20 );
    setValLabRot( 0 );
    setShowVal( true );
}

void Potentiometer::setResist( double r )
{
    if( r < RES_FLOOR ) r = RES_FLOOR;

    setValue( r );

    const double total = getmultValue();
    if( total == m_resist ) return;

    m_resist = total;

    // The position is untouched, which is the point of storing it as a position:
    // a pot at its midpoint stays at its midpoint when 1k becomes 10k, and only
    // the two halves' ohmages change.
    applySplit();
}

void Potentiometer::setUnit( const QString &un )
{
    Component::setUnit( un );

    m_resist = getmultValue();
    applySplit();
}

void Potentiometer::setWiperPos( int p )
{
    if( p <    0 ) p =    0;
    if( p > 1000 ) p = 1000;
    if( p == m_pos ) return;

    m_pos = p;
    applySplit();
}

int Potentiometer::wiperOhms() const
{
    const double ohms = m_resist*m_pos/1000.0;

    // Guarded because the property is an int and a track can be set to gigaohms.
    // A cast of something past INT_MAX is not a clamp, it is undefined.
    if( ohms > 2e9 ) return 2000000000;

    return int( ohms );
}

void Potentiometer::setWiperOhms( int ohms )
{
    if( ohms < 0 ) ohms = 0;

    // Clamped in double, before the cast, for the reason wiperOhms() gives.
    double p = ( m_resist > 0 ) ? ohms*1000.0/m_resist : 0;
    if( p <    0 ) p =    0;
    if( p > 1000 ) p = 1000;

    setWiperPos( int( p ) );
}

void Potentiometer::applySplit()
{
    // Upstream's two clamps, with one more floor in front of them.  A track
    // floored only at 1e-12 lets the first clamp raise r1 to 1e-3 - more than the
    // whole track - which makes r2 negative; the second clamp then sets r1 to
    // total-1e-6, which is negative too.  A negative resistance is a negative
    // conductance on the diagonal, and the matrix stops being diagonally dominant:
    // the solve still produces numbers, so what you get is a pot that pushes its
    // wiper the wrong way rather than one that fails loudly.  Flooring the total
    // at upstream's own first clamp makes both of them satisfiable.
    double total = m_resist;
    if( total < 1e-3 ) total = 1e-3;

    double r1 = total*m_pos/1000.0;
    double r2 = total - r1;

    if( r1 < 1e-6 ) { r1 = 1e-3; r2 = total - r1; }
    if( r2 < 1e-6 ) { r2 = 1e-6; r1 = total - r2; }

    m_admitA = 1/r1;
    m_admitB = 1/r2;
}

void Potentiometer::stamp()
{
    Pin *a = pinA();
    Pin *w = pinM();
    Pin *b = pinB();
    if( !a || !w || !b ) return;

    // The unwired wiper, which is what upstream's private mid-eNode exists for and
    // which has to be handled here rather than left to stampConductance().  With
    // nothing on the wiper its node has one pin and no matrix row, so both of the
    // half-stamps below would decline and the component would contribute nothing
    // at all - a pot that reads as an open circuit between A and B.  Electrically
    // it is not one: a pot with its wiper in the air is a resistor of the full
    // track value, and that is what gets stamped instead.  One term, and one fewer
    // node in the matrix than the private-node version needs.
    if( !w->connector() )
    {
        stampConductance( a, b, 1/m_resist );
        return;
    }

    stampConductance( a, w, m_admitA );
    stampConductance( w, b, m_admitB );
}

void Potentiometer::contextMenu( QGraphicsSceneContextMenuEvent *event, QMenu *menu )
{
    QAction *act = menu->addAction( tr( "Set Wiper..." ) );
    connect( act, SIGNAL( triggered() ), this, SLOT( slotSetWiper() ) );

    menu->addSeparator();

    Component::contextMenu( event, menu );
}

void Potentiometer::slotSetWiper()
{
    // The upper bound is the whole track - past it the two halves would swap
    // places, which the position already covers by running 0 to 1000.
    int hi = 2000000000;
    if( m_resist < hi ) hi = int( m_resist );

    bool ok = false;

    const int ohms = QInputDialog::getInt( 0l, tr( "Potentiometer" ),
                                           tr( "Wiper resistance (Ohm)" ),
                                           wiperOhms(), 0, hi, 1, &ok );
    if( ok ) setWiperOhms( ohms );
}

void Potentiometer::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                           QWidget *widget )
{
    Component::paint( painter, option, widget );

    // The track, drawn exactly as a resistor's is, which is what it is.
    painter->drawRect( QRectF( -10.5, -4, 21, 8 ) );

    QPen pen = painter->pen();
    pen.setWidth( 3 );
    painter->setPen( pen );

    // The wiper arrow, under the body and opening towards it.  It does not move
    // with the setting: the position is a number in the panel and in the file, and
    // upstream shows it the same way, with the dial carrying the value rather than
    // the symbol.
    painter->drawLine( QPointF( 0, 6 ), QPointF( -3, 9 ) );
    painter->drawLine( QPointF( 0, 6 ), QPointF(  3, 9 ) );
}

Component* createPotentiometer( QObject *parent, const QString &type, const QString &id )
{
    return new Potentiometer( parent, type, id );
}
