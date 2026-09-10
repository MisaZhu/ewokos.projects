/*
 * SimulIDE, ported to EwokOS - the component base.
 *
 * Upstream: src/gui/circuitwidget/component.h.  A Component is three things at
 * once, and keeping them in one object is what lets a symbol be edited, drawn
 * and simulated without a second bookkeeping layer:
 *
 *   - a QGraphicsItem, which puts a symbol on the sheet;
 *   - a QObject with Q_PROPERTYs, which is what the property panel edits and
 *     what the .simu writer serialises;
 *   - an eElement, which puts it in the simulation.
 *
 * The last one is a deviation.  Upstream 0.3.12 keeps Component purely
 * graphical and gives each component a separate eElement it owns - Resistor
 * holds an eResistor, Capacitor an eCapacitor - so every component is written
 * twice, once as a widget and once as a matrix entry, and the two have to be
 * kept in step by hand.  Here the component is the element, which is the shape
 * upstream itself moved to later.  It halves the number of classes in
 * components/ and removes a whole category of "forgot to forward the change"
 * bugs.
 *
 * Serialisation is by reflection over the meta-object, exactly as upstream
 * does: Circuit walks QMetaObject's property list and writes each one as an
 * attribute of an <item>, then reads them back the same way.  That is why the
 * property names below are upstream's names and cannot be chosen freely - they
 * are the attribute names in the file.  It is also why there is no write() or
 * read() here: a component persists a value by declaring a Q_PROPERTY for it
 * and does not otherwise have to know the file format exists.
 *
 * Labels are the other piece this port moves.  Upstream makes each of the id
 * and value labels a QGraphicsTextItem (its Label class) parented to the
 * component and counter-transformed so it stays readable and draggable; here
 * they are drawn by the scene in scene coordinates (Circuit::drawForeground)
 * from the same labelx/labely/labelrot triple the file stores.  They read the
 * same and round-trip the same, but they cannot be dragged independently of
 * their symbol - which is what upstream's 200-line Label class mostly exists
 * to support.
 */

#ifndef SIMU_COMPONENT_H
#define SIMU_COMPONENT_H

#include <QGraphicsItem>
#include <QList>
#include <QObject>
#include <QPainter>
#include <QPair>
#include <QString>
#include <QVariant>
#include <QVector>

#include "e-element.h"

class Pin;
class Circuit;

class Component : public QObject, public QGraphicsItem, public eElement
{
    Q_OBJECT
    Q_INTERFACES( QGraphicsItem )

    // USER true means "show this in the property panel".  Everything here is
    // written to the file whether it is USER or not - upstream's writer walks
    // the whole property list, which is how a .simu comes to carry labelx and
    // hflip alongside Resistance and Unit.
    Q_PROPERTY( QString itemtype READ itemType USER true )
    Q_PROPERTY( QString id       READ idLabel  WRITE setIdLabel USER true )
    Q_PROPERTY( bool    Show_id  READ showId   WRITE setShowId  USER true )
    Q_PROPERTY( int     x        READ gridX    WRITE setGridX )
    Q_PROPERTY( int     y        READ gridY    WRITE setGridY )
    Q_PROPERTY( qreal   rotation READ rotation WRITE setRotation )
    Q_PROPERTY( int     hflip    READ hflip    WRITE setHflip )
    Q_PROPERTY( int     vflip    READ vflip    WRITE setVflip )
    Q_PROPERTY( int     labelx    READ labelx    WRITE setLabelX )
    Q_PROPERTY( int     labely    READ labely    WRITE setLabelY )
    Q_PROPERTY( int     labelrot  READ labelRot  WRITE setLabelRot )
    Q_PROPERTY( int     valLabelx READ valLabelx WRITE setValLabelX )
    Q_PROPERTY( int     valLabely READ valLabely WRITE setValLabelY )
    Q_PROPERTY( int     valLabRot READ valLabRot WRITE setValLabRot )

    public:
        Component( QObject *parent, const QString &type, const QString &id );
        ~Component();

        // Upstream leaves Connector sharing Component's type and then tells
        // them apart by looking for "Connector" in the object name.  Giving
        // each its own value makes qgraphicsitem_cast reliable instead.
        enum { Type = QGraphicsItem::UserType + 1 };
        int type() const { return Type; }

        // m_area is the contract: everything the component draws has to fit
        // inside it, or it gets clipped rather than repainting its neighbours.
        QRectF boundingRect() const;
        void setArea( const QRectF &area ) { m_area = area; prepareGeometryChange(); }
        QRectF area() const { return m_area; }

        QString itemType() const { return m_type; }
        QString category() const { return m_category; }
        void setCategory( const QString &c ) { m_category = c; }

        // The blurb the property panel shows under the fields, taken from the
        // library entry.
        QString help() const { return m_help; }
        void setHelp( const QString &h ) { m_help = h; }

        // The two names upstream keeps apart, and the file records both:
        // objectName is the unique instance name ("Resistor-7"), held by
        // eElement::m_elmId; id is the label drawn next to the symbol.
        void setId( const QString &id );

        QString idLabel() const { return m_idLabel; }
        void setIdLabel( const QString &id );

        bool showId() const { return m_showId; }
        void setShowId( bool show );

        bool showVal() const { return m_showVal; }
        void setShowVal( bool show );

        // Text drawn by the scene, at these offsets from pos().
        virtual QString idLabelText() const;
        virtual QString valLabelText() const;

        // ---- value and unit ------------------------------------------------
        // Upstream's convention, kept exactly.  m_value is the number the panel
        // shows and the file stores; m_unitMult is the multiplier of the SI
        // prefix standing in front of the base unit; their product is the value
        // in base units, which is what the simulation reads.
        //
        // Storing the displayed number rather than the base one is what makes
        // reading a file order-independent.  A .simu carries Resistance="100"
        // and Unit=" kΩ" as separate attributes and they can arrive in either
        // order: setValue() multiplies by whatever prefix is current and then
        // renormalises, so 100 becomes 100 kΩ whether or not the unit has been
        // parsed yet.  Storing base units would need the unit to land first,
        // and nothing in the file format promises that.
        double value() const { return m_value; }
        double getmultValue() const { return m_value*m_unitMult; }

        QString unit() const { return m_mult + m_unit; }
        // Virtual because nearly every valued component has to recompute its
        // electrical value from m_value*m_unitMult after the prefix changes: a
        // .simu carries Resistance="100" and Unit=" kΩ" as two attributes and
        // nothing promises which arrives first, so the one that lands second is
        // the one that has to do the arithmetic.  A non-virtual setter here would
        // let applyAttrs() reach the base version through a Component pointer and
        // silently leave the element stamped at its old value.
        virtual void setUnit( const QString &un );
        // The base symbol the prefix goes in front of: "Ω", "F", "V", "H".
        void setBaseUnit( const QString &u ) { m_unit = u; }

        int labelx() const { return m_labelx; }
        void setLabelX( int x ) { m_labelx = x; }
        int labely() const { return m_labely; }
        void setLabelY( int y ) { m_labely = y; }
        int labelRot() const { return m_labelrot; }
        void setLabelRot( int r ) { m_labelrot = r; }

        int valLabelx() const { return m_valLabelx; }
        void setValLabelX( int x ) { m_valLabelx = x; }
        int valLabely() const { return m_valLabely; }
        void setValLabelY( int y ) { m_valLabely = y; }
        int valLabRot() const { return m_valLabRot; }
        void setValLabRot( int r ) { m_valLabRot = r; }

        int hflip() const { return m_Hflip; }
        void setHflip( int hf );
        int vflip() const { return m_Vflip; }
        void setVflip( int vf );

        int gridX() const { return qRound( pos().x() ); }
        int gridY() const { return qRound( pos().y() ); }
        void setGridX( int x );
        void setGridY( int y );

        // The sheet this component is on, or 0 while it is being created.
        Circuit *circuit() const;

        // ---- pins ---------------------------------------------------------
        // Every pin is both a QGraphicsItem child, so it follows the symbol
        // through rotation and flip, and an ePin registered on this eElement,
        // so stamping reaches the matrix.  Concrete classes call addPin() once
        // per terminal from their constructor and then initPins().  Upstream
        // news each Pin by hand and resizes m_pin itself; addPin() keeps the
        // graphical and electrical lists the same length, which is the
        // invariant stamp() relies on.
        int numPins() const { return m_pin.size(); }
        Pin *pin( int i ) const { return ( i < m_pin.size() ) ? m_pin[i] : 0l; }
        const QVector<Pin*> &pins() const { return m_pin; }
        Pin *getPin( const QString &pinId ) const;

        Pin *addPin( int x, int y, const QString &name, int angle, int length = 8 );
        void initPins();

        void setNumPins( int n );

        // Pins that are joined inside the symbol rather than by a wire: a
        // junction's legs, a DIP's common rail.  Circuit::updateNodes() unions
        // these exactly as it unions the two ends of a connector.
        void joinPins( int a, int b );
        const QList<QPair<int,int> > &joinedPins() const { return m_joined; }

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

        // Called by a pin when a wire is attached or detached.  A junction
        // uses it to dissolve itself once it has fewer than three wires, as
        // upstream's does.
        virtual void pinConnected( Pin *pin, bool connected );

        // Called once per simulator tick after the solve, for components that
        // show a reading.  Kept apart from eElement::updateStep(), which the
        // Simulator calls only on its reactive and logic lists and which a
        // capacitor needs for its own integration - calling that one from the
        // GUI tick too would integrate twice per step.
        virtual void updateDisplay() {}

        // Whether this component takes part in the simulation at all.  Shapes,
        // text and wires do not.
        virtual bool isSimulable() const { return true; }

        // Which of the Simulator's per-step lists this component belongs on,
        // declared rather than registered: a component's constructor runs before
        // it is on a sheet, and before the Simulator exists in the tests that
        // build one directly, so the lists are joined from Circuit::addComponent
        // - the one point at which both are certainly there.  Registering from a
        // constructor instead fails silently, and the failure is not "nothing
        // happens": a capacitor that never gets updateStep() keeps m_volt at zero
        // and stamps a bare conductance of dt/C, which at 1 uF and 1 MHz is a
        // tenth of an ohm.  It simulates as a short circuit.
        //
        // Reactive: integrates, so updateStep() runs after every solve.
        virtual bool isReactive() const { return false; }

        // Non-linear: its conductance depends on a voltage it has not solved for
        // yet, so solveAnalog() runs several passes over the matrix and re-stamps
        // between them.  Declaring it without needing it costs a factorisation
        // per step; not declaring it when needed leaves a diode linearised around
        // its last operating point forever.
        virtual bool isNonLinear() const { return false; }

        // Called by Circuit::updateNodes() once the new graph is complete, for
        // the components that watch a node rather than stamp one.  A logic device
        // asks each of its input eNodes to notify it when the voltage moves
        // (eNode::addToChangedFast), and those nodes are thrown away and rebuilt
        // whole on every structural edit - so the request has to be made again
        // each time, and this is the one point at which the new nodes certainly
        // exist.  Doing it from the constructor or from addComponent instead
        // registers against nodes that are about to be deleted, and the symptom
        // is a gate that computes once and then never reacts to its inputs again.
        //
        // Not needed by the stamping components: solveAnalog() walks the whole
        // element list every pass, so a resistor has nothing to re-attach.
        virtual void nodesUpdated() {}

        // A key this component claims, for the ones that carry a Key property -
        // a switch and a push button, which is what makes a keyboard-driven
        // circuit possible without a mouse on the sheet.  The view offers every
        // unmodified key to every component and stops at the first that takes
        // it, so the set of live keys is data in the file rather than a table in
        // the view.  Returning true means "this was mine"; the default claims
        // nothing, and a component with an empty Key must not claim the empty
        // string or it swallows every keystroke on the sheet.
        virtual bool keyPressed( const QString &key ) { return false; }

    public slots:
        virtual void slotProperties();
        virtual void slotCopy();
        virtual void rotateCW();
        virtual void rotateCCW();
        virtual void rotateHalf();
        virtual void H_flip();
        virtual void V_flip();
        virtual void slotRemove();
        virtual void remove();

    signals:
        void moved();

    protected:
        // Tears every pin down: takes the wires off, out of the sheet's pin map,
        // off the scene, and deletes them.  For the components whose terminal
        // count is a property - a Chip switching between its IC and logic
        // package, a ResistorDip being resized - and it has to be complete: a pin
        // left in the sheet's map would be found by a connector in a file being
        // loaded and wired to freed memory.  Leaves numPins() at 0, so the caller
        // follows it with its own addPin() calls and initPins().
        //
        // Protected because it leaves the component with no terminals at all,
        // which is a state only the component itself knows how to get out of.
        void clearPins() { dropPins( 0 ); }

        // The same teardown for a tail of the pin list, for the components that
        // grow and shrink: a ResistorDip being resized has to keep the wires on
        // the segments that survive, so it drops from 2*newSize rather than from
        // zero.  Pins below `from` keep their index and their wire, so a caller
        // that shrinks and regrows does not have to rebuild the whole symbol.
        void dropPins( int from );

        void contextMenuEvent( QGraphicsSceneContextMenuEvent *event );
        virtual void contextMenu( QGraphicsSceneContextMenuEvent *event, QMenu *menu );

        // Catches the position change so the wires hanging off this symbol can
        // follow it, and snaps the drag to the lattice.  The node graph does
        // not change when a component moves - only the geometry does - so this
        // re-routes and nothing more.
        QVariant itemChange( GraphicsItemChange change, const QVariant &value );

        // Mirrors the symbol about an axis.  Qt 5's QGraphicsItem scale is a
        // single qreal, so a mirror cannot be done with setScale(); it goes
        // through setTransform() instead, which is safe because the item's
        // transform is what QGraphicsItem takes as the base and post-multiplies
        // the rotation onto - the mirror is applied in the symbol's own frame and
        // the rotation still turns the mirrored symbol.  A subclass that wants a
        // transform of its own has to compose the flip into it, not replace it.
        virtual void setflip();

        // For the components that draw glyphs inside the symbol - a
        // seven-segment digit, an LCD character.  A negative scale leaves the
        // local frame left-handed and the text comes out backwards, so paint()
        // undoes the mirror before drawing it.  The outline stays mirrored,
        // which is what flipping is for.
        void painterUnflip( QPainter *painter ) const
        {
            if( m_Hflip*m_Vflip < 0 ) painter->scale( m_Hflip, m_Vflip );
        }

        // Takes `val` as being expressed in the current unit, converts it to
        // base units and then renormalises the prefix so the displayed number
        // lands in [1,1000): typing 4700 into a " Ω" field reads back as
        // 4.7 kΩ, which is how upstream's own example files come to carry
        // Resistance="100" Unit=" kΩ".
        void setValue( double val );

        double m_value;

        QString m_unit;
        QString m_mult;
        double m_unitMult;

        int m_Hflip;
        int m_Vflip;

        QString m_idLabel;
        QString m_type;
        QString m_category;
        QString m_help;

        QRectF m_area;
        QColor m_color;

        bool m_showId;
        bool m_showVal;

        int m_labelx, m_labely, m_labelrot;
        int m_valLabelx, m_valLabely, m_valLabRot;

        QVector<Pin*> m_pin;
        QList<QPair<int,int> > m_joined;
};

// Upstream's createItemPtr: what the item library stores against a type name.
typedef Component* (*createItemPtr)( QObject *parent, const QString &type, const QString &id );

#endif // SIMU_COMPONENT_H
