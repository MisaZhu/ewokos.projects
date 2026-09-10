/*
 * SimulIDE, ported to EwokOS - the wire.
 *
 * Upstream: src/gui/circuitwidget/connector.h, where a Connector is a
 * Component.  That is worth keeping, because it is what makes a wire round-trip
 * through a .simu file for free: the writer reflects over the meta-object, so a
 * Connector inherits objectName, x, y, rotation, hflip, labelx and the rest and
 * the file carries them exactly as upstream's does, and the four properties
 * below add startpinid, endpinid, enodeid and pointList on top.
 *
 * Electrically a wire is nothing.  It carries no impedance and stamps no row;
 * its whole job is to make two pins share one eNode, which Circuit::updateNodes()
 * works out by union-find over the connector list rather than by anything stored
 * here.  enodeid is kept so a saved circuit reads back with node names in
 * upstream's "Circ_eNode-N" shape.
 *
 * Two deviations from upstream:
 *
 * Upstream builds the wire out of child ConnectorLine items, one per segment, so
 * the user can drag a bend.  Here it is one QPainterPath.  Bends loaded from a
 * file are honoured exactly, but moving either end re-routes the whole wire, so
 * a hand-placed bend does not survive dragging a component.  That trades a nicety
 * for not having to keep a bend-editing mode alive across every component move,
 * and for one item per wire instead of up to six.
 *
 * Upstream keeps its item at the scene origin and its points in scene
 * coordinates, and declares boundingRect() as QRect(0,0,1,1) - which is why it
 * has to paint with the scene's clip rect effectively disabled.  Here the item
 * sits at its first point and the path is stored relative to that, so
 * boundingRect() is real and the view can cull a wire it cannot see.  The
 * property still reads and writes the first point, so the file is the same.
 */

#ifndef SIMU_CONNECTOR_H
#define SIMU_CONNECTOR_H

#include <QPainterPath>
#include <QPointF>
#include <QStringList>

#include "component.h"

class Pin;
class eNode;

// The sheet lattice.  Symbols are 8 or 16 across and their pins sit on
// multiples of it, so a wire that steps out one cell always lands on the grid.
#define GRID 8

class Connector : public Component
{
    Q_OBJECT
    Q_INTERFACES( QGraphicsItem )

    // A QStringList property is what makes pointList="-228,-164,-148,-164"
    // work: the writer joins with commas and the reader splits on them, and
    // the numbers come out flat rather than as "x,y" pairs, which is the
    // layout upstream's files use.
    Q_PROPERTY( QStringList pointList  READ pointList    WRITE setPointList )
    Q_PROPERTY( QString     startpinid READ startPinId   WRITE setStartPinId )
    Q_PROPERTY( QString     endpinid   READ endPinId     WRITE setEndPinId )
    Q_PROPERTY( QString     enodeid    READ enodeId      WRITE setEnodeId )

    public:
        Connector( QObject *parent, const QString &type, const QString &id,
                   Pin *startPin, Pin *endPin );
        ~Connector();

        enum { Type = QGraphicsItem::UserType + 2 };
        int type() const { return Type; }

        // A wire is not a device: it has no pins of its own and nothing to
        // stamp, so it stays out of the element list.
        bool isSimulable() const { return false; }

        QRectF boundingRect() const;
        QPainterPath shape() const;
        void paint( QPainter *painter,
                    const QStyleOptionGraphicsItem *option,
                    QWidget *widget );

        Pin *startPin() const { return m_startPin; }
        Pin *endPin()   const { return m_endPin; }

        QString startPinId() const;
        QString endPinId() const;
        void setStartPinId( const QString &id );
        void setEndPinId( const QString &id );

        // Scene coordinates, start pin first and end pin last.
        const QList<QPointF> &points() const { return m_pointList; }

        QStringList pointList() const;
        void setPointList( const QStringList &list );

        // Recomputes the polyline from where the two pins are now, and moves
        // the item to the first point.
        void updatePoints();

        QString enodeId() const { return m_enodeid; }
        void setEnodeId( const QString &id ) { m_enodeid = id; }

        bool isBus() const { return m_isBus; }
        void setIsBus( bool b );

        // Detaches from both pins and schedules its own deletion.
        void remove();

        // Called from ~Pin, so it only forgets the end: the wire is never
        // deleted from inside a destructor chain.  Circuit::remComponent()
        // takes the wires off a component before the component goes, which is
        // the one place teardown is driven from.
        void pinGone( Pin *pin );

    private:
        void updateShape();

        Pin *m_startPin;
        Pin *m_endPin;

        QList<QPointF> m_pointList;   // scene coordinates
        QPainterPath m_path;          // item coordinates
        QPainterPath m_shape;         // m_path widened, so a wire is easy to click

        QString m_enodeid;

        bool m_isBus;
};

#endif // SIMU_CONNECTOR_H
