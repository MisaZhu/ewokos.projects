/*
 * SimulIDE, ported to EwokOS - see active.h.
 */

#include <QtMath>

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

#include "active.h"

#include "circuit.h"
#include "pin.h"

#include "e-node.h"
#include "e-source.h"
#include "simulator.h"

// ---------------------------------------------------------------------------
// the junction helpers
// ---------------------------------------------------------------------------

// Largest argument the exponential is ever given.  active.h gives the reason it
// is 200 and not something smaller: a junction normalised at its own Forward_Volt
// evaluates exp() at Vf/Vt, and a red LED's 2.2 V is an argument of 85.
#define JUNC_AMAX 200.0

// Floor on a saturation current.  A Forward_Volt far enough above the cap makes
// the normalisation divide by an exponential that has gone to infinity, and an
// isat of zero would leave the junction with no current and no conductance at
// any bias - a part that has stopped being a diode rather than one that is
// unusual.  1e-30 is smaller than anything the sheet can display and keeps the
// exponential in play.
#define JUNC_ISAT_FLOOR 1e-30

// The exponent argument, capped.  Split out because both helpers below have to
// cap it the same way and a disagreement between them would mean a stamp whose
// conductance and whose current describe two different junctions.
static double juncArg( double v, double vt )
{
    double a = v/vt;

    if( a >  JUNC_AMAX ) a =  JUNC_AMAX;
    if( a < -JUNC_AMAX ) a = -JUNC_AMAX;

    return a;
}

double junctionCurrent( double v, double isat, double vt )
{
    if( isat < JUNC_ISAT_FLOOR ) isat = JUNC_ISAT_FLOOR;
    if( vt   <= 0 )              vt   = VT_300K;

    return isat*( qExp( juncArg( v, vt ) ) - 1 );
}

void lineariseJunction( double v, double isat, double vt,
                        double &g, double &is, double *icur )
{
    if( isat < JUNC_ISAT_FLOOR ) isat = JUNC_ISAT_FLOOR;
    if( vt   <= 0 )              vt   = VT_300K;

    const double a = juncArg( v, vt );
    const double e = qExp( a );

    // The pair is built about the capped voltage rather than about the one
    // passed in, so that it is self-consistent: the straight line it describes
    // goes through ( a*vt, i ) with slope g, and the stamp extrapolates from
    // there.  Building it about an uncapped v would give a line that misses the
    // exponential at the only point it was asked to match.
    const double vc = a*vt;

    double slope = isat/vt*e;
    if( slope < ACT_OPEN_ADMIT ) slope = ACT_OPEN_ADMIT;

    const double i = isat*( e - 1 );
    if( icur ) *icur = i;

    g  = slope;
    is = slope*vc - i;
}

// ---------------------------------------------------------------------------
// Diode
// ---------------------------------------------------------------------------

Diode::Diode( QObject *parent, const QString &type, const QString &id )
     : TwoPin( parent, type, id )
     , m_fwdVolt( 0 )
     , m_isat( 0 )
     , m_vlin( 0 )
{
    // Taller than a resistor's, because the triangle is drawn fourteen units
    // across the leads and m_area has to cover everything this paints or the
    // symbol gets clipped rather than repainting its neighbours.
    setArea( QRectF( -11, -9, 22, 18 ) );

    // Zero in the initialiser list above and 0.7 here, so that the setter's
    // "nothing changed" guard does not skip the normalisation the constructor
    // depends on.
    setFwdVolt( 0.7 );

    // Forward_Volt is a bare number of volts and is not routed through the unit
    // machinery, so there is no base unit and valLabelText() stays empty: a
    // diode draws its id and nothing else.  Upstream is the same.
    setLabelX( -16 );
    setLabelY( -24 );
    setLabelRot( 0 );
}

void Diode::setFwdVolt( double v )
{
    // Both ends clamped, and for two different reasons.  Below 0.05 V the
    // normalisation implies a saturation current larger than the test current and
    // the junction conducts as happily backwards as forwards.  Above 5 V
    // exp(Vf/Vt) is past JUNC_AMAX, the normalisation divides by an infinity and
    // the part silently becomes a JUNC_ISAT_FLOOR diode instead of the one the
    // number asked for.  Nothing a symbol on the sheet represents is outside
    // that range - germanium is 0.3 V, a blue LED 3.5.
    if( v < 0.05 ) v = 0.05;
    if( v > 5.0  ) v = 5.0;

    if( v == m_fwdVolt ) return;

    m_fwdVolt = v;

    updateIsat();
}

void Diode::updateIsat()
{
    // Invert i = Isat*(exp(v/Vt)-1) at the datasheet's test point, which is the
    // only thing Forward_Volt means: it is not a threshold, it is the bias at
    // which this junction passes DIODE_IFWD.
    m_isat = DIODE_IFWD/( qExp( m_fwdVolt/VT_300K ) - 1 );
}

double Diode::forwardCurrent() const
{
    return junctionCurrent( pinVolt(), m_isat, VT_300K );
}

void Diode::walkJunction( double &g, double &is )
{
    // Walk the linearisation point towards the solved voltage rather than jumping
    // to it - see the comment on m_vlin in active.h, which is what makes the
    // iteration monotone instead of throwing the diode from rail to rail.
    double v = pinVolt();

    if( v > m_vlin + DIODE_VLIM ) v = m_vlin + DIODE_VLIM;
    if( v < m_vlin - DIODE_VLIM ) v = m_vlin - DIODE_VLIM;

    m_vlin = v;

    g  = 0;
    is = 0;
    lineariseJunction( m_vlin, m_isat, VT_300K, g, is );
}

void Diode::stamp()
{
    double g = 0, is = 0;
    walkJunction( g, is );

    // Conductance first, and the source only if the conductance went in, exactly
    // as CapacitorBase::stamp() does and for the same reason: one lead unwired
    // means this is an open circuit, and a companion source stamped onto the live
    // end alone is a current sink with nothing feeding it.
    if( !stampPair( g ) ) return;

    // i = g*v - is from anode to cathode, and is goes onto the right hand side
    // positive into the node it is stamped at - so +is at the anode and -is at
    // the cathode.  is is negative for a bias between zero and one thermal
    // voltage, which is not an error: it is the straight line meeting the
    // exponential below the knee.
    lPin()->stampCurrent(  is );
    rPin()->stampCurrent( -is );
}

void Diode::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                   QWidget *widget )
{
    Component::paint( painter, option, widget );

    // Triangle from the anode to the cathode, bar on the cathode.  The two are
    // the whole of what the symbol says: current goes left to right and stops at
    // the bar, which is why anode() is lPin() and not the other way round.
    QPainterPath tri;
    tri.moveTo( -6, -7 );
    tri.lineTo(  6,  0 );
    tri.lineTo( -6,  7 );
    tri.closeSubpath();
    painter->drawPath( tri );

    painter->drawLine( QPointF( 6, -7 ), QPointF( 6, 7 ) );
}

Component* createDiode( QObject *parent, const QString &type, const QString &id )
{
    return new Diode( parent, type, id );
}

// ---------------------------------------------------------------------------
// Inductor
// ---------------------------------------------------------------------------

// Floor on an inductance.  The companion conductance is dt/(L+R*dt), so a zero
// would put dt/(R*dt) = 1/R on the diagonal - harmless - but a zero R alongside
// it would put an infinity there, and a property panel will hand back a zero
// sooner or later.
#define IND_FLOOR 1e-12

Inductor::Inductor( QObject *parent, const QString &type, const QString &id )
         : Component( parent, type, id )
         , m_ind( 0 )
         , m_res( 0 )
         , m_admit( 0 )
         , m_curr( 0 )
         , m_showRes( false )
{
    // Wider than a resistor's: the winding is four humps of six units, which is
    // twenty-four across, and the leads arrive from pins sixteen units out.
    setArea( QRectF( -12, -6, 24, 12 ) );

    addPin( -16, 0, "lPin", 180 );
    addPin(  16, 0, "rPin",   0 );

    for( int i = 0; i < numPins(); i++ )
        if( Pin *p = pin( i ) ) p->setIsAdmit( true );

    initPins();

    setBaseUnit( "H" );

    // Zero in the initialiser list and one here, for the reason the Diode's
    // constructor gives.
    setInduct( 1 );

    setLabelX( -16 );
    setLabelY( -24 );
    setLabelRot( 0 );

    setValLabelX( -16 );
    setValLabelY( 10 );
    setValLabRot( 0 );
    setShowVal( true );
}

void Inductor::setInduct( double l )
{
    if( l < IND_FLOOR ) l = IND_FLOOR;

    // In this order, and the henrys come out of getmultValue() rather than from
    // the argument: setValue() picks the prefix that puts the displayed number in
    // [1,1000), so 0.001 typed into a " H" field arrives here as 1 mH and the
    // element wants the product setValue() left behind.
    setValue( l );

    const double h = getmultValue();
    if( h == m_ind ) return;

    m_ind = h;

    updateAdmit();
}

void Inductor::setResist( double r )
{
    // No floor above zero: a winding resistance of exactly zero is what an ideal
    // inductor is, and the companion conductance handles it - dt/(L+0) = dt/L.
    if( r < 0 ) r = 0;
    if( r == m_res ) return;

    m_res = r;

    updateAdmit();
}

void Inductor::setUnit( const QString &un )
{
    // Component's.  There is no TwoPin in this class's ancestry - see the header
    // for why an inductor is not a two-terminal part - and the winding resistance
    // is a bare ohmage the prefix does not apply to, so the only thing a prefix
    // change can move is the inductance.
    Component::setUnit( un );

    m_ind = getmultValue();
    if( m_ind < IND_FLOOR ) m_ind = IND_FLOOR;

    updateAdmit();
}

void Inductor::updateAdmit()
{
    const double dt = simuDt();

    // L + R*dt is at least IND_FLOOR, so the division cannot be by zero, but the
    // test stays: dt comes from the Simulator and a part built before one exists
    // is the normal case rather than the exceptional one.
    const double denom = m_ind + m_res*dt;

    m_admit = ( denom > 0 ) ? dt/denom : 0;
}

double Inductor::pinVolt() const
{
    Pin *a = lPin();
    Pin *b = rPin();
    if( !a || !b ) return 0;

    return a->getVolt() - b->getVolt();
}

void Inductor::stamp()
{
    if( !stampConductance( lPin(), rPin(), m_admit ) ) return;

    // v = L di/dt + R i, backward Euler, rearranged for the current this step:
    //
    //   i_n = dt/(L+R*dt) * v_n  +  L/(L+R*dt) * i_{n-1}
    //
    // The first term is the conductance already stamped; the second is the memory
    // and rides in the companion source.  In the i = g*v - is form the stamp
    // idiom uses, is is the negative of it, and is goes onto the right hand side
    // positive into the node - so -mem at lPin and +mem at rPin.  The sign is
    // worth checking against a series L across a source: mem is positive when the
    // current already flows l to r, the right hand side at lPin goes negative,
    // and the solve gives i_n = G*v + mem, which grows.  That is an inductor
    // building up its field, which is the behaviour being modelled.
    const double dt    = simuDt();
    const double denom = m_ind + m_res*dt;
    const double mem   = ( denom > 0 ) ? m_ind*m_curr/denom : 0;

    lPin()->stampCurrent( -mem );
    rPin()->stampCurrent(  mem );
}

void Inductor::updateStep()
{
    // Not stamp(), and for the reason CapacitorBase::updateStep() gives: the
    // matrix is zeroed before the next round of stamps, so anything written from
    // here would be wiped before it was solved.  All this does is integrate -
    // take the voltage the solve just produced and advance the winding current,
    // which is the state the next step's memory term is built from.
    const double dt    = simuDt();
    const double denom = m_ind + m_res*dt;
    if( denom <= 0 ) return;

    m_curr = ( dt*pinVolt() + m_ind*m_curr )/denom;
}

void Inductor::resetStep()
{
    // dt can change under an inductor the same way it can under a capacitor: a
    // .simu carries its own simuRate and applies it after the components exist,
    // and the Simulator calls this on every element of its reactive list from
    // setSimuRate() and from startSim().
    updateAdmit();

    // And Start means from rest, so the field goes with it.
    m_curr = 0;
}

void Inductor::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                      QWidget *widget )
{
    Component::paint( painter, option, widget );

    // Four humps, which is the IEC winding and what upstream draws.  Each is the
    // top half of a six-unit arc, so the four together span the twenty-four the
    // area was sized for and meet the leads the pins draw in from +-16.
    for( int i = 0; i < 4; i++ )
        painter->drawArc( QRectF( -12 + 6*i, -5, 6, 10 ), 0, 180*16 );
}

Component* createInductor( QObject *parent, const QString &type, const QString &id )
{
    return new Inductor( parent, type, id );
}

// ---------------------------------------------------------------------------
// VoltReg
// ---------------------------------------------------------------------------

// The input's leakage, a gigaohm to the ground pin.  Not zero, and not because
// the part really draws a nanoamp: an input pin that declared isAdmit() and
// stamped nothing would leave its node's matrix row empty, which is the singular
// matrix switches.h warns about.  This keeps the row populated for the same price
// ACT_OPEN_ADMIT does elsewhere.
#define VREG_IN_ADMIT 1e-9

VoltReg::VoltReg( QObject *parent, const QString &type, const QString &id )
        : Component( parent, type, id )
        , m_volt( 0 )
        , m_dropout( 2.0 )
{
    // The TO-220 outline: a wide body with the ground tab hanging below it, which
    // is how the part is drawn on every datasheet and how upstream draws it.
    setArea( QRectF( -20, -14, 40, 26 ) );

    addPin( -24,  0, "inputpin",  180 );
    addPin(  24,  0, "outputpin",   0 );
    addPin(   0, 16, "groundpin",  90 );

    Pin *in  = inPin();
    Pin *out = outPin();
    Pin *gnd = gndPin();

    if( in  ) in->setIsAdmit( true );
    if( gnd ) gnd->setIsAdmit( true );

    if( out )
    {
        // Driven and not a load, so an output with nothing on it stays a single
        // node and takes its voltage straight off the pin - see e-node.h.
        out->setIsOutput( true );
        out->setIsAdmit( false );
    }
    initPins();

    setBaseUnit( "V" );
    setVolt( 5 );
    setShowVal( true );

    // Both labels above the body: the ground pin and its lead own everything
    // below it, and a value label there would sit on the wire.
    setLabelX( -16 );
    setLabelY( -40 );
    setLabelRot( 0 );

    setValLabelX( -16 );
    setValLabelY( -24 );
    setValLabRot( 0 );
}

void VoltReg::setVolt( double v )
{
    setValue( v );

    m_volt = getmultValue();

    // The output pin carries the level for the single-node path, and a stopped
    // circuit still has to show the state it is in - VoltageBase::applyOut()
    // exists for the same reason.
    if( Pin *out = outPin() ) out->setVoltOut( m_volt );

    update();
}

void VoltReg::setUnit( const QString &un )
{
    Component::setUnit( un );

    // The prefix moved under m_value, so the electrical value has to be retaken
    // from the product.
    m_volt = getmultValue();
}

void VoltReg::stamp()
{
    Pin *in  = inPin();
    Pin *out = outPin();
    Pin *gnd = gndPin();
    if( !in || !out || !gnd ) return;

    // The input senses and does not supply: a gigaohm to the ground pin, so the
    // part draws no current it was not asked for and the input node still has a
    // populated row.
    stampConductance( in, gnd, VREG_IN_ADMIT );

    const double vgnd = gnd->getVolt();
    const double vin  = in->getVolt() - vgnd;

    // Dropout, and the test is written twice because it is not symmetric.  A
    // regulator needs its input far enough from its output in the direction the
    // output sits: a 78xx needs vin above vout, a 79xx needs it below.  Folding
    // the two into one comparison on |vin| would let a negative rail pass a test
    // it should fail.  Inside the dropout the output follows the input down, but
    // never past zero relative to its own ground pin - a real part's pass
    // transistor stops conducting there rather than reversing.
    double vout = m_volt;

    if( m_volt >= 0 )
    {
        const double head = vin - m_dropout;
        if( head < vout ) vout = ( head > 0 ) ? head : 0;
    }
    else
    {
        const double head = vin + m_dropout;
        if( head > vout ) vout = ( head < 0 ) ? head : 0;
    }

    // The ground pin unwired is the sheet's reference, not an open circuit.
    // Without this the two-terminal stamp below would decline - stampConductance
    // refuses a pair with a dangling end, and an unwired pin's node has one pin
    // and no matrix row - and a regulator dropped onto a sheet and connected only
    // at its input and output would drive nothing at all.  That reads as a part
    // that has stopped working rather than as a wiring mistake, which is the same
    // argument Potentiometer::stamp() makes for its unwired wiper.
    if( !gnd->connector() )
    {
        out->setIsOutput( true );
        stampSource( out, vout, ESOURCE_IMPED );
        return;
    }

    // The output is a source between itself and the ground pin, so the pin has to
    // carry the level relative to the sheet's reference for solveSingle() to read
    // when nothing loads it.
    out->setIsOutput( true );
    out->setVoltOut( vgnd + vout );

    // Stiff conductance rather than a true voltage-source row, exactly as
    // VoltSource::stamp() does and for the same reason: at 1e-3 ohm the error
    // against any load a user places is below what the sheet displays and the
    // matrix stays square in the node voltages alone.  The ground pin is the
    // negative terminal and the output the positive one, so the right hand side
    // is -G*V at the ground and +G*V at the output.
    const double g = 1/ESOURCE_IMPED;

    if( !stampConductance( gnd, out, g ) ) return;

    const double i = g*vout;

    gnd->stampCurrent( -i );
    out->stampCurrent(  i );
}

void VoltReg::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                     QWidget *widget )
{
    Component::paint( painter, option, widget );

    painter->drawRoundedRect( QRectF( -20, -14, 40, 26 ), 2, 2 );

    // The mounting tab, which is the line that makes this read as a TO-220 rather
    // than as an op-amp in a box.
    painter->drawLine( QPointF( -20, -8 ), QPointF( 20, -8 ) );
}

Component* createVoltReg( QObject *parent, const QString &type, const QString &id )
{
    return new VoltReg( parent, type, id );
}

// ---------------------------------------------------------------------------
// OpAmp
// ---------------------------------------------------------------------------

// The two inputs' bias path, a gigaohm each to the reference.  A real op-amp
// draws nanoamps and a model that draws nothing leaves the input node's row
// depending entirely on what else happens to be wired to it.
#define OPAMP_IN_ADMIT 1e-9

// What an unwired supply pin is taken to be.  The clamp has to be against
// something, and an unwired pin reports zero - which would make every textbook
// circuit built around one of these, none of which bother to draw two rails onto
// the sheet, an amplifier with an output of exactly zero.
#define OPAMP_RAIL 5.0

OpAmp::OpAmp( QObject *parent, const QString &type, const QString &id )
      : Component( parent, type, id )
      , m_gain( 0 )
{
    setArea( QRectF( -20, -20, 40, 40 ) );

    addPin(  24,   0, "outpin",      0 );
    addPin( -24,  -8, "invpin",    180 );
    addPin( -24,   8, "noninvpin", 180 );
    addPin(   0, -24, "vccpin",    270 );
    addPin(   0,  24, "veepin",     90 );

    Pin *out = outPin();
    if( out )
    {
        out->setIsOutput( true );
        out->setIsAdmit( false );
    }

    // The inputs load their nodes, which costs the single-node fast path on any
    // net one of them touches and buys a bias path that does not depend on what
    // else is wired there.  The supply pins do not: they sense, and a net made
    // only of a rail and a supply pin stays single.
    if( Pin *p = invPin()    ) p->setIsAdmit( true );
    if( Pin *p = noninvPin() ) p->setIsAdmit( true );

    initPins();

    setGain( 1e5 );

    setLabelX( -16 );
    setLabelY( -40 );
    setLabelRot( 0 );
}

void OpAmp::setGain( double g )
{
    // Both ends clamped.  A gain below one is an attenuator and the property is
    // not the place to build one; above 1e9 the output is at a rail for any input
    // difference at all, the clamp does all the work, and the iteration converges
    // in one pass because there is nothing left to converge on.
    if( g <   1 ) g =   1;
    if( g > 1e9 ) g = 1e9;

    if( g == m_gain ) return;

    m_gain = g;
}

void OpAmp::stamp()
{
    Pin *out = outPin();
    Pin *inv = invPin();
    Pin *non = noninvPin();
    if( !out || !inv || !non ) return;

    // A conductance to the reference, and no off-diagonal partner: these pins
    // have nothing on the other side of them.
    inv->stampAdmitance( OPAMP_IN_ADMIT );
    non->stampAdmitance( OPAMP_IN_ADMIT );

    // The rails, read off the pins so that the clamp follows whatever the sheet
    // actually put there - a part powered from a 3.3 V rail limits to 3.3 V and
    // not to a number written into this file.
    double hi =  OPAMP_RAIL;
    double lo = -OPAMP_RAIL;

    if( Pin *p = vccPin() ) if( p->connector() ) hi = p->getVolt();
    if( Pin *p = veePin() ) if( p->connector() ) lo = p->getVolt();

    // A sheet wired the other way round is a mistake rather than a request, and
    // leaving it alone would clamp the output to an empty interval - hi below lo
    // makes both tests fire and the output follow whichever ran last.
    if( hi < lo ) qSwap( hi, lo );

    double v = m_gain*( non->getVolt() - inv->getVolt() );

    if( v > hi ) v = hi;
    if( v < lo ) v = lo;

    // Single-ended, against the sheet's reference rather than as a VCVS between
    // the output and an input: one matrix row more, and an output driving a logic
    // input and nothing else never enters the matrix at all.
    out->setIsOutput( true );
    stampSource( out, v, ESOURCE_IMPED );
}

void OpAmp::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                   QWidget *widget )
{
    Component::paint( painter, option, widget );

    // The triangle, pointing at the output.
    QPainterPath tri;
    tri.moveTo( -12, -16 );
    tri.lineTo(  14,   0 );
    tri.lineTo( -12,  16 );
    tri.closeSubpath();
    painter->drawPath( tri );

    // Leads out to the five pins.  The pin stubs reach seven units in from the
    // terminals, so each of these overlaps the stub it is meeting and the symbol
    // is continuous at any view scale.
    painter->drawLine( QPointF( -12, -8 ), QPointF( -20,  -8 ) );   // inverting
    painter->drawLine( QPointF( -12,  8 ), QPointF( -20,   8 ) );   // non-inverting
    painter->drawLine( QPointF(  14,  0 ), QPointF(  20,   0 ) );   // output

    // The supplies come off the sloping edges at x=-4, where the triangle is
    // eleven units from the axis.
    painter->drawLine( QPointF( -4, -11 ), QPointF( -4, -20 ) );    // V+
    painter->drawLine( QPointF( -4,  11 ), QPointF( -4,  20 ) );    // V-
}

Component* createOpAmp( QObject *parent, const QString &type, const QString &id )
{
    return new OpAmp( parent, type, id );
}

// ---------------------------------------------------------------------------
// Mosfet
// ---------------------------------------------------------------------------

Mosfet::Mosfet( QObject *parent, const QString &type, const QString &id )
        : Component( parent, type, id )
        , m_vth( 2.0 )
        , m_rds( 0 )
        , m_k( 0 )
        , m_pChan( false )
{
    setArea( QRectF( -20, -20, 40, 40 ) );

    addPin( -24,   0, "gatepin",   180 );
    addPin(   0, -24, "drainpin",  270 );
    addPin(   0,  24, "sourcepin",  90 );

    // The gate presents nothing at all to its node, which is the DC behaviour the
    // part actually has, so it does not declare isAdmit(): a gate driven straight
    // from a logic output stays a single node and never reaches the matrix.  The
    // channel is a finite conductance whether it is on or off, so the other two
    // do declare it - and the off state stamps the floor rather than nothing, for
    // the reason switches.h gives at length.
    if( Pin *p = drainPin()  ) p->setIsAdmit( true );
    if( Pin *p = sourcePin() ) p->setIsAdmit( true );

    initPins();

    setBaseUnit( "Ω" );
    setRdsOn( 0.1 );
}

void Mosfet::setPChannel( bool p )
{
    if( m_pChan == p ) return;

    m_pChan = p;

    // The arrow on the channel turns round, which is the only thing on the symbol
    // that says which way the part conducts.
    update();
}

void Mosfet::setThreshold( double v )
{
    // Clamped to something a gate on the sheet can actually reach.  A threshold
    // of a kilovolt is a part that never turns on, and it would not be obvious
    // from the panel that the number was the reason.
    if( v < -100 ) v = -100;
    if( v >  100 ) v =  100;

    if( v == m_vth ) return;

    m_vth = v;
}

void Mosfet::setRdsOn( double r )
{
    if( r < RES_FLOOR ) r = RES_FLOOR;

    setValue( r );

    const double ohms = getmultValue();
    if( ohms == m_rds ) return;

    m_rds = ohms;

    updateK();
}

void Mosfet::setUnit( const QString &un )
{
    Component::setUnit( un );

    m_rds = getmultValue();
    if( m_rds < RES_FLOOR ) m_rds = RES_FLOOR;

    updateK();
}

void Mosfet::updateK()
{
    // k = 1/(RDSon*VOV_REF), which fixes what RDSon means: at one volt of
    // overdrive the knee current is k*VOV_REF^2 = 1/RDSon, so the channel passes
    // an amp per ohm of the number in the panel.  Without a reference overdrive
    // the property would be an ohmage with no operating point attached to it.
    m_k = 1/( m_rds*MOSFET_OV_REF );
}

void Mosfet::stamp()
{
    // The pins are pg/pd/ps and not g/d/s: g is the conductance the stamp pair
    // below is called, which is the name every other stamp() in this file uses
    // for it.
    Pin *pg = gatePin();
    Pin *pd = drainPin();
    Pin *ps = sourcePin();
    if( !pg || !pd || !ps ) return;

    // The channel is symmetric - it is a piece of doped silicon with a field
    // across it and does not know which terminal the symbol calls the drain - so
    // the terminal at the higher potential is the one the current enters.  Taking
    // that from the solved voltages rather than from the pin names is what lets the
    // part conduct backwards, which a MOSFET used as a switch does every time the
    // load is inductive, and it keeps the square law in the branch it is written
    // for instead of being asked about a negative Vds.
    //
    // For a P-channel the roles reverse: the current enters the source, and the
    // gate threshold is measured the other way round.
    Pin *ph = m_pChan ? ps : pd;
    Pin *pl = m_pChan ? pd : ps;

    double vh = ph->getVolt();
    double vl = pl->getVolt();

    if( vh < vl )
    {
        qSwap( ph, pl );
        qSwap( vh, vl );
    }

    const double vgs = m_pChan ? ( vh - pg->getVolt() ) : ( pg->getVolt() - vl );
    const double vds = vh - vl;
    const double vov = vgs - m_vth;

    // Off, triode and saturation, as the pair (g, is) the stamp idiom wants.
    //
    //   triode      i = k*(2*Vov*Vds - Vds^2)     g = di/dVds = 2k*(Vov - Vds)
    //   saturation  i = k*Vov^2                   g falls away to the floor
    //
    // They meet at Vds = Vov: the triode conductance reaches zero there and the
    // triode current reaches k*Vov^2, which is the saturation current.  That
    // continuity is what keeps the Newton iteration from bouncing between the two
    // regions, and it is the reason the saturation branch carries its whole
    // current in the companion source rather than in the conductance.
    double g  = ACT_OPEN_ADMIT;
    double is = 0;

    if( vov > 0 )
    {
        if( vds >= vov )
        {
            // is = g*Vds - i, with g at the floor: the source is doing nearly all
            // of the work and the conductance is only there to keep the row
            // populated.
            is = ACT_OPEN_ADMIT*vds - m_k*vov*vov;
        }
        else
        {
            g  = 2*m_k*( vov - vds );
            is = -m_k*vds*vds;

            if( g < ACT_OPEN_ADMIT ) g = ACT_OPEN_ADMIT;
        }
    }

    if( !stampConductance( ph, pl, g ) ) return;

    ph->stampCurrent(  is );
    pl->stampCurrent( -is );
}

void Mosfet::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                    QWidget *widget )
{
    Component::paint( painter, option, widget );

    // The gate plate, separated from the channel bar: the gap between the two
    // lines is the oxide, and it is what the symbol has to say about a MOSFET -
    // the gate drives the bar and draws nothing from it.
    painter->drawLine( QPointF( -8, -10 ), QPointF( -8, 10 ) );
    painter->drawLine( QPointF( -3, -10 ), QPointF( -3, 10 ) );

    painter->drawLine( QPointF( -20,  0 ), QPointF(  -8,   0 ) );   // gate lead
    painter->drawLine( QPointF(  -3, -8 ), QPointF(   0,  -8 ) );
    painter->drawLine( QPointF(   0, -8 ), QPointF(   0, -20 ) );   // drain
    painter->drawLine( QPointF(  -3,  8 ), QPointF(   0,   8 ) );
    painter->drawLine( QPointF(   0,  8 ), QPointF(   0,  20 ) );   // source

    // The arrow across the gap, into the bar for an N-channel and out of it for a
    // P-channel, which is the only mark that says which way the part conducts.
    if( m_pChan )
    {
        painter->drawLine( QPointF( -8, 0 ), QPointF( -4, -3 ) );
        painter->drawLine( QPointF( -8, 0 ), QPointF( -4,  3 ) );
    }
    else
    {
        painter->drawLine( QPointF( -3, 0 ), QPointF( -7, -3 ) );
        painter->drawLine( QPointF( -3, 0 ), QPointF( -7,  3 ) );
    }
}

Component* createMosfet( QObject *parent, const QString &type, const QString &id )
{
    return new Mosfet( parent, type, id );
}

// ---------------------------------------------------------------------------
// BJT
// ---------------------------------------------------------------------------

BJT::BJT( QObject *parent, const QString &type, const QString &id )
    : Component( parent, type, id )
    , m_gain( 0 )
    , m_ib( 0 )
    , m_isat( 0 )
    , m_pnp( false )
    , m_vlin( 0 )
{
    setArea( QRectF( -14, -20, 28, 40 ) );

    addPin( -24,   0, "basepin",      180 );
    addPin(   0, -24, "collectorpin", 270 );
    addPin(   0,  24, "emitterpin",    90 );

    // All three present a finite admittance: the base-emitter junction always
    // does, and the collector-emitter branch carries the floor conductance even
    // when it is a pure current source, so a node with only a collector on it
    // still has a row with something in it.
    for( int i = 0; i < numPins(); i++ )
        if( Pin *p = pin( i ) ) p->setIsAdmit( true );

    initPins();

    // The saturation current that puts BJT_IB_ON through the base-emitter
    // junction at BJT_VBE_ON.  Computed rather than a constant so that the two
    // defines stay the single place the silicon's numbers are written down.
    m_isat = BJT_IB_ON/( qExp( BJT_VBE_ON/VT_300K ) - 1 );

    setGain( 100 );

    setLabelX( -16 );
    setLabelY( -40 );
    setLabelRot( 0 );
}

void BJT::setPnp( bool p )
{
    if( m_pnp == p ) return;

    m_pnp = p;

    // The emitter arrow turns round, and so does the sense of everything the model
    // computes, so a part switched while the simulation is running converges from
    // its old operating point rather than from zero.
    m_vlin = 0;
    m_ib   = 0;

    update();
}

void BJT::setGain( double g )
{
    // Clamped below at one, because a gain under unity is not a transistor and a
    // negative one is a current source pointing the wrong way - which the
    // saturation clamp below would then meet with a negative ic and would not
    // know what to do with.
    if( g <   1 ) g =   1;
    if( g > 1e6 ) g = 1e6;

    if( g == m_gain ) return;

    m_gain = g;
}

void BJT::stamp()
{
    Pin *b = basePin();
    Pin *c = collectorPin();
    Pin *e = emitterPin();
    if( !b || !c || !e ) return;

    // A PNP is the same device with every voltage negated, so the model below is
    // written once and the two pins each branch is stamped between are exchanged
    // rather than the arithmetic being duplicated with the signs flipped.  "p" is
    // the terminal the current enters and "n" the one it leaves, in device terms:
    // for an NPN the base-emitter junction is forward-biased with the base
    // positive, and for a PNP with the emitter positive.
    Pin *jp = m_pnp ? e : b;
    Pin *jn = m_pnp ? b : e;
    Pin *cp = m_pnp ? e : c;
    Pin *cn = m_pnp ? c : e;

    // Walk the base-emitter linearisation point, which is Diode::stamp() under
    // another name and for the reason given on m_vlin.
    double vbe = jp->getVolt() - jn->getVolt();

    if( vbe > m_vlin + DIODE_VLIM ) vbe = m_vlin + DIODE_VLIM;
    if( vbe < m_vlin - DIODE_VLIM ) vbe = m_vlin - DIODE_VLIM;

    m_vlin = vbe;

    double gbe = 0, isbe = 0, ib = 0;
    lineariseJunction( m_vlin, m_isat, VT_300K, gbe, isbe, &ib );

    m_ib = ib;

    if( stampConductance( jp, jn, gbe ) )
    {
        jp->stampCurrent(  isbe );
        jn->stampCurrent( -isbe );
    }

    // Collector-emitter.  Above the knee it is a current source of Gain*Ib with
    // the floor conductance beside it, so the collector's row is never empty;
    // below it the device is a resistance of Vce_sat/(Gain*Ib), which carries
    // exactly Gain*Ib at the knee - so the two branches meet continuously - and
    // carries proportionally less below it, which is what stops an unclamped CCCS
    // from driving the collector past the emitter when the base asks for more
    // current than the external circuit can supply.
    const double ic  = m_gain*ib;
    const double vce = cp->getVolt() - cn->getVolt();

    double gce  = ACT_OPEN_ADMIT;
    double isce = 0;

    if( ic > 0 && vce < BJT_VCE_SAT )
    {
        gce = ic/BJT_VCE_SAT;
        if( gce < ACT_OPEN_ADMIT ) gce = ACT_OPEN_ADMIT;

        // i = g*v with no source alongside it.
        isce = 0;
    }
    else
    {
        // i = g*v - is, with the whole of the current in is.
        isce = gce*vce - ic;
    }

    if( stampConductance( cp, cn, gce ) )
    {
        cp->stampCurrent(  isce );
        cn->stampCurrent( -isce );
    }
}

void BJT::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                 QWidget *widget )
{
    Component::paint( painter, option, widget );

    painter->drawEllipse( QRectF( -12, -12, 24, 24 ) );

    // The base bar, and the two junctions sloping out of it to the collector and
    // the emitter - the standard symbol, in which the bar is the base and the
    // arrow is on the emitter.
    painter->drawLine( QPointF( -6, -8 ), QPointF( -6,  8 ) );
    painter->drawLine( QPointF( -20, 0 ), QPointF( -6,  0 ) );      // base lead

    painter->drawLine( QPointF( -6, -5 ), QPointF(  0, -12 ) );
    painter->drawLine( QPointF(  0, -12 ), QPointF( 0, -20 ) );     // collector
    painter->drawLine( QPointF( -6,  5 ), QPointF(  0,  12 ) );
    painter->drawLine( QPointF(  0,  12 ), QPointF( 0,  20 ) );     // emitter

    // The emitter arrow, away from the base for an NPN and towards it for a PNP,
    // which is the mark that says which way the conventional current goes.
    const int dir = m_pnp ? -1 : 1;

    painter->drawLine( QPointF( 0, 14 + 2*dir ), QPointF( -4, 14 - 2*dir ) );
    painter->drawLine( QPointF( 0, 14 + 2*dir ), QPointF(  4, 14 - 2*dir ) );
}

Component* createBJT( QObject *parent, const QString &type, const QString &id )
{
    return new BJT( parent, type, id );
}

// ---------------------------------------------------------------------------
// MuxAnalog
// ---------------------------------------------------------------------------

// The address pins' logic threshold.  Half of five volts, which is the midpoint a
// TTL-level address sits either side of and the figure the rest of this library
// uses where a logic level has to become a boolean.
#define MUX_VTH 2.5

// Ceiling on the channel count.  Sixteen is four address pins, which is as wide
// as the body can be drawn with the channels eight units apart and still fit on a
// sheet next to anything else; past it the property is not a mux any more.
#define MUX_MAX_CHANNELS 16

MuxAnalog::MuxAnalog( QObject *parent, const QString &type, const QString &id )
          : Component( parent, type, id )
          , m_channels( 0 )
          , m_addrPins( 0 )
          , m_ohms( 0 )
{
    // Zero first, so that setChannels(2) below reads as growth from nothing
    // rather than as a resize of a mux that already has channels in it - the same
    // reason ResistorDip zeroes m_size before sizing itself.
    setChannels( 2 );

    setBaseUnit( "Ω" );
    setImpedance( 100 );

    setLabelX( -16 );
    setLabelY( -40 );
    setLabelRot( 0 );

    setValLabelX( 20 );
    setValLabelY( -20 );
    setValLabRot( 0 );
    setShowVal( true );
}

void MuxAnalog::setChannels( int n )
{
    if( n < 1 ) n = 1;
    if( n > MUX_MAX_CHANNELS ) n = MUX_MAX_CHANNELS;
    if( n == m_channels ) return;

    m_channels = n;

    // The address width follows the channel count, so a resize rebuilds every pin
    // rather than adding or dropping a few.  It is destructive in the way a Chip's
    // package change is: the pins that carried the wires are not the pins that come
    // back.
    buildPins();

    // The node graph now holds pins that no longer exist, or none for pins that
    // do.  updateNodes() rebuilds it whole and pauses the simulation itself while
    // it does, because a step in flight would be walking the eNodes being freed.
    if( Circuit *circ = circuit() ) circ->updateNodes();
}

void MuxAnalog::buildPins()
{
    Circuit *circ = circuit();

    clearPins();

    // log2 of the channel count, rounded up - and zero for a single channel,
    // which needs no address at all.  Written as a shift rather than as a
    // logarithm so that no floating point is involved in a pin count.
    m_addrPins = 0;
    while( ( 1 << m_addrPins ) < m_channels ) m_addrPins++;

    // The channels are centred on the axis the common sits on, so the body grows
    // symmetrically about y=0 and the common lead stays where it is whatever the
    // channel count is.
    const int half = ( m_channels-1 )*4;

    addPin( 24, 0, "commonpin", 0 );

    for( int i = 0; i < m_channels; i++ )
    {
        Pin *p = addPin( -24, -half + 8*i,
                         "channelpin" + QString::number( i ), 180 );
        if( p ) p->setIsAdmit( true );
    }

    for( int i = 0; i < m_addrPins; i++ )
    {
        // Spread along the bottom of the body, centred the same way the channels
        // are.  No isAdmit(): an address is a logic level and presents nothing to
        // the net driving it, which is what lets a mux hang off a counter without
        // dragging the counter's outputs into the matrix.
        const int x = ( 2*i - ( m_addrPins-1 ) )*8;

        addPin( x, half + 16, "addresspin" + QString::number( i ), 90 );
    }

    if( Pin *p = commonPin() ) p->setIsAdmit( true );

    // The sheet's pin map, which Component::addPin() cannot fill because a
    // component's constructor runs before it is on a sheet - and which has to be
    // filled here for the same reason ResistorDip::addSegments() fills it: a pin
    // that is not in the map is invisible to a connector naming it.
    if( circ )
        for( int i = 0; i < numPins(); i++ )
            if( Pin *p = pin( i ) ) circ->addPin( p );

    initPins();

    setArea( QRectF( -16, -half - 8, 32, 2*half + 16 ) );
}

void MuxAnalog::setImpedance( double r )
{
    if( r < RES_FLOOR ) r = RES_FLOOR;

    setValue( r );

    const double ohms = getmultValue();
    if( ohms == m_ohms ) return;

    m_ohms = ohms;
}

void MuxAnalog::setUnit( const QString &un )
{
    Component::setUnit( un );

    m_ohms = getmultValue();
    if( m_ohms < RES_FLOOR ) m_ohms = RES_FLOOR;
}

int MuxAnalog::selected() const
{
    int sel = 0;

    for( int i = 0; i < m_addrPins; i++ )
    {
        Pin *p = addressPin( i );
        if( p && p->getVolt() > MUX_VTH ) sel |= ( 1 << i );
    }

    // An address past the channel count is a file that was written for a wider
    // part, or a counter that has run on.  Clamping to the last channel keeps the
    // stamp inside the pin list rather than reading a null out of addressPin().
    if( sel >= m_channels ) sel = m_channels - 1;
    if( sel <  0 )          sel = 0;

    return sel;
}

void MuxAnalog::stamp()
{
    Pin *com = commonPin();
    if( !com ) return;

    const int sel = selected();

    // Every channel, on every pass, and not only the selected one.  A channel pin
    // declares isAdmit(), so its node has a matrix row whether it is being routed
    // or not, and a row that stamped nothing is a row of zeros - a singular matrix
    // on a circuit that is merely switching a signal.  The unselected ones get the
    // floor admittance, which is the same figure switches.h uses for an open
    // contact and for the same reason.
    const double on = 1/m_ohms;

    for( int i = 0; i < m_channels; i++ )
        stampConductance( com, channelPin( i ), ( i == sel ) ? on : ACT_OPEN_ADMIT );
}

void MuxAnalog::nodesUpdated()
{
    Simulator *sim = Simulator::self();
    if( !sim ) return;

    // Both halves of being notified, and the order does not matter because both
    // are idempotent: the Simulator's list is what settleLogic() walks to find
    // flagged elements, and the node's list is what notifyFast() walks to flag
    // them.  Either one alone is a device that never reacts.
    sim->addToChangedFast( this );

    // The eNodes this was registered against on the last rebuild are gone - see
    // Component::nodesUpdated() - so there is nothing to unregister from, and the
    // registration has to be made again every time rather than once.
    for( int i = 0; i < m_addrPins; i++ )
    {
        Pin *p = addressPin( i );
        if( !p ) continue;

        if( eNode *nod = p->getEnode() ) nod->addToChangedFast( this );
    }
}

void MuxAnalog::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                      QWidget *widget )
{
    Component::paint( painter, option, widget );

    painter->drawRoundedRect( m_area, 2, 2 );

    // The channel count and the address width, written on the body: a mux is the
    // one part in this file whose pin count is a property, so the symbol has to
    // say what it currently is or the sheet gives no way to tell a 2:1 from an 8:1
    // without counting terminals.
    const QString text = QString::number( m_channels ) + ":1";

    painter->setPen( QPen( QColor( 0, 0, 0 ), 1 ) );
    painter->drawText( m_area, Qt::AlignCenter, text );
}

Component* createMuxAnalog( QObject *parent, const QString &type, const QString &id )
{
    return new MuxAnalog( parent, type, id );
}
