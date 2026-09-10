/*
 * SimulIDE, ported to EwokOS - the graphical pin.
 *
 * Upstream: src/gui/circuitwidget/pin.h.  A Pin is the ePin with a body: it is
 * a QGraphicsItem child of its Component, so it follows the symbol through
 * rotation and flip for free, and it is the thing the user drags a wire onto.
 *
 * The id it carries is "<component objectName>-<pin name>", which is exactly
 * what a .simu Connector's startpinid and endpinid name, so the file can refer
 * to a terminal without knowing where it is on the sheet.  That is why
 * Component::setId() walks its pins and rebuilds these.
 *
 * Animation is the only thing this port draws that upstream draws elsewhere:
 * when the simulator is running with animate on, the square fills with a
 * colour scaled from the node voltage, which is how you watch a signal travel.
 */

#ifndef SIMU_PIN_H
#define SIMU_PIN_H

#include <QColor>
#include <QGraphicsItem>
#include <QObject>
#include <QPainterPath>

#include "e-pin.h"

class Component;
class Connector;
class eNode;

class Pin : public QObject, public QGraphicsItem, public ePin
{
    Q_OBJECT
    Q_INTERFACES( QGraphicsItem )

    public:
        Pin( int x, int y, const QString &name, int angle,
             Component *parent, int index );
        ~Pin();

        enum { Type = QGraphicsItem::UserType + 3 };
        int type() const { return Type; }

        QRectF boundingRect() const;

        // The square alone, never the label.  boundingRect() has to be wide
        // enough to paint the label in, but shape() is what decides what the
        // user can click and what Circuit::pinAt() reports - a hit area the size
        // of a label would swallow the pins next to it.
        QPainterPath shape() const;

        void paint( QPainter *painter,
                    const QStyleOptionGraphicsItem *option,
                    QWidget *widget );

        QString pinName() const { return m_name; }
        void setPinName( const QString &n );

        // ---- the label -----------------------------------------------------
        // Upstream's Pin carries these four, and a Chip fills all of them from
        // its .package file: an AVR without "PB0" next to pin 14 is unusable, and
        // the bubble on an inverted input is the only thing that says the sense
        // is reversed.  Ordinary two-terminal components leave the label empty,
        // which is why boundingRect() grows only when there is one.

        QString labelText() const { return m_label; }
        void setLabelText( const QString &l );

        QColor labelColor() const { return m_labelColor; }
        void setLabelColor( const QColor &c ) { m_labelColor = c; update(); }

        bool isInverted() const { return m_inverted; }
        void setInverted( bool inv ) { m_inverted = inv; update(); }

        // A pin the package declares but the device does not use.  Drawn grey
        // and left out of the label, and isAvailable() refuses it so a wire
        // cannot be hung on it by mistake.
        bool isUnused() const { return m_unused; }
        void setUnused( bool un ) { m_unused = un; update(); }

        // How long the lead is, in scene units.  It governs two things that have
        // to agree, which is why there is one number: the router steps the wire
        // out this far along angle() before its first bend, and paint() draws a
        // stub this far back towards the symbol.  Together they make a wire leave
        // a terminal pointing the way the terminal points and meet the body it
        // belongs to.  Node's pins are 0 long, which is what makes three wires
        // meet at a dot instead of sprouting stubs; the 8 a normal pin gets is
        // one grid cell.
        int length() const { return m_length; }

        // boundingRect() grows to cover the lead, so a change of length is a
        // change of geometry and the scene has to be told before it happens or
        // the old rect is what gets repainted.
        void setLength( int l )
            { if( l == m_length ) return; prepareGeometryChange(); m_length = l; }

        // Rebuilds m_pinId from the component's current name.  Called by
        // Component::setId(), which is the only thing that can invalidate it.
        void updatePinId();

        Component *component() const { return m_comp; }

        // Angle of the lead leaving the symbol, in degrees: 0 right, 90 down,
        // 180 left, 270 up.  Connector uses it to decide which way the first
        // segment of the wire runs.
        int angle() const { return m_angle; }
        void setAngle( int a ) { m_angle = a; }

        // The wire attached here, or 0.  A pin takes at most one, as upstream
        // does; a junction is a Node component rather than a second wire.
        // Wires are made by Circuit::addConnector(), which is the only place
        // that can also put the new item on the sheet.
        Connector *connector() const { return m_con; }
        void setConnector( Connector *con ) { m_con = con; }

        // Detaches the wire, which deletes it.  Named disconnectPin rather
        // than disconnect, which QObject already has.
        void disconnectPin();

        void setEnode( eNode *enode );
        eNode *getEnode() const { return m_enode; }

        // Whether a wire may start here: an already-wired pin, an invisible one
        // and one the package marks unused all refuse.
        bool isAvailable() const;

        // Repaints with the voltage colour.  Called from the step when the
        // simulator is animating, and from setEnode so a freshly wired pin
        // picks up its node's colour immediately.
        void updateStep();

    private:
        // The unit vector this pin's lead points in, in scene coordinates: the
        // pin's own angle composed with the parent symbol's rotation and mirror.
        // Both the label placement and the inversion bubble come from it.
        void sceneDir( int &ux, int &uy ) const;

        Component *m_comp;
        Connector *m_con;

        QString m_name;
        int m_angle;
        int m_length;

        QString m_label;
        QColor m_labelColor;

        bool m_inverted;
        bool m_unused;

        QColor m_color;
};

#endif // SIMU_PIN_H
