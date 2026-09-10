/*
 * SimulIDE, ported to EwokOS - see twopin.h.
 */

#include "twopin.h"

#include "pin.h"

#include "e-node.h"
#include "simulator.h"

bool stampConductance( ePin *a, ePin *b, double admit )
{
    if( !a || !b ) return false;

    eNode *na = a->getEnode();
    eNode *nb = b->getEnode();
    if( !na || !nb ) return false;

    // Neither end may be without a matrix row, and if one is, nothing at all is
    // stamped - not even the diagonal, which is the part that is easy to get
    // wrong.  A conductance whose off-diagonal partner is missing is not a
    // smaller version of the same element: it is a conductance to ground, and it
    // loads the node it hangs off by exactly that much.  So a resistor with one
    // lead unwired - the state a sheet is in half the time while it is being
    // built - would drag every node it touches towards zero instead of passing
    // no current, which is what an open lead does.
    //
    // A negative node number is enough to detect it.  createMatrix() gives -1
    // for two reasons, a single node and a node with one pin, and only the
    // second can happen here: the node this element's own pin sits on has an
    // admitting pin on it, which is the very thing that stops a node
    // classifying as single.  So -1 on this side of the stamp means dangling.
    //
    // Sources are the other shape and deliberately unaffected.  An eSource has
    // one terminal and stamps its internal conductance against the reference
    // with no far end at all, which is a conductance to ground on purpose - it
    // is what makes a loaded rail solve to its own voltage instead of to
    // V*Gsource/Gload.  That is why this test lives here, in the two-terminal
    // idiom, and not in eNode::stampAdmit() where it would break them.
    const int numa = na->getNodeNumber();
    const int numb = nb->getNodeNumber();
    if( numa < 0 || numb < 0 ) return false;

    // Both directions.  stampAdmitance() puts the conductance on the pin's own
    // diagonal; setOtherNode() is what turns it into the off-diagonal term in
    // the OTHER node's row.  The matrix comes out symmetric only because each
    // end does both halves.
    a->stampAdmitance( admit );
    na->setOtherNode( a, numb );

    b->stampAdmitance( admit );
    nb->setOtherNode( b, numa );

    return true;
}

void stampSource( ePin *pin, double volt, double imped )
{
    if( !pin ) return;

    // The pin carries the Thevenin voltage itself, for eNode::solveSingle() to
    // read when nothing loads the node.  Setting it on every stamp rather than
    // only when the value changes is what lets a device drive a new level and
    // have the node follow on the next pass with no second notification path.
    pin->setVoltOut( volt );

    if( imped < 1e-9 ) imped = 1e-9;

    const double admit = 1/imped;

    // No node test here, unlike stampConductance(): a source stamps against the
    // reference and has no far end to be missing.  stampAdmitance() is a no-op
    // on a single node anyway, because stampMatrix() skips those.
    pin->stampAdmitance( admit );
    pin->stampCurrent( volt*admit );
}

TwoPin::TwoPin( QObject *parent, const QString &type, const QString &id )
     : Component( parent, type, id )
     , m_resist( 100 )
     , m_admit( 1/100.0 )
{
    // Upstream Resistor's geometry, which is what its example circuits are laid
    // out against: a body twenty-two across with leads of eight, so the next
    // symbol over sits a grid cell clear of the terminals.  The area is also the
    // selection outline, and it is drawn a little larger than the body so the
    // dash does not land on the line it is framing.
    setArea( QRectF( -11, -4.5, 22, 9 ) );

    // The pin names are the suffix of the id a .simu Connector refers to, so
    // they are part of the file format and not free to be tidied up.
    addPin( -16, 0, "lPin", 180 );
    addPin(  16, 0, "rPin",   0 );

    // Load-bearing, and the easiest thing here to forget.  A node decides
    // whether it needs a matrix row by counting the attached pins that declare
    // a finite admittance, so a two-terminal part that leaves these clear is
    // invisible: both its nodes classify as single, no row is written for
    // either, and the conductance never reaches the matrix.  The circuit then
    // solves happily and gives the answer for a part that is not there.
    for( int i = 0; i < numPins(); i++ )
        if( Pin *p = pin( i ) ) p->setIsAdmit( true );

    initPins();
}

void TwoPin::setOhms( double r )
{
    if( qAbs( r ) < RES_FLOOR ) r = ( r < 0 ) ? -RES_FLOOR : RES_FLOOR;

    if( r == m_resist ) return;

    m_resist = r;
    m_admit  = 1/r;

    // Nothing is stamped here, which is a change from upstream and worth being
    // explicit about: solveAnalog() calls stamp() on every element between
    // zeroing the matrix and solving it, so whatever a setter wrote would be
    // overwritten before it was read.  The value is the only state that has to
    // survive to the next pass.
}

double TwoPin::pinVolt() const
{
    Pin *a = lPin();
    Pin *b = rPin();
    if( !a || !b ) return 0;

    return a->getVolt() - b->getVolt();
}

double TwoPin::current() const
{
    return pinVolt()/m_resist;
}

void TwoPin::stamp()
{
    stampPair( m_admit );
}

bool TwoPin::stampPair( double admit )
{
    return stampConductance( lPin(), rPin(), admit );
}

void TwoPin::setUnit( const QString &un )
{
    Component::setUnit( un );

    // A prefix change moves m_unitMult and leaves m_value alone, which is the
    // whole point - "100" in kilo-ohms is a different resistance than "100" in
    // ohms - so the element has to be told the new product.
    setOhms( getmultValue() );
}

void TwoPin::setDisplayValue( double val )
{
    // In this order.  setValue() picks the prefix that puts the displayed
    // number in [1,1000) and so decides what m_unitMult ends up as; the ohms
    // are the product it leaves behind, not the argument that went in.
    setValue( val );
    setOhms( getmultValue() );
}

double simuDt()
{
    Simulator *sim = Simulator::self();

    // Both guards are for a part built before its sheet has a simulator, which
    // happens while a circuit is being constructed in a test.  A dt of zero
    // would make the first capacitor's companion conductance infinite.
    if( !sim ) return 1.0/DEF_SIMURATE;

    const int rate = sim->simuRate();
    if( rate <= 0 ) return 1.0/DEF_SIMURATE;

    return 1.0/rate;
}

double TwoPin::dt() const
{
    return simuDt();
}
