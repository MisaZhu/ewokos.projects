/*
 * SimulIDE, ported to EwokOS - see connector.h.
 */

#include <QPainter>
#include <QPainterPathStroker>
#include <QPen>
#include <QStyleOptionGraphicsItem>

#include "connector.h"

#include "circuit.h"
#include "pin.h"

static const QColor kWireColor( 70, 70, 70 );
static const QColor kWireSelected( 255, 150, 50 );
static const QColor kBusColor( 240, 170, 20 );

// How far aside a wire steps when there is nowhere to turn between the two
// leads.  Two cells, so the detour clears a symbol sitting between them.
#define DETOUR ( 2*GRID )

Connector::Connector( QObject *parent, const QString &type, const QString &id,
                      Pin *startPin, Pin *endPin )
          : Component( parent, type, id )
          , m_startPin( startPin )
          , m_endPin( endPin )
          , m_isBus( false )
{
    // A wire is not dragged and has no position of its own: it sits where its
    // first point is, which updatePoints() sets.  Turning the geometry flag off
    // also means Component::itemChange never runs, so the snapping that a
    // symbol wants would fight the routing here.
    setFlag( QGraphicsItem::ItemIsMovable, false );
    setFlag( QGraphicsItem::ItemSendsGeometryChanges, false );

    // Under the symbols: a wire must never cover the pin it lands on, or the
    // next click aimed at that pin would select the wire instead.
    setZValue( -1 );

    // No id label on a wire, and nothing that would make the property panel
    // interesting.
    setShowId( false );
    setShowVal( false );

    updatePoints();
}

Connector::~Connector()
{
    // Clearing the back-pointers here is what makes teardown safe in either
    // order.  A pin whose m_con is non-null knows the wire is still alive,
    // because the wire nulls it on the way out - so ~Pin can write to it
    // without a liveness check, and a wire deleted before its pins leaves no
    // dangling pointer behind.
    if( m_startPin ) m_startPin->setConnector( 0l );
    if( m_endPin )   m_endPin->setConnector( 0l );
}

// ---- the two ends ------------------------------------------------------------

QString Connector::startPinId() const
{
    return m_startPin ? m_startPin->pinId() : QString();
}

QString Connector::endPinId() const
{
    return m_endPin ? m_endPin->pinId() : QString();
}

void Connector::setStartPinId( const QString &id )
{
    Circuit *circ = circuit();
    if( !circ ) return;

    Pin *pin = circ->getPin( id );
    if( !pin || pin == m_startPin ) return;

    if( m_startPin ) m_startPin->setConnector( 0l );

    m_startPin = pin;
    pin->setConnector( this );

    updatePoints();
}

void Connector::setEndPinId( const QString &id )
{
    Circuit *circ = circuit();
    if( !circ ) return;

    Pin *pin = circ->getPin( id );
    if( !pin || pin == m_endPin ) return;

    if( m_endPin ) m_endPin->setConnector( 0l );

    m_endPin = pin;
    pin->setConnector( this );

    updatePoints();
}

// ---- pointList ---------------------------------------------------------------

QStringList Connector::pointList() const
{
    QStringList list;

    for( const QPointF &p : m_pointList )
    {
        // The file carries the coordinates flat - x,y,x,y - and as integers in
        // practice, which is what QString::number's default 'g',6 gives for a
        // whole double.
        list << QString::number( p.x() ) << QString::number( p.y() );
    }
    return list;
}

void Connector::setPointList( const QStringList &list )
{
    if( list.size() < 4 ) return;   // fewer than two points is not a wire

    m_pointList.clear();

    // An odd trailing number is dropped rather than read as a coordinate with
    // no partner: upstream's writer can leave one behind if a file was edited
    // by hand, and a wire that ends half a pixel off its pin is worse than one
    // that ends on it.
    for( int i = 0; i+1 < list.size(); i += 2 )
        m_pointList.append( QPointF( list.at(i).toDouble(), list.at(i+1).toDouble() ) );

    QGraphicsItem::setPos( m_pointList.first() );

    updateShape();
}

void Connector::setIsBus( bool b )
{
    if( m_isBus == b ) return;

    m_isBus = b;
    update();
}

// ---- routing -----------------------------------------------------------------

// `len` out of the pin, along the lead.
static QPointF stepOut( const QPointF &p, int angle, int len )
{
    if( len <= 0 ) return p;

    switch( angle )
    {
        case 0:   return p + QPointF(  len, 0 );
        case 90:  return p + QPointF( 0,  len );
        case 180: return p + QPointF( -len, 0 );
        case 270: return p + QPointF( 0, -len );
    }
    return p;
}

static bool isHorizontal( int angle ) { return angle == 0 || angle == 180; }

// Whether the lead at `from` points at `to` rather than away from it.
static bool pointsAt( const QPointF &from, int angle, const QPointF &to )
{
    switch( angle )
    {
        case 0:   return to.x() > from.x();
        case 180: return to.x() < from.x();
        case 90:  return to.y() > from.y();
        case 270: return to.y() < from.y();
    }
    return false;
}

// Whether the two sit on one line as seen along the lead.
static bool aligned( const QPointF &a, int angle, const QPointF &b )
{
    return isHorizontal( angle ) ? qFuzzyCompare( a.y(), b.y() )
                                 : qFuzzyCompare( a.x(), b.x() );
}

// Snaps to the lattice so a bend lands where the next wire could join it.
static qreal snap( qreal v ) { return qRound( v/(qreal)GRID )*(qreal)GRID; }

void Connector::updatePoints()
{
    prepareGeometryChange();

    if( !m_startPin || !m_endPin )
    {
        m_pointList.clear();
        updateShape();
        return;
    }

    const QPointF sp = m_startPin->scenePos();
    const QPointF ep = m_endPin->scenePos();

    const int sa = m_startPin->angle();
    const int ea = m_endPin->angle();

    // A pin's own lead length is the stub the wire has to respect.  A normal
    // terminal gets one grid cell, which is why wires leave a symbol square on
    // to the lead; a Node's pins are zero length and all three sit at the
    // junction's centre, so the three wires meeting there converge on one dot
    // instead of sprouting stubs over each other.
    const int slen = m_startPin->length();
    const int elen = m_endPin->length();

    m_pointList.clear();
    m_pointList.append( sp );

    if( slen == 0 && elen == 0 )
    {
        // Junction to junction: nothing to step out of, so the wire is the
        // straight run between the two centres.
        m_pointList.append( ep );
    }
    else if( slen == 0 )
    {
        const QPointF e1 = stepOut( ep, ea, elen );

        // Turn at the far stub's line and run along it.
        m_pointList.append( isHorizontal( ea ) ? QPointF( sp.x(), e1.y() )
                                               : QPointF( e1.x(), sp.y() ) );
        m_pointList.append( e1 );
        m_pointList.append( ep );
    }
    else if( elen == 0 )
    {
        const QPointF s1 = stepOut( sp, sa, slen );

        m_pointList.append( s1 );
        m_pointList.append( isHorizontal( sa ) ? QPointF( ep.x(), s1.y() )
                                               : QPointF( s1.x(), ep.y() ) );
        m_pointList.append( ep );
    }
    else if( aligned( sp, sa, ep ) && pointsAt( sp, sa, ep )
          && aligned( ep, ea, sp ) && pointsAt( ep, ea, sp ) )
    {
        // The two leads face each other along one line, so the stubs are
        // already on the run and the wire is the straight between the pins.
        // This is the common case and the one a saved file most often carries
        // as just its two endpoints.
        m_pointList.append( ep );
    }
    else
    {
        const QPointF s1 = stepOut( sp, sa, slen );
        const QPointF e1 = stepOut( ep, ea, elen );

        m_pointList.append( s1 );

        if( isHorizontal( sa ) != isHorizontal( ea ) )
        {
            // One lead leaves sideways and the other vertically, so the corner
            // the two stubs share is one bend away.
            m_pointList.append( isHorizontal( sa ) ? QPointF( e1.x(), s1.y() )
                                                   : QPointF( s1.x(), e1.y() ) );
            m_pointList.append( e1 );
            m_pointList.append( ep );
        }
        else if( pointsAt( sp, sa, ep ) && pointsAt( ep, ea, sp ) )
        {
            // Both face each other but are offset: an S through the middle of
            // the gap, which is the shape a real lead would be bent into.
            if( isHorizontal( sa ) )
            {
                const qreal mx = snap( ( s1.x()+e1.x() )/2 );
                m_pointList.append( QPointF( mx, s1.y() ) );
                m_pointList.append( QPointF( mx, e1.y() ) );
            }
            else
            {
                const qreal my = snap( ( s1.y()+e1.y() )/2 );
                m_pointList.append( QPointF( s1.x(), my ) );
                m_pointList.append( QPointF( e1.x(), my ) );
            }
            m_pointList.append( e1 );
            m_pointList.append( ep );
        }
        else
        {
            // At least one lead points away from the other pin, so there is no
            // room to turn between them.  Step aside, cross over, step back:
            // the shortest route that never doubles back through a symbol.
            const QPointF off = isHorizontal( sa ) ? QPointF( 0, -DETOUR )
                                                   : QPointF( -DETOUR, 0 );
            m_pointList.append( s1+off );

            if( isHorizontal( sa ) )
            {
                m_pointList.append( QPointF( e1.x(), s1.y()+off.y() ) );
                m_pointList.append( QPointF( e1.x(), e1.y()+off.y() ) );
            }
            else
            {
                m_pointList.append( QPointF( s1.x()+off.x(), e1.y() ) );
                m_pointList.append( QPointF( e1.x()+off.x(), e1.y() ) );
            }
            m_pointList.append( e1+off );
            m_pointList.append( e1 );
            m_pointList.append( ep );
        }
    }

    // The item lives at its first point, so the path below is relative to it
    // and boundingRect() is the wire's own extent rather than a nominal 1x1.
    QGraphicsItem::setPos( m_pointList.first() );

    updateShape();
}

void Connector::updateShape()
{
    m_path = QPainterPath();

    if( m_pointList.size() < 2 )
    {
        m_shape = QPainterPath();
        return;
    }

    const QPointF origin = m_pointList.first();

    m_path.moveTo( QPointF( 0, 0 ) );
    for( int i = 1; i < m_pointList.size(); ++i )
        m_path.lineTo( m_pointList[i] - origin );

    // A 2 px line is 2 px wide to aim at; the stroke makes the clickable band
    // as wide as the pin it ends on.
    QPainterPathStroker stroker;
    stroker.setWidth( 8 );
    stroker.setJoinStyle( Qt::RoundJoin );
    stroker.setCapStyle( Qt::RoundCap );
    m_shape = stroker.createStroke( m_path );
}

// ---- item --------------------------------------------------------------------

QRectF Connector::boundingRect() const
{
    if( m_pointList.size() < 2 ) return QRectF();

    return m_path.boundingRect().adjusted( -5, -5, 5, 5 );
}

QPainterPath Connector::shape() const
{
    return m_shape;
}

void Connector::paint( QPainter *painter, const QStyleOptionGraphicsItem*, QWidget* )
{
    if( m_pointList.size() < 2 ) return;

    QColor col = m_isBus ? kBusColor : kWireColor;
    if( isSelected() ) col = kWireSelected;

    QPen pen( col, m_isBus ? 3 : 2 );
    pen.setJoinStyle( Qt::RoundJoin );
    pen.setCapStyle( Qt::RoundCap );

    painter->setPen( pen );
    painter->setBrush( Qt::NoBrush );
    painter->drawPath( m_path );
}

// ---- teardown ----------------------------------------------------------------

void Connector::pinGone( Pin *pin )
{
    // Reached only from ~Pin, and only while this wire is still alive - ~Pin
    // can tell because this destructor clears the pin's pointer.  The wire is
    // left in place with one end missing: paint() and updatePoints() both draw
    // nothing for it, and whichever teardown is running reaps it.
    if( pin == m_startPin ) m_startPin = 0l;
    if( pin == m_endPin )   m_endPin   = 0l;
}

void Connector::remove()
{
    Pin *sp = m_startPin;
    Pin *ep = m_endPin;

    m_startPin = 0l;
    m_endPin   = 0l;

    if( sp ) sp->setConnector( 0l );
    if( ep ) ep->setConnector( 0l );

    // Null once the scene is on its way out and this is no longer a Circuit.
    Circuit *circ = circuit();
    if( circ ) circ->remConnector( this );

    if( QGraphicsScene *sc = scene() ) sc->removeItem( this );

    deleteLater();
}
