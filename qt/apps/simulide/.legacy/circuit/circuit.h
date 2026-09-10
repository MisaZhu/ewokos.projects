/*
 * SimulIDE, ported to EwokOS - the sheet.
 *
 * Upstream: src/gui/circuitwidget/circuit.h, which holds the QGraphicsScene, the
 * component and connector lists, the node graph, the .simu reader and writer and
 * the editing verbs.  This is that class.
 *
 * Four deviations, all of them consequences of what is available here:
 *
 * QDomDocument is not.  The Qt in this tree is built -no-feature-xml: the Qt5Xml
 * headers are installed but there is no libQt5Xml to link, which is a trap worth
 * naming.  QXmlStreamReader and QXmlStreamWriter live in QtCore and do link, so
 * a .simu is streamed rather than held as a document.  The visible consequence is
 * that a circuit is parsed once into attribute maps and that there is no undo
 * stack of QDomDocuments - upstream implements undo by snapshotting the whole
 * document, which needs the document.
 *
 * Serialisation is by reflection over QMetaObject, exactly as upstream's
 * objectToDom() and loadObjectProperties() do.  Neither Component nor any
 * subclass has a write() or read(): the writer walks the property list of the
 * meta-object and emits one attribute per property, and the reader walks the same
 * list and looks each name up in the file.  That is why a .simu carries objectName
 * (QObject's own first property) alongside Resistance and Unit, and why a
 * component that declares a new Q_PROPERTY is saved, loaded, copied and shown in
 * the property panel without anything else being touched.
 *
 * The node graph is rebuilt whole.  Upstream patches it as wires are drawn and
 * cut, merging and splitting eNodes incrementally.  Here any structural change
 * runs a union-find over every pin, which is a few hundred operations and happens
 * only on a mouse edit or a file load.  The invariant is worth more than the
 * incremental speed: there is no reachable state in which two pins a wire joins
 * are on different nodes.
 *
 * There is no singleton.  Upstream keeps Circuit::m_pSelf and reaches for
 * Circuit::self() from components, nodes and connectors; here a component asks its
 * own scene, which is the Circuit it is on.  One sheet per window is a limitation
 * this port accepts, but nothing in it is global.
 *
 * The Simulator must exist before a Circuit does, because updateNodes() registers
 * the nodes it builds with it.  MainWindow creates them in that order.
 */

#ifndef SIMU_CIRCUIT_H
#define SIMU_CIRCUIT_H

#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>

class Component;
class Connector;
class Node;
class Pin;
class eNode;
class QXmlStreamWriter;

// One <item>'s attributes, in the order the meta-object declares its properties.
// That is the order upstream writes them in, and keeping it means a file loaded
// and saved again differs only where a value actually changed - which is what
// makes it possible to diff a port's output against upstream's.
typedef QList<QPair<QString,QString> > ItemAttrs;

class Circuit : public QGraphicsScene
{
    Q_OBJECT

    public:
        explicit Circuit( QObject *parent = 0l );
        ~Circuit();

        // ---- components ---------------------------------------------------

        // Builds one of the library's types, names it uniquely and puts it on
        // the sheet.  Returns 0 for a type the library does not know.
        Component *createComponent( const QString &type, const QPointF &pos );

        // Duplicates a component one grid step to the lower right, which is what
        // upstream's context-menu Copy does.  The copy is made through the same
        // reflection the file writer uses, so a component is copied exactly as
        // far as it is saved.
        Component *copyComponent( Component *comp );

        // Takes ownership of a component the caller built, which is what loading
        // a file needs: the attributes have to be applied before the component is
        // wired to anything.
        void addComponent( Component *comp );

        // Unlists a component and takes the wires off its pins first.  It does
        // not delete: the caller does, or the item's own remove() does.  During
        // teardown m_deleting is set and the list surgery is skipped, because the
        // scene is already destroying everything.
        void remComponent( Component *comp );

        const QList<Component*> &compList() const { return m_compList; }
        Component *getComp( const QString &objName ) const;

        // ---- connectors ---------------------------------------------------

        // Returns 0 if either pin is null, already wired, or the same pin.
        Connector *addConnector( Pin *start, Pin *end );
        void remConnector( Connector *con );
        const QList<Connector*> &conList() const { return m_conList; }

        // ---- pins ----------------------------------------------------------

        // Called from a component as it makes each pin.  A .simu names a pin
        // "<objectName>-<pin name>", so the map has to exist before the
        // connectors in the file are read.
        void addPin( Pin *pin );
        void removePin( const QString &pinId );
        Pin *getPin( const QString &pinId ) const;

        // The unwired pin under a scene point, or 0.  A Node's three pins all sit
        // at its centre, so the first available one among the items at that point
        // is the answer - which is the pin the user meant, since the wired ones
        // are not candidates.
        Pin *pinAt( const QPointF &scenePos ) const;

        // The wire under a scene point, or 0.  Dropping a wire on another wire
        // rather than on a pin is how a T junction is made, and this is how the
        // view finds the wire to split.
        Connector *connectorAt( const QPointF &scenePos ) const;

        // ---- the node graph ------------------------------------------------

        // One eNode per set of pins joined by wires or inside a symbol.
        void updateNodes();
        void cleanNodes();
        const QList<eNode*> &eNodeList() const { return m_eNodeList; }

        // ---- the wire being drawn -------------------------------------------

        void beginConnector( Pin *start, const QPointF &pos );
        void dragConnector( const QPointF &pos );
        void endConnector( const QPointF &pos );
        bool isConnecting() const { return m_rubberPin != 0l; }

        // ---- file -----------------------------------------------------------

        bool saveToFile( const QString &path );
        bool openFromFile( const QString &path );
        void clearAll();

        QString fileName() const { return m_fileName; }
        void setFileName( const QString &f ) { m_fileName = f; }

        bool isModified() const { return m_modified; }
        void setModified( bool m );

        QString error() const { return m_error; }

        // True while a file is being read, which suppresses the modified flag,
        // the node rebuilds and the junction clean-up that a half-loaded sheet
        // would otherwise trigger on every item.
        bool loading() const { return m_loading; }

        // The <item>s that are not components: upstream keeps the Plotter and the
        // SerialPort in the circuit file even though both are window furniture.
        // They are collected here, in file order, so MainWindow can apply them and
        // so a round trip writes them back out rather than silently dropping them.
        // Anything this build has no component for lands here too, which is why an
        // example circuit using a part that has not been ported yet still loads and
        // still saves.
        const QList<QPair<QString,ItemAttrs> > &extraItems() const { return m_extraItems; }

        // A unique "Resistor-7" style name for a new instance of `type`.
        QString newObjectName( const QString &type );

        void deleteSelection();

        // Whether a position a component asks for is snapped to the lattice
        // before it is applied.  The view leaves this on; it is off for the
        // duration of a file load and of createComponent(), because upstream's own
        // examples are not all on this port's lattice and re-snapping them would
        // shift every symbol.  m_loading is folded in here rather than tested at
        // each call site so no caller can forget it.
        bool snapping() const { return m_snapping && !m_loading; }
        void setSnapping( bool s ) { m_snapping = s; }

        bool deleting() const { return m_deleting; }

        // ---- the <circuit> root attributes ----------------------------------
        //
        // Forwarded to the Simulator, which owns them, except noLinStep: this
        // port's Newton iteration runs inside the step rather than on a clock of
        // its own, so the value is recorded and written back for round-trip
        // fidelity and drives nothing.

        int  circSpeed() const;
        void setCircSpeed( int rate );

        int  reactStep() const;
        void setReactStep( int steps );

        int  noLinStep() const { return m_noLinStep; }
        void setNoLinStep( int steps ) { m_noLinStep = steps; }

        int  noLinAcc() const;
        void setNoLinAcc( int acc );

        bool animate() const;
        void setAnimate( bool a );

        // All five of the above back to their defaults, which is what a New has
        // to do: otherwise the sheet inherits the settings of the file that was
        // open before it and writes them into its own.
        void resetSettings();

    signals:
        // The window opens the property panel on this component.
        void propsRequested( Component *comp );
        // Something that would change the file has changed.  Carries the new
        // state and fires on the way back to false as well: a title bar has to
        // learn that saving cleared the flag, and a signal that only ever
        // announces "dirty" leaves the listener guessing when it became clean.
        void circuitModified( bool modified );
        // The topology moved, so the matrix has to be rebuilt.
        void nodesChanged();

    public slots:
        void showProperties( Component *comp );

        // Re-routes the wires.  A component that moves changes no connection, so
        // this does not touch the node graph.
        void slotItemMoved();

        // Connected to Simulator::stepDone().  Pushes the solved voltages out to
        // the pin colours and to the components that display a reading.
        void slotStepDone();

    protected:
        void drawBackground( QPainter *painter, const QRectF &rect );
        void drawForeground( QPainter *painter, const QRectF &rect );

    private:
        // Reflection over obj's meta-object, in both directions.  The writer
        // emits every property as an attribute of one <item>; the reader applies
        // every attribute it recognises as a property.  A QStringList property is
        // joined and split on commas, which is how pointList="-228,-164,-148,-164"
        // works, and it is done here rather than left to QVariant because Qt's
        // implicit QString-to-QStringList conversion is deprecated.
        void writeItem( QXmlStreamWriter &xml, const QObject *obj ) const;
        void writeAttrs( QXmlStreamWriter &xml, const QString &label,
                         const ItemAttrs &attr ) const;
        void applyAttrs( const ItemAttrs &attr, QObject *obj ) const;
        ItemAttrs itemAttrs( const QObject *obj ) const;

        // One <item> out of a file: works out what it is, builds it, renames it
        // into this sheet's numbering and applies its attributes.  Returns false
        // only for a type nothing here can build, which the caller reports as a
        // warning rather than a failure.
        bool loadOneItem( const ItemAttrs &attr,
                          QHash<QString,QString> &idMap,
                          QList<Node*> &joints );

        // A pin within 4 scene units of (x,y), preferring one whose id starts
        // with the same character as `hint`.  The fallback for a file whose pin
        // names do not match this build's - upstream's own loader has it, because
        // pin names have changed between releases and the geometry has not.
        Pin *pinNear( int x, int y, const QString &hint ) const;

        // Node names are taken from the file where it supplies them, so a round
        // trip renames nothing; m_nodeIds keeps a generated name from colliding
        // with one that was loaded.
        QString nextNodeId();
        void takeNodeId( const QString &id );

        QList<Component*> m_compList;
        QList<Connector*> m_conList;
        QList<eNode*> m_eNodeList;

        QHash<QString,Pin*> m_pinMap;

        // One counter for every item, not one per type: upstream numbers the
        // whole sheet from a single sequence, which is why one of its examples
        // reads Resistor-1, Capacitor-2, Node-3, Connector-11.  The numbers are
        // the order things were created in, and they stay unique across types,
        // which is what the pin map keys on.
        int m_seqNumber;

        QSet<QString> m_nodeIds;
        int m_nodeNum;

        // The wire in progress.  It is a plain path item rather than a Connector
        // with one end missing: nothing has to be undone if the user releases over
        // empty sheet, and a half-built Connector is a thing the rest of the class
        // would have to keep checking for.
        QGraphicsPathItem *m_rubber;
        Pin *m_rubberPin;

        QList<QPair<QString,ItemAttrs> > m_extraItems;

        QString m_fileName;
        QString m_error;

        int m_noLinStep;

        bool m_modified;
        bool m_loading;
        bool m_deleting;
        bool m_snapping;
};

#endif // SIMU_CIRCUIT_H
