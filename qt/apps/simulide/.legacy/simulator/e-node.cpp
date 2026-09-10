/*
 * SimulIDE, ported to EwokOS - see e-node.h.
 */

#include <QtGlobal>

#include "e-node.h"

#include "circmatrix.h"
#include "e-element.h"
#include "e-pin.h"

// A node voltage that moves by less than this is not a change.  Logic devices
// are notified on a change, and without a floor the noise in the last few bits
// of a solved matrix would keep a gate re-triggering forever.
#define VOLTEPS 1e-9

eNode::eNode( const QString &id )
       : m_totalCurr( 0 )
       , m_totalAdmit( 0 )
       , m_volt( 0 )
       , m_nodeNum( -1 )
       , m_id( id )
       , m_currChanged( true )
       , m_admitChanged( true )
       , m_voltChanged( false )
       , m_single( false )
       , m_switched( false )
       , m_isBus( false )
{
}

eNode::~eNode()
{
}

void eNode::addEpin( ePin *epin )
{
    if( !epin ) return;
    if( m_ePinList.contains( epin ) ) return;

    m_ePinList.append( epin );
    epin->setEnode( this );
}

void eNode::remEpin( ePin *epin )
{
    if( !epin ) return;

    m_ePinList.removeAll( epin );
    m_admitList.remove( epin );
    m_currList.remove( epin );
    m_nodeList.remove( epin );

    m_admitChanged = true;
    m_currChanged  = true;

    if( epin->getEnode() == this ) epin->setEnode( 0l );
}

void eNode::stampCurrent( ePin *epin, double data )
{
    m_currList[epin] = data;
    m_currChanged = true;
}

void eNode::stampAdmitance( ePin *epin, double data )
{
    // Only a real change invalidates the factorisation.  A linear circuit
    // re-stamps the same conductances every step, and without this test it
    // would pay an O(n^3) factorisation for each of them; with it, the LU in
    // CircMatrix survives until a component value or a non-linear operating
    // point actually moves.
    QHash<ePin*,double>::iterator it = m_admitList.find( epin );
    if( it != m_admitList.end() && qAbs( it.value()-data ) <= 1e-12*qAbs( data ) )
        return;

    m_admitList[epin] = data;
    m_admitChanged = true;
}

void eNode::setOtherNode( ePin *epin, int nodeNum )
{
    // Guarded the way stampAdmitance() is, and for the same reason.  A
    // two-terminal element stamps its far end on every pass of every step, so
    // an unconditional flag would invalidate the factorisation each time and
    // undo the caching that test exists for.  The first call after a rebuild
    // still lands, because initialize() cleared the hash and the lookup misses.
    QHash<ePin*,int>::iterator it = m_nodeList.find( epin );
    if( it != m_nodeList.end() && it.value() == nodeNum ) return;

    m_nodeList[epin] = nodeNum;
    m_admitChanged = true;
}

void eNode::setVolt( double volt )
{
    if( qAbs( volt-m_volt ) < VOLTEPS )
    {
        m_voltChanged = false;
        return;
    }
    m_volt = volt;
    m_voltChanged = true;
}

void eNode::initialize()
{
    m_admitList.clear();
    m_currList.clear();
    m_nodeList.clear();
    m_admit.clear();

    m_totalCurr  = 0;
    m_totalAdmit = 0;

    m_admitChanged = true;
    m_currChanged  = true;
    m_voltChanged  = false;

    // Nothing but outputs and high-impedance inputs: no equation to write.
    int admits = 0;
    for( ePin *pin : m_ePinList )
        if( pin->isAdmit() ) admits++;

    m_single = ( admits == 0 );
}

void eNode::solveSingle()
{
    for( ePin *pin : m_ePinList )
    {
        if( !pin->isOutput() ) continue;

        setVolt( pin->voltOut() );
        return;
    }
    // No driver at all: an input-only node floats at whatever it was, which
    // for a freshly built circuit is 0.
}

void eNode::stampAdmit()
{
    if( !m_admitChanged ) return;

    m_admitChanged = false;
    m_totalAdmit = 0;
    m_admit.clear();

    QHash<ePin*,double>::const_iterator it;
    for( it = m_admitList.constBegin(); it != m_admitList.constEnd(); ++it )
    {
        const double admit = it.value();
        m_totalAdmit += admit;

        // The far end of the element this pin belongs to.  Its row is not this
        // one, so the admittance leaves the matrix as an off-diagonal term.
        //
        // The diagonal term is added whether or not there is one, and that is
        // correct rather than an oversight: a source has a single terminal and
        // stamps its internal conductance against the reference with no far end
        // at all, so skipping here would leave a loaded rail solving to
        // V*Gsource/Gload instead of to V.  The element that does have two ends
        // is responsible for not stamping when one of them is dangling, and
        // stampConductance() in components/twopin.cpp is where that is done.
        const int other = m_nodeList.value( it.key(), -1 );
        if( other > -1 ) m_admit[other] -= admit;
    }
}

void eNode::stampCurr()
{
    if( !m_currChanged ) return;

    m_currChanged = false;
    m_totalCurr = 0;

    QHash<ePin*,double>::const_iterator it;
    for( it = m_currList.constBegin(); it != m_currList.constEnd(); ++it )
        m_totalCurr += it.value();
}

bool eNode::stampMatrix()
{
    if( m_single || m_nodeNum < 0 ) return false;

    const bool admChanged = m_admitChanged;

    stampAdmit();
    stampCurr();

    CircMatrix *matrix = CircMatrix::self();

    matrix->stampMatrix( m_nodeNum, m_nodeNum, m_totalAdmit );

    QHash<int,double>::const_iterator it;
    for( it = m_admit.constBegin(); it != m_admit.constEnd(); ++it )
        matrix->stampMatrix( m_nodeNum, it.key(), it.value() );

    matrix->stampCoef( m_nodeNum, m_totalCurr );

    return admChanged;
}

void eNode::addToChangedFast( eElement *el )
{
    if( !el ) return;
    if( m_changedFast.contains( el ) ) return;

    m_changedFast.append( el );
}

void eNode::remFromChangedFast( eElement *el )
{
    m_changedFast.removeAll( el );
}

void eNode::notifyFast()
{
    if( !m_voltChanged ) return;

    for( eElement *el : m_changedFast ) el->setChanged( true );
}
