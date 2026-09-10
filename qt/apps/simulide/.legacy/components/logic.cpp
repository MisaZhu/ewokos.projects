/*
 * SimulIDE, ported to EwokOS - see logic.h.
 */

#include <QtMath>

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

#include "logic.h"

#include "circuit.h"
#include "pin.h"
#include "twopin.h"

#include "e-node.h"
#include "e-source.h"
#include "simulator.h"

// ---------------------------------------------------------------------------
// the geometry every device in this file shares
// ---------------------------------------------------------------------------

// Where a terminal sits, one lattice cell outside the body edge.  Pin::length()
// defaults to 8 and the lead is drawn back from the terminal towards the symbol,
// so a terminal at +-24 against a body at +-16 leaves the lead exactly meeting
// the outline at any view scale.
#define LOGIC_PINX ( LOGIC_HALF + LOGIC_CELL )

// A row of `rows` pins is centred on the axis the body is centred on, LOGIC_CELL
// apart.  An even count therefore straddles the axis by half a cell, which is the
// same choice MuxAnalog::buildPins() in active.cpp makes and for the same reason:
// centring an even row on a multiple of the cell would put one pin on the axis
// and the other an odd distance off it, and then a wire leaving the top pin
// would not be on the lattice.
static int rowHalf( int rows )
{
    if( rows < 1 ) rows = 1;
    return ( rows - 1 )*LOGIC_CELL/2;
}

// The body for a device whose tallest edge carries `rows` pins.  Four units of
// margin above and below the outermost terminal, which is half a cell and enough
// that the pin square - drawn centred on the terminal - does not overlap the
// outline.
static QRectF bodyRect( int rows )
{
    const int h = rowHalf( rows );

    return QRectF( -LOGIC_HALF, -h - LOGIC_CELL/2,
                   2*LOGIC_HALF, 2*h + LOGIC_CELL );
}

// The same with `nctrl` controls hanging below: the body grows down by one cell
// of margin plus one cell per control row, and the controls themselves sit a
// further cell below the new bottom edge.  Kept apart from bodyRect() because a
// device with controls has to know both numbers - the body to draw and the y to
// put its clock pin at.
static QRectF bodyRectCtrl( int rows, int nctrl )
{
    QRectF r = bodyRect( rows );

    if( nctrl > 0 ) r.setHeight( r.height() + LOGIC_CELL*( nctrl + 1 ) );

    return r;
}

static qreal ctrlY( const QRectF &body )
{
    return body.bottom() + LOGIC_CELL;
}

// A control pin's x, spread across the bottom edge and centred, in the same
// arithmetic the rows use.
static qreal ctrlX( int i, int n )
{
    return -rowHalf( n ) + LOGIC_CELL*i;
}

// The two body colours.  Chip uses the same pair and for the same reason: the
// logic symbol is a white drawing of a function and the IC symbol a dark one of
// a package, and telling them apart at a glance is the whole point of having
// both.
static const QColor kIcColor( 50, 50, 70 );
static const QColor kLsColor( 255, 255, 255 );

// The pen Component::paint() leaves behind.  A subclass that draws text or a
// thin guide line has to restore it afterwards, and spelling it out once is
// better than four copies of five numbers that can drift apart.
static const QPen kBodyPen( QColor( 0, 0, 0 ), 1.5, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin );

// The label colour on a dark body, which is chip.cpp's.
static const QColor kIcText( 250, 250, 200 );

int quint32BitParity( quint32 v )
{
    // Fold rather than loop: five shifts and five XORs, no branch, and the same
    // answer for every width.  A loop over 32 bits in a gate that is evaluated a
    // million times a second is the difference between a XOR gate and something
    // noticeably slower than one.
    v ^= v >> 16;
    v ^= v >>  8;
    v ^= v >>  4;
    v ^= v >>  2;
    v ^= v >>  1;

    return int( v & 1u );
}

// ---------------------------------------------------------------------------
// LogicBase
// ---------------------------------------------------------------------------

LogicBase::LogicBase( QObject *parent, const QString &type, const QString &id )
         : Component( parent, type, id )
         , m_inHighV( LOGIC_IN_HIGH )
         , m_inLowV( LOGIC_IN_LOW )
         , m_outHighV( LOGIC_OUT_HIGH )
         , m_outLowV( LOGIC_OUT_LOW )
         , m_outImped( LOGIC_OUT_IMPED )
         , m_isLS( false )
{
    m_color = kIcColor;

    // A logic device is always named.  On a sheet with forty gates on it "U7" is
    // the only way to say which one is which, and Chip makes the same call for
    // the same reason.
    setShowId( true );
}

void LogicBase::setInputHighV( double v )
{
    // The pair is a Schmitt trigger, so the two numbers have an order and the
    // setters enforce it rather than letting a property panel produce a trigger
    // whose window is inside out.  An inverted window - low above high - would
    // make readInput() take the first branch on every level and never the
    // second, which is a device that latches high on the first step and stays
    // there.  Clamping the one just set against the other is the fix that needs
    // no error message.
    if( v < m_inLowV ) v = m_inLowV;

    if( v == m_inHighV ) return;

    m_inHighV = v;

    compute();
    applyOutputs();
}

void LogicBase::setInputLowV( double v )
{
    if( v > m_inHighV ) v = m_inHighV;
    if( v < 0 ) v = 0;

    if( v == m_inLowV ) return;

    m_inLowV = v;

    compute();
    applyOutputs();
}

void LogicBase::setOutHighV( double v )
{
    if( v < m_outLowV ) v = m_outLowV;

    if( v == m_outHighV ) return;

    m_outHighV = v;

    applyOutputs();
}

void LogicBase::setOutLowV( double v )
{
    if( v > m_outHighV ) v = m_outHighV;

    if( v == m_outLowV ) return;

    m_outLowV = v;

    applyOutputs();
}

void LogicBase::setOutImped( double r )
{
    // Floored rather than allowed to zero: the impedance becomes an admittance
    // in stampSource(), and stampSource() floors it at 1e-9 anyway - but doing it
    // here means outImped() reads back the number the element is actually
    // stamped with instead of a zero that is silently something else.
    if( r < 1e-9 ) r = 1e-9;

    if( r == m_outImped ) return;

    m_outImped = r;
}

void LogicBase::setLogicSymbol( bool ls )
{
    if( m_isLS == ls ) return;

    m_isLS = ls;
    m_color = ls ? kLsColor : kIcColor;

    update();
}

// ---- pins --------------------------------------------------------------------

void LogicBase::setPinRoles( const QVector<int> &inputs, const QVector<int> &outputs )
{
    m_inIdx  = inputs;
    m_outIdx = outputs;

    // Inputs present nothing at all to the net driving them.  This is not a
    // simplification and not an approximation: it is what keeps a net made of
    // one gate output and one gate input classified "single" by
    // eNode::initialize(), out of the matrix entirely, and a logic circuit
    // running at the speed it does.  Declaring an admittance here - upstream's
    // Input_Imped, a gigaohm - would give every logic net a matrix row and cost
    // the fast path for a load that passes picoamps.
    for( int idx : m_inIdx )
        if( Pin *p = pin( idx ) ) p->setIsAdmit( false );

    for( int idx : m_outIdx )
    {
        Pin *p = pin( idx );
        if( !p ) continue;

        // Driven and not a load, so an output with nothing on it stays a single
        // node and eNode::solveSingle() takes the level straight off the pin.
        p->setIsOutput( true );
        p->setIsAdmit( false );
        p->setVoltOut( m_outLowV );
    }

    m_outState.fill( false, m_outIdx.size() );

    // Sized by pin index rather than by input ordinal, because readPin() is
    // handed a Pin and has to find its hysteresis state without a search.
    m_inPrev.fill( false, numPins() );

    initPins();

    // Above the body rather than at Component's default of y=-24, which assumes
    // a symbol drawn about its origin: a logic body can be sixty units tall and
    // the default would put the name across the middle of it.
    setLabelX( int( m_area.left() ) );
    setLabelY( int( m_area.top() ) - 20 );
    setLabelRot( 0 );
}

void LogicBase::rebuildPins()
{
    // The state vectors go with the pins.  m_inPrev in particular is indexed by
    // pin index, and a rebuild reorders indices - so keeping it would leave the
    // hysteresis of one input attached to a different input, which shows up as a
    // device that reads its own previous output on a pin that is now an input.
    clearPins();

    m_inIdx.clear();
    m_outIdx.clear();
    m_outState.clear();
    m_inPrev.clear();
}

void LogicBase::publishPins()
{
    if( Circuit *circ = circuit() )
    {
        // The sheet's pin map, which Component::addPin() cannot fill because a
        // component's constructor runs before it is on a sheet - and which has
        // to be filled here for the reason MuxAnalog::buildPins() gives: a pin
        // that is not in the map is invisible to a connector naming it.
        for( int i = 0; i < numPins(); i++ )
            if( Pin *p = pin( i ) ) circ->addPin( p );

        circ->updateNodes();
    }

    compute();
    applyOutputs();
}

// ---- levels ------------------------------------------------------------------

bool LogicBase::readPin( Pin *p )
{
    if( !p ) return false;

    const int idx = p->pinIndex();
    const double v = p->getVolt();

    bool prev = ( idx >= 0 && idx < m_inPrev.size() ) ? m_inPrev.at( idx ) : false;

    // The Schmitt trigger.  Between the two thresholds the input holds what it
    // held, which is the whole reason there are two: a single threshold makes an
    // input sitting on it - a net mid-slew, a divider left at its midpoint -
    // chatter once per step, and a chatter on a clock input is a counter that
    // counts noise.
    bool now = prev;

    if( v > m_inHighV )      now = true;
    else if( v < m_inLowV )  now = false;

    if( idx >= 0 && idx < m_inPrev.size() ) m_inPrev[idx] = now;

    return now;
}

bool LogicBase::readInput( int i )
{
    return readPin( inPin( i ) );
}

bool LogicBase::readInputHard( int i ) const
{
    Pin *p = inPin( i );
    if( !p ) return false;

    // The midpoint of the trigger window rather than the upper threshold.  A
    // device that reads a level rather than a state - an address, a BCD code, a
    // truth table's input byte - must give the same answer for the same voltage
    // every time it is asked, and reading at the upper edge of the window would
    // make an address of 2.4 V a zero on one pass and, after any state had
    // accumulated, something else on another.
    return p->getVolt() > ( m_inHighV + m_inLowV )*0.5;
}

void LogicBase::writeOutput( int i, bool state )
{
    if( i < 0 || i >= m_outState.size() ) return;

    m_outState[i] = state;

    // Onto the pin as well as into the state, and not only for the repaint: a
    // single node takes its voltage from the pin in eNode::solveSingle(), and
    // settleLogic() calls that immediately after updateStep() rather than
    // re-solving.  Leaving it to the next stamp() would put every gate delay in
    // a chain of single nodes one step behind the gate that drove it.
    if( Pin *p = outPin( i ) ) p->setVoltOut( state ? m_outHighV : m_outLowV );
}

void LogicBase::applyOutputs()
{
    for( int i = 0; i < m_outIdx.size(); i++ )
        if( Pin *p = outPin( i ) ) p->setVoltOut( outLevel( i ) );

    update();
}

// ---- the step ----------------------------------------------------------------

void LogicBase::stamp()
{
    // Publishes, and does not compute - see the header for why, which comes down
    // to stamp() running once per non-linear pass rather than once per step.
    for( int i = 0; i < m_outIdx.size(); i++ )
    {
        Pin *p = outPin( i );
        if( !p ) continue;

        // Set on every stamp and not only on a change, because a node classifies
        // as single from isOutput()/isAdmit() and VoltageBase::stamp() sets it
        // the same way for the same reason: an output that stopped being one
        // without saying so would leave its node driven by nothing.
        p->setIsOutput( true );

        stampSource( p, outLevel( i ), m_outImped );
    }
}

void LogicBase::updateStep()
{
    compute();
    applyOutputs();
}

void LogicBase::nodesUpdated()
{
    // Both halves of being notified, exactly as MuxAnalog::nodesUpdated() does
    // it: the Simulator's list is what settleLogic() walks to find flagged
    // elements and the node's list is what notifyFast() walks to flag them, and
    // either one alone is a device that never reacts.
    if( Simulator *sim = Simulator::self() ) sim->addToChangedFast( this );

    // The eNodes this was registered against on the last rebuild are gone -
    // Circuit::updateNodes() throws the whole graph away and builds a new one -
    // so there is nothing to unregister from and the request has to be made
    // again every time.
    for( int idx : m_inIdx )
    {
        Pin *p = pin( idx );
        if( !p ) continue;

        if( eNode *nod = p->getEnode() ) nod->addToChangedFast( this );
    }

    // And once here, so a freshly loaded sheet shows the state its inputs imply
    // rather than zeros until something moves.  A gate with every input low has
    // nothing to change and would otherwise never be notified at all.
    compute();
    applyOutputs();
}

// ---- painting ----------------------------------------------------------------

void LogicBase::paintBody( QPainter *painter )
{
    painter->drawRoundedRect( m_area, 2, 2 );
}

void LogicBase::paintBubble( QPainter *painter, double x, double y )
{
    painter->drawEllipse( QRectF( x, y - 3, 6, 6 ) );
}

void LogicBase::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                       QWidget *widget )
{
    Component::paint( painter, option, widget );

    paintBody( painter );

    // The IEC label goes on the box and only on the box.  In logic-symbol mode
    // the subclass has drawn the distinct shape - a D, a shield, a half-moon -
    // and the shape is the label; writing "&" on top of an AND shield says the
    // same thing twice in two notations.
    const QString lab = iecLabel();

    if( !m_isLS && !lab.isEmpty() )
    {
        painter->setPen( kIcText );
        painter->drawText( m_area, Qt::AlignCenter, lab );
        painter->setPen( kBodyPen );
    }
}

// ---------------------------------------------------------------------------
// Gate
// ---------------------------------------------------------------------------

Gate::Gate( QObject *parent, const QString &type, const QString &id, int inputs )
     : LogicBase( parent, type, id )
     , m_inputs( 0 )
     , m_inverted( false )
{
    // Straight to the member and then buildGate(), rather than through
    // setInputs(): the setter's "nothing changed" guard compares against
    // m_inputs, which is zero here, and a caller asking for one input would be
    // told there was nothing to do.
    m_inputs = qBound( 1, inputs, LOGIC_MAX_IN );

    buildGate();
}

void Gate::setInverted( bool inv )
{
    if( m_inverted == inv ) return;

    m_inverted = inv;

    // Not a rebuild: inversion is applied to the function's answer, so the pins
    // are where they were and only the output moves.
    compute();
    applyOutputs();
}

void Gate::setInputs( int n )
{
    if( n < 1 ) n = 1;
    if( n > LOGIC_MAX_IN ) n = LOGIC_MAX_IN;

    if( n == m_inputs ) return;

    m_inputs = n;

    // A rebuild, and a destructive one in the way Chip::setPackage() is: the
    // pins are thrown away and made again, so a wire attached to an input that
    // no longer exists goes with it.  That is upstream's behaviour too and it is
    // the honest one - a gate resized from four inputs to two has nowhere to put
    // the other two wires.
    buildGate();
}

void Gate::setLogicSymbol( bool ls )
{
    LogicBase::setLogicSymbol( ls );
}

void Gate::buildGate()
{
    rebuildPins();

    const int h = rowHalf( m_inputs );

    QVector<int> in, out;

    for( int i = 0; i < m_inputs; i++ )
    {
        addPin( -LOGIC_PINX, -h + LOGIC_CELL*i,
                "inputpin" + QString::number( i ), 180 );
        in.append( numPins() - 1 );
    }

    // Centred on the axis whatever the input count, so the output lead of a gate
    // resized from two inputs to six stays where the wire going out of it expects
    // it to be.
    addPin( LOGIC_PINX, 0, "outputpin0", 0 );
    out.append( numPins() - 1 );

    setArea( bodyRect( m_inputs ) );
    setPinRoles( in, out );

    publishPins();
}

void Gate::compute()
{
    quint32 bits = 0;

    // Hysteresis, not readInputHard(): a gate input is a state as much as a
    // level, and the two thresholds are what stop a slow edge from producing
    // several output transitions instead of one.
    for( int i = 0; i < m_inputs; i++ )
        if( readInput( i ) ) bits |= ( 1u << i );

    bool state = gateFunction( bits );

    // Inversion applied here rather than in the subclass, so a subclass writes
    // the function it is named for and the property makes it the complement -
    // and so that four classes do not each have to remember to do it.
    if( m_inverted ) state = !state;

    writeOutput( 0, state );
}

void Gate::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                  QWidget *widget )
{
    if( m_isLS )
    {
        // The distinct shape instead of the box, and Component::paint rather
        // than LogicBase::paint: the box is exactly what this branch is not
        // drawing, and LogicBase::paint would draw it.
        Component::paint( painter, option, widget );

        paintShape( painter );
    }
    else
    {
        LogicBase::paint( painter, option, widget );
    }

    // The bubble sits outside the body on the output lead, which is where both
    // notations put it: an inversion is a property of the output, not of the
    // function, and drawing it inside the outline would make it part of the
    // shape being switched between.
    if( m_inverted ) paintBubble( painter, LOGIC_HALF + 1, -3 );
}

// ---- Buffer ------------------------------------------------------------------

Buffer::Buffer( QObject *parent, const QString &type, const QString &id )
       : Gate( parent, type, id, 1 )
{
}

void Buffer::paintShape( QPainter *painter )
{
    // The triangle, pointing at the output.  Drawn to x=14 rather than to the
    // body edge at 16 so the outline and the lead meet without the pen's round
    // cap bulging past the corner.
    QPainterPath tri;
    tri.moveTo( -12, -10 );
    tri.lineTo(  14,   0 );
    tri.lineTo( -12,  10 );
    tri.closeSubpath();

    painter->drawPath( tri );
}

Component* createBuffer( QObject *parent, const QString &type, const QString &id )
{
    return new Buffer( parent, type, id );
}

// ---- AndGate -----------------------------------------------------------------

AndGate::AndGate( QObject *parent, const QString &type, const QString &id )
        : Gate( parent, type, id, 2 )
{
}

void AndGate::paintShape( QPainter *painter )
{
    // The D: a straight back and a semicircular front, which is the shape the
    // distinct-shape notation gives an AND and the one a reader recognises at a
    // glance on a busy sheet.  The arc is the right half of a square whose left
    // edge is the body's centre, so the flat back runs from there to the left
    // edge and the two meet without a seam.
    QPainterPath shape;
    shape.moveTo( 0, -12 );
    shape.lineTo( -12, -12 );
    shape.lineTo( -12,  12 );
    shape.lineTo(  0,  12 );
    shape.arcTo( QRectF( 0, -12, 24, 24 ), -90, 180 );
    shape.closeSubpath();

    painter->drawPath( shape );
}

Component* createAndGate( QObject *parent, const QString &type, const QString &id )
{
    return new AndGate( parent, type, id );
}

// ---- OrGate ------------------------------------------------------------------

OrGate::OrGate( QObject *parent, const QString &type, const QString &id )
      : Gate( parent, type, id, 2 )
{
}

void OrGate::paintShape( QPainter *painter )
{
    // The shield: a back that curves away from the inputs and two sweeps that
    // meet at the output.  Both cubicBeziers are drawn from the left edge to the
    // point, so the shape is closed by a straight line back and the fill covers
    // the wire running under it - which is why Component::paint's brush matters.
    QPainterPath shape;
    shape.moveTo( -12, -12 );
    shape.cubicTo( -4, -12, 6, -10, 14, 0 );
    shape.cubicTo( 6,  10, -4,  12, -12, 12 );
    shape.cubicTo( -8,   0, -8,   0, -12, -12 );
    shape.closeSubpath();

    painter->drawPath( shape );
}

Component* createOrGate( QObject *parent, const QString &type, const QString &id )
{
    return new OrGate( parent, type, id );
}

// ---- XorGate -----------------------------------------------------------------

XorGate::XorGate( QObject *parent, const QString &type, const QString &id )
        : Gate( parent, type, id, 2 )
{
}

void XorGate::paintShape( QPainter *painter )
{
    // The shield again, with the second curve in front of the back edge: that
    // extra line is the whole of what distinguishes an XOR from an OR in this
    // notation, and it is drawn as its own path rather than added to the shield's
    // because it does not bound the filled area.
    QPainterPath shape;
    shape.moveTo( -12, -12 );
    shape.cubicTo( -4, -12, 6, -10, 14, 0 );
    shape.cubicTo( 6,  10, -4,  12, -12, 12 );
    shape.cubicTo( -8,   0, -8,   0, -12, -12 );
    shape.closeSubpath();

    painter->drawPath( shape );

    QPainterPath extra;
    extra.moveTo( -16, -12 );
    extra.cubicTo( -12, 0, -12, 0, -16, 12 );

    painter->drawPath( extra );
}

Component* createXorGate( QObject *parent, const QString &type, const QString &id )
{
    return new XorGate( parent, type, id );
}

// ---------------------------------------------------------------------------
// Function
// ---------------------------------------------------------------------------

namespace {

// The expression compiler.  File-scope and anonymous because nothing outside
// this file parses a truth table, and because a parser is the sort of thing
// that grows a second caller if it is anywhere anybody can see it.
//
// It builds an arena of nodes and then evaluates the tree for all 256 input
// combinations, which is the point: compute() runs a million times a second and
// is eight array lookups, while the parse runs once per property write.

enum NodeOp
{
    kOpIn  = 0,     // a = the input index
    kOpNot = 1,     // a
    kOpAnd = 2,     // a, b
    kOpOr  = 3,
    kOpXor = 4
};

// See ExprParser::m_depth.
#define FUNC_MAX_DEPTH 64
static const int kMaxDepth = FUNC_MAX_DEPTH;

struct ExprNode
{
    quint8  op;
    quint16 a;
    quint16 b;
};

class ExprParser
{
    public:
        ExprParser( const QString &src )
            : m_src( src )
            , m_pos( 0 )
            , m_depth( 0 )
        {
        }

        // Fills `table` with 256 evaluated entries.  Returns false and sets
        // `error` on the first thing it cannot read.
        bool compile( QVector<bool> &table, QString &error )
        {
            m_nodes.clear();
            m_error.clear();
            m_depth = 0;

            const int root = parseExpr();

            if( root < 0 )
            {
                error = m_error;
                return false;
            }

            skipSpace();

            if( m_pos < m_src.size() )
            {
                error = QString( "unexpected '%1' at position %2" )
                        .arg( m_src.at( m_pos ) ).arg( m_pos );
                return false;
            }

            table.clear();
            table.resize( 256 );

            for( int in = 0; in < 256; in++ )
                table[in] = eval( root, quint32( in ) );

            return true;
        }

    private:
        // ---- the lexical half ------------------------------------------------

        void skipSpace()
        {
            while( m_pos < m_src.size() && m_src.at( m_pos ).isSpace() ) m_pos++;
        }

        bool atEnd() const { return m_pos >= m_src.size(); }

        QChar peek()
        {
            skipSpace();
            return atEnd() ? QChar() : m_src.at( m_pos );
        }

        bool accept( char c )
        {
            if( peek() != QLatin1Char( c ) ) return false;

            m_pos++;
            return true;
        }

        void fail( const QString &what )
        {
            if( m_error.isEmpty() )
                m_error = what + " at position " + QString::number( m_pos );
        }

        int node( quint8 op, int a, int b = -1 )
        {
            ExprNode n;
            n.op = op;
            n.a  = quint16( a );
            n.b  = quint16( b );

            m_nodes.append( n );

            return m_nodes.size() - 1;
        }

        // ---- the grammar ------------------------------------------------------

        int parseExpr()
        {
            Depth d( this );
            if( !d.ok() ) return -1;

            int left = parseTerm();

            for( ;; )
            {
                if( !accept( '|' ) ) break;

                const int right = parseTerm();
                if( right < 0 ) return -1;

                left = node( kOpOr, left, right );
            }
            return left;
        }

        int parseTerm()
        {
            int left = parseXor();

            for( ;; )
            {
                if( !accept( '&' ) ) break;

                const int right = parseXor();
                if( right < 0 ) return -1;

                left = node( kOpAnd, left, right );
            }
            return left;
        }

        int parseXor()
        {
            int left = parseUnary();

            for( ;; )
            {
                if( !accept( '^' ) ) break;

                const int right = parseUnary();
                if( right < 0 ) return -1;

                left = node( kOpXor, left, right );
            }
            return left;
        }

        int parseUnary()
        {
            Depth d( this );
            if( !d.ok() ) return -1;

            if( accept( '!' ) )
            {
                const int a = parseUnary();
                if( a < 0 ) return -1;

                return node( kOpNot, a );
            }
            return parsePrimary();
        }

        int parsePrimary()
        {
            const QChar c = peek();

            if( c.isNull() )
            {
                fail( "end of expression" );
                return -1;
            }

            if( c == QLatin1Char( '(' ) )
            {
                m_pos++;

                const int inner = parseExpr();
                if( inner < 0 ) return -1;

                if( !accept( ')' ) )
                {
                    fail( "expected ')'" );
                    return -1;
                }
                return inner;
            }

            // "in3" and "3" name the same input.  The spelled-out form is
            // accepted because a bare digit inside a long expression is easy to
            // misread as a constant, and there is no constant syntax here for it
            // to be confused with - which is itself worth saying, since an
            // expression that wants a one just writes !0.
            if( c.toLower() == QLatin1Char( 'i' )
                && m_pos + 1 < m_src.size()
                && m_src.at( m_pos + 1 ).toLower() == QLatin1Char( 'n' ) )
            {
                m_pos += 2;
            }

            const QChar d = peek();

            if( !d.isDigit() )
            {
                fail( QString( "expected an input number, got '%1'" ).arg( c ) );
                return -1;
            }

            m_pos++;

            const int idx = d.digitValue();

            if( idx >= LOGIC_MAX_IN )
            {
                fail( QString( "input %1 does not exist, there are %2" )
                      .arg( idx ).arg( LOGIC_MAX_IN ) );
                return -1;
            }

            return node( kOpIn, idx );
        }

        bool eval( int i, quint32 in ) const
        {
            // Bounded because a malformed arena index would read past the vector.
            if( i < 0 || i >= m_nodes.size() ) return false;

            const ExprNode &n = m_nodes.at( i );

            switch( n.op )
            {
                case kOpIn:  return ( in & ( 1u << n.a ) ) != 0;
                case kOpNot: return !eval( n.a, in );
                case kOpAnd: return eval( n.a, in ) && eval( n.b, in );
                case kOpOr:  return eval( n.a, in ) || eval( n.b, in );
                case kOpXor: return eval( n.a, in ) != eval( n.b, in );
                default:     return false;
            }
        }

        const QString &m_src;
        int m_pos;

        // Nesting depth, capped.  parseUnary() recurses on '!' and parseExpr() on
        // '(', so a .simu carrying "!!!!!!...0" would otherwise build a stack as
        // deep as the file is long - and the depth is not bounded by anything
        // else, since each level consumes one character and one arena node.  A
        // truth table has 256 rows, so an expression that cannot be written in
        // 64 levels of nesting is not one anybody meant.
        int m_depth;

        // The cap lives here rather than in the two call sites so the two cannot
        // drift, and so the failure message is written once.
        struct Depth
        {
            Depth( ExprParser *p ) : m_p( p ), m_ok( false )
            {
                if( p->m_depth >= kMaxDepth )
                {
                    p->fail( "expression nested too deeply" );
                    return;
                }
                p->m_depth++;
                m_ok = true;
            }
            ~Depth() { if( m_ok ) m_p->m_depth--; }

            bool ok() const { return m_ok; }

            ExprParser *m_p;
            bool m_ok;
        };

        QVector<ExprNode> m_nodes;
        QString m_error;
};

} // namespace

Function::Function( QObject *parent, const QString &type, const QString &id )
         : LogicBase( parent, type, id )
{
    buildPins();

    // Eight tables of 256 entries, all false.  Sized once and never resized, so
    // a Functions list shorter than eight leaves the outputs it does not mention
    // driving low rather than reading an empty table, and one longer than eight
    // is truncated rather than growing the device.
    m_table.resize( LOGIC_MAX_OUT );
    for( int i = 0; i < LOGIC_MAX_OUT; i++ ) m_table[i].resize( 256 );
}

void Function::buildPins()
{
    rebuildPins();

    const int h = rowHalf( LOGIC_MAX_IN );

    QVector<int> in, out;

    for( int i = 0; i < LOGIC_MAX_IN; i++ )
    {
        addPin( -LOGIC_PINX, -h + LOGIC_CELL*i,
                "inputpin" + QString::number( i ), 180 );
        in.append( numPins() - 1 );
    }

    for( int i = 0; i < LOGIC_MAX_OUT; i++ )
    {
        addPin( LOGIC_PINX, -h + LOGIC_CELL*i,
                "outputpin" + QString::number( i ), 0 );
        out.append( numPins() - 1 );
    }

    setArea( bodyRect( LOGIC_MAX_IN ) );
    setPinRoles( in, out );

    publishPins();
}

void Function::setFunctions( const QStringList &list )
{
    m_funcs = list;
    m_error.clear();

    // Parsed into the tables one at a time, and a failure leaves the table it
    // was going to overwrite alone.  Half-applied truth tables would be worse
    // than either extreme: a device whose output 3 is the new expression and
    // whose output 4 is the old one is not a state anybody can reason about, and
    // the error string says which entry stopped the parse.
    for( int i = 0; i < LOGIC_MAX_OUT; i++ )
    {
        if( i >= list.size() )
        {
            // Not mentioned, not an error: a Function with two expressions is a
            // two-output device whose other six drive low.
            m_table[i].fill( false );
            continue;
        }

        QVector<bool> table;
        QString err;

        ExprParser parser( list.at( i ) );

        if( !parser.compile( table, err ) )
        {
            m_error = QString( "output %1: %2" ).arg( i ).arg( err );
            continue;
        }

        m_table[i] = table;
    }

    compute();
    applyOutputs();
}

void Function::compute()
{
    if( m_table.size() != LOGIC_MAX_OUT ) return;

    // Hard reads, and this is the one device in the file that must not use the
    // Schmitt trigger: a truth table is a function of its inputs, and hysteresis
    // would make the same voltage give different answers depending on what the
    // inputs were last step - which is a latch, and Function is not one.
    quint32 in = 0;

    for( int i = 0; i < LOGIC_MAX_IN; i++ )
        if( readInputHard( i ) ) in |= ( 1u << i );

    for( int o = 0; o < LOGIC_MAX_OUT; o++ )
        writeOutput( o, m_table.at( o ).value( int( in ), false ) );
}

Component* createFunction( QObject *parent, const QString &type, const QString &id )
{
    return new Function( parent, type, id );
}

// ---------------------------------------------------------------------------
// FlipFlop
// ---------------------------------------------------------------------------

FlipFlop::FlipFlop( QObject *parent, const QString &type, const QString &id )
         : LogicBase( parent, type, id )
         , m_state( false )
         , m_clkPrev( false )
         , m_setReset( false )
         , m_iSet( -1 )
         , m_iReset( -1 )
         , m_iClock( -1 )
         , m_iQ( -1 )
         , m_iQn( -1 )
{
    // No pins yet.  A flip-flop on its own does not know how many data inputs it
    // has - that is what D and JK differ in - so the constructor leaves the
    // symbol empty and the subclass fills it from its own buildPins().  Building
    // anything here would mean building it twice, and the first build would
    // register pins in the sheet's map that the second throws away.
}

void FlipFlop::setSetResetEnabled( bool en )
{
    if( m_setReset == en ) return;

    m_setReset = en;

    // The pins are marked unused rather than removed, and the difference matters
    // to a file: a .simu that wired a preset input keeps its connector when the
    // property is turned off, and turning it back on restores a working device
    // instead of one whose wiring was quietly deleted.  Pin::paint() hides the
    // square and the label of an unused pin, so the symbol says the same thing
    // the electrical behaviour does - the pin is there and it is not read.
    if( Pin *p = pin( m_iSet )   ) p->setUnused( !en );
    if( Pin *p = pin( m_iReset ) ) p->setUnused( !en );

    compute();
    applyOutputs();
}

void FlipFlop::buildFlipFlop( int nData )
{
    rebuildPins();

    m_iData.clear();

    if( nData < 0 ) nData = 0;
    if( nData > LOGIC_MAX_IN ) nData = LOGIC_MAX_IN;

    // Two rows even for one data pin: Q and Q-bar are on the right and they need
    // somewhere to be.  An even row straddles the axis by half a cell, which puts
    // Q at -4 and Q-bar at +4 and both on the lattice.
    const int rows = qMax( nData, 2 );
    const int h    = rowHalf( rows );

    // One control row below the body, for Clock and Reset side by side.  The Set
    // pin is above it and does not need room of its own: it is a pin, and a pin
    // is a child item with its own bounding rect rather than something m_area has
    // to cover.
    const QRectF body = bodyRectCtrl( rows, 1 );

    QVector<int> in, out;

    for( int i = 0; i < nData; i++ )
    {
        addPin( -LOGIC_PINX, -h + LOGIC_CELL*i,
                "datapin" + QString::number( i ), 180 );

        m_iData.append( numPins() - 1 );
        in.append( numPins() - 1 );
    }

    addPin( 0, int( body.top() ) - LOGIC_CELL, "setpin", 270 );
    m_iSet = numPins() - 1;
    in.append( m_iSet );

    // Clock left of Reset, which is the order the two are read in and the order a
    // datasheet pinout puts them in.  A reader who has wired a clock to the pin
    // under the wedge does not want to be told it moved.
    addPin( int( ctrlX( 0, 2 ) ), int( ctrlY( body ) ), "clockpin", 90 );
    m_iClock = numPins() - 1;
    in.append( m_iClock );

    addPin( int( ctrlX( 1, 2 ) ), int( ctrlY( body ) ), "resetpin", 90 );
    m_iReset = numPins() - 1;
    in.append( m_iReset );

    addPin( LOGIC_PINX, -LOGIC_CELL/2, "qpin", 0 );
    m_iQ = numPins() - 1;
    out.append( m_iQ );

    addPin( LOGIC_PINX, LOGIC_CELL/2, "qnpin", 0 );
    m_iQn = numPins() - 1;
    out.append( m_iQn );

    setArea( body );
    setPinRoles( in, out );

    // Labels on the pins that a box does not otherwise identify.  The data inputs
    // are left unlelled on purpose: D and JK name them from their own subclass by
    // writing the letter onto the body, and a second copy of the same letter next
    // to the pin is noise at the zoom a sheet of these is read at.
    if( Pin *p = pin( m_iSet )   ) p->setLabelText( QStringLiteral( "S" ) );
    if( Pin *p = pin( m_iReset ) ) p->setLabelText( QStringLiteral( "R" ) );
    if( Pin *p = pin( m_iClock ) ) p->setLabelText( QStringLiteral( "CLK" ) );

    if( Pin *p = pin( m_iSet )   ) p->setUnused( !m_setReset );
    if( Pin *p = pin( m_iReset ) ) p->setUnused( !m_setReset );

    // Above the Set pin rather than at setPinRoles()'s default, which puts it two
    // units above the body and would land on the pin's own square.
    setLabelY( int( body.top() ) - LOGIC_CELL - 20 );

    publishPins();
}

bool FlipFlop::clockEdge()
{
    // readPin() rather than a bare voltage test, for the hysteresis: a clock edge
    // taken at a single threshold is taken twice by any input that slews slowly
    // through it, and a flip-flop that counts twice per clock is not a slightly
    // wrong flip-flop, it is a divider by the wrong number.
    const bool now  = readPin( pin( m_iClock ) );
    const bool edge = now && !m_clkPrev;

    m_clkPrev = now;

    return edge;
}

void FlipFlop::compute()
{
    if( m_setReset )
    {
        // Reset first and Set second, so Set wins when both are held.  That is
        // what the latch inside a 74xx74 does - the preset feeds the same pair of
        // gates as the clear and has the shorter path - and it is the behaviour a
        // user who wired both to a rail expects rather than the one they would
        // have to read the source to find.
        if( readPin( pin( m_iReset ) ) ) m_state = false;
        if( readPin( pin( m_iSet )   ) ) m_state = true;
    }

    // The edge is taken whether or not the asynchronous pins just moved the
    // state, and clockEdge() is called unconditionally rather than from inside a
    // branch: it updates m_clkPrev, and a clock that was not sampled on a step
    // where the preset happened to be high would be seen as an edge on the next
    // one, which is a spurious count.
    if( clockEdge() ) m_state = nextState( m_state );

    // Q and Q-bar, in the order buildFlipFlop() appended them.  Q-bar is the
    // complement of the state and not a second output with its own function, so
    // the two can never disagree - which is the property a latch built from them
    // depends on.
    writeOutput( 0, m_state );
    writeOutput( 1, !m_state );
}

void FlipFlop::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                      QWidget *widget )
{
    LogicBase::paint( painter, option, widget );

    // The clock wedge: a triangle pointing into the body at the clock pin.  It is
    // the only marking that says which of the two bottom pins is the clock, and
    // it is drawn in both notations because both of them use it - the distinct
    // shape notation has no other way to say "edge triggered" either.
    const qreal cx = ctrlX( 0, 2 );
    const qreal by = m_area.bottom();

    QPainterPath wedge;
    wedge.moveTo( cx - 4, by );
    wedge.lineTo( cx,     by - 5 );
    wedge.lineTo( cx + 4, by );

    painter->drawPath( wedge );
}

// ---- FlipFlopD ---------------------------------------------------------------

FlipFlopD::FlipFlopD( QObject *parent, const QString &type, const QString &id )
         : FlipFlop( parent, type, id )
         , m_iD( -1 )
{
    buildPins();
}

void FlipFlopD::buildPins()
{
    buildFlipFlop( 1 );

    m_iD = dataIdx( 0 );
}

bool FlipFlopD::nextState( bool current )
{
    Q_UNUSED( current );

    // D ignores what was there, which is the whole of what makes it a D.
    return readPin( pin( m_iD ) );
}

Component* createFlipFlopD( QObject *parent, const QString &type, const QString &id )
{
    return new FlipFlopD( parent, type, id );
}

// ---- FlipFlopJK --------------------------------------------------------------

FlipFlopJK::FlipFlopJK( QObject *parent, const QString &type, const QString &id )
         : FlipFlop( parent, type, id )
         , m_iJ( -1 )
         , m_iK( -1 )
{
    buildPins();
}

void FlipFlopJK::buildPins()
{
    buildFlipFlop( 2 );

    m_iJ = dataIdx( 0 );
    m_iK = dataIdx( 1 );
}

bool FlipFlopJK::nextState( bool current )
{
    // J and K are read here rather than in compute(), and that is not a
    // distinction without a difference: they are read on the step the edge was
    // detected, so a J that changed between two clocks is not seen at all.  A
    // level-sensitive read of the same pins - what a latch does - would let the
    // output follow J while the clock is high, which is the exact bug a JK
    // flip-flop exists to not have.
    const bool j = readPin( pin( m_iJ ) );
    const bool k = readPin( pin( m_iK ) );

    if( j && k ) return !current;     // toggle
    if( j )      return true;         // set
    if( k )      return false;        // reset

    return current;                   // hold
}

Component* createFlipFlopJK( QObject *parent, const QString &type, const QString &id )
{
    return new FlipFlopJK( parent, type, id );
}

// ---------------------------------------------------------------------------
// LatchD
// ---------------------------------------------------------------------------

LatchD::LatchD( QObject *parent, const QString &type, const QString &id )
        : LogicBase( parent, type, id )
        , m_size( 0 )
        , m_iEnable( -1 )
{
    setSize( 4 );
}

void LatchD::setSize( int n )
{
    if( n < 1 ) n = 1;
    if( n > LOGIC_MAX_BITS ) n = LOGIC_MAX_BITS;

    if( n == m_size ) return;

    m_size = n;

    buildPins();
}

void LatchD::buildPins()
{
    rebuildPins();

    const int h = rowHalf( m_size );

    // The controls row is there for one pin, and the arithmetic is the same as
    // for two so that a single Enable lands on the axis rather than half a cell
    // off it - ctrlX(0,1) is zero.
    const QRectF body = bodyRectCtrl( m_size, 1 );

    QVector<int> in, out;

    for( int i = 0; i < m_size; i++ )
    {
        addPin( -LOGIC_PINX, -h + LOGIC_CELL*i,
                "datapin" + QString::number( i ), 180 );
        in.append( numPins() - 1 );
    }

    for( int i = 0; i < m_size; i++ )
    {
        addPin( LOGIC_PINX, -h + LOGIC_CELL*i,
                "qpin" + QString::number( i ), 0 );
        out.append( numPins() - 1 );
    }

    addPin( 0, int( ctrlY( body ) ), "enablepin", 90 );
    m_iEnable = numPins() - 1;
    in.append( m_iEnable );

    setArea( body );
    setPinRoles( in, out );

    // Sized here and not in the constructor: setSize() calls buildPins(), which
    // calls publishPins(), which calls compute(), and a compute() that indexed an
    // empty m_latch would write nothing and read nothing and the latch would come
    // up holding whatever the vector happened to contain.
    m_latch.fill( false, m_size );

    publishPins();
}

void LatchD::compute()
{
    if( m_latch.size() != m_size ) return;

    // Transparent, and the return is the whole of what "hold" means here: the
    // outputs are not written, so they keep the state writeOutput() left them in.
    // Storing the held value in m_latch on every call and writing it back out
    // would be the same answer with two more loops in it.
    if( !readPin( pin( m_iEnable ) ) ) return;

    for( int i = 0; i < m_size; i++ )
    {
        const bool b = readInput( i );

        m_latch[i] = b;

        writeOutput( i, b );
    }
}

Component* createLatchD( QObject *parent, const QString &type, const QString &id )
{
    return new LatchD( parent, type, id );
}

// ---------------------------------------------------------------------------
// BinCounter
// ---------------------------------------------------------------------------

BinCounter::BinCounter( QObject *parent, const QString &type, const QString &id )
           : LogicBase( parent, type, id )
           , m_bits( 0 )
           , m_max( 0 )
           , m_count( 0 )
           , m_clkPrev( false )
           , m_iClock( -1 )
           , m_iReset( -1 )
           , m_iEnable( -1 )
           , m_iCarry( -1 )
{
    setBits( 4 );
}

void BinCounter::setBits( int n )
{
    if( n < 1 ) n = 1;
    if( n > LOGIC_MAX_BITS ) n = LOGIC_MAX_BITS;

    if( n == m_bits ) return;

    m_bits = n;

    // The count is thrown away with the width, and deliberately: a counter
    // narrowed from eight bits to four has a value whose upper half no longer has
    // anywhere to go, and keeping it would produce an output pattern that is not
    // the number the panel says the counter is at.
    m_count = 0;

    buildPins();
}

void BinCounter::setMaxValue( int v )
{
    if( v < 0 ) v = 0;

    if( v == m_max ) return;

    m_max = v;

    // A limit that the current count is already past is applied at once rather
    // than at the next edge, because the alternative is a counter that reads 300
    // with Max_Value 10 on the panel and stays that way until something clocks
    // it - which on a stopped circuit is never.
    if( m_max > 0 && m_count >= m_max ) m_count = 0;

    compute();
    applyOutputs();
}

void BinCounter::buildPins()
{
    rebuildPins();

    // One row more than there are bits, for the carry output at the bottom of the
    // right-hand edge.  Giving it a row of its own is what keeps it on the
    // lattice: an output hung below the body at an arbitrary y is a terminal a
    // wire cannot be routed to without a bend that is not on the grid.
    const int rows = m_bits + 1;
    const int h    = rowHalf( rows );

    const QRectF body = bodyRectCtrl( rows, 1 );

    QVector<int> in, out;

    for( int i = 0; i < m_bits; i++ )
    {
        addPin( LOGIC_PINX, -h + LOGIC_CELL*i,
                "qpin" + QString::number( i ), 0 );
        out.append( numPins() - 1 );
    }

    addPin( LOGIC_PINX, -h + LOGIC_CELL*m_bits, "carrypin", 0 );
    m_iCarry = numPins() - 1;
    out.append( m_iCarry );

    addPin( int( ctrlX( 0, 3 ) ), int( ctrlY( body ) ), "clockpin", 90 );
    m_iClock = numPins() - 1;
    in.append( m_iClock );

    addPin( int( ctrlX( 1, 3 ) ), int( ctrlY( body ) ), "resetpin", 90 );
    m_iReset = numPins() - 1;
    in.append( m_iReset );

    addPin( int( ctrlX( 2, 3 ) ), int( ctrlY( body ) ), "enablepin", 90 );
    m_iEnable = numPins() - 1;
    in.append( m_iEnable );

    setArea( body );
    setPinRoles( in, out );

    if( Pin *p = pin( m_iClock ) ) p->setLabelText( QStringLiteral( "CLK" ) );
    if( Pin *p = pin( m_iReset ) ) p->setLabelText( QStringLiteral( "RST" ) );
    if( Pin *p = pin( m_iEnable )) p->setLabelText( QStringLiteral( "EN" ) );

    setLabelY( int( body.top() ) - 20 );

    publishPins();
}

void BinCounter::compute()
{
    // Read once, and the single read is the point: readPin() moves the hysteresis
    // state it keeps in m_inPrev, so reading the clock twice is not a redundant
    // lookup but two samples of the same pin through a trigger whose memory the
    // first call just changed.
    const bool clk  = readPin( pin( m_iClock ) );
    const bool edge = clk && !m_clkPrev;

    // Updated unconditionally and not only when the edge fires, for the reason
    // FlipFlop::compute() gives: a sample skipped on one path is an edge invented
    // on the next.
    m_clkPrev = clk;

    // Synchronous reset would be the more common device, and this is not it: the
    // reset is read every call rather than on an edge, so a counter held in reset
    // stays at zero whatever its clock does.  A synchronous reset here would mean
    // a counter that cannot be started from a known state without also clocking
    // it, which is the thing a reset pin is on the symbol to avoid.
    if( readPin( pin( m_iReset ) ) )
    {
        m_count = 0;
    }
    else if( edge )
    {
        // Enable unwired counts, and the test is on the connector rather than on
        // the voltage because readPin() has already decided that a floating input
        // is a zero - which is right for a data input and wrong here, where it
        // would make a counter dropped on a sheet and wired only to a clock a
        // counter that never moves.
        Pin *en = pin( m_iEnable );

        if( !en || !en->connector() || readPin( en ) )
        {
            m_count++;

            const int lim = ( m_max > 0 ) ? m_max : ( 1 << m_bits );

            if( m_count >= lim ) m_count = 0;
        }
    }

    for( int i = 0; i < m_bits; i++ )
        writeOutput( i, ( m_count >> i ) & 1 );

    // Terminal count rather than "the count is not zero", which is what a carry
    // is for: the next counter in a chain clocks once per full cycle of this one,
    // and a carry that came high anywhere else would make a two-digit counter
    // skip.
    const int lim = ( m_max > 0 ) ? m_max : ( 1 << m_bits );

    writeOutput( m_bits, m_count == lim - 1 );
}

Component* createBinCounter( QObject *parent, const QString &type, const QString &id )
{
    return new BinCounter( parent, type, id );
}

// ---------------------------------------------------------------------------
// FullAdder
// ---------------------------------------------------------------------------

FullAdder::FullAdder( QObject *parent, const QString &type, const QString &id )
           : LogicBase( parent, type, id )
{
    buildPins();
}

void FullAdder::buildPins()
{
    rebuildPins();

    // Three rows, which is the tallest edge: two inputs and a carry in on the
    // left, a sum and a carry out on the right, and three is what covers both
    // without one of them sitting outside the outline.
    const int h = rowHalf( 3 );

    const QRectF body = bodyRect( 3 );

    QVector<int> in, out;

    addPin( -LOGIC_PINX, -h, "apin", 180 );
    in.append( numPins() - 1 );

    addPin( -LOGIC_PINX, -h + LOGIC_CELL, "bpin", 180 );
    in.append( numPins() - 1 );

    addPin( -LOGIC_PINX, -h + 2*LOGIC_CELL, "cinpin", 180 );
    in.append( numPins() - 1 );

    addPin( LOGIC_PINX, -h, "sumpin", 0 );
    out.append( numPins() - 1 );

    addPin( LOGIC_PINX, -h + LOGIC_CELL, "coutpin", 0 );
    out.append( numPins() - 1 );

    setArea( body );
    setPinRoles( in, out );

    publishPins();
}

void FullAdder::compute()
{
    const bool a   = readInput( 0 );
    const bool b   = readInput( 1 );
    const bool cin = readInput( 2 );

    // Written as the two-level form rather than as a chain of half adders, which
    // is the same function and one less place for the carry to be taken from the
    // wrong pair: the carry is generated by a and b, or propagated by their
    // difference into the incoming one, and both terms have to be there.
    writeOutput( 0, ( a != b ) != cin );
    writeOutput( 1, ( a && b ) || ( cin && ( a != b ) ) );
}

Component* createFullAdder( QObject *parent, const QString &type, const QString &id )
{
    return new FullAdder( parent, type, id );
}

// ---------------------------------------------------------------------------
// ShiftReg
// ---------------------------------------------------------------------------

ShiftReg::ShiftReg( QObject *parent, const QString &type, const QString &id )
         : LogicBase( parent, type, id )
         , m_bits( 0 )
         , m_reg( 0 )
         , m_clkPrev( false )
         , m_lsbFirst( false )
         , m_iClock( -1 )
         , m_iReset( -1 )
         , m_iSerIn( -1 )
         , m_iSerOut( -1 )
{
    setBits( 8 );
}

void ShiftReg::setBits( int n )
{
    if( n < 1 ) n = 1;
    if( n > LOGIC_MAX_BITS ) n = LOGIC_MAX_BITS;

    if( n == m_bits ) return;

    m_bits = n;
    m_reg  = 0;

    buildPins();
}

void ShiftReg::setLsbFirst( bool lsb )
{
    if( m_lsbFirst == lsb ) return;

    m_lsbFirst = lsb;

    // Not a rebuild: the pins are the same pins, and which end a bit enters at is
    // a property of the shift rather than of the symbol.  The register is left
    // alone, so a device whose direction is changed mid-run keeps its contents
    // and starts shifting them the other way - which is what a daisy chain being
    // rewired does, and what makes the property usable on a running circuit.
    compute();
    applyOutputs();
}

void ShiftReg::buildPins()
{
    rebuildPins();

    // As BinCounter: one row more than there are bits, and the extra row is the
    // serial output at the bottom of the right-hand edge.  The serial input takes
    // the top row of the left, so the two are at opposite corners and a pair of
    // these daisy-chained left to right has its output level with the next one's
    // input.
    const int rows = m_bits + 1;
    const int h    = rowHalf( rows );

    const QRectF body = bodyRectCtrl( rows, 1 );

    QVector<int> in, out;

    addPin( -LOGIC_PINX, -h, "serinpin", 180 );
    m_iSerIn = numPins() - 1;
    in.append( m_iSerIn );

    for( int i = 0; i < m_bits; i++ )
    {
        addPin( LOGIC_PINX, -h + LOGIC_CELL*i,
                "qpin" + QString::number( i ), 0 );
        out.append( numPins() - 1 );
    }

    addPin( LOGIC_PINX, -h + LOGIC_CELL*m_bits, "seroutpin", 0 );
    m_iSerOut = numPins() - 1;
    out.append( m_iSerOut );

    addPin( int( ctrlX( 0, 2 ) ), int( ctrlY( body ) ), "clockpin", 90 );
    m_iClock = numPins() - 1;
    in.append( m_iClock );

    addPin( int( ctrlX( 1, 2 ) ), int( ctrlY( body ) ), "resetpin", 90 );
    m_iReset = numPins() - 1;
    in.append( m_iReset );

    setArea( body );
    setPinRoles( in, out );

    if( Pin *p = pin( m_iClock ) ) p->setLabelText( QStringLiteral( "CLK" ) );
    if( Pin *p = pin( m_iReset ) ) p->setLabelText( QStringLiteral( "RST" ) );

    setLabelY( int( body.top() ) - 20 );

    publishPins();
}

void ShiftReg::compute()
{
    const bool clk  = readPin( pin( m_iClock ) );
    const bool edge = clk && !m_clkPrev;

    m_clkPrev = clk;

    if( readPin( pin( m_iReset ) ) )
    {
        m_reg = 0;
    }
    else if( edge )
    {
        const bool b = readPin( pin( m_iSerIn ) );

        // The mask has to be computed rather than written, because m_bits is a
        // property and 1 << 32 is undefined on a 32-bit shift - which is exactly
        // the width a user asking for "32 bits" gets, since LOGIC_MAX_BITS is 16
        // and the clamp keeps it there.  The test stays for the same reason a
        // division by zero test stays in Inductor::updateAdmit(): the arithmetic
        // is only safe because of a clamp that lives somewhere else.
        const quint32 mask = ( m_bits >= 32 ) ? 0xFFFFFFFFu
                                              : ( ( 1u << m_bits ) - 1u );

        if( m_lsbFirst )
            m_reg = ( m_reg >> 1 ) | ( b ? ( 1u << ( m_bits - 1 ) ) : 0u );
        else
            m_reg = ( ( m_reg << 1 ) | ( b ? 1u : 0u ) ) & mask;
    }

    for( int i = 0; i < m_bits; i++ )
        writeOutput( i, ( m_reg >> i ) & 1 );

    // The bit that leaves next, which is the one furthest from the end it entered
    // at.  Taking it from the wrong end is not a small error: a daisy chain wired
    // to it repeats one bit forever and the register downstream never sees the
    // rest.
    writeOutput( m_bits, m_lsbFirst ? ( m_reg & 1u )
                                    : ( ( m_reg >> ( m_bits - 1 ) ) & 1u ) );
}

Component* createShiftReg( QObject *parent, const QString &type, const QString &id )
{
    return new ShiftReg( parent, type, id );
}

// ---------------------------------------------------------------------------
// Mux
// ---------------------------------------------------------------------------

Mux::Mux( QObject *parent, const QString &type, const QString &id )
   : LogicBase( parent, type, id )
   , m_channels( 0 )
   , m_addrPins( 0 )
   , m_iOut( -1 )
{
    // Two rather than zero in the constructor argument, and straight to setSize
    // rather than through the setter, for the reason Gate's constructor gives:
    // the setter's guard compares against a member that is zero here.
    setInputs( 2 );
}

void Mux::setInputs( int n )
{
    if( n < 1 ) n = 1;
    if( n > LOGIC_MAX_CHAN ) n = LOGIC_MAX_CHAN;

    if( n == m_channels ) return;

    m_channels = n;

    buildPins();
}

// Both selectors count their address pins with the same loop, and both need it
// before they have placed anything: the number of controls decides the body's
// height, and the body's height decides where the controls go.
static int addrWidth( int channels )
{
    int n = 0;
    while( ( 1 << n ) < channels ) n++;

    return n;
}

void Mux::buildPins()
{
    rebuildPins();

    m_addrPins = addrWidth( m_channels );

    const int h = rowHalf( m_channels );

    // No controls row for a single channel: addrWidth(1) is zero, and a body
    // grown for a row of pins that do not exist is a box with a hole under it.
    const QRectF body = bodyRectCtrl( m_channels, m_addrPins );

    QVector<int> in, out;

    for( int i = 0; i < m_channels; i++ )
    {
        addPin( -LOGIC_PINX, -h + LOGIC_CELL*i,
                "inputpin" + QString::number( i ), 180 );
        in.append( numPins() - 1 );
    }

    // Appended after the channels, which fixes the ordinal an address pin has in
    // m_inIdx at m_channels + i.  compute() relies on that order, and it is the
    // one order a rebuild preserves - the channels have to be first because they
    // are what readInput( sel ) selects between.
    for( int i = 0; i < m_addrPins; i++ )
    {
        addPin( int( ctrlX( i, m_addrPins ) ), int( ctrlY( body ) ),
                "addresspin" + QString::number( i ), 90 );
        in.append( numPins() - 1 );
    }

    addPin( LOGIC_PINX, 0, "outputpin0", 0 );
    m_iOut = 0;
    out.append( numPins() - 1 );

    setArea( body );
    setPinRoles( in, out );

    setLabelY( int( body.top() ) - 20 );

    publishPins();
}

void Mux::compute()
{
    int sel = 0;

    // Hard reads: an address is a number and not a state, and a Schmitt trigger on
    // one would remember the last address it saw and refuse to change to a code
    // sitting between the two thresholds - which reads as a mux that sticks on a
    // channel.
    for( int i = 0; i < m_addrPins; i++ )
        if( readInputHard( m_channels + i ) ) sel |= ( 1 << i );

    // Clamped rather than left to select nothing.  A code past the last channel is
    // a wiring mistake, and the two possible answers are "the output holds low"
    // and "the output follows the last channel": the second is what a user with a
    // two-bit address on a two-channel mux sees when they set the address to 2 by
    // accident, and it is diagnosable from the sheet in a way a stuck low is not.
    if( sel >= m_channels ) sel = m_channels - 1;

    writeOutput( 0, readInput( sel ) );
}

Component* createMux( QObject *parent, const QString &type, const QString &id )
{
    return new Mux( parent, type, id );
}

// ---------------------------------------------------------------------------
// Demux
// ---------------------------------------------------------------------------

Demux::Demux( QObject *parent, const QString &type, const QString &id )
       : LogicBase( parent, type, id )
       , m_channels( 0 )
       , m_addrPins( 0 )
       , m_iIn( -1 )
{
    setOutputs( 2 );
}

void Demux::setOutputs( int n )
{
    if( n < 1 ) n = 1;
    if( n > LOGIC_MAX_CHAN ) n = LOGIC_MAX_CHAN;

    if( n == m_channels ) return;

    m_channels = n;

    buildPins();
}

void Demux::buildPins()
{
    rebuildPins();

    m_addrPins = addrWidth( m_channels );

    const int h = rowHalf( m_channels );

    const QRectF body = bodyRectCtrl( m_channels, m_addrPins );

    QVector<int> in, out;

    // The single input first, so its ordinal is zero and the address pins' are
    // 1+i - the mirror of the order Mux uses, and for the same reason: the pin
    // that is not an address comes first.
    addPin( -LOGIC_PINX, 0, "inputpin0", 180 );
    m_iIn = 0;
    in.append( numPins() - 1 );

    for( int i = 0; i < m_channels; i++ )
    {
        addPin( LOGIC_PINX, -h + LOGIC_CELL*i,
                "outputpin" + QString::number( i ), 0 );
        out.append( numPins() - 1 );
    }

    for( int i = 0; i < m_addrPins; i++ )
    {
        addPin( int( ctrlX( i, m_addrPins ) ), int( ctrlY( body ) ),
                "addresspin" + QString::number( i ), 90 );
        in.append( numPins() - 1 );
    }

    setArea( body );
    setPinRoles( in, out );

    setLabelY( int( body.top() ) - 20 );

    publishPins();
}

void Demux::compute()
{
    int sel = 0;

    for( int i = 0; i < m_addrPins; i++ )
        if( readInputHard( 1 + i ) ) sel |= ( 1 << i );

    const bool v = readInput( 0 );

    // Not clamped, and this is where the two selectors differ on purpose.  A mux
    // has to drive something - it has one output and leaving it undriven is a
    // stuck line - while a demux has one output per channel and an address past
    // the last of them means none of them is selected, which is the state a
    // decoder in the middle of a rollover is actually in.  Forcing a channel on
    // here would light two lines at once for the step either side of the wrap.
    for( int i = 0; i < m_channels; i++ )
        writeOutput( i, ( i == sel ) && v );
}

Component* createDemux( QObject *parent, const QString &type, const QString &id )
{
    return new Demux( parent, type, id );
}

// ---------------------------------------------------------------------------
// BcdToDec
// ---------------------------------------------------------------------------

BcdToDec::BcdToDec( QObject *parent, const QString &type, const QString &id )
          : LogicBase( parent, type, id )
{
    buildPins();
}

void BcdToDec::buildPins()
{
    rebuildPins();

    // Ten rows, which is the decoded side and the taller of the two edges.  The
    // four code inputs take the top four positions rather than being centred,
    // because a decoder is read top to bottom: input A at the top, output 0 at the
    // top, and the two line up.
    const int h = rowHalf( 10 );

    const QRectF body = bodyRect( 10 );

    QVector<int> in, out;

    for( int i = 0; i < 4; i++ )
    {
        addPin( -LOGIC_PINX, -h + LOGIC_CELL*i,
                "inputpin" + QString::number( i ), 180 );
        in.append( numPins() - 1 );
    }

    for( int i = 0; i < 10; i++ )
    {
        addPin( LOGIC_PINX, -h + LOGIC_CELL*i,
                "outputpin" + QString::number( i ), 0 );
        out.append( numPins() - 1 );
    }

    setArea( body );
    setPinRoles( in, out );

    setLabelY( int( body.top() ) - 20 );

    publishPins();
}

void BcdToDec::compute()
{
    int code = 0;

    for( int i = 0; i < 4; i++ )
        if( readInputHard( i ) ) code |= ( 1 << i );

    // Ten writes and not one, so a code of 12 drives nothing at all rather than
    // leaving the line that code 9 selected high.  writeOutput() is a store and a
    // pin write, and skipping it would leave the output at whatever the previous
    // call put there - a decoder that holds its last digit while its input is
    // invalid, which is exactly the behaviour a counter mid-rollover must not
    // have.
    for( int i = 0; i < 10; i++ )
        writeOutput( i, i == code );
}

Component* createBcdToDec( QObject *parent, const QString &type, const QString &id )
{
    return new BcdToDec( parent, type, id );
}

// ---------------------------------------------------------------------------
// DecToBcd
// ---------------------------------------------------------------------------

DecToBcd::DecToBcd( QObject *parent, const QString &type, const QString &id )
          : LogicBase( parent, type, id )
{
    buildPins();
}

void DecToBcd::buildPins()
{
    rebuildPins();

    const int h = rowHalf( 10 );

    const QRectF body = bodyRect( 10 );

    QVector<int> in, out;

    for( int i = 0; i < 10; i++ )
    {
        addPin( -LOGIC_PINX, -h + LOGIC_CELL*i,
                "inputpin" + QString::number( i ), 180 );
        in.append( numPins() - 1 );
    }

    for( int i = 0; i < 4; i++ )
    {
        addPin( LOGIC_PINX, -h + LOGIC_CELL*i,
                "outputpin" + QString::number( i ), 0 );
        out.append( numPins() - 1 );
    }

    setArea( body );
    setPinRoles( in, out );

    setLabelY( int( body.top() ) - 20 );

    publishPins();
}

void DecToBcd::compute()
{
    // Searched from the top down and the first hit taken, which is what makes
    // "more than one line high" have an answer: the highest-numbered line wins.
    // Any other tie-break - the lowest, the parity of all of them - is equally
    // arbitrary and less predictable, and the alternative to picking one is a
    // device whose output depends on the order readInputHard() happened to be
    // called in.
    int code = 0;

    for( int i = 9; i >= 0; i-- )
        if( readInputHard( i ) ) { code = i; break; }

    for( int i = 0; i < 4; i++ )
        writeOutput( i, ( code >> i ) & 1 );
}

Component* createDecToBcd( QObject *parent, const QString &type, const QString &id )
{
    return new DecToBcd( parent, type, id );
}

// ---------------------------------------------------------------------------
// BcdTo7S
// ---------------------------------------------------------------------------

// Common-cathode, bit 0 = segment a through bit 6 = segment g, which is the
// order the segments are named in and the order a datasheet's truth table lists
// them in.  One table and three readers - this IC, the display below and the
// SevenSegment in outputs.cpp - because three copies would eventually disagree
// about what a 6 looks like and the disagreement would be invisible until someone
// wired up a clock.
static const quint8 kSeg7[10] =
{
    0x3F,   // 0: a b c d e f
    0x06,   // 1: b c
    0x5B,   // 2: a b d e g
    0x4F,   // 3: a b c d g
    0x66,   // 4: b c f g
    0x6D,   // 5: a c d f g
    0x7D,   // 6: a c d e f g
    0x07,   // 7: a b c
    0x7F,   // 8: all
    0x6F    // 9: a b c d f g
};

quint8 BcdTo7S::segments( int digit )
{
    if( digit < 0 || digit > 9 ) return 0;

    return kSeg7[digit];
}

BcdTo7S::BcdTo7S( QObject *parent, const QString &type, const QString &id )
       : LogicBase( parent, type, id )
{
    buildPins();
}

void BcdTo7S::buildPins()
{
    rebuildPins();

    // Seven rows for the seven segment lines, and the four code inputs take the
    // top four of them.
    const int h = rowHalf( 7 );

    const QRectF body = bodyRect( 7 );

    QVector<int> in, out;

    for( int i = 0; i < 4; i++ )
    {
        addPin( -LOGIC_PINX, -h + LOGIC_CELL*i,
                "inputpin" + QString::number( i ), 180 );
        in.append( numPins() - 1 );
    }

    for( int i = 0; i < 7; i++ )
    {
        // Named for the segment rather than numbered, because the seven outputs of
        // a decoder are not interchangeable and a wire attached to "outputpin3"
        // says nothing about which segment it lights.  These are the ids a .simu
        // names the pins by, and they are the datasheet's letters.
        const char name = char( 'a' + i );

        addPin( LOGIC_PINX, -h + LOGIC_CELL*i,
                QString( "segmentpin" ) + QLatin1Char( name ), 0 );
        out.append( numPins() - 1 );
    }

    setArea( body );
    setPinRoles( in, out );

    setLabelY( int( body.top() ) - 20 );

    publishPins();
}

void BcdTo7S::compute()
{
    int code = 0;

    for( int i = 0; i < 4; i++ )
        if( readInputHard( i ) ) code |= ( 1 << i );

    // segments() blanks anything above nine, so the loop below needs no test of
    // its own and a code of 13 turns every segment off rather than lighting the
    // pattern for 13 & 7.
    const quint8 s = segments( code );

    for( int i = 0; i < 7; i++ )
        writeOutput( i, ( s >> i ) & 1 );
}

Component* createBcdTo7S( QObject *parent, const QString &type, const QString &id )
{
    return new BcdTo7S( parent, type, id );
}

// ---------------------------------------------------------------------------
// SevenSegmentBCD
// ---------------------------------------------------------------------------

SevenSegmentBCD::SevenSegmentBCD( QObject *parent, const QString &type,
                                  const QString &id )
                 : LogicBase( parent, type, id )
                 , m_seg( 0 )
{
    buildPins();
}

void SevenSegmentBCD::buildPins()
{
    rebuildPins();

    const int h = rowHalf( 4 );

    QVector<int> in, out;

    for( int i = 0; i < 4; i++ )
    {
        addPin( -LOGIC_PINX, -h + LOGIC_CELL*i,
                "inputpin" + QString::number( i ), 180 );
        in.append( numPins() - 1 );
    }

    // No outputs, and an empty vector rather than a skipped call: setPinRoles()
    // sizes m_inPrev from numPins() and calls initPins(), both of which this
    // needs, and a device with nothing to drive is not a special case.
    setArea( QRectF( -LOGIC_HALF, -20, 44, 40 ) );
    setPinRoles( in, out );

    setLabelY( int( m_area.top() ) - 20 );

    publishPins();
}

void SevenSegmentBCD::compute()
{
    int code = 0;

    for( int i = 0; i < 4; i++ )
        if( readInputHard( i ) ) code |= ( 1 << i );

    m_seg = BcdTo7S::segments( code );
}

void SevenSegmentBCD::updateDisplay()
{
    // Once per GUI tick, and compute() rather than a cached read: the tick is
    // 60 Hz and the read is four voltage comparisons, while the alternative -
    // letting settleLogic() drive the repaint - would call update() on every logic
    // transition in the circuit and cost more than the simulation itself.
    compute();

    update();
}

void SevenSegmentBCD::paint( QPainter *painter,
                             const QStyleOptionGraphicsItem *option,
                             QWidget *widget )
{
    // Component rather than LogicBase: there is no IEC label to write and no box
    // to write it on, and the digit is the whole of the symbol.
    Component::paint( painter, option, widget );

    painter->drawRoundedRect( m_area, 2, 2 );

    // The seven segments as thick lines rather than as polygons.  A polygon set is
    // what a real display has - each segment is a hexagon - and at the twelve
    // units a digit is drawn at here the difference is invisible while the code is
    // four times as long.  The gap between the horizontal and the vertical runs is
    // two units, which is what stops a lit 'a' and a lit 'b' merging into a corner
    // with no joint in it.
    const QPointF a0( -6, -14 ), a1( 6, -14 );   // top
    const QPointF b0(  7, -13 ), b1( 7,  -1 );   // upper right
    const QPointF c0(  7,   1 ), c1( 7,  13 );   // lower right
    const QPointF d0( -6,  14 ), d1( 6,  14 );   // bottom
    const QPointF e0( -7,   1 ), e1( -7, 13 );   // lower left
    const QPointF f0( -7, -13 ), f1( -7, -1 );   // upper left
    const QPointF g0( -6,   0 ), g1( 6,   0 );   // middle

    static const QPointF *kSeg[7][2] =
    {
        { &a0, &a1 }, { &b0, &b1 }, { &c0, &c1 }, { &d0, &d1 },
        { &e0, &e1 }, { &f0, &f1 }, { &g0, &g1 }
    };

    // Drawn unlit first and lit second, in two passes rather than one with a pen
    // change per segment: the lit colour is brighter and a pen swap inside the loop
    // would leave the last segment's pen behind for whatever the caller draws next.
    for( int pass = 0; pass < 2; pass++ )
    {
        painter->setPen( QPen( pass ? QColor( 255, 60, 40 ) : QColor( 70, 70, 70 ),
                               3, Qt::SolidLine, Qt::FlatCap ) );

        for( int i = 0; i < 7; i++ )
        {
            const bool lit = ( m_seg >> i ) & 1;
            if( lit != ( pass != 0 ) ) continue;

            painter->drawLine( *kSeg[i][0], *kSeg[i][1] );
        }
    }

    painter->setPen( kBodyPen );
}

Component* createSevenSegmentBCD( QObject *parent, const QString &type,
                                  const QString &id )
{
    return new SevenSegmentBCD( parent, type, id );
}

// ---------------------------------------------------------------------------
// ADC
// ---------------------------------------------------------------------------

// A floor on a reference voltage.  Not because a zero reference is meaningful - it
// is not, it is a converter with an infinite code per volt - but because the
// division by it is reached from a pin that reads zero whenever it is unwired, and
// an unwired Vref has to give a full-scale answer rather than a NaN that spreads
// through every output bit.
#define ADC_VREF_FLOOR 1e-6

ADC::ADC( QObject *parent, const QString &type, const QString &id )
   : LogicBase( parent, type, id )
   , m_bits( 0 )
   , m_vref( 5.0 )
   , m_clkPrev( false )
   , m_iAnalog( -1 )
   , m_iVref( -1 )
   , m_iClock( -1 )
   , m_iEnable( -1 )
{
    setSize( 8 );
}

void ADC::setSize( int n )
{
    if( n < 1 ) n = 1;
    if( n > LOGIC_MAX_BITS ) n = LOGIC_MAX_BITS;

    if( n == m_bits ) return;

    m_bits = n;

    buildPins();
}

void ADC::setVref( double v )
{
    if( v < ADC_VREF_FLOOR ) v = ADC_VREF_FLOOR;

    if( v == m_vref ) return;

    m_vref = v;

    // A reference change moves the code for the same input voltage, so the
    // conversion is re-run rather than left until the next clock: on a stopped
    // circuit the next clock is never, and a panel that says Vref=3.3 next to a
    // display still showing the 5 V conversion reads as a property that does
    // nothing.
    compute();
    applyOutputs();
}

void ADC::buildPins()
{
    rebuildPins();

    // Three rows at least, so the two analog pins on the left have somewhere to be
    // that is not on top of each other: at one bit a row of one would put both of
    // them at y=0.
    const int rows = qMax( m_bits, 3 );
    const int h    = rowHalf( rows );

    const QRectF body = bodyRectCtrl( rows, 1 );

    QVector<int> in, out;

    addPin( -LOGIC_PINX, -h, "analogpin", 180 );
    m_iAnalog = numPins() - 1;
    in.append( m_iAnalog );

    addPin( -LOGIC_PINX, -h + LOGIC_CELL, "vrefpin", 180 );
    m_iVref = numPins() - 1;
    in.append( m_iVref );

    for( int i = 0; i < m_bits; i++ )
    {
        addPin( LOGIC_PINX, -h + LOGIC_CELL*i,
                "outputpin" + QString::number( i ), 0 );
        out.append( numPins() - 1 );
    }

    addPin( int( ctrlX( 0, 2 ) ), int( ctrlY( body ) ), "clockpin", 90 );
    m_iClock = numPins() - 1;
    in.append( m_iClock );

    addPin( int( ctrlX( 1, 2 ) ), int( ctrlY( body ) ), "enablepin", 90 );
    m_iEnable = numPins() - 1;
    in.append( m_iEnable );

    setArea( body );
    setPinRoles( in, out );

    // After setPinRoles(), which sets isAdmit() false on every input without
    // exception and for a good reason.  These two are not logic inputs: they are
    // nodes the converter has to read a real voltage from, and a pin that reads a
    // voltage has to be in the matrix to have one.  The cost is that any net they
    // touch loses the single-node fast path, which is the correct price - a net
    // with an ADC on it is an analog net.
    if( Pin *p = pin( m_iAnalog ) ) p->setIsAdmit( true );
    if( Pin *p = pin( m_iVref )   ) p->setIsAdmit( true );

    if( Pin *p = pin( m_iClock ) ) p->setLabelText( QStringLiteral( "CLK" ) );
    if( Pin *p = pin( m_iEnable )) p->setLabelText( QStringLiteral( "EN" ) );

    setLabelY( int( body.top() ) - 20 );

    publishPins();
}

void ADC::stamp()
{
    LogicBase::stamp();

    // The two analog pins' leakage, and it is stamped on every pass rather than
    // once at build time because the matrix is zeroed between them.  Both pins
    // declared an admittance, so both of their nodes have a row, and a row that
    // nothing is written into is a row of zeros - which is the singular matrix
    // switches.h warns about at length.
    if( Pin *p = pin( m_iAnalog ) ) p->stampAdmitance( 1/LOGIC_ANALOG_IMPED );
    if( Pin *p = pin( m_iVref )   ) p->stampAdmitance( 1/LOGIC_ANALOG_IMPED );
}

void ADC::compute()
{
    Pin *ana = pin( m_iAnalog );
    if( !ana ) return;

    Pin *clk = pin( m_iClock );
    Pin *en  = pin( m_iEnable );

    const bool now  = readPin( clk );
    const bool edge = now && !m_clkPrev;

    m_clkPrev = now;

    // Enable and Clock both treat "nothing wired" as "asserted", and it is the
    // same argument twice: readPin() says a floating input is a zero, which is
    // right for a data line and wrong for a control whose only job is to stop the
    // device working.  An ADC dropped on a sheet with its input and its outputs
    // wired and nothing else has to convert.
    const bool go = ( !en  || !en->connector()  || readPin( en )  )
                 && ( !clk || !clk->connector() || edge );

    if( !go ) return;

    double vref = m_vref;

    // Taken off the pin when something is wired to it, so a sheet that generates
    // its own reference - a pot across a rail, which is how a real one is trimmed -
    // gets the reference it drew rather than the number in the panel.
    if( Pin *p = pin( m_iVref ) )
        if( p->connector() ) vref = p->getVolt();

    if( vref < ADC_VREF_FLOOR ) vref = ADC_VREF_FLOOR;

    // Truncating rather than rounding, which is what an ideal converter does: the
    // code is the number of whole steps the input has passed, and rounding would
    // put code 0 at half a step wide and every other code at one.
    const double v    = ana->getVolt();
    const int    full = ( 1 << m_bits );

    int code = int( v/vref*full );

    if( code < 0 )          code = 0;
    if( code > full - 1 )   code = full - 1;

    for( int i = 0; i < m_bits; i++ )
        writeOutput( i, ( code >> i ) & 1 );
}

Component* createADC( QObject *parent, const QString &type, const QString &id )
{
    return new ADC( parent, type, id );
}

// ---------------------------------------------------------------------------
// DAC
// ---------------------------------------------------------------------------

DAC::DAC( QObject *parent, const QString &type, const QString &id )
   : LogicBase( parent, type, id )
   , m_bits( 0 )
   , m_vref( 5.0 )
   , m_vout( 0 )
   , m_iVref( -1 )
   , m_iOut( -1 )
   , m_iEnable( -1 )
{
    setSize( 8 );
}

void DAC::setSize( int n )
{
    if( n < 1 ) n = 1;
    if( n > LOGIC_MAX_BITS ) n = LOGIC_MAX_BITS;

    if( n == m_bits ) return;

    m_bits = n;

    buildPins();
}

void DAC::setVref( double v )
{
    if( v < ADC_VREF_FLOOR ) v = ADC_VREF_FLOOR;

    if( v == m_vref ) return;

    m_vref = v;

    compute();
}

void DAC::buildPins()
{
    rebuildPins();

    const int rows = qMax( m_bits, 2 );
    const int h    = rowHalf( rows );

    const QRectF body = bodyRectCtrl( rows, 1 );

    QVector<int> in, out;

    for( int i = 0; i < m_bits; i++ )
    {
        addPin( -LOGIC_PINX, -h + LOGIC_CELL*i,
                "inputpin" + QString::number( i ), 180 );
        in.append( numPins() - 1 );
    }

    addPin( -LOGIC_PINX, h, "vrefpin", 180 );
    m_iVref = numPins() - 1;
    in.append( m_iVref );

    // The analog output is deliberately NOT in the outputs vector.  An entry in
    // m_outIdx makes LogicBase::stamp() drive it as a logic level through
    // m_outImped, which is ten ohms and two rail voltages - and what hangs off a
    // DAC is a reconstruction filter and an amplifier, neither of which wants a
    // driver that can only reach 0 V or 5 V.  It is stamped by DAC::stamp() below
    // instead, as a real source.
    addPin( LOGIC_PINX, 0, "outpin", 0 );
    m_iOut = numPins() - 1;

    addPin( 0, int( ctrlY( body ) ), "enablepin", 90 );
    m_iEnable = numPins() - 1;
    in.append( m_iEnable );

    setArea( body );
    setPinRoles( in, out );

    Pin *o = pin( m_iOut );
    if( o )
    {
        // Output and not a load, so an unloaded DAC output stays a single node and
        // solveSingle() takes the voltage straight off the pin.
        o->setIsOutput( true );
        o->setIsAdmit( false );
        o->setVoltOut( 0 );
    }

    if( Pin *p = pin( m_iVref ) ) p->setIsAdmit( true );

    if( Pin *p = pin( m_iEnable ) ) p->setLabelText( QStringLiteral( "EN" ) );

    setLabelY( int( body.top() ) - 20 );

    publishPins();
}

void DAC::stamp()
{
    LogicBase::stamp();

    if( Pin *p = pin( m_iVref ) ) p->stampAdmitance( 1/LOGIC_ANALOG_IMPED );

    // ESOURCE_IMPED rather than m_outImped: see buildPins() for why the analog
    // output is not a logic output, of which this is the second half.  A milliohm
    // holds the voltage up against anything a filter presents.
    Pin *o = pin( m_iOut );
    if( o )
    {
        o->setIsOutput( true );
        stampSource( o, m_vout, ESOURCE_IMPED );
    }
}

void DAC::compute()
{
    quint32 code = 0;

    // Hard reads, and here the reason is stronger than for an address: a code bit
    // is a weight, and a hysteresis state that survived from the previous code
    // would make the same set of input voltages produce a different number than
    // the one on the wires.
    for( int i = 0; i < m_bits; i++ )
        if( readInputHard( i ) ) code |= ( 1u << i );

    Pin *en = pin( m_iEnable );
    if( en && en->connector() && !readPin( en ) ) return;

    double vref = m_vref;

    if( Pin *p = pin( m_iVref ) )
        if( p->connector() ) vref = p->getVolt();

    if( vref < ADC_VREF_FLOOR ) vref = ADC_VREF_FLOOR;

    // Full scale is vref*(2^n - 1)/2^n and not vref, which is the one place an
    // ideal converter and a real one agree: the top code is one step below the
    // reference because the reference itself is not a code.  Dividing by 2^n
    // rather than by 2^n - 1 is what keeps the step size uniform, and the cost is
    // that a DAC asked for its maximum gives 4.98 V out of a 5 V reference - which
    // is what the part on the bench does.
    m_vout = vref*code/( 1u << m_bits );
}

Component* createDAC( QObject *parent, const QString &type, const QString &id )
{
    return new DAC( parent, type, id );
}

// ---------------------------------------------------------------------------
// Bus
// ---------------------------------------------------------------------------

Bus::Bus( QObject *parent, const QString &type, const QString &id )
   : Component( parent, type, id )
   , m_size( 0 )
{
    setSize( 8 );
}

void Bus::setSize( int n )
{
    if( n < 1 ) n = 1;
    if( n > LOGIC_MAX_BITS ) n = LOGIC_MAX_BITS;

    if( n == m_size ) return;

    m_size = n;

    buildPins();
}

void Bus::buildPins()
{
    // clearPins() and not rebuildPins(): there is no LogicBase in this class's
    // ancestry and nothing to clear but the pins.  The joins go with them -
    // dropPins() removes every pair naming an index at or above the cut, and a
    // join left behind would solder two of the new pins together, which is the
    // short that moves when the width changes.
    clearPins();

    const int h = rowHalf( m_size );

    for( int i = 0; i < m_size; i++ )
    {
        addPin( -LOGIC_PINX, -h + LOGIC_CELL*i,
                "inpin" + QString::number( i ), 180 );
        addPin( LOGIC_PINX, -h + LOGIC_CELL*i,
                "outpin" + QString::number( i ), 0 );

        // The join is the device.  Circuit::updateNodes() unions a joined pair
        // exactly as it unions the two ends of a wire, so pin i on the left and
        // pin i on the right end up on one eNode and there is nothing left to
        // compute - which is why isSimulable() is false and why this class has no
        // stamp().
        joinPins( 2*i, 2*i + 1 );
    }

    // Cosmetic, and only cosmetic: eNode::isBus() is read by updateNodes() to set
    // the node's flag, which Connector::paint() turns into a three-unit line in
    // the bus colour instead of a two-unit one in the wire colour.  A wire
    // leaving a bus should look like the bus, and nothing in the solver reads it.
    for( int i = 0; i < numPins(); i++ )
        if( Pin *p = pin( i ) )
        {
            p->setIsBus( true );
            p->setBusWidth( m_size );
        }

    initPins();

    setArea( QRectF( -LOGIC_HALF, -h - LOGIC_CELL/2,
                     2*LOGIC_HALF, 2*h + LOGIC_CELL ) );

    // Named, because a sheet with three buses on it has nothing else to tell them
    // apart, and the width is written under it for the same reason a resistor
    // shows its value.
    setShowId( true );
    setLabelX( int( m_area.left() ) );
    setLabelY( int( m_area.top() ) - 20 );
    setLabelRot( 0 );

    setShowVal( true );
    setValLabelX( int( m_area.left() ) );
    setValLabelY( int( m_area.bottom() ) + 8 );
    setValLabRot( 0 );

    if( Circuit *circ = circuit() )
    {
        for( int i = 0; i < numPins(); i++ )
            if( Pin *p = pin( i ) ) circ->addPin( p );

        circ->updateNodes();
    }
}

QString Bus::valLabelText() const
{
    return QString::number( m_size );
}

void Bus::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                 QWidget *widget )
{
    Component::paint( painter, option, widget );

    // The sleeve first, and thin: it is a frame around the bundle rather than one
    // of the conductors in it, and drawing it at bus weight would make the outline
    // and the outermost line the same stroke.
    painter->drawRoundedRect( m_area, 2, 2 );

    // One thick line per row, which is the join drawn.  Bus weight rather than
    // wire weight so the bundle and the wires leaving it are the same stroke, and
    // a crossing wire - which the brush would otherwise have to hide - reads as a
    // crossing rather than as a junction.
    painter->setPen( QPen( QColor( 0, 0, 0 ), 3, Qt::SolidLine,
                           Qt::FlatCap, Qt::RoundJoin ) );

    const int h = rowHalf( m_size );

    for( int i = 0; i < m_size; i++ )
    {
        const qreal y = -h + LOGIC_CELL*i;

        painter->drawLine( QPointF( -LOGIC_HALF, y ), QPointF( LOGIC_HALF, y ) );
    }

    painter->setPen( kBodyPen );
}

Component* createBus( QObject *parent, const QString &type, const QString &id )
{
    return new Bus( parent, type, id );
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

// The level a control or address pin reads at, and the level a data pin drives.
// Memory is a Chip rather than a LogicBase, so it has no Input_High_V of its own
// and no property panel entry for one; it takes the family's defaults.  Which is
// also the answer to why it has no supply pins: a RAM whose rails were terminals
// would need its thresholds and its drive levels to follow them, and every sheet
// that used one would have to wire two pins that do nothing but say "5 volts".
#define MEM_HIGH LOGIC_IN_HIGH
#define MEM_DRIVE_HIGH LOGIC_OUT_HIGH
#define MEM_DRIVE_LOW LOGIC_OUT_LOW
#define MEM_DRIVE_IMPED LOGIC_OUT_IMPED

// What an undriven bidirectional pin presents.  Zero would be a short to whatever
// the net is doing; nothing at all is what a tristated output actually is.  The
// figure is never stamped - isAdmit() stays false on every data pin - so it exists
// only to say, in one place, that the answer is "no admittance".
#define MEM_TRISTATE 1e30

// The package.  Twenty-eight pins because that is the smallest standard DIP that
// holds the widest device the properties allow: three controls, sixteen address
// bits and eight data bits is twenty-seven.
static const char kMemPackage[] = "dip28";

Memory::Memory( QObject *parent, const QString &type, const QString &id )
      : Chip( parent, type, id )
      , m_addrBits( 8 )
      , m_dataBits( 8 )
      , m_persist( false )
      , m_writePrev( false )
      , m_lastAddr( -1 )
      , m_iCs( 0 )
      , m_iWe( 1 )
      , m_iOe( 2 )
      , m_addrBase( 3 )
      , m_dataBase( 11 )
{
    // In the constructor body and not from Chip's, because initChip() is virtual
    // and a call from a base constructor dispatches to the base: Chip::initChip()
    // would lay out the DIP and Memory::assignRoles() would never run, leaving a
    // memory with twenty-eight pins labelled 1 to 28 and no idea which is which.
    // From here the vtable is Memory's and the override is reached.
    setPackage( QLatin1String( kMemPackage ) );

    setShowVal( true );
    setValLabelX( 0 );
    setValLabelY( int( m_area.bottom() ) + 8 );
    setValLabRot( 0 );
}

void Memory::setAddrBits( int n )
{
    if( n < 1 ) n = 1;
    if( n > LOGIC_MAX_BITS ) n = LOGIC_MAX_BITS;

    // Against the pins that exist rather than against the cap alone: the widths
    // are properties and the package is a fixed twenty-eight, so a width that does
    // not fit has to be refused here rather than silently run off the end of the
    // pin list, where assignRoles() would rename nothing and the data pins would
    // be addresses.
    const int room = numPins() - 3 - m_dataBits;
    if( n > room ) n = room;
    if( n < 1 ) n = 1;

    if( n == m_addrBits ) return;

    m_addrBits = n;

    // The contents go with the width.  A memory widened from eight address bits to
    // twelve has four times as many cells and no opinion about what is in the new
    // ones; keeping the old array in the low half would be a defensible choice,
    // but the honest one is that resizing a RAM is not something a real part does
    // and there is no state worth preserving across it.
    m_mem.fill( 0 );
    m_mem.resize( 1 << m_addrBits );

    m_lastAddr = -1;

    assignRoles();
}

void Memory::setDataBits( int n )
{
    if( n < 1 ) n = 1;
    if( n > 8 ) n = 8;

    const int room = numPins() - 3 - m_addrBits;
    if( n > room ) n = room;
    if( n < 1 ) n = 1;

    if( n == m_dataBits ) return;

    m_dataBits = n;

    m_mem.fill( 0 );
    m_mem.resize( 1 << m_addrBits );

    m_lastAddr = -1;

    assignRoles();
}

bool Memory::initChip()
{
    // Chip's half first: it reads the package file, or lays out the DIP its name
    // ends in when there is no file, and either way there are pins by the time it
    // returns.  Roles go on afterwards because they depend on the widths, which
    // are properties and arrive from the file after the package does.
    if( !Chip::initChip() ) return false;

    assignRoles();

    // And again after.  Chip::initChip() asked the circuit for a node graph when
    // it had finished making pins, but assignRoles() has just decided which of
    // them declare an admittance - and that is what tells eNode::initialize()
    // whether a net needs a matrix row.  A graph built before the decision is a
    // graph built on the wrong answer.
    if( Circuit *circ = circuit() ) circ->updateNodes();

    return true;
}

void Memory::assignRoles()
{
    m_iCs      = 0;
    m_iWe      = 1;
    m_iOe      = 2;
    m_addrBase = 3;
    m_dataBase = 3 + m_addrBits;

    m_mem.fill( 0 );
    m_mem.resize( 1 << m_addrBits );

    if( Pin *p = pin( m_iCs ) ) p->setLabelText( QStringLiteral( "CS" ) );
    if( Pin *p = pin( m_iWe ) ) p->setLabelText( QStringLiteral( "WE" ) );
    if( Pin *p = pin( m_iOe ) ) p->setLabelText( QStringLiteral( "OE" ) );

    for( int i = 0; i < m_addrBits; i++ )
        if( Pin *p = pin( m_addrBase + i ) )
            p->setLabelText( "A" + QString::number( i ) );

    for( int i = 0; i < m_dataBits; i++ )
        if( Pin *p = pin( m_dataBase + i ) )
        {
            p->setLabelText( "D" + QString::number( i ) );

            // Bidirectional and never a load.  isAdmit() stays clear on both
            // halves of the pin's life, so a data net with nothing on it but
            // memories stays single and out of the matrix; isOutput() is what
            // moves, per step, in stamp() below.
            p->setIsAdmit( false );
            p->setIsOutput( false );
        }

    // Every pin the widths do not reach is marked unused rather than left looking
    // like a terminal: a 28-pin body holding a 19-pin device has nine positions
    // that a wire must not be attachable to, and Pin::paint() hides the square and
    // the label of an unused pin, which is the drawing of "nothing behind this".
    const int used = m_dataBase + m_dataBits;

    for( int i = used; i < numPins(); i++ )
        if( Pin *p = pin( i ) )
        {
            p->setUnused( true );
            p->setIsAdmit( false );
            p->setIsOutput( false );
        }

    for( int i = 0; i < used; i++ )
        if( Pin *p = pin( i ) ) p->setUnused( false );

    update();
}

// The level a pin is at.  A bare threshold and not a Schmitt trigger: a memory's
// controls are read fresh every step and there is no state to remember a level
// against, which is the distinction LogicBase::readInputHard() makes and the
// reason that one exists.
static bool memLevel( Pin *p )
{
    return p && p->getVolt() > MEM_HIGH;
}

void Memory::stamp()
{
    // Read path.  CS and OE both have to be asserted for the array to drive, and
    // the test is on the levels rather than on a state computed in updateStep()
    // because stamp() is what runs between the zeroing and the solve: a read has
    // to reflect the address the solve is about to produce voltages for, and
    // deferring it to updateStep() would put every read one step behind the bus
    // driving it.
    const bool cs = memLevel( pin( m_iCs ) );
    const bool oe = memLevel( pin( m_iOe ) );
    const bool drive = cs && oe;

    int addr = 0;
    for( int i = 0; i < m_addrBits; i++ )
        if( memLevel( pin( m_addrBase + i ) ) ) addr |= ( 1 << i );

    if( m_mem.isEmpty() ) return;

    addr &= ( m_mem.size() - 1 );

    quint8 byte = m_mem.at( addr );

    // A write taken on the previous step at this same address is read back here
    // rather than from the array, which is what m_lastAddr is for: stamp() runs
    // before updateStep() in a step, so without it a bus that writes and then
    // reads in the same step sees the old contents and a memory-mapped register
    // appears to be one step slow.
    if( addr == m_lastAddr ) byte = m_mem.at( addr );

    for( int i = 0; i < m_dataBits; i++ )
    {
        Pin *p = pin( m_dataBase + i );
        if( !p ) continue;

        // Set on every stamp and cleared on every stamp, in both directions.  A
        // pin left flagged as an output after the device stopped driving is a net
        // with two drivers on it, and eNode::solveSingle() takes whichever one it
        // reaches first.
        p->setIsOutput( drive );

        if( !drive ) continue;

        const bool bit = ( byte >> i ) & 1;

        stampSource( p, bit ? MEM_DRIVE_HIGH : MEM_DRIVE_LOW, MEM_DRIVE_IMPED );
    }
}

void Memory::updateStep()
{
    // Write path, and here rather than in stamp() for the reason the header gives:
    // WE is edged, and stamp() runs once per non-linear pass rather than once per
    // step, so a write taken from stamp() on a circuit that happens to contain a
    // diode happens five times per step.  It would still write the same byte four
    // of those times - but the auto-increment below would move the pointer four
    // cells further than the master intended, and the failure is a memory that
    // fills in every fourth location.
    const bool we = memLevel( pin( m_iWe ) );

    const bool edge = we && !m_writePrev;

    m_writePrev = we;

    if( !edge ) return;

    if( !memLevel( pin( m_iCs ) ) ) return;

    if( m_mem.isEmpty() ) return;

    int addr = 0;
    for( int i = 0; i < m_addrBits; i++ )
        if( memLevel( pin( m_addrBase + i ) ) ) addr |= ( 1 << i );

    addr &= ( m_mem.size() - 1 );

    quint8 byte = 0;
    for( int i = 0; i < m_dataBits; i++ )
        if( memLevel( pin( m_dataBase + i ) ) ) byte |= quint8( 1 << i );

    m_mem[addr]  = byte;
    m_lastAddr   = addr;
}

void Memory::resetStep()
{
    // The reactive list is walked from both setSimuRate() and startSim(), which is
    // the hook the Persistence property hangs off: off, a Start clears the array,
    // because that is what a RAM does when its supply goes away and what makes a
    // counter built out of one begin from zero rather than from whatever the last
    // run left in it.
    if( !m_persist ) m_mem.fill( 0 );

    m_writePrev = false;
    m_lastAddr  = -1;
}

void Memory::nodesUpdated()
{
    // Nothing to register.  The Simulator's reactive list is walked every step and
    // updateStep() reaches this from there, so there is no changedFast request to
    // renew - and making one would put the write edge on two lists, which is the
    // double-count the header's isReactive() comment exists to prevent.
    //
    // What is needed is one evaluation, so a sheet that has just been loaded shows
    // the byte at the address its pins are already at rather than an empty symbol
    // until something clocks.
    stamp();
}

QString Memory::valLabelText() const
{
    // The geometry rather than a value: two numbers and a shift, because
    // "32K x 8" is how a memory is named on a datasheet and how a user picks one.
    const int cells = 1 << m_addrBits;

    QString sz;
    if( cells >= 1024 ) sz = QString::number( cells/1024 ) + QLatin1Char( 'K' );
    else                sz = QString::number( cells );

    return sz + " x " + QString::number( m_dataBits );
}

void Memory::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                    QWidget *widget )
{
    Chip::paint( painter, option, widget );

    if( m_isLS ) return;

    // The function written on the body, which is the only thing that distinguishes
    // this from the AVR next to it once both are dark boxes with twenty-eight pins.
    painter->setPen( kIcText );
    painter->drawText( m_area, Qt::AlignCenter, QStringLiteral( "RAM" ) );
    painter->setPen( kBodyPen );
}

Component* createMemory( QObject *parent, const QString &type, const QString &id )
{
    return new Memory( parent, type, id );
}

// ---------------------------------------------------------------------------
// I2CBase
// ---------------------------------------------------------------------------

// What an open-drain output pulls through.  The family's ten ohms rather than the
// milliohm an ideal source uses, because SDA is shared: a slave that pulled the
// line low through a milliohm would dominate the pull-up resistor the user drew
// and the rise time the sheet shows would be the one this file picked rather than
// the one the resistor did.
#define I2C_PULL_IMPED LOGIC_OUT_IMPED

I2CBase::I2CBase( QObject *parent, const QString &type, const QString &id )
         : LogicBase( parent, type, id )
         , m_iScl( -1 )
         , m_iSda( -1 )
         , m_code( 0x50 )
         , m_sclPrev( false )
         , m_sdaPrev( false )
         , m_active( false )
         , m_reading( false )
         , m_addressed( false )
         , m_ackPending( false )
         , m_sdaLow( false )
         , m_txFrame( false )
         , m_bitCount( 0 )
         , m_shift( 0 )
         , m_addrCount( 0 )
{
    m_addrBytes[0] = 0;
    m_addrBytes[1] = 0;
}

void I2CBase::setControlCode( int c )
{
    // Seven bits, and the eighth is not stored: the low bit of a byte on the wire
    // is the direction flag, so a Control_Code of 0xA1 would be an address of 0x50
    // and a request to read, and keeping it would mean the property said two
    // things one of which the master decides.
    c &= 0x7F;

    if( c == m_code ) return;

    m_code = c;

    // A slave that stops being addressed mid-transaction has to let go of the bus.
    // Leaving m_addressed set would have it acknowledging bytes for an address it
    // no longer has, which on a shared bus is two devices driving SDA at once.
    m_active    = false;
    m_addressed = false;
    m_ackPending= false;
    m_sdaLow    = false;
    m_txFrame   = false;
    m_bitCount  = 0;
    m_shift     = 0;
}

void I2CBase::compute()
{
    runBus();
}

void I2CBase::stamp()
{
    LogicBase::stamp();

    Pin *sda = pin( m_iSda );
    if( !sda ) return;

    // Open drain, and the asymmetry is the whole of it: the pin is either driven
    // to ground or it is nothing at all, and there is no third state in which it
    // drives high.  A slave that drove a one would fight the pull-up resistor and
    // every other slave on the bus, and the bus would not so much fail as work
    // intermittently in a way that depends on which device spoke last.
    sda->setIsOutput( m_sdaLow );

    if( m_sdaLow ) stampSource( sda, 0, I2C_PULL_IMPED );
}

void I2CBase::loadTxByte()
{
    quint8 b = 0;

    const bool have = driveRead( b );

    // Shifted here rather than left for the mid-frame edge to do, and the reason is
    // an off-by-one that is invisible on a scope and wrong in the data: this call
    // happens on the falling edge that closes the previous frame, which is the same
    // edge that has to present bit 7.  Leaving the shift to the next falling edge
    // would present bit 7 twice and drop bit 0.
    m_shift  = quint16( b << 1 );
    m_sdaLow = have ? !( b & 0x80 ) : false;

    if( !have )
    {
        // Nothing to say.  Releasing the line is the only honest answer: the
        // master sees a one it did not get from a driver, and the transaction
        // ends the way it ends when a slave disappears.
        m_txFrame = false;
        m_active  = false;
    }
}

void I2CBase::runBus()
{
    Pin *pscl = pin( m_iScl );
    Pin *psda = pin( m_iSda );
    if( !pscl || !psda ) return;

    // The midpoint of the trigger window rather than its upper edge: SCL and SDA
    // are read as levels to find an edge between them, and reading at the top of
    // the window would put the START condition at a different voltage from the one
    // the data bits are sampled at.
    const double mid = ( m_inHighV + m_inLowV )*0.5;

    const bool scl = pscl->getVolt() > mid;
    const bool sda = psda->getVolt() > mid;

    // START and STOP first, and tested before the clock edges: both are SDA moving
    // while SCL is high, which is the one thing the bus protocol says cannot
    // happen during a bit, and a slave that looked at the clock first would see a
    // START as a data bit on a line that was already high.
    if( scl && m_sdaPrev && !sda )
    {
        // Repeated START lands here too, which is what makes it one: the frame is
        // abandoned wherever it got to and a fresh address byte is expected.
        m_active     = true;
        m_addressed  = false;
        m_reading    = false;
        m_ackPending = false;
        m_txFrame    = false;
        m_sdaLow     = false;
        m_bitCount   = 0;
        m_shift      = 0;
        m_addrCount  = 0;
    }
    else if( scl && !m_sdaPrev && sda )
    {
        m_active     = false;
        m_addressed  = false;
        m_ackPending = false;
        m_txFrame    = false;
        m_sdaLow     = false;
        m_bitCount   = 0;
        m_shift      = 0;
    }
    else if( m_active )
    {
        if( scl && !m_sclPrev )
        {
            // ---- rising edge: the bit on the line is valid --------------------
            if( m_bitCount < 8 )
            {
                if( !m_txFrame )
                    m_shift = quint16( ( m_shift << 1 ) | ( sda ? 1 : 0 ) );

                m_bitCount++;
            }
            else if( m_ackPending && m_txFrame )
            {
                // The master's ninth bit, and the only bit in a frame that travels
                // the other way while we are transmitting.  Stored in m_shift
                // because there is nothing else in it at that moment - the byte it
                // held has already been driven out - and one means NACK.
                m_shift = sda ? 1 : 0;
            }
        }
        else if( !scl && m_sclPrev )
        {
            // ---- falling edge: the line is free to move -----------------------
            if( m_bitCount >= 8 )
            {
                if( m_ackPending )
                {
                    // Close the frame and start the next one.
                    m_ackPending = false;
                    m_bitCount   = 0;

                    if( m_txFrame )
                    {
                        if( m_shift & 1 )
                        {
                            // NACK: the master has read enough.  Letting go of SDA
                            // and going quiet is the correct response, and staying
                            // active would have this slave answering a STOP with a
                            // byte nobody asked for.
                            m_txFrame = false;
                            m_active  = false;
                            m_sdaLow  = false;
                        }
                        else
                        {
                            loadTxByte();
                        }
                    }
                    else
                    {
                        m_sdaLow = false;

                        // The direction of the frame that follows an acknowledged
                        // receive is the direction bit of the address, which is
                        // m_reading and has been since the address was matched.  A
                        // write transaction has it clear and receives again; a read
                        // transaction has it set and the byte after the address is
                        // ours to send.
                        if( m_reading )
                        {
                            m_txFrame = true;
                            loadTxByte();
                        }
                    }

                    m_shift = 0;
                }
                else
                {
                    // Eight data bits are in.  The ninth slot belongs to whoever
                    // received them, so it is ours unless we were transmitting.
                    m_ackPending = true;

                    if( m_txFrame )
                    {
                        m_sdaLow = false;
                    }
                    else if( !m_addressed )
                    {
                        const quint8 addr = quint8( m_shift >> 1 );
                        const bool   rw   = ( m_shift & 1 ) != 0;

                        if( addr == ( m_code & 0x7F ) )
                        {
                            m_addressed = true;
                            m_reading   = rw;
                            m_addrCount = 0;

                            m_addrBytes[0] = addr;
                            m_addrBytes[1] = 0;

                            // Pulled low for as long as the ninth bit lasts, which
                            // is the ACK.  A slave that released the line here
                            // would be saying it was not there.
                            m_sdaLow = true;
                        }
                        else
                        {
                            // Somebody else's transaction.  Going inactive rather
                            // than staying addressed-but-silent is what lets a
                            // repeated START with our own code in it be seen: the
                            // START branch above sets m_active again.
                            m_active     = false;
                            m_ackPending = false;
                            m_sdaLow     = false;
                        }
                    }
                    else
                    {
                        const bool nack = onByte( quint8( m_shift ), false );

                        m_sdaLow = !nack;
                    }

                    m_shift = 0;
                }
            }
            else
            {
                // Mid-frame: present the next bit, or get off the line.
                if( m_txFrame )
                {
                    m_sdaLow = !( m_shift & 0x80 );
                    m_shift  = quint16( m_shift << 1 );
                }
                else
                {
                    m_sdaLow = false;
                }
            }
        }
    }

    m_sclPrev = scl;
    m_sdaPrev = sda;
}

// ---------------------------------------------------------------------------
// I2CRam
// ---------------------------------------------------------------------------

// The smallest and largest arrays, both powers of two.  The power of two is not a
// restriction imposed for tidiness: the pointer wraps with a mask, and a size that
// is not one would need a compare and a subtract on every byte, in a machine that
// runs once per bus edge.
#define I2CRAM_MIN 256
#define I2CRAM_MAX 65536

I2CRam::I2CRam( QObject *parent, const QString &type, const QString &id )
        : I2CBase( parent, type, id )
        , m_size( I2CRAM_MIN )
        , m_ptr( 0 )
{
    buildPins();

    m_mem.fill( 0, m_size );
}

void I2CRam::setSize( int n )
{
    // Rounded up to the next power of two rather than truncated, so a user who
    // types 300 gets 512 and not 256: the property is a size and the useful answer
    // to "300 bytes" is the part that holds 300 bytes.
    int sz = I2CRAM_MIN;
    while( sz < n && sz < I2CRAM_MAX ) sz <<= 1;

    if( sz == m_size ) return;

    m_size = sz;

    m_mem.fill( 0, m_size );

    m_ptr = 0;
}

void I2CRam::buildPins()
{
    rebuildPins();

    // Two rows, one for each wire.  The bus is on the left because that is where
    // an input edge is read from, and there is no right-hand edge at all: an
    // I2CRam has nothing to drive but SDA, which is not in m_outIdx and is handled
    // by I2CBase::stamp() instead.
    const int h = rowHalf( 2 );

    QVector<int> in, out;

    addPin( -LOGIC_PINX, -h, "sclpin", 180 );
    m_iScl = numPins() - 1;
    in.append( m_iScl );

    addPin( -LOGIC_PINX, h, "sdapin", 180 );
    m_iSda = numPins() - 1;
    in.append( m_iSda );

    setArea( QRectF( -LOGIC_HALF, -h - LOGIC_CELL/2,
                     2*LOGIC_HALF, 2*h + LOGIC_CELL ) );
    setPinRoles( in, out );

    setLabelY( int( m_area.top() ) - 20 );

    publishPins();
}

bool I2CRam::onByte( quint8 byte, bool isAddr )
{
    Q_UNUSED( isAddr );

    const int mask = m_size - 1;

    if( m_mem.size() != m_size ) m_mem.fill( 0, m_size );

    // The first byte after the address is the word address and every byte after it
    // is data, which is the 24Cxx protocol and the reason m_addrCount exists: it
    // counts bytes since the address was matched, and zero means the pointer has
    // not been loaded yet.
    if( m_addrCount == 0 )
    {
        m_ptr = byte & mask;
    }
    else
    {
        m_mem[m_ptr] = byte;

        // Auto-increment, with the wrap the mask gives for free.  A master writing
        // a block sends one address and then N bytes, and a slave that did not
        // advance would store all N in the same cell.
        m_ptr = ( m_ptr + 1 ) & mask;
    }

    m_addrCount++;

    // Always accepted: an EEPROM would NACK a byte it was busy committing, and
    // this one has no write cycle.
    return false;
}

bool I2CRam::driveRead( quint8 &byte )
{
    if( m_mem.size() != m_size ) return false;

    byte = m_mem.at( m_ptr );

    m_ptr = ( m_ptr + 1 ) & ( m_size - 1 );

    return true;
}

Component* createI2CRam( QObject *parent, const QString &type, const QString &id )
{
    return new I2CRam( parent, type, id );
}

// ---------------------------------------------------------------------------
// I2CToParallel
// ---------------------------------------------------------------------------

I2CToParallel::I2CToParallel( QObject *parent, const QString &type,
                              const QString &id )
               : I2CBase( parent, type, id )
               , m_port( 0 )
               , m_iReset( -1 )
{
    buildPins();
}

void I2CToParallel::buildPins()
{
    rebuildPins();

    // Eight rows for the eight port bits, and the two bus wires take the top two
    // positions on the left rather than being centred: an expander is read as a bus
    // in at the top and a port out along the side.
    const int h = rowHalf( 8 );

    const QRectF body = bodyRectCtrl( 8, 1 );

    QVector<int> in, out;

    addPin( -LOGIC_PINX, -h, "sclpin", 180 );
    m_iScl = numPins() - 1;
    in.append( m_iScl );

    addPin( -LOGIC_PINX, -h + LOGIC_CELL, "sdapin", 180 );
    m_iSda = numPins() - 1;
    in.append( m_iSda );

    for( int i = 0; i < 8; i++ )
    {
        addPin( LOGIC_PINX, -h + LOGIC_CELL*i,
                "portpin" + QString::number( i ), 0 );
        out.append( numPins() - 1 );
    }

    addPin( 0, int( ctrlY( body ) ), "resetpin", 90 );
    m_iReset = numPins() - 1;
    in.append( m_iReset );

    setArea( body );
    setPinRoles( in, out );

    if( Pin *p = pin( m_iReset ) ) p->setLabelText( QStringLiteral( "RST" ) );

    setLabelY( int( body.top() ) - 20 );

    publishPins();
}

void I2CToParallel::compute()
{
    I2CBase::compute();

    // Reset wins over whatever the bus just delivered, and is read every call
    // rather than on an edge: an expander held in reset has to stay at zero, and
    // an edge-sensitive read would let a byte clocked in while the pin was already
    // high reach the port.
    if( readPin( pin( m_iReset ) ) ) m_port = 0;

    for( int i = 0; i < 8; i++ )
        writeOutput( i, ( m_port >> i ) & 1 );
}

bool I2CToParallel::onByte( quint8 byte, bool isAddr )
{
    Q_UNUSED( isAddr );

    // Every byte is the port.  There is no register pointer to load first, so
    // m_addrCount is not consulted and a master that writes two bytes in one
    // transaction gets the second of them on the pins - which is what a PCF8574
    // does, and what makes the device usable without a protocol of its own.
    m_port = byte;

    m_addrCount++;

    return false;
}

bool I2CToParallel::driveRead( quint8 &byte )
{
    // Readable back, which is what lets an expander whose port pins are also its
    // inputs be used as one: the pins are driven here, so a master reading gets the
    // last byte written rather than what the sheet has pulled them to.  A part that
    // sampled its own pins would need them to be bidirectional in a way this
    // port's Pin is not.
    byte = m_port;

    return true;
}

Component* createI2CToParallel( QObject *parent, const QString &type,
                                const QString &id )
{
    return new I2CToParallel( parent, type, id );
}

// ---------------------------------------------------------------------------
// Lm555
// ---------------------------------------------------------------------------

// What the discharge transistor is when it is on.  Ten ohms rather than the
// milliohm an ideal switch would use: a timing capacitor discharged through a
// milliohm empties in a step the solver has to take in one dt, and the current
// spike that implies is limited by nothing in the circuit.  Ten ohms is what a real
// 555's discharge saturation resistance is to within an order of magnitude, and it
// gives the falling edge a shape.
#define T555_DISCH_IMPED 10.0

// What it is when it is off, and the reason it is not zero is the one switches.h
// gives at length: pin 7 declares an admittance, so its node has a matrix row, and
// a row with nothing in it is a singular matrix.
#define T555_OPEN_ADMIT 1e-12

// The leakage on the three pins that only sense.  A gigaohm, so a timing network
// of megohms - which is what a 555 running at a fraction of a hertz uses - is not
// loaded by the thing measuring it.
#define T555_SENSE_ADMIT 1e-9

// What an unwired VCC is taken to be.  The same argument OpAmp makes for its
// supply pins and the same number: a 555 dropped on a sheet with a capacitor, two
// resistors and no rail is the astable every tutorial starts with, and an output
// of zero volts would make it a part that does not work rather than a wiring
// omission.
#define T555_VCC 5.0

static const char k555Package[] = "dip8";

Lm555::Lm555( QObject *parent, const QString &type, const QString &id )
     : Chip( parent, type, id )
     , m_latch( false )
     , m_threshPrev( false )
     , m_iGnd( 0 )
     , m_iTrig( 1 )
     , m_iOut( 2 )
     , m_iReset( 3 )
     // The four right-hand roles.  Index == datasheet pin - 1 for every one of
     // them, which is the invariant Chip::buildDipFallback() and initChip() both
     // guarantee, and the same numbering upstream's own Lm555 uses (Gnd at
     // m_pin[0], CV at m_pin[4], Vcc at m_pin[7]).  Written out rather than
     // computed because a role table that has to be looked up is a role table
     // that can silently disagree with the datasheet in front of the reader.
     , m_iCtrl( 4 )
     , m_iThres( 5 )
     , m_iDisch( 6 )
     , m_iVcc( 7 )
{
    // From the constructor body, for the reason Memory's gives: initChip() is
    // virtual and a base constructor cannot reach the override.
    setPackage( QLatin1String( k555Package ) );
}

bool Lm555::initChip()
{
    if( !Chip::initChip() ) return false;

    assignRoles();

    // After the roles, and not before: which pins declare an admittance is what
    // decides whether the nets they sit on need a matrix row, and Chip::initChip()
    // has already asked for a node graph with every pin looking like a sense input.
    if( Circuit *circ = circuit() ) circ->updateNodes();

    return true;
}

void Lm555::assignRoles()
{
    // Datasheet pin number less one, which is the index Chip gives a DIP pin -
    // see the invariant buildDipFallback() documents.  Pin 1 is GND at the top
    // left and the count runs down the left side then up the right, so pins 5 to
    // 8 read off the right side bottom to top.
    m_iGnd   = 0;      // pin 1
    m_iTrig  = 1;      // pin 2
    m_iOut   = 2;      // pin 3
    m_iReset = 3;      // pin 4
    m_iCtrl  = 4;      // pin 5
    m_iThres = 5;      // pin 6
    m_iDisch = 6;      // pin 7
    m_iVcc   = 7;      // pin 8

    // Through the role indices rather than as literals.  index == pin - 1 now
    // holds on both sides, so a literal would be correct too - but setPinLabel()
    // takes an index and reads like it takes a pin number, and a call site that
    // has to be checked against that off-by-one every time it is read is a call
    // site that will eventually be written wrong.  Naming the role makes the
    // eight lines self-checking.
    setPinLabel( m_iGnd,   QStringLiteral( "GND"   ) );
    setPinLabel( m_iTrig,  QStringLiteral( "TRIG"  ) );
    setPinLabel( m_iOut,   QStringLiteral( "OUT"   ) );
    setPinLabel( m_iReset, QStringLiteral( "RST"   ) );
    setPinLabel( m_iCtrl,  QStringLiteral( "CTRL"  ) );
    setPinLabel( m_iThres, QStringLiteral( "THRES" ) );
    setPinLabel( m_iDisch, QStringLiteral( "DISCH" ) );
    setPinLabel( m_iVcc,   QStringLiteral( "VCC"   ) );

    // The three that measure something declare an admittance, so the nodes they sit
    // on get a row and the leakage stamped below has somewhere to go.  The two
    // supply pins and the reset do not: they sense, and a net made only of a rail
    // and pin 8 stays single, which is the fast path and the correct answer.
    if( Pin *p = pin( m_iTrig )  ) p->setIsAdmit( true );
    if( Pin *p = pin( m_iThres ) ) p->setIsAdmit( true );
    if( Pin *p = pin( m_iCtrl )  ) p->setIsAdmit( true );
    if( Pin *p = pin( m_iDisch ) ) p->setIsAdmit( true );
    if( Pin *p = pin( m_iGnd )   ) p->setIsAdmit( true );

    if( Pin *p = pin( m_iVcc )   ) p->setIsAdmit( false );
    if( Pin *p = pin( m_iReset ) ) p->setIsAdmit( false );

    if( Pin *p = pin( m_iOut ) )
    {
        p->setIsOutput( true );
        p->setIsAdmit( false );
    }

    update();
}

void Lm555::stamp()
{
    Pin *gnd   = pin( m_iGnd );
    Pin *out   = pin( m_iOut );
    Pin *disch = pin( m_iDisch );
    if( !gnd || !out || !disch ) return;

    // The sense pins' leakage, stamped on every pass because the matrix is zeroed
    // between them and a row that nothing is written into is a row of zeros.
    if( Pin *p = pin( m_iTrig )  ) p->stampAdmitance( T555_SENSE_ADMIT );
    if( Pin *p = pin( m_iThres ) ) p->stampAdmitance( T555_SENSE_ADMIT );
    if( Pin *p = pin( m_iCtrl )  ) p->stampAdmitance( T555_SENSE_ADMIT );

    const double vgnd = gnd->getVolt();

    // The output, referenced to pin 1 rather than to the sheet: a 555 whose ground
    // is not the sheet's ground is a 555 in a circuit with a split supply, and
    // driving an absolute level there would put the output somewhere between the
    // two rails instead of at one of them.
    const double vout = m_latch ? T555_VCC : 0;

    out->setIsOutput( true );
    out->setVoltOut( vgnd + vout );

    if( gnd->connector() )
    {
        const double g = 1/ESOURCE_IMPED;

        if( stampConductance( gnd, out, g ) )
        {
            const double i = g*vout;

            gnd->stampCurrent( -i );
            out->stampCurrent(  i );
        }
    }
    else
    {
        // Pin 1 unwired is the sheet's reference rather than an open circuit, and
        // the fallback is VoltReg's for the same reason: stampConductance() refuses
        // a pair with a dangling end, and without this a 555 connected only at its
        // output and its timing pins would drive nothing at all.
        stampSource( out, vout, ESOURCE_IMPED );
    }

    // The discharge transistor: a ten ohm path to pin 1 while the latch is clear,
    // and a floor admittance while it is set.  The floor rather than nothing, so
    // pin 7's row is never empty.
    const double gd = m_latch ? T555_OPEN_ADMIT : 1/T555_DISCH_IMPED;

    if( stampConductance( disch, gnd, gd ) )
    {
        // No companion source: a switch has no memory and no EMF, and the pair
        // (g, 0) is the whole of what it contributes.  The stamp is written out
        // rather than left to stampConductance() only for symmetry with the output
        // above - a conductance with no source needs no right hand side.
    }
}

void Lm555::updateStep()
{
    Pin *gnd   = pin( m_iGnd );
    Pin *trig  = pin( m_iTrig );
    Pin *thres = pin( m_iThres );
    if( !gnd || !trig || !thres ) return;

    const double vgnd = gnd->getVolt();

    // The supply, taken off pin 8 when something is wired to it.  A floor of one
    // volt rather than zero, because the thresholds below are fractions of it and a
    // VCC of zero makes both of them zero, which makes the trigger comparator
    // permanently satisfied and the latch permanently set - a 555 that has stopped
    // being a timer rather than one that is unpowered.
    double vcc = T555_VCC;

    if( Pin *p = pin( m_iVcc ) )
        if( p->connector() ) vcc = p->getVolt() - vgnd;

    if( vcc < 1.0 ) vcc = 1.0;

    // The two thresholds.  A voltage on pin 5 replaces the divider's top tap and
    // halves to give the bottom one, which is what the control pin is for and what
    // makes it possible to build a Schmitt trigger with a window nobody chose.
    double hi = vcc*2/3;
    double lo = vcc/3;

    if( Pin *p = pin( m_iCtrl ) )
        if( p->connector() )
        {
            hi = p->getVolt() - vgnd;
            lo = hi*0.5;
        }

    const double vt = trig->getVolt()  - vgnd;
    const double vh = thres->getVolt() - vgnd;

    // The SR latch, and the order of the two tests is the device: SET is tested
    // first and wins when both are true, which is what the gates inside a real 555
    // do and what makes the astable work at all - at the moment the capacitor
    // crosses back down through a third of VCC the threshold pin may still be above
    // two thirds of it, and a latch that let RESET win there would stay discharged
    // forever.
    if( vt < lo )        m_latch = true;
    else if( vh > hi )   m_latch = false;

    // Pin 4 is active low and an unwired pin 4 is inactive, so the test is on the
    // connector rather than on the voltage: a floating reset reads zero, and a
    // reset that reads zero holds the timer off, which is a 555 that never starts.
    if( Pin *p = pin( m_iReset ) )
        if( p->connector() )
            if( p->getVolt() - vgnd < lo ) m_latch = false;

    // The upper comparator's output, kept so the repaint is asked for on the
    // transition rather than on every step: a 555 oscillating at 100 kHz in a
    // circuit stepping at 1 MHz would otherwise call update() ten times per cycle,
    // and the scene coalesces none of them.
    const bool thresNow = ( vh > hi );

    if( thresNow != m_threshPrev )
    {
        m_threshPrev = thresNow;
        update();
    }
}

void Lm555::nodesUpdated()
{
    // Nothing to register - the reactive list reaches updateStep() every step - and
    // one evaluation, so a sheet that has just been loaded shows the output its
    // timing network implies rather than the cleared latch the constructor left.
    updateStep();
}

void Lm555::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                   QWidget *widget )
{
    Chip::paint( painter, option, widget );

    if( m_isLS ) return;

    // The part number on the body.  An eight-pin DIP with a notch is otherwise
    // indistinguishable from every other eight-pin DIP on the sheet, and the 555 is
    // the one part whose number everybody recognises.
    painter->setPen( kIcText );
    painter->drawText( m_area, Qt::AlignCenter, QStringLiteral( "555" ) );
    painter->setPen( kBodyPen );
}

Component* createLm555( QObject *parent, const QString &type, const QString &id )
{
    return new Lm555( parent, type, id );
}
