/*
 * SimulIDE, ported to EwokOS - see circuit.h.
 */

#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QMetaProperty>
#include <QPainter>
#include <QPen>
#include <QVector>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "circuit.h"

#include "component.h"
#include "connector.h"
#include "itemlibrary.h"
#include "node.h"
#include "pin.h"

#include "e-node.h"
#include "simulator.h"

// The sheet.  Upstream's CircuitWidget picks these when it builds the scene, and
// its own loader centres the view on (1200+x, 950+y), which is the offset back to
// the origin from a rect starting at (-1200,-950).  A 2400x1900 sheet centred on
// the origin comfortably holds every example that ships with SimulIDE, whose
// components sit within a few hundred units of it.
static const QRectF kSceneRect( -1200, -950, 2400, 1900 );

// Upstream's colours for the sheet and its lattice.
static const QColor kSheetColor( 240, 240, 210 );
static const QColor kGridColor( 210, 210, 210 );

// The label colours, matching the QGraphicsTextItems upstream uses.
static const QColor kIdLabelColor( 0, 0, 160 );
static const QColor kValLabelColor( 80, 0, 130 );

// The <circuit> root's type attribute.  Upstream has written this string since
// the first release that had a file format and has never bumped it, so a reader
// cannot use it to tell versions apart - which is why the loader below accepts
// whatever it finds and copes by name instead.
static const char kDocType[] = "simulide_0.1";

// Half the size of a pin square, which is the tolerance for "the user meant this
// pin".
#define PIN_TOL 4

// The default for the one root attribute this port records but does not act on:
// upstream runs its Newton iteration on a clock of its own, here it runs inside
// the step, so there is nothing for a non-linear step count to drive.
#define DEF_NOLINSTEP 50

// Whether an item is written to the file.  Upstream's rule, and it is what keeps
// the parts a Chip builds inside itself - its pins' sub-items, a board's LEDs -
// out of the file: an internal item's name does not end in a number.
static bool isExternal( const QString &objName )
{
    bool ok = false;
    objName.section( QLatin1Char( '-' ), -1 ).toInt( &ok );
    return ok;
}

// Drops one attribute from an ordered list.  Copying uses it to leave out the two
// or three that the copy has already been given.
static void dropAttr( ItemAttrs &attr, const char *name )
{
    for( int i = attr.size()-1; i >= 0; --i )
        if( attr.at(i).first == QLatin1String( name ) ) attr.removeAt( i );
}

static QString attrValue( const ItemAttrs &attr, const QString &name )
{
    for( const QPair<QString,QString> &p : attr )
        if( p.first == name ) return p.second;

    return QString();
}

Circuit::Circuit( QObject *parent )
        : QGraphicsScene( kSceneRect, parent )
        , m_seqNumber( 0 )
        , m_nodeNum( 0 )
        , m_rubber( 0l )
        , m_rubberPin( 0l )
        , m_noLinStep( DEF_NOLINSTEP )
        , m_modified( false )
        , m_loading( false )
        , m_deleting( false )
        , m_snapping( false )
{
    setObjectName( "Circuit" );

    setSceneRect( kSceneRect );
}

Circuit::~Circuit()
{
    clearAll();
}

// ---- components ---------------------------------------------------------------

Component* Circuit::createComponent( const QString &type, const QPointF &pos )
{
    createItemPtr fn = ItemLibrary::self()->createFn( type );
    if( !fn ) return 0l;

    // The canonical name, not the one asked for: a type reached through an alias
    // reports what it actually is, so a file loaded from an older release is
    // written back with the current spelling.
    LibraryItem *item = ItemLibrary::self()->libraryItem( type );
    const QString canon = item ? item->type : type;

    Component *comp = fn( this, canon, newObjectName( canon ) );
    if( !comp ) return 0l;

    if( item )
    {
        comp->setCategory( item->category );
        comp->setHelp( item->help );
    }

    // Snapping is off while a position is being applied, so an off-lattice point
    // from a file or a paste lands exactly where it was asked to.
    const bool wasSnapping = m_snapping;
    m_snapping = false;
    comp->setPos( pos );
    m_snapping = wasSnapping;

    addComponent( comp );

    return comp;
}

Component* Circuit::copyComponent( Component *comp )
{
    if( !comp ) return 0l;
    if( qgraphicsitem_cast<Connector*>( comp ) ) return 0l;

    // Through the same reflection the file writer uses, so a component is copied
    // exactly as far as it is saved and no further: anything it holds that is not
    // a Q_PROPERTY is internal state that a copy should not inherit anyway.
    Component *copy = createComponent( comp->itemType(),
                                       comp->scenePos() + QPointF( 2*GRID, 2*GRID ) );
    if( !copy ) return 0l;

    // Everything but the name, which createComponent() has already made unique,
    // and the position, which has already been offset.
    ItemAttrs attr = itemAttrs( comp );
    dropAttr( attr, "objectName" );
    dropAttr( attr, "x" );
    dropAttr( attr, "y" );

    applyAttrs( attr, copy );

    copy->setSelected( true );

    setModified( true );
    updateNodes();

    return copy;
}

void Circuit::addComponent( Component *comp )
{
    if( !comp ) return;
    if( m_compList.contains( comp ) ) return;

    m_compList.append( comp );

    if( !items().contains( comp ) ) QGraphicsScene::addItem( comp );

    // A pin has to be in the map before a connector in a file can name it, and
    // the component's own constructor ran before it was on a sheet, so it could
    // not register them itself.
    for( int i = 0; i < comp->numPins(); i++ )
        if( Pin *pin = comp->pin( i ) ) addPin( pin );

    if( comp->isSimulable() )
        if( Simulator *sim = Simulator::self() )
        {
            sim->addToElementList( comp );

            // The per-step lists are joined here for the same reason the pins are
            // registered here: the constructor ran too early.  See the comment on
            // Component::isReactive() for what a missed registration costs.
            if( comp->isReactive()   ) sim->addToReactiveList( comp );
            if( comp->isNonLinear()  ) sim->addToNoLinList( comp );
        }

    connect( comp, SIGNAL( moved() ), this, SLOT( slotItemMoved() ) );

    if( !m_loading )
    {
        setModified( true );
        updateNodes();
    }
}

void Circuit::remComponent( Component *comp )
{
    if( !comp ) return;

    // Out of the step loop first and while the object is still whole: remove()
    // schedules the deletion rather than performing it, so an element left on the
    // list would be stepped once more against freed memory.
    if( Simulator *sim = Simulator::self() ) sim->remFromElementList( comp );

    if( m_deleting ) return;

    // Take the wires off before the pins go.  ~Pin only tells its wire that it is
    // leaving; it never deletes it, so this is the one place a component's wires
    // are collected - and doing it here rather than in ~Component means the order
    // the scene happens to destroy things in cannot matter.
    for( int i = 0; i < comp->numPins(); i++ )
    {
        Pin *pin = comp->pin( i );
        if( !pin ) continue;

        if( Connector *con = pin->connector() ) con->remove();

        removePin( pin->pinId() );
    }

    m_compList.removeAll( comp );

    disconnect( comp, SIGNAL( moved() ), this, SLOT( slotItemMoved() ) );

    setModified( true );
    updateNodes();
}

Component* Circuit::getComp( const QString &objName ) const
{
    for( Component *comp : m_compList )
        if( comp && comp->objectName() == objName ) return comp;

    return 0l;
}

QString Circuit::newObjectName( const QString &type )
{
    return type + "-" + QString::number( ++m_seqNumber );
}

// ---- connectors ---------------------------------------------------------------

Connector* Circuit::addConnector( Pin *start, Pin *end )
{
    if( !start || !end || start == end ) return 0l;

    // A pin takes one wire.  A second one is a junction's job, and letting two
    // wires share a pin would leave the second invisible under the first with no
    // way to select it.
    if( !start->isAvailable() || !end->isAvailable() ) return 0l;

    Connector *con = new Connector( this, "Connector",
                                    newObjectName( "Connector" ), start, end );

    start->setConnector( con );
    end->setConnector( con );

    m_conList.append( con );
    QGraphicsScene::addItem( con );

    // After the bookkeeping, because a junction that has just reached three wires
    // is fine but one that has just dropped below three dissolves itself, and
    // dissolving makes and removes wires of its own.
    if( Component *c = start->component() ) c->pinConnected( start, true );
    if( Component *c = end->component() )   c->pinConnected( end, true );

    if( !m_loading )
    {
        setModified( true );
        updateNodes();
    }

    return con;
}

void Circuit::remConnector( Connector *con )
{
    if( !con ) return;
    if( !m_conList.removeAll( con ) ) return;

    if( m_deleting ) return;

    setModified( true );
    updateNodes();
}

// ---- pins ---------------------------------------------------------------------

void Circuit::addPin( Pin *pin )
{
    if( !pin ) return;

    m_pinMap.insert( pin->pinId(), pin );
}

void Circuit::removePin( const QString &pinId )
{
    m_pinMap.remove( pinId );
}

Pin* Circuit::getPin( const QString &pinId ) const
{
    return m_pinMap.value( pinId, 0l );
}

Pin* Circuit::pinAt( const QPointF &p ) const
{
    const QRectF area( p.x()-PIN_TOL, p.y()-PIN_TOL, 2*PIN_TOL, 2*PIN_TOL );

    // Topmost first, and pins sit above the wires they land on, so the first
    // available one is the one the user is looking at.
    const QList<QGraphicsItem*> list = items( area );

    for( QGraphicsItem *it : list )
    {
        Pin *pin = qgraphicsitem_cast<Pin*>( it );
        if( pin && pin->isAvailable() ) return pin;
    }
    return 0l;
}

Connector* Circuit::connectorAt( const QPointF &p ) const
{
    const QList<QGraphicsItem*> list = items( p );

    for( QGraphicsItem *it : list )
    {
        Connector *con = qgraphicsitem_cast<Connector*>( it );
        if( con ) return con;
    }
    return 0l;
}

Pin* Circuit::pinNear( int x, int y, const QString &hint ) const
{
    const QRectF area( x-PIN_TOL, y-PIN_TOL, 2*PIN_TOL, 2*PIN_TOL );

    const QList<QGraphicsItem*> list = items( area );

    // Upstream's fallback: a pin whose id starts with the same character as the
    // one asked for.  Pin names have changed between SimulIDE releases - a diode's
    // terminals were renamed - but the first letter usually survives, and the
    // geometry in the file is still exactly where the pin is.
    const QChar first = hint.isEmpty() ? QChar() : hint.at( 0 ).toLower();

    if( !first.isNull() )
    {
        for( QGraphicsItem *it : list )
        {
            Pin *pin = qgraphicsitem_cast<Pin*>( it );
            if( !pin ) continue;

            const QString id = pin->pinId();
            const int dash = id.lastIndexOf( QLatin1Char( '-' ) );
            if( dash < 0 || dash+1 >= id.size() ) continue;

            if( id.at( dash+1 ).toLower() == first ) return pin;
        }
    }

    for( QGraphicsItem *it : list )
    {
        Pin *pin = qgraphicsitem_cast<Pin*>( it );
        if( pin && pin->isAvailable() ) return pin;
    }
    return 0l;
}

// ---- the node graph -----------------------------------------------------------

// Path-halving find.  The sets here are tiny - a node is a handful of pins - so
// this is a loop rather than a recursive find with full compression.
static int ufFind( QVector<int> &parent, int i )
{
    while( parent[i] != i )
    {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

void Circuit::updateNodes()
{
    if( m_loading ) return;

    Simulator *sim = Simulator::self();

    // A step in flight is walking the old node list, and the nodes it holds are
    // about to be freed.  Upstream pauses around every structural edit for the
    // same reason; resuming afterwards is cheap because the step count survives.
    const bool wasRunning = sim && sim->isRunning();
    if( wasRunning ) sim->pauseSim();

    // ---- collect every pin on the sheet ---------------------------------
    QList<Pin*> all;

    for( Component *comp : m_compList )
    {
        if( !comp ) continue;
        for( int i = 0; i < comp->numPins(); i++ )
            if( Pin *pin = comp->pin( i ) ) all.append( pin );
    }

    const int n = all.size();

    QHash<Pin*,int> index;
    index.reserve( n );
    for( int i = 0; i < n; i++ ) index.insert( all.at(i), i );

    QVector<int> parent( n );
    for( int i = 0; i < n; i++ ) parent[i] = i;

    // ---- union: one wire, one junction ----------------------------------
    for( Connector *con : m_conList )
    {
        if( !con ) continue;

        Pin *a = con->startPin();
        Pin *b = con->endPin();
        if( !a || !b ) continue;

        const int ia = index.value( a, -1 );
        const int ib = index.value( b, -1 );
        if( ia < 0 || ib < 0 ) continue;

        const int ra = ufFind( parent, ia );
        const int rb = ufFind( parent, ib );
        if( ra != rb ) parent[ra] = rb;
    }

    // Pins joined inside a symbol: a junction's three legs, a DIP's common rail.
    for( Component *comp : m_compList )
    {
        if( !comp ) continue;

        const QList<QPair<int,int> > &joined = comp->joinedPins();

        for( const QPair<int,int> &pair : joined )
        {
            Pin *a = comp->pin( pair.first );
            Pin *b = comp->pin( pair.second );
            if( !a || !b ) continue;

            const int ia = index.value( a, -1 );
            const int ib = index.value( b, -1 );
            if( ia < 0 || ib < 0 ) continue;

            const int ra = ufFind( parent, ia );
            const int rb = ufFind( parent, ib );
            if( ra != rb ) parent[ra] = rb;
        }
    }

    // ---- group ----------------------------------------------------------
    // Keyed by root, in pin order, so the numbering a saved file gets is stable
    // for a sheet that has not changed.
    QMap<int,QList<Pin*> > groups;
    for( int i = 0; i < n; i++ ) groups[ufFind( parent, i )].append( all.at(i) );

    // ---- throw the old graph away ---------------------------------------
    // Whole, rather than patched: an eNode is small, there are only as many as
    // there are distinct nets, and this runs on a mouse edit or a file load
    // rather than per step.  What it buys is that there is no reachable state in
    // which two pins a wire joins sit on different nodes.
    for( Pin *pin : all ) pin->setEnode( 0l );

    if( sim )
    {
        for( eNode *nod : m_eNodeList ) sim->remFromEnodeList( nod, true );
    }
    else
    {
        qDeleteAll( m_eNodeList );
    }
    m_eNodeList.clear();

    // ---- build the new one ----------------------------------------------
    for( auto it = groups.constBegin(); it != groups.constEnd(); ++it )
    {
        const QList<Pin*> &pins = it.value();

        eNode *nod = new eNode( nextNodeId() );

        bool bus = false;
        for( Pin *pin : pins )
        {
            pin->setEnode( nod );
            nod->addEpin( pin );

            // A pin's own bus flag is what its component declared; the node is a
            // bus if anything on it is.
            if( pin->isBus() ) bus = true;
        }
        nod->setIsBus( bus );

        m_eNodeList.append( nod );
        if( sim ) sim->addToEnodeList( nod );
    }

    // A wire takes its node from the pin it starts on, and reads fat when that
    // node is a bus.
    for( Connector *con : m_conList )
    {
        if( !con ) continue;

        Pin *p = con->startPin();
        eNode *nod = p ? p->getEnode() : 0l;

        con->setEnodeId( nod ? nod->itemId() : QString() );
        con->setIsBus( nod ? nod->isBus() : false );
    }

    if( sim ) sim->setCircChanged();

    // The components that watch a node rather than stamp one re-attach to the
    // graph that was just built.  After setCircChanged() so a device that
    // reacts by changing something does not lose the flag.
    for( Component *comp : m_compList )
        if( comp ) comp->nodesUpdated();

    if( wasRunning ) sim->resumeSim();

    emit nodesChanged();
}

void Circuit::cleanNodes()
{
    // A junction created while a file was being read has had no chance to notice
    // that one of its wires never arrived.  Snapshot the list first: dissolving a
    // junction makes and removes wires, and can dissolve its neighbours.
    QList<Node*> joints;

    for( Component *comp : m_compList )
        if( Node *node = qobject_cast<Node*>( comp ) ) joints.append( node );

    for( Node *node : joints )
        if( m_compList.contains( node ) ) node->cleanUp();
}

QString Circuit::nextNodeId()
{
    QString id;
    do
    {
        id = "Circ_eNode-" + QString::number( ++m_nodeNum );
    }
    while( m_nodeIds.contains( id ) );

    return id;
}

void Circuit::takeNodeId( const QString &id )
{
    m_nodeIds.insert( id );

    // Keep the generator ahead of anything the file used, so a node created after
    // a load cannot collide with one that came out of it.
    const QString num = id.section( '-', -1 );

    bool ok = false;
    const int v = num.toInt( &ok );
    if( ok && v > m_nodeNum ) m_nodeNum = v;
}

// ---- the wire being drawn ------------------------------------------------------

void Circuit::beginConnector( Pin *start, const QPointF &pos )
{
    if( !start || !start->isAvailable() ) return;
    if( m_rubberPin ) return;

    m_rubberPin = start;

    m_rubber = new QGraphicsPathItem();
    m_rubber->setPen( QPen( QColor( 100, 100, 100 ), 2, Qt::DashLine ) );
    m_rubber->setZValue( 3 );
    QGraphicsScene::addItem( m_rubber );

    dragConnector( pos );
}

void Circuit::dragConnector( const QPointF &pos )
{
    if( !m_rubber || !m_rubberPin ) return;

    // A straight rubber band rather than a preview of the routed wire: the route
    // depends on which pin it lands on, and until then there is nothing to route
    // to.
    QPainterPath path( m_rubberPin->scenePos() );
    path.lineTo( pos );
    m_rubber->setPath( path );
}

void Circuit::endConnector( const QPointF &pos )
{
    if( !m_rubberPin ) return;

    Pin *start = m_rubberPin;
    m_rubberPin = 0l;

    if( m_rubber )
    {
        removeItem( m_rubber );
        delete m_rubber;
        m_rubber = 0l;
    }

    Pin *end = pinAt( pos );

    if( end )
    {
        if( end != start ) addConnector( start, end );
        return;
    }

    // Released on a wire rather than on a pin, which is how a T is made: put a
    // junction there and split the wire through it.  Upstream splits the wire in
    // place and hangs the third leg off it; going through a Node means the sheet
    // afterwards holds three ordinary wires and one ordinary junction, so nothing
    // else has to know a split ever happened.
    Connector *con = connectorAt( pos );
    if( !con ) return;

    Pin *a = con->startPin();
    Pin *b = con->endPin();
    if( !a || !b ) return;

    const bool bus = con->isBus();

    // On the lattice, so the junction can be dragged and the three wires that
    // meet on it stay orthogonal.
    QPointF where( qRound( pos.x()/(qreal)GRID )*GRID,
                   qRound( pos.y()/(qreal)GRID )*GRID );

    con->remove();

    Node *node = qobject_cast<Node*>( createComponent( "Node", where ) );
    if( !node ) return;

    Pin *p0 = node->pin( 0 );
    Pin *p1 = node->pin( 1 );
    Pin *p2 = node->pin( 2 );
    if( !p0 || !p1 || !p2 ) return;

    Connector *c0 = addConnector( start, p0 );
    Connector *c1 = addConnector( p1, a );
    Connector *c2 = addConnector( p2, b );

    if( bus )
    {
        if( c0 ) c0->setIsBus( true );
        if( c1 ) c1->setIsBus( true );
        if( c2 ) c2->setIsBus( true );
    }
}

// ---- file ----------------------------------------------------------------------

bool Circuit::saveToFile( const QString &path )
{
    QString name = path;
    if( !name.endsWith( ".simu" ) ) name.append( ".simu" );

    QFile file( name );

    if( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
    {
        m_error = tr( "Cannot write file %1:\n%2." ).arg( name ).arg( file.errorString() );
        return false;
    }

    m_error.clear();

    QXmlStreamWriter xml( &file );
    xml.setAutoFormatting( true );

    xml.writeStartDocument();
    xml.writeStartElement( "circuit" );

    xml.writeAttribute( "type",      QLatin1String( kDocType ) );
    xml.writeAttribute( "speed",     QString::number( circSpeed() ) );
    xml.writeAttribute( "reactStep", QString::number( reactStep() ) );
    xml.writeAttribute( "noLinStep", QString::number( noLinStep() ) );
    xml.writeAttribute( "noLinAcc",  QString::number( noLinAcc() ) );
    xml.writeAttribute( "animate",   animate() ? "1" : "0" );

    // Components, then wires, then the furniture - upstream's order, and it has to
    // be: a reader resolves a wire by looking its two pins up by name, so every
    // component has to have been created before the first wire is read.
    for( Component *comp : m_compList )
        if( comp && isExternal( comp->objectName() ) ) writeItem( xml, comp );

    for( Connector *con : m_conList )
        if( con && isExternal( con->objectName() ) ) writeItem( xml, con );

    for( const QPair<QString,ItemAttrs> &extra : m_extraItems )
        writeAttrs( xml, extra.first, extra.second );

    xml.writeEndElement();
    xml.writeEndDocument();

    file.close();

    m_fileName = name;
    setModified( false );

    return true;
}

bool Circuit::openFromFile( const QString &path )
{
    QFile file( path );

    if( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
    {
        m_error = tr( "Cannot read file %1:\n%2." ).arg( path ).arg( file.errorString() );
        return false;
    }

    clearAll();

    m_error.clear();
    m_loading = true;
    m_fileName = path;

    QXmlStreamReader xml( &file );

    QHash<QString,QString> idMap;
    QList<Node*> joints;

    QStringList unknown;

    bool parsed = false;

    if( xml.readNextStartElement() && xml.name() == QLatin1String( "circuit" ) )
    {
        const QXmlStreamAttributes root = xml.attributes();

        if( root.hasAttribute( "speed" ) )     setCircSpeed( root.value( "speed" ).toInt() );
        if( root.hasAttribute( "reactStep" ) ) setReactStep( root.value( "reactStep" ).toInt() );
        if( root.hasAttribute( "noLinStep" ) ) setNoLinStep( root.value( "noLinStep" ).toInt() );
        if( root.hasAttribute( "noLinAcc" ) )  setNoLinAcc( root.value( "noLinAcc" ).toInt() );
        if( root.hasAttribute( "animate" ) )   setAnimate( root.value( "animate" ).toInt() != 0 );

        // <item> is always empty in a .simu, but reading it as an element and
        // skipping it covers both that and a file someone has expanded by hand.
        while( xml.readNextStartElement() )
        {
            if( xml.name() != QLatin1String( "item" ) )
            {
                xml.skipCurrentElement();
                continue;
            }

            ItemAttrs attr;
            const QXmlStreamAttributes a = xml.attributes();
            for( const QXmlStreamAttribute &at : a )
                attr.append( qMakePair( at.name().toString(), at.value().toString() ) );

            xml.skipCurrentElement();

            if( !loadOneItem( attr, idMap, joints ) )
            {
                const QString t = attrValue( attr, "itemtype" );
                if( !t.isEmpty() && !unknown.contains( t ) ) unknown.append( t );
            }
        }

        parsed = !xml.hasError();
    }

    if( xml.hasError() )
        m_error = tr( "Not a SimulIDE circuit: %1" ).arg( xml.errorString() );

    file.close();

    // A junction whose third wire never arrived - because it pointed at a
    // component this build has no class for - is not a junction any more.
    for( Node *node : joints ) node->cleanUp();

    m_loading = false;

    updateNodes();
    setModified( false );

    if( !unknown.isEmpty() )
    {
        // Not a failure: everything this build knows is on the sheet, and the
        // unknown items are kept in m_extraItems so saving puts them back.  But
        // saying nothing would look like a component had vanished.
        m_error = tr( "No component for: %1" ).arg( unknown.join( ", " ) );
    }

    return parsed;
}

bool Circuit::loadOneItem( const ItemAttrs &attr,
                           QHash<QString,QString> &idMap,
                           QList<Node*> &joints )
{
    const QString objNam = attrValue( attr, "objectName" );
    const QString type   = attrValue( attr, "itemtype" );

    if( type.isEmpty() ) return false;

    // Everything is renamed into this sheet's own numbering, as upstream does.
    // Keeping the file's names would collide with whatever is already here once
    // two circuits are merged, and a pin id carries its component's name, so the
    // two have to move together.
    const QString id = objNam.section( QLatin1Char( '-' ), 0, 0 )
                     + "-" + QString::number( ++m_seqNumber );

    ItemAttrs a = attr;
    for( int i = 0; i < a.size(); ++i )
        if( a.at(i).first == QLatin1String( "objectName" ) ) a[i].second = id;

    if( type == QLatin1String( "Connector" ) )
    {
        // Upstream's remap: a pin id is "<component>-<pin>", so the component half
        // goes through the map and the pin half is left alone.
        QString startId = attrValue( attr, "startpinid" );
        QString endId   = attrValue( attr, "endpinid" );

        const QString startComp = startId.section( QLatin1Char( '-' ), 0, 1 );
        const QString endComp   = endId.section( QLatin1Char( '-' ), 0, 1 );

        if( idMap.contains( startComp ) ) startId.replace( startComp, idMap.value( startComp ) );
        if( idMap.contains( endComp ) )   endId.replace( endComp, idMap.value( endComp ) );

        Pin *start = getPin( startId );
        Pin *end   = getPin( endId );

        // The name did not match, so try where the file says the wire starts and
        // ends.  Upstream's own loader does this, because pin names have changed
        // between releases and the geometry has not.
        const QStringList pts = attrValue( attr, "pointList" ).split( QLatin1Char( ',' ) );

        if( !start && pts.size() >= 2 )
            start = pinNear( pts.at(0).toInt(), pts.at(1).toInt(), startId );

        if( !end && pts.size() >= 2 )
            end = pinNear( pts.at( pts.size()-2 ).toInt(),
                           pts.at( pts.size()-1 ).toInt(), endId );

        if( !start || !end || start == end ) return false;
        if( !start->isAvailable() || !end->isAvailable() ) return false;

        Connector *con = new Connector( this, type, id, start, end );

        start->setConnector( con );
        end->setConnector( con );

        m_conList.append( con );
        QGraphicsScene::addItem( con );

        // The ids as remapped, so the wire's own properties agree with the pins it
        // was actually given.
        for( int i = 0; i < a.size(); ++i )
        {
            if( a.at(i).first == QLatin1String( "startpinid" ) ) a[i].second = start->pinId();
            if( a.at(i).first == QLatin1String( "endpinid" ) )   a[i].second = end->pinId();
        }

        // pointList last: it moves the item, and the x/y attributes would move it
        // back if they were applied after.
        applyAttrs( a, con );

        if( Component *c = start->component() ) c->pinConnected( start, true );
        if( Component *c = end->component() )   c->pinConnected( end, true );

        return true;
    }

    // Not a component at all: window furniture, or a part this build has no class
    // for.  Kept verbatim so a round trip does not drop it.
    if( !ItemLibrary::self()->createFn( type ) )
    {
        for( int i = m_extraItems.size()-1; i >= 0; --i )
            if( m_extraItems.at(i).first == objNam ) m_extraItems.removeAt( i );

        m_extraItems.append( qMakePair( objNam, attr ) );

        return false;
    }

    createItemPtr fn = ItemLibrary::self()->createFn( type );

    Component *comp = fn( this, ItemLibrary::self()->resolve( type ), id );
    if( !comp ) return false;

    if( LibraryItem *li = ItemLibrary::self()->libraryItem( type ) )
    {
        comp->setCategory( li->category );
        comp->setHelp( li->help );
    }

    addComponent( comp );

    // x and y last, so a position is applied once and with snapping off.
    ItemAttrs geom;
    ItemAttrs rest;
    for( const QPair<QString,QString> &p : a )
    {
        if( p.first == QLatin1String( "x" ) || p.first == QLatin1String( "y" ) ) geom.append( p );
        else rest.append( p );
    }

    applyAttrs( rest, comp );
    applyAttrs( geom, comp );

    takeNodeId( attrValue( attr, "enodeid" ) );

    if( Node *node = qobject_cast<Node*>( comp ) ) joints.append( node );

    idMap.insert( objNam, id );

    return true;
}

void Circuit::clearAll()
{
    if( m_rubber )
    {
        removeItem( m_rubber );
        delete m_rubber;
        m_rubber = 0l;
    }
    m_rubberPin = 0l;

    Simulator *sim = Simulator::self();
    const bool wasRunning = sim && sim->isRunning();
    if( wasRunning ) sim->stopSim();

    const bool wasLoading = m_loading;

    m_loading  = true;
    m_deleting = true;

    // Wires before symbols.  ~Pin only tells its wire that it is going and never
    // deletes it, so taking the wires out first means the order the scene would
    // otherwise destroy things in cannot matter.
    while( !m_conList.isEmpty() )
    {
        Connector *con = m_conList.takeFirst();

        if( Pin *p = con->startPin() ) p->setConnector( 0l );
        if( Pin *p = con->endPin() )   p->setConnector( 0l );

        removeItem( con );
        delete con;
    }

    while( !m_compList.isEmpty() )
    {
        Component *comp = m_compList.takeFirst();

        for( int i = 0; i < comp->numPins(); i++ )
            if( Pin *pin = comp->pin( i ) ) removePin( pin->pinId() );

        if( sim ) sim->remFromElementList( comp );

        removeItem( comp );
        delete comp;
    }

    m_pinMap.clear();

    // Same split as updateNodes(): remFromEnodeList(..., true) is what frees the
    // node, so the list is only deleted directly when there is no Simulator to
    // hand it to.  Doing both would free every node twice.
    if( sim )
    {
        for( eNode *nod : m_eNodeList ) sim->remFromEnodeList( nod, true );
    }
    else
    {
        qDeleteAll( m_eNodeList );
    }
    m_eNodeList.clear();

    m_extraItems.clear();
    m_nodeIds.clear();

    m_seqNumber = 0;
    m_nodeNum   = 0;

    m_deleting = false;
    m_loading  = wasLoading;

    // Through the setter and not a direct write, so a title bar learns the sheet
    // stopped being dirty.  openFromFile() clears it again at the end; that call
    // is then a no-op because the flag already reads false.
    setModified( false );

    if( wasRunning && sim ) sim->startSim();
}

void Circuit::resetSettings()
{
    // The five attributes of the <circuit> root, back to what a sheet that has
    // never been saved would write.  A New after an Open has to do this or the
    // new file inherits the old one's speed and animation choices.
    setCircSpeed( DEF_SIMURATE );
    setReactStep( DEF_REACTSTEP );
    setNoLinStep( DEF_NOLINSTEP );
    setNoLinAcc(  DEF_NOLINACC );
    setAnimate( false );
}

// ---- reflection ----------------------------------------------------------------

ItemAttrs Circuit::itemAttrs( const QObject *obj ) const
{
    ItemAttrs attr;

    if( !obj ) return attr;

    const QMetaObject *meta = obj->metaObject();
    const int count = meta->propertyCount();

    // From 0, which is QObject's own objectName.  That is why a .simu item
    // carries objectName as an attribute alongside Resistance and Unit: upstream
    // walks the same list from the same place.
    for( int i = 0; i < count; ++i )
    {
        const QMetaProperty prop = meta->property( i );
        const QVariant value = obj->property( prop.name() );

        QString text;

        if( prop.type() == QVariant::StringList )
            text = value.toStringList().join( QLatin1Char( ',' ) );
        else
            text = value.toString();

        attr.append( qMakePair( QString::fromUtf8( prop.name() ), text ) );
    }

    return attr;
}

void Circuit::writeItem( QXmlStreamWriter &xml, const QObject *obj ) const
{
    if( !obj ) return;

    writeAttrs( xml, obj->objectName(), itemAttrs( obj ) );
}

void Circuit::writeAttrs( QXmlStreamWriter &xml, const QString &label,
                          const ItemAttrs &attr ) const
{
    // Upstream writes the item's name as a text node in front of it, which is
    // what gives a .simu the annotated look it has.  A reader ignores text
    // between elements, so it costs nothing to keep.
    xml.writeCharacters( "\n" + label + ": \n" );

    xml.writeStartElement( "item" );

    for( const QPair<QString,QString> &p : attr )
        xml.writeAttribute( p.first, p.second );

    xml.writeEndElement();
}

void Circuit::applyAttrs( const ItemAttrs &attr, QObject *obj ) const
{
    if( !obj ) return;

    const QMetaObject *meta = obj->metaObject();
    const int count = meta->propertyCount();

    for( int i = 0; i < count; ++i )
    {
        const QMetaProperty prop = meta->property( i );

        if( !prop.isWritable() ) continue;

        const QString name = QString::fromUtf8( prop.name() );

        // objectName is the caller's: it has already been chosen, and setting it
        // here would go behind Component::setId() and leave the pin ids pointing
        // at the old name.
        if( name == QLatin1String( "objectName" ) ) continue;

        QString text = attrValue( attr, name );

        if( text.isNull() )
        {
            // Upstream's files have had the capitalisation of some properties
            // changed between releases, and its loader tries the lower-cased
            // first letter when the declared name is absent.  Same here, so a
            // file from either side of that change reads.
            QString alt = name;
            alt.replace( 0, 1, alt.at(0).toLower() );

            text = attrValue( attr, alt );
            if( text.isNull() ) continue;
        }

        const QVariant value( text );

        switch( prop.type() )
        {
            case QVariant::Int:
                obj->setProperty( prop.name(), value.toInt() );
                break;
            case QVariant::UInt:
                obj->setProperty( prop.name(), value.toUInt() );
                break;
            case QVariant::Double:
                obj->setProperty( prop.name(), value.toDouble() );
                break;
            case QVariant::Bool:
                obj->setProperty( prop.name(), value.toBool() );
                break;
            case QVariant::StringList:
                // Split here rather than leaning on QVariant's implicit
                // QString-to-QStringList conversion, which Qt has deprecated and
                // Qt 6 removes.
                obj->setProperty( prop.name(),
                                  QVariant( text.split( QLatin1Char( ',' ) ) ) );
                break;
            default:
                obj->setProperty( prop.name(), text );
                break;
        }
    }
}

// ---- editing -------------------------------------------------------------------

void Circuit::deleteSelection()
{
    Simulator *sim = Simulator::self();
    const bool wasRunning = sim && sim->isRunning();
    if( wasRunning ) sim->pauseSim();

    // Wires first.  Removing a symbol takes its own wires with it, but a wire
    // selected on its own - the common case when dragging a box across a busy
    // sheet - has to be collected separately.
    QList<Connector*> cons;
    QList<Component*> comps;

    const QList<QGraphicsItem*> list = selectedItems();

    for( QGraphicsItem *it : list )
    {
        if( Connector *con = qgraphicsitem_cast<Connector*>( it ) )
        {
            if( !cons.contains( con ) ) cons.append( con );
            continue;
        }
        if( Component *comp = qgraphicsitem_cast<Component*>( it ) )
            comps.append( comp );
    }

    for( Connector *con : cons )
        if( m_conList.contains( con ) ) con->remove();

    // Junctions are left to themselves: removing a wire from one drops it below
    // three and it dissolves, splicing the survivors.  Removing the junction
    // outright would delete the wires still hanging off the other side.
    for( Component *comp : comps )
        if( !qobject_cast<Node*>( comp ) ) comp->remove();

    cleanNodes();

    if( wasRunning && sim ) sim->resumeSim();
}

void Circuit::showProperties( Component *comp )
{
    emit propsRequested( comp );
}

void Circuit::slotItemMoved()
{
    if( m_loading || m_deleting ) return;

    // Geometry only: which pin is joined to which has not changed, so the node
    // graph is left alone and only the wires are re-run.
    for( Connector *con : m_conList )
        if( con ) con->updatePoints();
}

void Circuit::slotStepDone()
{
    if( m_loading || m_deleting ) return;

    // Once per tick rather than once per step: the step loop may run hundreds of
    // steps between two repaints and only the last of them is visible.
    for( Component *comp : m_compList )
    {
        if( !comp ) continue;

        for( int i = 0; i < comp->numPins(); i++ )
            if( Pin *pin = comp->pin( i ) ) pin->updateStep();

        comp->updateDisplay();
    }
}

void Circuit::setModified( bool m )
{
    if( m_modified == m ) return;

    m_modified = m;

    emit circuitModified( m );
}

// ---- painting ------------------------------------------------------------------

void Circuit::drawBackground( QPainter *painter, const QRectF &rect )
{
    Q_UNUSED( rect );

    painter->setBrush( kSheetColor );
    painter->drawRect( kSceneRect );

    painter->setPen( QPen( kGridColor, 0 ) );

    // Upstream's lattice: a line every 8 units, offset by 4 so the lines fall
    // between the pins rather than through them.  Drawn over the whole sheet
    // rather than the exposed rect, because the painter is already clipped and
    // computing the visible span is more arithmetic than it saves.
    const int x0 = int( kSceneRect.left() );
    const int x1 = int( kSceneRect.right() );
    const int y0 = int( kSceneRect.top() );
    const int y1 = int( kSceneRect.bottom() );

    for( int x = x0+4; x < x1; x += 8 ) painter->drawLine( x, y0, x, y1 );
    for( int y = y0+4; y < y1; y += 8 ) painter->drawLine( x0, y, x1, y );
}

void Circuit::drawForeground( QPainter *painter, const QRectF &rect )
{
    // The labels are drawn here rather than being items of their own.  Upstream
    // makes each one a QGraphicsTextItem parented to its component and
    // counter-transformed so it stays readable and can be dragged on its own;
    // that is most of what its 200-line Label class exists for.  Drawing them
    // from the same labelx/labely/labelrot the file stores reads identically and
    // round-trips identically, at the cost of not being draggable separately.
    QFont font;
    font.setPixelSize( 10 );

    for( Component *comp : m_compList )
    {
        if( !comp ) continue;

        // A label sits outside the symbol's own area, so the test has to be
        // wider than the item or a symbol scrolled half off screen would lose
        // its text before it lost its body.
        const QRectF where = comp->sceneBoundingRect().adjusted( -48, -48, 48, 48 );
        if( !rect.intersects( where ) ) continue;

        const QPointF origin = comp->scenePos();

        if( comp->showId() )
        {
            const QString text = comp->idLabelText();
            if( !text.isEmpty() )
            {
                painter->save();
                painter->translate( origin + QPointF( comp->labelx(), comp->labely() ) );
                painter->rotate( comp->labelRot() );
                painter->setFont( font );
                painter->setPen( kIdLabelColor );
                painter->drawText( QRectF( -80, -12, 160, 14 ),
                                   Qt::AlignCenter, text );
                painter->restore();
            }
        }

        if( comp->showVal() )
        {
            const QString text = comp->valLabelText();
            if( !text.isEmpty() )
            {
                painter->save();
                painter->translate( origin + QPointF( comp->valLabelx(), comp->valLabely() ) );
                painter->rotate( comp->valLabRot() );
                painter->setFont( font );
                painter->setPen( kValLabelColor );
                painter->drawText( QRectF( -80, -12, 160, 14 ),
                                   Qt::AlignCenter, text );
                painter->restore();
            }
        }
    }
}

// ---- the <circuit> root attributes ---------------------------------------------

int  Circuit::circSpeed() const
{
    Simulator *sim = Simulator::self();
    return sim ? sim->simuRate() : DEF_SIMURATE;
}

void Circuit::setCircSpeed( int rate )
{
    if( Simulator *sim = Simulator::self() ) sim->setSimuRate( rate );
}

int  Circuit::reactStep() const
{
    Simulator *sim = Simulator::self();
    return sim ? sim->reactStep() : DEF_REACTSTEP;
}

void Circuit::setReactStep( int steps )
{
    if( Simulator *sim = Simulator::self() ) sim->setReactStep( steps );
}

int  Circuit::noLinAcc() const
{
    Simulator *sim = Simulator::self();
    return sim ? sim->noLinAcc() : DEF_NOLINACC;
}

void Circuit::setNoLinAcc( int acc )
{
    if( Simulator *sim = Simulator::self() ) sim->setNoLinAcc( acc );
}

bool Circuit::animate() const
{
    Simulator *sim = Simulator::self();
    return sim ? sim->animate() : false;
}

void Circuit::setAnimate( bool a )
{
    if( Simulator *sim = Simulator::self() ) sim->setAnimate( a );

    // Off means the pins stop showing voltage, so they have to be told once
    // rather than waiting for a step that will not come.
    if( !a )
        for( Component *comp : m_compList )
            for( int i = 0; comp && i < comp->numPins(); i++ )
                if( Pin *pin = comp->pin( i ) ) pin->updateStep();
}
