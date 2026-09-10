/*
 * SimulIDE, ported to EwokOS - see circuitview.h.
 */

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include "circuitview.h"

#include "circuit.h"
#include "component.h"
#include "connector.h"
#include "itemlibrary.h"
#include "pin.h"

// One wheel notch multiplies the magnification by this.  Chosen so three notches
// is about 1.5x, which is roughly what a hand expects a scroll to do.
#define ZOOM_STEP 1.15

// Past about 8x the lattice stops being a lattice and the pin squares stop being
// aimable; below 0.2x a whole sheet is a smudge.
#define ZOOM_MAX 8.0
#define ZOOM_MIN 0.2

CircuitView::CircuitView( Circuit *circ, QWidget *parent )
           : QGraphicsView( circ, parent )
           , m_circuit( circ )
           , m_zoom( 1.0 )
           , m_wiring( false )
{
    // Rubber banding for a drag on empty sheet, and no scrollbars fighting the
    // sheet for space: a circuit is panned by dragging and zoomed by the wheel,
    // and upstream's window has the sheet filling the space between its panels.
    setDragMode( QGraphicsView::RubberBandDrag );
    setTransformationAnchor( QGraphicsView::AnchorUnderMouse );
    setResizeAnchor( QGraphicsView::AnchorViewCenter );
    setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );

    setRenderHint( QPainter::Antialiasing, true );
    setRenderHint( QPainter::SmoothPixmapTransform, true );

    // A wire lands on a pin, not on a pixel, so the cursor has to be reportable
    // to the scene exactly where it is.
    setMouseTracking( true );

    setAcceptDrops( true );

    setBackgroundBrush( QColor( 240, 240, 210 ) );

    // Snapping stays on for the whole life of the view.  Circuit::snapping()
    // turns itself off while a file is being read and createComponent() turns it
    // off around the position it applies, so the two cases that must not snap are
    // handled where they happen rather than by the view bracketing every drag.
    if( m_circuit ) m_circuit->setSnapping( true );
}

CircuitView::~CircuitView()
{
}

// ---- zoom -----------------------------------------------------------------------

void CircuitView::applyZoom( qreal z )
{
    if( z > ZOOM_MAX ) z = ZOOM_MAX;
    if( z < ZOOM_MIN ) z = ZOOM_MIN;

    if( qFuzzyCompare( z, m_zoom ) ) return;

    // Set absolutely rather than scaled relatively.  scale() multiplies into
    // whatever is already there, so a long session of wheel zooming accumulates
    // rounding error and the reported magnification drifts away from the real one.
    resetTransform();
    scale( z, z );

    m_zoom = z;
}

void CircuitView::zoomIn()    { applyZoom( m_zoom*ZOOM_STEP ); }
void CircuitView::zoomOut()   { applyZoom( m_zoom/ZOOM_STEP ); }
void CircuitView::zoomReset() { applyZoom( 1.0 ); }

void CircuitView::wheelEvent( QWheelEvent *event )
{
    // Ctrl turns the wheel into a zoom, which is what every drawing program does
    // and what upstream does.  Without it the wheel scrolls, which on a sheet
    // larger than the window is the more common want.
    if( !( event->modifiers() & Qt::ControlModifier ) )
    {
        QGraphicsView::wheelEvent( event );
        return;
    }

    // angleDelta is in eighths of a degree and a standard wheel notch is 15
    // degrees, so 120 - but a trackpad reports small values many times over, and
    // taking the sign of the delta rather than counting notches is what makes
    // both feel the same.
    const int dy = event->angleDelta().y();

    if( dy > 0 )      applyZoom( m_zoom*ZOOM_STEP );
    else if( dy < 0 ) applyZoom( m_zoom/ZOOM_STEP );

    event->accept();
}

// ---- the mouse ------------------------------------------------------------------

void CircuitView::mousePressEvent( QMouseEvent *event )
{
    if( !m_circuit || event->button() != Qt::LeftButton )
    {
        QGraphicsView::mousePressEvent( event );
        return;
    }

    const QPointF scenePos = mapToScene( event->pos() );

    // A pin under the cursor means a wire is being started, and that has to be
    // decided before the event reaches the scene: pins are child items of the
    // symbol they belong to and are neither movable nor selectable, so left to
    // QGraphicsView the press would go to the parent component and drag the whole
    // symbol instead of pulling a wire off its terminal.
    if( Pin *pin = m_circuit->pinAt( scenePos ) )
    {
        m_wiring = true;
        m_circuit->beginConnector( pin, scenePos );
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent( event );
}

void CircuitView::mouseMoveEvent( QMouseEvent *event )
{
    if( m_wiring && m_circuit )
    {
        m_circuit->dragConnector( mapToScene( event->pos() ) );
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent( event );
}

void CircuitView::mouseReleaseEvent( QMouseEvent *event )
{
    if( m_wiring && m_circuit )
    {
        m_wiring = false;

        // The scene decides what the release means: onto a pin it makes the wire,
        // onto another wire it splits that wire through a new junction, and onto
        // empty sheet it throws the rubber band away.  All three need to know what
        // is under the cursor and be able to create items, which is why the
        // decision lives there and not here.
        m_circuit->endConnector( mapToScene( event->pos() ) );

        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent( event );
}

// ---- the keyboard ----------------------------------------------------------------

void CircuitView::keyPressEvent( QKeyEvent *event )
{
    const bool ctrl  = event->modifiers() & Qt::ControlModifier;
    const bool shift = event->modifiers() & Qt::ShiftModifier;

    switch( event->key() )
    {
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            deleteSelection();
            event->accept();
            return;

        case Qt::Key_Escape:
            // Drop a wire in progress rather than let it hang off the cursor.
            if( m_wiring && m_circuit )
            {
                m_wiring = false;
                m_circuit->endConnector( QPointF( -1e6, -1e6 ) );
            }
            event->accept();
            return;

        case Qt::Key_A:
            if( ctrl ) { selectAll(); event->accept(); return; }
            break;

        case Qt::Key_C:
        case Qt::Key_D:
            if( ctrl ) { duplicateSelection(); event->accept(); return; }
            break;

        case Qt::Key_R:
            if( ctrl && shift ) { rotateSelectionCCW(); event->accept(); return; }
            if( ctrl )          { rotateSelectionCW();  event->accept(); return; }
            break;

        case Qt::Key_Plus:
        case Qt::Key_Equal:
            zoomIn();  event->accept(); return;

        case Qt::Key_Minus:
        case Qt::Key_Underscore:
            zoomOut(); event->accept(); return;

        case Qt::Key_0:
            if( ctrl ) { zoomReset(); event->accept(); return; }
            break;

        // The arrows move by one lattice step rather than one pixel, so nudging
        // is a way of getting onto the grid and not a way of drifting off it.
        case Qt::Key_Left:  nudge( -GRID, 0 ); event->accept(); return;
        case Qt::Key_Right: nudge(  GRID, 0 ); event->accept(); return;
        case Qt::Key_Up:    nudge( 0, -GRID ); event->accept(); return;
        case Qt::Key_Down:  nudge( 0,  GRID ); event->accept(); return;

        default: break;
    }

    // A switch's Key property.  Offered to every component rather than matched
    // against a table here, because which keys are live is data in the file and
    // two switches on the same key is the user's mistake to make.  Only
    // unmodified keys: Ctrl+R has to stay a rotate and not reach a switch whose
    // Key happens to be "r".
    if( !ctrl && m_circuit )
    {
        const QString key = event->text();

        if( !key.isEmpty() )
            for( Component *comp : m_circuit->compList() )
                if( comp && comp->keyPressed( key ) )
                {
                    event->accept();
                    return;
                }
    }

    QGraphicsView::keyPressEvent( event );
}

// ---- the selection ---------------------------------------------------------------
//
// The selection is the scene's, not the view's: QGraphicsView has no
// selectedItems() of its own and merely shows what QGraphicsScene has marked.  A
// wire is filtered out everywhere below, because a wire is defined by the two pins
// it joins - copying, rotating or nudging it on its own would leave it pointing at
// terminals that are no longer where it draws them.

QList<Component*> CircuitView::selectedComponents() const
{
    QList<Component*> comps;

    if( !m_circuit ) return comps;

    const QList<QGraphicsItem*> list = m_circuit->selectedItems();

    for( QGraphicsItem *it : list )
    {
        // One cast does the filtering.  qgraphicsitem_cast is an exact type()
        // match, so it catches every component that leaves Component's Type alone
        // - which includes Node - and misses Connector and Pin, which have Type
        // values of their own.  Excluding wires is what these four methods want;
        // including junctions is harmless, because Circuit::deleteSelection() is
        // the one verb that has to treat a junction specially and it tests for one
        // with qobject_cast instead.
        if( Component *comp = qgraphicsitem_cast<Component*>( it ) ) comps.append( comp );
    }
    return comps;
}

void CircuitView::selectAll()
{
    if( !m_circuit ) return;

    // QGraphicsScene has clearSelection() but no selectAll(), and neither does the
    // view, so the items are marked by hand.  Wires are included: "select all"
    // followed by Delete has to clear the sheet, and Circuit::deleteSelection()
    // collects wires separately from symbols precisely so that it can.
    //
    // The two casts are not redundant, and the reason is the trap this whole port
    // is built around: qgraphicsitem_cast is an EXACT type() match in Qt, so
    // qgraphicsitem_cast<Component*> does not catch a Connector even though
    // Connector derives from Component - it has its own Type value.
    const QList<QGraphicsItem*> list = m_circuit->items();

    for( QGraphicsItem *it : list )
    {
        if( qgraphicsitem_cast<Component*>( it ) ) it->setSelected( true );
        else if( qgraphicsitem_cast<Connector*>( it ) ) it->setSelected( true );
    }
}

void CircuitView::deleteSelection()
{
    if( m_circuit ) m_circuit->deleteSelection();
}

void CircuitView::duplicateSelection()
{
    if( !m_circuit ) return;

    // Snapshot first: copyComponent() selects the copy it makes, so iterating the
    // live selection would follow the list while it grows and copy each new one
    // again.
    const QList<Component*> comps = selectedComponents();

    for( Component *comp : comps ) m_circuit->copyComponent( comp );
}

void CircuitView::rotateSelectionCW()
{
    const QList<Component*> comps = selectedComponents();

    for( Component *comp : comps ) comp->rotateCW();
}

void CircuitView::rotateSelectionCCW()
{
    const QList<Component*> comps = selectedComponents();

    for( Component *comp : comps ) comp->rotateCCW();
}

void CircuitView::nudge( int dx, int dy )
{
    const QList<Component*> comps = selectedComponents();

    for( Component *comp : comps )
        comp->setPos( comp->pos() + QPointF( dx, dy ) );
}

// ---- drag and drop ---------------------------------------------------------------

QString CircuitView::droppedType( const QMimeData *mime )
{
    if( !mime || !mime->hasText() ) return QString();

    // The text has to name something the library can build, or a drag out of
    // another application - or out of a text editor holding a circuit's name -
    // would be accepted and then produce nothing.
    const QString type = mime->text().trimmed();

    if( type.isEmpty() ) return QString();
    if( !ItemLibrary::self()->createFn( type ) ) return QString();

    return type;
}

void CircuitView::dragEnterEvent( QDragEnterEvent *event )
{
    if( droppedType( event->mimeData() ).isEmpty() )
    {
        event->ignore();
        return;
    }

    // Copy rather than move: the selector keeps its entry after a drop, and the
    // user drags the same part onto the sheet as many times as the circuit needs.
    event->setDropAction( Qt::CopyAction );
    event->accept();
}

void CircuitView::dragMoveEvent( QDragMoveEvent *event )
{
    if( droppedType( event->mimeData() ).isEmpty() )
    {
        event->ignore();
        return;
    }

    event->setDropAction( Qt::CopyAction );
    event->accept();
}

void CircuitView::dropEvent( QDropEvent *event )
{
    const QString type = droppedType( event->mimeData() );

    if( !m_circuit || type.isEmpty() )
    {
        event->ignore();
        return;
    }

    // posF() and not pos(): createComponent() takes a scene point, and the
    // fractional part is what decides which side of a lattice line the symbol
    // lands on before snapping rounds it.
    const QPointF where = mapToScene( event->posF().toPoint() );

    Component *comp = m_circuit->createComponent( type, where );

    if( !comp )
    {
        event->ignore();
        return;
    }

    comp->setSelected( true );

    event->setDropAction( Qt::CopyAction );
    event->accept();

    // What was just dropped is what the property panel should be showing, and the
    // user's next act is usually to set its value.
    emit hoverComponent( comp );
}
