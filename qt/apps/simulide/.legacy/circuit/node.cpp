/*
 * SimulIDE, ported to EwokOS - see node.h.
 */

#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include "node.h"

#include "circuit.h"
#include "connector.h"
#include "pin.h"

#include "e-node.h"

Node::Node( QObject *parent, const QString &type, const QString &id )
     : Component( parent, type, id )
{
    // The dot is 4 across, or 6 for a bus, and the wires land on its centre.
    setArea( QRectF( -5, -5, 10, 10 ) );

    m_color = QColor( Qt::black );

    // Three pins at the centre, one every 90 degrees, all zero length so a wire
    // arriving here meets the dot rather than a stub sticking out of it.  The
    // names are the digits, which is what a .simu file's startpinid carries:
    // "Node-7-2" is pin 2 of Node-7.
    for( int i = 0; i < 3; i++ )
        addPin( 0, 0, QString::number( i ), 90*i, 0 );

    initPins();

    // One conductor, three legs.
    joinPins( 0, 1 );
    joinPins( 0, 2 );

    // A junction is furniture: nobody labels it.
    setShowId( false );
    setShowVal( false );
}

int Node::connectedPins() const
{
    int n = 0;
    for( int i = 0; i < numPins(); i++ )
    {
        Pin *p = pin( i );
        if( p && p->connector() ) n++;
    }
    return n;
}

void Node::pinConnected( Pin*, bool )
{
    // While a file is being read the wires arrive after the node, so the count
    // passes through 1 and 2 on the way to 3.  The loader calls cleanUp() once
    // it has finished, which is the same check with the right timing - and the
    // same thing upstream does with its joint list at the end of loadDomDoc().
    Circuit *circ = circuit();
    if( circ && circ->loading() ) return;

    cleanUp();
}

void Node::cleanUp()
{
    if( connectedPins() < 3 ) dissolve();
}

void Node::dissolve()
{
    Circuit *circ = circuit();

    // Collect the wires still attached and the pin at the far end of each.
    QList<Connector*> cons;
    QList<Pin*> far;

    for( int i = 0; i < numPins(); i++ )
    {
        Pin *p = pin( i );
        if( !p || !p->connector() ) continue;

        Connector *con = p->connector();
        Pin *other = ( con->startPin() == p ) ? con->endPin() : con->startPin();
        if( !other ) continue;

        cons.append( con );
        far.append( other );
    }

    if( cons.size() == 2 )
    {
        // Two wires through a junction that is no longer one: splice them.  The
        // old pair goes first so both pins are free when the new wire is made,
        // and a pin takes only one.
        Pin *a = far.at( 0 );
        Pin *b = far.at( 1 );

        const bool bus = a->isBus() || b->isBus();

        cons.at( 0 )->remove();
        cons.at( 1 )->remove();

        if( circ )
        {
            Connector *con = circ->addConnector( a, b );
            if( con ) con->setIsBus( bus );
        }
    }

    // A lone wire, or none: remComponent() takes whatever is left off the pins
    // before the junction itself goes.
    Component::remove();
}

void Node::paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                  QWidget *widget )
{
    Component::paint( painter, option, widget );

    // A bus is drawn fat, which is the only thing that distinguishes one from a
    // signal at a glance once the sheet is busy.  The question is asked of the
    // node, not of the pin: a pin's own flag is what its component declared, and
    // a junction in the middle of a bus is not itself a bus component - it is
    // Circuit::updateNodes() that collects the declaration onto the eNode.
    eNode *en = pin( 0 ) ? pin( 0 )->getEnode() : 0l;
    const bool bus = en && en->isBus();

    painter->setPen( Qt::NoPen );
    painter->setBrush( bus ? QColor( 240, 170, 20 ) : m_color );

    const int r = bus ? 3 : 2;
    painter->drawEllipse( QRect( -r, -r, 2*r, 2*r ) );
}

// What the item library calls.  Declared in itemlibrary.cpp alongside every other
// component's factory, which is the one list a new component has to be added to.
Component* createNode( QObject *parent, const QString &type, const QString &id )
{
    return new Node( parent, type, id );
}
