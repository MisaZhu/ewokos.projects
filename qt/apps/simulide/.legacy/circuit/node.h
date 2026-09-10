/*
 * SimulIDE, ported to EwokOS - the junction.
 *
 * Upstream: src/gui/circuitwidget/node.h.  A Node is the dot three wires meet
 * at.  It is a Component with three zero-length pins all sitting at its centre,
 * which is what makes the wires converge on one point instead of each sprouting
 * a stub over the others, and all three pins are joined inside the symbol so
 * Circuit::updateNodes() unions them into one eNode.
 *
 * The interesting behaviour is that a Node does not want to exist.  It is a
 * editing convenience, not a device: the moment fewer than three wires are
 * attached it dissolves, and if two are left they are spliced into a single wire
 * so the sheet does not accumulate dots that no longer join anything.  Upstream
 * drives this from Pin::inStateChanged(int), a three-valued code where 1 means
 * "a wire went away", 0 means "recompute what is connected" and 2 means
 * "propagate bus-ness"; here it is the pinConnected() hook Component already
 * declares, and bus-ness is not propagated at all because updateNodes() derives
 * it for every pin from the node it lands on, in one pass.
 */

#ifndef SIMU_NODE_H
#define SIMU_NODE_H

#include "component.h"

class Node : public Component
{
    Q_OBJECT
    Q_INTERFACES( QGraphicsItem )

    public:
        Node( QObject *parent, const QString &type, const QString &id );

        enum { Type = QGraphicsItem::UserType + 4 };
        int type() const { return Type; }

        // Nothing to stamp: the node's three pins are unioned into one eNode by
        // the circuit, so the junction itself is not an element.
        bool isSimulable() const { return false; }

        void paint( QPainter *painter,
                    const QStyleOptionGraphicsItem *option,
                    QWidget *widget );

        // A wire attached or detached.  Dissolves the junction once it stops
        // being one.
        void pinConnected( Pin *pin, bool connected );

        // How many wires are attached.
        int connectedPins() const;

        // Dissolves the junction whatever it is holding: two wires are spliced
        // into one, a single wire is deleted with the node, and three or more
        // are left alone.  Public because loading a file creates nodes before
        // it creates their wires, so the check has to wait until the file has
        // been read - upstream runs the same pass over its joint list at the
        // end of loadDomDoc().
        void cleanUp();

    private:
        void dissolve();
};

#endif // SIMU_NODE_H
