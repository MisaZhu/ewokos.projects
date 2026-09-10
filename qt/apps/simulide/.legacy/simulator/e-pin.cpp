/*
 * SimulIDE, ported to EwokOS - see e-pin.h.
 */

#include "e-pin.h"

#include "e-node.h"

ePin::ePin( const QString &id, int index )
      : m_index( index )
      , m_enode( 0l )
      , m_element( 0l )
      , m_current( 0 )
      , m_imped( 1e30 )          // an un-driven pin loads its node not at all
      , m_voltOut( 0 )
      , m_isOutput( false )
      , m_isAdmit( false )
      , m_isBus( false )
      , m_busWidth( 1 )
{
    m_pinId = id;
}

ePin::~ePin()
{
}

void ePin::setEnode( eNode *enode )
{
    if( m_enode == enode ) return;

    m_enode = enode;
}

double ePin::getVolt() const
{
    if( !m_enode ) return 0;

    return m_enode->getVolt();
}

void ePin::setVolt( double volt )
{
    if( m_enode ) m_enode->setVolt( volt );
}

void ePin::setIsOutput( bool out )
{
    m_isOutput = out;
}

void ePin::stampCurrent( double data )
{
    if( m_enode ) m_enode->stampCurrent( this, data );
}

void ePin::stampAdmitance( double data )
{
    if( m_enode ) m_enode->stampAdmitance( this, data );
}
