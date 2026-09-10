/*
 * SimulIDE, ported to EwokOS - see sources.h.
 */

#include <QtMath>

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

#include "sources.h"

#include "circuit.h"
#include "pin.h"
#include "twopin.h"

#include "e-source.h"
#include "simulator.h"

// The internal impedance every ideal source here presents.  e-source.h explains the
// choice of 1e-3 ohm over upstream's sub-microohm: it is indistinguishable from
// ideal against any load a user places and keeps the matrix condition number
// inside what a LU factorisation in double can hold.
#define SRC_IMPED ESOURCE_IMPED

// What an off source presents instead of an open circuit.  A node whose only
// attachment is a turned-off source would otherwise have an entirely empty
// matrix row, and the solve would fail on a circuit that is merely switched off.
#define SRC_OFF_IMPED 1e30

// ---- VoltageBase ---------------------------------------------------------------

VoltageBase::VoltageBase( QObject *parent, const QString &type, const QString &id )
           : Component( parent, type, id )
           , m_imped( SRC_IMPED )
           , m_volt( 0 )
           , m_out( true )
{
    setBaseUnit( "V" );

    setLabelX( -16 );
    setLabelY( -24 );
    setLabelRot( 0 );

    setValLabelX( -16 );
    setValLabelY( 8 );
    setValLabRot( 0 );
}

void VoltageBase::setVolt( double v )
{
    // Through Component::setValue(), which renormalises the prefix, so typing
    // 3300 into a " V" field reads back as 3.3 kV and a file carrying
    // Voltage="100" Unit=" mV" loads as a tenth of a volt whichever of the two
    // attributes the reader meets first.
    setValue( v );

    m_volt = getmultValue();

    applyOut();
}

void VoltageBase::setUnit( const QString &un )
{
    Component::setUnit( un );

    // The prefix moved under m_value, so the electrical value has to be retaken
    // from the product.  Not doing this is the bug that shows up as a rail whose
    // label says 5 mV and whose node says 5 V.
    m_volt = getmultValue();

    applyOut();
}

void VoltageBase::setOut( bool out )
{
    if( m_out == out ) return;

    m_out = out;

    applyOut();
}

void VoltageBase::applyOut()
{
    // The pin carries the level whether the matrix is about to read it or not.
    // A stopped circuit still has to show the state it is in, and a single node
    // takes its voltage from here rather than from the stamp.
    if( Pin *p = outPin() ) p->setVoltOut( level() );

    update();
}

void VoltageBase::stamp()
{
    Pin *p = outPin();
    if( !p ) return;

    // The output flag has to reach the pin rather than only the stamp: a node
    // classifies as single from isOutput()/isAdmit(), and a source that turned
    // off without saying so would keep driving a node that has stopped being
    // driven.
    p->setIsOutput( m_out );

    if( m_out ) stampSource( p, level(), m_imped );
    else        stampSource( p, 0,        SRC_OFF_IMPED );
}

void VoltageBase::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                         QWidget *widget )
{
    Component::paint( painter, option, widget );
}

// ---- FixedVoltage --------------------------------------------------------------

FixedVoltage::FixedVoltage( QObject *parent, const QString &type, const QString &id )
            : VoltageBase( parent, type, id )
{
    setArea( QRectF( -10, -10, 20, 20 ) );

    addPin( 16, 0, "lPin", 0 );

    Pin *p = outPin();
    if( p )
    {
        // Driven, and not a load: with nothing else on the node the pair makes it
        // single and eNode::solveSingle() takes the voltage straight off the pin.
        p->setIsOutput( true );
        p->setIsAdmit( false );
    }
    initPins();

    setVolt( 5 );
    setShowVal( true );
}

void FixedVoltage::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                          QWidget *widget )
{
    VoltageBase::paint( painter, option, widget );

    painter->drawEllipse( QRectF( -10, -10, 20, 20 ) );

    // The polarity mark, which is the only thing distinguishing a rail from a
    // lamp at a glance on a crowded sheet.
    painter->drawLine( QPointF( -4, 0 ), QPointF( 4, 0 ) );
    painter->drawLine( QPointF( 0, -4 ), QPointF( 0, 4 ) );
}

// ---- Ground --------------------------------------------------------------------

Ground::Ground( QObject *parent, const QString &type, const QString &id )
      : Component( parent, type, id )
{
    setArea( QRectF( -8, -10, 16, 20 ) );

    // Upstream's Ground declares no properties, so it does not derive from
    // VoltageBase: the writer walks the whole meta-object chain and an inherited
    // Voltage would appear in every saved file.
    addPin( 0, -16, "lPin", 270 );

    Pin *p = pin( 0 );
    if( p )
    {
        p->setIsOutput( true );
        p->setIsAdmit( false );
        p->setVoltOut( 0 );
    }
    initPins();
}

void Ground::stamp()
{
    stampSource( pin( 0 ), 0, SRC_IMPED );
}

void Ground::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                   QWidget *widget )
{
    Component::paint( painter, option, widget );

    painter->drawLine( QPointF(  0, -10 ), QPointF( 0, 2 ) );
    painter->drawLine( QPointF( -8,   2 ), QPointF( 8, 2 ) );
    painter->drawLine( QPointF( -5,   6 ), QPointF( 5, 6 ) );
    painter->drawLine( QPointF( -2,  10 ), QPointF( 2, 10 ) );
}

// ---- LogicInput ----------------------------------------------------------------

LogicInput::LogicInput( QObject *parent, const QString &type, const QString &id )
          : VoltageBase( parent, type, id )
{
    setArea( QRectF( -10, -10, 20, 20 ) );

    addPin( 16, 0, "lPin", 0 );

    Pin *p = outPin();
    if( p )
    {
        p->setIsOutput( true );
        p->setIsAdmit( false );
    }
    initPins();

    setVolt( 5 );

    // Off by default: upstream's is, and a sheet of inputs that all start high
    // makes the first thing anyone builds - a gate with two inputs - start in
    // the state that is hardest to see change.
    setOut( false );
}

void LogicInput::mousePressEvent( QGraphicsSceneMouseEvent *event )
{
    if( event->button() == Qt::LeftButton )
    {
        setOut( !m_out );

        // A single node has already been solved and will not be again until
        // something tells the matrix the circuit moved.  Without this the lamp
        // changes but the gate it feeds does not, until the next unrelated edit.
        if( Simulator *sim = Simulator::self() ) sim->setCircChanged();
    }

    // The base still has to see the press, or the symbol can be switched but not
    // dragged - and a component that cannot be moved after it has been clicked
    // once looks like a hang rather than like a missing call.
    Component::mousePressEvent( event );
}

void LogicInput::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                        QWidget *widget )
{
    // The body colour is the state, which is what m_color is for: Component::paint
    // fills with it, so there is no second rectangle to keep in step.
    m_color = m_out ? QColor( 100, 220, 100 ) : QColor( 230, 230, 230 );

    VoltageBase::paint( painter, option, widget );

    painter->drawRoundedRect( QRectF( -10, -10, 20, 20 ), 3, 3 );

    // The digit rather than only the colour, so the state survives a monochrome
    // printout and a user who cannot tell green from grey.
    painter->setPen( QPen( QColor( 0, 0, 0 ), 1 ) );
    painter->setBrush( Qt::NoBrush );
    painter->drawText( QRectF( -10, -10, 20, 20 ), Qt::AlignCenter,
                       m_out ? QLatin1String( "1" ) : QLatin1String( "0" ) );
}

// ---- LogicOutput ---------------------------------------------------------------

LogicOutput::LogicOutput( QObject *parent, const QString &type, const QString &id )
           : Component( parent, type, id )
           , m_lit( false )
{
    setArea( QRectF( -10, -10, 20, 20 ) );

    addPin( -16, 0, "rPin", 180 );

    Pin *p = pin( 0 );
    if( p )
    {
        // High impedance: an indicator must not load what it is indicating, or
        // putting a lamp on a node changes the node.
        p->setIsAdmit( false );
    }
    initPins();
}

void LogicOutput::updateDisplay()
{
    Pin *p = pin( 0 );
    if( !p ) return;

    // 2.5 V is the threshold upstream uses for a 5 V logic family, and it is the
    // right one here too: it is far enough from both rail to survive a driven
    // node that has not finished settling, and close enough to the middle that a
    // level between the two reads as neither.
    const bool lit = ( p->getVolt() > 2.5 );

    if( lit == m_lit ) return;

    m_lit = lit;
    update();
}

void LogicOutput::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                         QWidget *widget )
{
    m_color = m_lit ? QColor( 250, 200, 60 ) : QColor( 230, 230, 230 );

    Component::paint( painter, option, widget );

    painter->drawEllipse( QRectF( -10, -10, 20, 20 ) );
}

// ---- Clock ---------------------------------------------------------------------

Clock::Clock( QObject *parent, const QString &type, const QString &id )
     : VoltageBase( parent, type, id )
     , m_freq( 1 )
     , m_time( 0 )
     , m_alwaysOn( true )
     , m_high( false )
{
    setArea( QRectF( -10, -10, 20, 20 ) );

    addPin( 16, 0, "lPin", 0 );

    Pin *p = outPin();
    if( p )
    {
        p->setIsOutput( true );
        p->setIsAdmit( false );
    }
    initPins();

    setVolt( 5 );
    setFreq( 1 );
}

void Clock::setFreq( double f )
{
    // Bounded at both ends rather than clamped silently to zero.  At the top end
    // the period is 10 ns, which is one hundredth of a step at the default 1 MHz
    // simulation rate: past that the square wave aliases into whatever the step
    // boundary happens to land on, and a clock that appears to run at the wrong
    // frequency is worse than one that refuses to be set there.
    if( f < 1e-6 ) f = 1e-6;
    if( f > 1e5  ) f = 1e5;

    if( m_freq == f ) return;

    m_freq = f;
    m_time = 0;
}

double Clock::level() const
{
    if( !m_alwaysOn && !m_out ) return 0;

    return m_high ? m_volt : 0;
}

void Clock::updateStep()
{
    if( !m_alwaysOn && !m_out ) return;

    const double half = 0.5/m_freq;

    m_time += simuDt();

    if( m_time >= half )
    {
        // Skip whole half-cycles rather than playing them out one step at a
        // time.  A simulation running slower than the clock it drives - a 1 kHz
        // clock on a circuit that only achieves 100 steps a second - has to
        // advance by the time that really elapsed, or the output would lag
        // further behind every step and the measured frequency would be wrong.
        const qint64 halves = (qint64)( m_time/half );

        m_time -= (double)halves*half;

        if( halves & 1 ) m_high = !m_high;
    }

    applyOut();
}

void Clock::resetStep()
{
    m_time = 0;
    m_high = false;

    applyOut();
}

void Clock::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                   QWidget *widget )
{
    VoltageBase::paint( painter, option, widget );

    painter->drawEllipse( QRectF( -10, -10, 20, 20 ) );

    // One cycle of the square wave inside the body, so a Clock is not mistaken
    // for a Fixed Voltage of the same size and colour.
    painter->drawLine( QPointF( -6,  4 ), QPointF( -3,  4 ) );
    painter->drawLine( QPointF( -3,  4 ), QPointF( -3, -4 ) );
    painter->drawLine( QPointF( -3, -4 ), QPointF(  3, -4 ) );
    painter->drawLine( QPointF(  3, -4 ), QPointF(  3,  4 ) );
    painter->drawLine( QPointF(  3,  4 ), QPointF(  6,  4 ) );
}

// ---- WaveGen -------------------------------------------------------------------

WaveGen::WaveGen( QObject *parent, const QString &type, const QString &id )
       : VoltageBase( parent, type, id )
       , m_waveType( "Sine" )
       , m_freq( 1000 )
       , m_base( 0 )
       , m_phase( 0 )
       , m_quality( 100 )
       , m_alwaysOn( true )
{
    setArea( QRectF( -12, -12, 24, 24 ) );

    addPin( 16, 0, "lPin", 0 );

    Pin *p = outPin();
    if( p )
    {
        p->setIsOutput( true );
        p->setIsAdmit( false );
    }
    initPins();

    setVolt( 5 );
    setFreq( 1000 );
    setQuality( 100 );
}

void WaveGen::setWaveType( const QString &t )
{
    m_waveType = t;

    buildTable();
    applyOut();
}

void WaveGen::setQuality( int q )
{
    // Two is the smallest table that can represent any of the shapes at all - a
    // square wave - and a hundred thousand is past the point where the samples
    // are further apart than a step.
    if( q < 2      ) q = 2;
    if( q > 100000 ) q = 100000;

    if( m_quality == q ) return;

    m_quality = q;

    buildTable();
}

void WaveGen::setFreq( double f )
{
    if( f < 1e-6 ) f = 1e-6;
    if( f > 1e5  ) f = 1e5;

    if( m_freq == f ) return;

    m_freq = f;
    m_phase = 0;
}

// The shapes, defined rather than assumed.  `ph` is the position in the period
// in [0,1) and `s` the normalised output, which buildTable() scales by the
// amplitude and offsets by Volt_Base:
//
//   Sine      sin(2*pi*ph)                        continuous, zero at ph=0
//   Triangle  four linear segments                continuous, zero at ph=0,
//             0 -> +1 -> 0 -> -1 -> 0             peaks at ph=0.25 and 0.75
//   Saw       2*ph - 1                            rising ramp with one
//                                                 discontinuity, -1 -> +1
//   Ramp      ph                                  rising, unipolar 0 -> 1
//   Square    +1 for ph < 0.5, else -1            two discontinuities
//
// Sine and Triangle start at zero so a generator and a clock agree about where
// a period begins, which is what makes a phase measurement between two of them
// mean something.
static double waveShape( const QString &type, double ph )
{
    if( type == QLatin1String( "Square" ) )
        return ( ph < 0.5 ) ? 1.0 : -1.0;

    if( type == QLatin1String( "Triangle" ) )
    {
        if( ph < 0.25 ) return  4.0*ph;
        if( ph < 0.75 ) return  2.0 - 4.0*ph;
        return 4.0*ph - 4.0;
    }
    if( type == QLatin1String( "Saw" )  ) return 2.0*ph - 1.0;
    if( type == QLatin1String( "Ramp" ) ) return ph;

    return qSin( 2.0*M_PI*ph );
}

void WaveGen::buildTable()
{
    // The amplitude is m_volt - the Voltage property - and the offset m_base, so
    // the table holds finished volts and updateStep() does nothing but index it.
    // Rebuilt rather than computed per step for the reason Quality exists.
    const double amp = m_volt;

    m_table.resize( m_quality );

    for( int i = 0; i < m_quality; i++ )
        m_table[i] = waveShape( m_waveType, i/(double)m_quality )*amp + m_base;
}

double WaveGen::level() const
{
    if( !m_alwaysOn && !m_out ) return 0;

    if( m_table.isEmpty() ) return 0;

    int idx = (int)( m_phase*m_quality );
    if( idx < 0 ) idx = 0;
    if( idx >= m_quality ) idx = m_quality-1;

    return m_table[idx];
}

void WaveGen::updateStep()
{
    if( !m_alwaysOn && !m_out ) return;

    // The phase advance is dt*f, and the reduction keeps it in [0,1) with one
    // subtraction however many cycles elapsed - which matters, because a
    // generator set near its 100 kHz ceiling on a simulation achieving a few
    // hundred steps a second skips thousands of cycles per step.
    m_phase += simuDt()*m_freq;

    if( m_phase >= 1.0 )
    {
        const qint64 whole = (qint64)m_phase;

        m_phase -= (double)whole;
    }

    applyOut();
}

void WaveGen::resetStep()
{
    m_phase = 0;

    applyOut();
}

void WaveGen::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                     QWidget *widget )
{
    VoltageBase::paint( painter, option, widget );

    painter->drawRoundedRect( QRectF( -12, -12, 24, 24 ), 3, 3 );

    // The waveform named by Wave_Type, drawn from the same table the simulation
    // reads, so the symbol cannot come to disagree with the output.
    if( m_table.isEmpty() ) return;

    const int n = 24;
    QPointF prev( -8, 0 );

    for( int i = 0; i <= n; i++ )
    {
        const double ph = i/(double)n;

        int idx = (int)( ph*m_quality );
        if( idx >= m_quality ) idx = m_quality-1;

        // Normalised back out of the table: the glyph has to fit the body
        // whatever amplitude and offset are set to.
        double s = 0;
        if( m_volt != 0 ) s = ( m_table[idx] - m_base )/m_volt;
        if( s >  1 ) s =  1;
        if( s < -1 ) s = -1;

        const QPointF pt( -8 + 16*ph, -s*7 );

        painter->drawLine( prev, pt );
        prev = pt;
    }
}

// ---- VoltSource ------------------------------------------------------------------

VoltSource::VoltSource( QObject *parent, const QString &type, const QString &id )
          : Component( parent, type, id )
          , m_volt( 0 )
{
    setArea( QRectF( -10, -10, 20, 20 ) );

    addPin( -16, 0, "lPin", 180 );
    addPin(  16, 0, "rPin",   0 );

    // Both terminals load their node.  Unlike the single-ended sources these are
    // a branch between two nodes rather than a drive against the reference, so a
    // node holding one of them is never single and always needs its row.
    for( int i = 0; i < numPins(); i++ )
        if( Pin *p = pin( i ) ) p->setIsAdmit( true );

    initPins();

    setBaseUnit( "V" );
    setVolt( 5 );
    setShowVal( true );

    setLabelX( -16 );
    setLabelY( -24 );
    setValLabelX( -16 );
    setValLabelY( 8 );
}

void VoltSource::setVolt( double v )
{
    setValue( v );

    m_volt = getmultValue();

    update();
}

void VoltSource::setUnit( const QString &un )
{
    Component::setUnit( un );

    m_volt = getmultValue();
}

void VoltSource::stamp()
{
    // An EMF of V between the two terminals, rPin positive, modelled as that EMF
    // in series with a very stiff conductance G.  Deriving the stamp rather than
    // writing it from the single-ended form, because the signs are where this
    // goes wrong and a wrong sign solves to a plausible negative voltage:
    //
    //   current from r to l inside the branch, i_rl = G*( v_r - v_l - V )
    //
    // Node r, currents leaving positive:  i_rl + (load terms) = 0
    //   ->  G*v_r - G*v_l = +G*V
    // Node l, the same branch traversed the other way:
    //   ->  G*v_l - G*v_r = -G*V
    //
    // The conductance halves of both rows are exactly what stampConductance()
    // writes, so what is left to add is the right-hand side: +G*V at rPin and
    // -G*V at lPin, positive meaning into the node, which is this matrix's
    // convention.  With lPin earthed that gives v_r = V, as it should.
    //
    // Not a true voltage-source row.  A stiff conductance instead of an extra
    // matrix row is the same trade-off e-source.h makes for a rail, and for the
    // same reason: at 1e-3 ohm the error against any load a user places is below
    // what the sheet displays, and the matrix stays square in the node voltages
    // alone.
    const double g = 1/SRC_IMPED;

    if( !stampConductance( lPin(), rPin(), g ) ) return;

    const double i = g*m_volt;

    lPin()->stampCurrent( -i );
    rPin()->stampCurrent(  i );
}

void VoltSource::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                        QWidget *widget )
{
    Component::paint( painter, option, widget );

    painter->drawEllipse( QRectF( -10, -10, 20, 20 ) );

    // Long plate positive on the right, short negative on the left: the cell
    // symbol, which says which terminal is which without a label.
    painter->drawLine( QPointF( -4, -5 ), QPointF( -4, 5 ) );
    painter->drawLine( QPointF(  4, -8 ), QPointF(  4, 8 ) );
}

// ---- CurrSource ------------------------------------------------------------------

CurrSource::CurrSource( QObject *parent, const QString &type, const QString &id )
          : Component( parent, type, id )
          , m_curr( 0 )
{
    setArea( QRectF( -10, -10, 20, 20 ) );

    addPin( -16, 0, "lPin", 180 );
    addPin(  16, 0, "rPin",   0 );

    for( int i = 0; i < numPins(); i++ )
        if( Pin *p = pin( i ) ) p->setIsAdmit( true );

    initPins();

    setBaseUnit( "A" );
    setCurr( 1 );
    setShowVal( true );

    setLabelX( -16 );
    setLabelY( -24 );
    setValLabelX( -16 );
    setValLabelY( 8 );
}

void CurrSource::setCurr( double i )
{
    setValue( i );

    m_curr = getmultValue();

    update();
}

void CurrSource::setUnit( const QString &un )
{
    Component::setUnit( un );

    m_curr = getmultValue();
}

void CurrSource::stamp()
{
    // A current of I pushed from lPin to rPin: rPin's node gains it and lPin's
    // loses it, positive meaning into the node.
    //
    // The 1e9 ohm across it is the part that needs saying.  An ideal current
    // source has no conductance, so a sheet holding one and nothing else - which
    // is the state it is in the moment it is dropped - has a matrix row of all
    // zeros and a factorisation that fails.  The bleed is nine orders of
    // magnitude below any load that would be put in front of it, so it changes no
    // answer, and it turns "the solver reported an error on an empty sheet" into
    // "the node floats to I*1e9 and settles as soon as anything is connected".
    if( !stampConductance( lPin(), rPin(), 1e-9 ) ) return;

    lPin()->stampCurrent( -m_curr );
    rPin()->stampCurrent(  m_curr );
}

void CurrSource::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                        QWidget *widget )
{
    Component::paint( painter, option, widget );

    painter->drawEllipse( QRectF( -10, -10, 20, 20 ) );

    // The arrow points from lPin to rPin, which is the direction the stamp pushes
    // current in, so the symbol and the sign convention agree.
    painter->drawLine( QPointF( -6, 0 ), QPointF( 6, 0 ) );
    painter->drawLine( QPointF( 6, 0 ), QPointF( 2, -3 ) );
    painter->drawLine( QPointF( 6, 0 ), QPointF( 2,  3 ) );
}

// ---- factories -------------------------------------------------------------------

Component* createFixedVoltage( QObject *parent, const QString &type, const QString &id )
{ return new FixedVoltage( parent, type, id ); }

Component* createGround( QObject *parent, const QString &type, const QString &id )
{ return new Ground( parent, type, id ); }

Component* createLogicInput( QObject *parent, const QString &type, const QString &id )
{ return new LogicInput( parent, type, id ); }

Component* createLogicOutput( QObject *parent, const QString &type, const QString &id )
{ return new LogicOutput( parent, type, id ); }

Component* createClock( QObject *parent, const QString &type, const QString &id )
{ return new Clock( parent, type, id ); }

Component* createWaveGen( QObject *parent, const QString &type, const QString &id )
{ return new WaveGen( parent, type, id ); }

Component* createVoltSource( QObject *parent, const QString &type, const QString &id )
{ return new VoltSource( parent, type, id ); }

Component* createCurrSource( QObject *parent, const QString &type, const QString &id )
{ return new CurrSource( parent, type, id ); }
