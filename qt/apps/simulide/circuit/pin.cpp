/*
 * SimulIDE, ported to EwokOS - see pin.h.
 */

#include <QFont>
#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>

#include "pin.h"

#include "component.h"
#include "connector.h"
#include "simulator.h"

// The square is 8 across, one grid cell, so a pin and the wire stub leaving it
// meet exactly and the thing you aim at is the size of the lattice step.
#define PIN_SIZE 8
#define PIN_HALF 4

// Room around the square for a label.  A label is drawn scene-aligned, so it
// moves around the pin as the parent symbol is turned rather than turning with
// it, which means boundingRect() has to cover every rotation at once.  40 units
// holds "OSC1/CLKO/PC6" - the longest name in SimulIDE's AVR packages - at the
// 8px font the labels use.
#define PIN_LABEL_EXTENT 40

// How far the label sits from the edge of the square.
#define PIN_LABEL_GAP 6

// Grey while the simulation is stopped, which is also how an unwired pin reads.
static const QColor kPinIdle( 130, 130, 130 );

// The colour a package marks an unconnected pin with.
static const QColor kPinUnused( 200, 200, 200 );

// Upstream's label colours: near-white on an IC body, black on a logic symbol.
static const QColor kLabelOnIc( 250, 250, 200 );

// The unit vector the lead points in, for the four angles a package uses.  A
// table rather than sin/cos so this file needs no math header, and it also keeps
// the four cases exact - a pin at 90 degrees has to point straight down, not at
// 89.9999.
//
// THESE ANGLES ARE NOT UPSTREAM'S, and the difference is worth knowing before
// porting a component.  Upstream runs its pin angle the other way vertically:
// its own Pin::updateLabel() comments 90 as "Top" and 270 as "bottom", because
// it composes the angle into a QGraphicsItem rotation (setRotation(180-angle))
// where positive turns counter-clockwise on screen.  This port reads the angle
// as a direction in scene coordinates, where y grows downward, so 90 is down
// and 270 is up.  Left and right are the same in both.
//
// So when transcribing a component: an upstream pin declared 270 - the wiper of
// a Potentiometer, the emitter of a BJT, anything hanging below its symbol -
// becomes 90 here, and an upstream 90 becomes 270.  The angle is not stored in
// a .simu, so nothing outside these constructors can disagree with the choice;
// getting it wrong shows up as a wire whose first segment runs back into the
// symbol it came out of.
static void leadDir( int angle, int &ux, int &uy )
{
    switch( ((angle % 360) + 360) % 360 )
    {
        case  90: ux =  0; uy =  1; break;      // down
        case 180: ux = -1; uy =  0; break;      // left
        case 270: ux =  0; uy = -1; break;      // up
        default:  ux =  1; uy =  0; break;      // right, and anything odd
    }
}

// Turns (ux,uy) clockwise by whole 90-degree steps, which is the only rotation a
// component can have: QGraphicsItem's rotation is any angle, but a symbol is
// drawn on a square lattice and Component::rotateCW() only ever adds 90.
static void rotDir( int &ux, int &uy, int steps )
{
    steps = ((steps % 4) + 4) % 4;
    for( int i = 0; i < steps; i++ )
    {
        const int t = ux;
        ux = -uy;       // clockwise in a y-down frame: (1,0) -> (0,1) -> down
        uy = t;
    }
}

Pin::Pin( int x, int y, const QString &name, int angle,
          Component *parent, int index )
      : QObject( parent )
      , QGraphicsItem()
      , ePin( name, index )
      , m_comp( parent )
      , m_con( 0l )
      , m_name( name )
      , m_angle( angle )
      , m_length( PIN_SIZE )
      , m_labelColor( kLabelOnIc )
      , m_inverted( false )
      , m_unused( false )
      , m_color( kPinIdle )
{
    // The pin id is the name a .simu Connector uses to find this terminal.
    // Component::setId() rebuilds it if the component is renamed later.
    m_pinId = parent->itemID() + "-" + name;

    m_element = parent;

    setPos( x, y );
    setZValue( 1 );         // above the symbol, so it is always clickable
    // Mouse buttons stay accepted even though Pin handles none of them:
    // CircuitView::mousePressEvent asks the scene for the pin under the cursor
    // before it lets the event reach the component, and an item that accepts
    // no buttons is one the scene may not report.
}

Pin::~Pin()
{
    // Only tell the wire, never delete it.  A non-null m_con means the wire is
    // still alive, because ~Connector clears this pointer on the way out, so
    // the call is safe whichever of the two is destroyed first - and deleting
    // a wire from inside a destructor chain is what would make the order
    // matter.  Circuit::remComponent() takes a component's wires off it before
    // the component is destroyed, which is the one place teardown is driven.
    if( m_con ) m_con->pinGone( this );
}

QRectF Pin::boundingRect() const
{
    if( m_label.isEmpty() )
    {
        QRectF r( -PIN_HALF-1, -PIN_HALF-1, PIN_SIZE+2, PIN_SIZE+2 );

        // The lead reaches m_length-1 back towards the symbol, so the rect grows
        // on that side and only that side.  It is m_length that has to fit here,
        // not the square: a pin whose lead is clipped stops repainting where the
        // wire meets the symbol, and the stale pixels that leaves behind look
        // like a broken connection rather than like a repaint bug.
        //
        // Two units of margin, for the three-wide round cap the lead is drawn
        // with: the cap runs 1.5 past the end of the segment and 1.5 either side
        // of it, and a rect that fits the segment but not the cap clips the joint
        // with the symbol it is reaching for.
        if( m_length > 1 )
        {
            int lx = 1, ly = 0;
            leadDir( m_angle, lx, ly );

            const qreal ex = -lx*( m_length-1 );
            const qreal ey = -ly*( m_length-1 );

            r = r.united( QRectF( qMin( 0.0, ex )-2, qMin( 0.0, ey )-2,
                                  qAbs( ex )+4,      qAbs( ey )+4 ) );
        }
        return r;
    }

    // Big enough for the label at any rotation of the parent symbol, which is
    // why it is a square centred on the pin rather than a rect reaching out one
    // side: turning a chip moves its labels around the pins instead of turning
    // them, so the area has to cover all four placements at once.  It also
    // swallows the lead, which never reaches further than a label does.
    return QRectF( -PIN_HALF-PIN_LABEL_EXTENT, -PIN_HALF-PIN_LABEL_EXTENT,
                   PIN_SIZE+2*PIN_LABEL_EXTENT, PIN_SIZE+2*PIN_LABEL_EXTENT );
}

QPainterPath Pin::shape() const
{
    QPainterPath path;
    path.addRect( QRectF( -PIN_HALF, -PIN_HALF, PIN_SIZE, PIN_SIZE ) );
    return path;
}

void Pin::setLabelText( const QString &l )
{
    if( m_label == l ) return;

    // The label decides how big boundingRect() is, so the scene has to be told
    // before it changes or the item is left with a stale repaint area.
    prepareGeometryChange();

    m_label = l;

    update();
}

// The scene-frame direction the lead points in, which is where the label goes.
// The pin's own angle is in the symbol's frame; the symbol's rotation and mirror
// have to be composed onto it.  Mirroring negates the axis it mirrors about, and
// both axes negated is a half turn, which the same two negations produce.
void Pin::sceneDir( int &ux, int &uy ) const
{
    leadDir( m_angle, ux, uy );

    if( m_comp )
    {
        rotDir( ux, uy, qRound( m_comp->rotation() )/90 );

        if( m_comp->hflip() < 0 ) ux = -ux;
        if( m_comp->vflip() < 0 ) uy = -uy;
    }
}

void Pin::paint( QPainter *painter, const QStyleOptionGraphicsItem*, QWidget* )
{
    int ux = 1, uy = 0;
    sceneDir( ux, uy );

    // ---- the lead ----------------------------------------------------------
    // Upstream draws this and it is what joins a terminal to its symbol: the pin
    // sits m_length units outside the body it belongs to, so without the lead
    // there is a gap between the two and every symbol reads as disconnected from
    // its own terminals.  It runs inward, against the direction the terminal
    // points - the wire, which the router steps OUT along the same angle, covers
    // the outward half.
    //
    // Three wide with a round cap, upstream's numbers, and the width is not
    // cosmetic.  A capacitor's plates are drawn three wide too, at x=+-3, with
    // their leads arriving from pins twelve units out; the lead reaches x=-5 and
    // the plate's near face is at -4.5, so the two meet only in the cap.  At a
    // width of one they touch at a single pixel column and the joint breaks under
    // any view scale that is not exactly one.
    //
    // In the pin's own colour rather than the square's outline colour, so the stub
    // reads as part of the filled square next to it and animates with it.
    //
    // In the pin's own frame rather than the scene one: the painter is already
    // inside the parent symbol's rotation and mirror, so composing them again
    // here would turn the lead twice.  The label below does the opposite and
    // undoes them, because it is drawn scene-aligned on purpose.
    if( m_length > 1 )
    {
        int lx = 1, ly = 0;
        leadDir( m_angle, lx, ly );

        painter->setPen( QPen( m_unused ? kPinUnused : m_color, 3,
                               Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
        painter->drawLine( QPointF( 0, 0 ),
                           QPointF( -lx*( m_length-1 ), -ly*( m_length-1 ) ) );
    }

    // ---- the square -------------------------------------------------------
    painter->setPen( QPen( QColor( 40, 40, 40 ), 1 ) );
    painter->setBrush( m_unused ? kPinUnused : m_color );
    painter->drawRect( QRectF( -PIN_HALF, -PIN_HALF, PIN_SIZE, PIN_SIZE ) );

    // ---- the inversion bubble ---------------------------------------------
    // On the lead, outside the square, which is where upstream draws it and
    // where a reader of a datasheet expects to see it.
    if( m_inverted )
    {
        painter->setBrush( Qt::white );
        painter->drawEllipse( QRectF( ux*(PIN_HALF+1)-2.5, uy*(PIN_HALF+1)-2.5, 5, 5 ) );
    }

    if( m_label.isEmpty() || m_unused ) return;

    // ---- the label ---------------------------------------------------------
    // Drawn with the symbol's rotation and mirror undone, so it reads the same
    // way up whatever the chip has been turned to.  Undoing them here is safe
    // because the frame the painter is in at this point is exactly the symbol's
    // transform applied to this pin's position, and rotate(-r) followed by
    // scale(h,v) takes it back to scene axes with the pin still at the origin -
    // so the offsets below are scene offsets.
    painter->save();

    if( m_comp )
    {
        painter->rotate( -m_comp->rotation() );

        const int hf = m_comp->hflip();
        const int vf = m_comp->vflip();
        if( hf*vf < 0 ) painter->scale( hf, vf );
    }

    QFont font;
    font.setPixelSize( 8 );
    painter->setFont( font );
    painter->setPen( m_labelColor );

    const int off  = PIN_HALF + PIN_LABEL_GAP;
    const int half = PIN_LABEL_EXTENT - PIN_LABEL_GAP;

    QRectF where;
    Qt::Alignment align;

    if( ux > 0 )       { where = QRectF( off, -6, half, 12 );  align = Qt::AlignLeft   | Qt::AlignVCenter; }
    else if( ux < 0 )  { where = QRectF( -off-half, -6, half, 12 ); align = Qt::AlignRight | Qt::AlignVCenter; }
    else if( uy > 0 )  { where = QRectF( -half, off, 2*half, 12 );  align = Qt::AlignHCenter | Qt::AlignTop; }
    else               { where = QRectF( -half, -off-12, 2*half, 12 ); align = Qt::AlignHCenter | Qt::AlignBottom; }

    painter->drawText( where, int( align ), m_label );

    painter->restore();
}

void Pin::setPinName( const QString &n )
{
    m_name = n;
    updatePinId();
}

void Pin::updatePinId()
{
    m_pinId = ( m_comp ? m_comp->itemID() : QString() ) + "-" + m_name;
}

bool Pin::isAvailable() const
{
    return !m_con && !m_unused && isVisible();
}

void Pin::disconnectPin()
{
    if( !m_con ) return;

    Connector *con = m_con;
    m_con = 0l;

    con->remove();      // also clears the far end and deletes itself
}

void Pin::setEnode( eNode *enode )
{
    ePin::setEnode( enode );

    updateStep();
}

void Pin::updateStep()
{
    Simulator *sim = Simulator::self();

    QColor col;

    if( !sim || !sim->isRunning() || !sim->animate() )
        col = kPinIdle;
    else if( isBus() )
        col = QColor( 240, 170, 20 );       // a bus reads as a bus at any value
    else
    {
        const double v = getVolt();

        if( v < -1e-6 )
        {
            // Negative rail: blue, brightening towards -5 V.
            int k = qBound( 0, (int)( -v*255/5.0 ), 255 );
            col = QColor( 40, 80, 100+k*155/255 );
        }
        else if( v < 1e-6 )
        {
            col = QColor( 50, 50, 50 );     // earth, and anything near it
        }
        else
        {
            // 0 V to 5 V ramps dark red to bright red, so a logic high is
            // unmistakable and an analogue node still reads as a magnitude.
            int k = qBound( 0, (int)( v*255/5.0 ), 255 );
            col = QColor( 90+k*165/255, 50-k*20/255, 50-k*20/255 );
        }
    }

    if( col == m_color ) return;

    m_color = col;
    update();
}
