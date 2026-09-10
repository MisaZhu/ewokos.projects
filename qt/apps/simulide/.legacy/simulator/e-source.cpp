/*
 * SimulIDE, ported to EwokOS - see e-source.h.
 */

#include "e-source.h"

#include "e-pin.h"

eSource::eSource( const QString &id, ePin *epin )
         : eElement( id )
         , m_ePin( epin )
         , m_voltOut( 0 )
         , m_imped( ESOURCE_IMPED )
         , m_output( 0 )
         , m_out( true )
{
    setNumEpins( 1 );
    m_epin[0] = epin;

    if( m_ePin )
    {
        m_ePin->setElement( this );
        m_ePin->setIsOutput( true );
        m_ePin->setImped( m_imped );
    }
}

eSource::~eSource()
{
}

void eSource::setOut( bool out )
{
    m_out = out;

    if( !m_ePin ) return;

    m_ePin->setIsOutput( out );

    if( !out ) m_ePin->setImped( 1e30 );
    else       m_ePin->setImped( m_imped );
}

void eSource::setVoltage( double v )
{
    m_voltOut = v;

    if( m_ePin ) m_ePin->setVoltOut( v );
}

void eSource::setImped( double i )
{
    if( i < 1e-9 ) i = 1e-9;

    m_imped = i;

    if( m_ePin ) m_ePin->setImped( m_out ? i : 1e30 );
}

void eSource::stamp()
{
    if( !m_ePin || !m_out ) return;

    m_ePin->setVoltOut( m_voltOut );

    // Norton equivalent of the Thevenin source: a conductance from the node to
    // the reference, and a current source of V/R alongside it.  When the node
    // turns out to be single neither is read - solveSingle() takes m_voltOut
    // straight off the pin.
    const double admit = 1/m_imped;

    m_ePin->stampAdmitance( admit );

    m_output = m_voltOut*admit;
    m_ePin->stampCurrent( m_output );
}

void eSource::resetStep()
{
    m_output = 0;
}

void eSource::updateStep()
{
    stamp();
}
