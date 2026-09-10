/*
 * SimulIDE, ported to EwokOS - see e-element.h.
 */

#include "e-element.h"

#include "e-pin.h"
#include "simulator.h"

eElement::eElement( const QString &id )
           : m_numEpins( 0 )
           , m_elmId( id )
           , m_changed( false )
{
}

eElement::~eElement()
{
    // Undo every registration the element may have made.  A component that
    // forgets one of these would leave the Simulator stepping over freed
    // memory, and the failure would surface a long way from the mistake, so
    // the base does it for all of them.
    if( Simulator *sim = Simulator::self() )
    {
        sim->remFromElementList( this );
        sim->remFromReactiveList( this );
        sim->remFromNoLinList( this );
        sim->remFromChangedFast( this );
    }
}

void eElement::setNumEpins( int n )
{
    if( n == m_numEpins ) return;

    m_numEpins = n;
    m_epin.resize( n );
    for( int i=0; i<n; ++i )
        if( m_epin[i] ) m_epin[i]->setElement( this );
}

void eElement::setEpin( int pin, ePin *epin )
{
    if( pin >= m_numEpins ) setNumEpins( pin+1 );

    m_epin[pin] = epin;
    if( epin ) epin->setElement( this );
}

ePin* eElement::getEpin( int pin ) const
{
    if( pin >= m_numEpins ) return 0l;

    return m_epin[pin];
}
