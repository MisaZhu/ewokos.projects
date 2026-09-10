/*
 * SimulIDE, ported to EwokOS - the widget that shows a sheet.
 *
 * Upstream: src/gui/circuitwidget/circuitview.h.  A QGraphicsView over the
 * Circuit, and the place the editing gestures live: dragging a wire, box
 * selecting, dropping a component from the selector, zooming, and the keyboard
 * shortcuts that do not belong on a menu.
 *
 * Three deviations.
 *
 * No singleton.  Upstream keeps CircuitView::m_pSelf and reaches for it from
 * components; here the view is handed its Circuit in the constructor and a
 * component reaches its sheet through its own scene().
 *
 * The wire being drawn is the scene's, not the view's.  Upstream builds a
 * half-finished Connector and closes it with closeCon(); here the press is
 * forwarded to Circuit::beginConnector/dragConnector/endConnector, which draws a
 * dashed rubber band and only makes a real Connector once there is a pin to land
 * on.  Keeping the in-progress state in the scene is what lets a T junction be
 * recognised on release - the scene is the only object that can ask what is under
 * the cursor and also make the Node the split needs.
 *
 * There is no clipboard.  Copy is "duplicate the selection one grid step away",
 * which is the case the context menu and Ctrl+C are actually used for here.  A
 * real cut/copy/paste needs the reflection Circuit keeps private and needs to
 * carry the wires between the copied parts with it, which is MainWindow's Edit
 * menu rather than the view's business.
 */

#ifndef SIMU_CIRCUITVIEW_H
#define SIMU_CIRCUITVIEW_H

#include <QGraphicsView>

class Circuit;
class Component;
class QMimeData;

class CircuitView : public QGraphicsView
{
    Q_OBJECT

    public:
        explicit CircuitView( Circuit *circ, QWidget *parent = 0l );
        ~CircuitView();

        Circuit *circuit() const { return m_circuit; }

        // The current magnification, 1.0 being one scene unit per device pixel.
        qreal zoom() const { return m_zoom; }

    public slots:
        void zoomIn();
        void zoomOut();
        void zoomReset();

        // Duplicates every selected component one grid step to the lower right.
        void duplicateSelection();

        void deleteSelection();
        void selectAll();

        void rotateSelectionCW();
        void rotateSelectionCCW();

        // Nudges the selection one lattice step, which is how a symbol gets onto
        // the grid after it has been dropped somewhere arbitrary.
        void nudge( int dx, int dy );

    signals:
        // The component under the cursor changed, which is what the status line
        // and the property panel follow.
        void hoverComponent( Component *comp );

    protected:
        void mousePressEvent( QMouseEvent *event );
        void mouseMoveEvent( QMouseEvent *event );
        void mouseReleaseEvent( QMouseEvent *event );

        void keyPressEvent( QKeyEvent *event );

        void wheelEvent( QWheelEvent *event );

        void dragEnterEvent( QDragEnterEvent *event );
        void dragMoveEvent( QDragMoveEvent *event );
        void dropEvent( QDropEvent *event );

        // No contextMenuEvent override: QGraphicsView forwards the event to the
        // scene, which gives it to the item under the cursor, and Component
        // already builds the menu a symbol needs.  Overriding it here would put
        // two menus in competition for the same right-click.

    private:
        // The selected components, wires filtered out.  See the note above the
        // selection methods in the .cpp for why a wire is never in the list.
        QList<Component*> selectedComponents() const;

        // The type name a drop carries, or an empty string for anything else.
        // The selector puts a plain library type name on the mime data, which is
        // the same string Circuit::createComponent() takes.
        static QString droppedType( const QMimeData *mime );

        void applyZoom( qreal z );

        Circuit *m_circuit;

        qreal m_zoom;

        // True between a press that started a wire and its release.  While it is
        // set the view does not let QGraphicsView do its own rubber banding or
        // item dragging, which would fight the scene over the same mouse.
        bool m_wiring;
};

#endif // SIMU_CIRCUITVIEW_H
