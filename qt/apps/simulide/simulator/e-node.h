/*
 * SimulIDE, ported to EwokOS - the electrical node.
 *
 * Upstream: src/simulator/e-node.h.  An eNode is one equipotential: the pins
 * of every component a wire joins end up on the same one, and it owns the
 * matrix row those pins stamp into.
 *
 * The stamping is per-pin and replaces rather than accumulates, which is what
 * lets an element re-linearise itself (a diode re-stamping its conductance
 * from the voltage just solved) without the previous value lingering.
 *
 * "Single" nodes are the reason SimulIDE can run gate-level circuits at
 * speed.  A node whose only attachments are output pins and high-impedance
 * inputs has nothing to solve: its voltage is whatever the output drives.
 * eNode::solveSingle() assigns that directly and the node never gets a matrix
 * row, so a circuit of logic gates builds an empty matrix.  The classification
 * is ePin::isAdmit(), which a two-terminal element sets on its pins and a
 * logic input leaves clear - see e-pin.h.  An output pin does stamp its
 * internal admittance, because the moment a resistor is hung on that node the
 * node stops being single and the source has to appear in the matrix; the
 * stamp is simply ignored while it is single.
 */

#ifndef SIMU_ENODE_H
#define SIMU_ENODE_H

#include <QHash>
#include <QList>
#include <QString>

class ePin;
class eElement;

class eNode
{
    public:
        explicit eNode( const QString &id );
        ~eNode();

        QString itemId() const { return m_id; }

        void addEpin( ePin *epin );
        void remEpin( ePin *epin );
        QList<ePin*> getEpins() const { return m_ePinList; }
        int numPins() const { return m_ePinList.size(); }

        // Stamping, called through ePin.  The value replaces whatever this pin
        // stamped on the previous pass.
        void stampCurrent( ePin *epin, double data );
        void stampAdmitance( ePin *epin, double data );

        // The node number on the far side of a two-terminal element: it is what
        // turns a pin's admittance into an off-diagonal matrix entry.  -1 when
        // the other end is unconnected or is a single node.
        void setOtherNode( ePin *epin, int nodeNum );

        int  getNodeNumber() const { return m_nodeNum; }
        void setNodeNumber( int n ) { m_nodeNum = n; }

        double getVolt() const { return m_volt; }
        void   setVolt( double volt );

        bool voltChanged() const { return m_voltChanged; }
        void setVoltChanged( bool c ) { m_voltChanged = c; }

        bool isSingle() const { return m_single; }
        void setSingle( bool s ) { m_single = s; }

        bool isSwitched() const { return m_switched; }
        void setSwitched( bool s ) { m_switched = s; }

        bool isBus() const { return m_isBus; }
        void setIsBus( bool b ) { m_isBus = b; }

        // Classify the node and drop the stamps of the previous step.  Called
        // by CircMatrix::createMatrix() before anything stamps.
        void initialize();

        // A single node's voltage, taken from the output driving it.
        void solveSingle();

        // Push this node's row into the matrix.  No-op when single.  Returns
        // whether the admittance half of the row changed, which is what tells
        // CircMatrix it has to factor again rather than only re-solve.
        bool stampMatrix();
        void stampAdmit();
        void stampCurr();

        double totalAdmit() const { return m_totalAdmit; }
        double totalCurr() const { return m_totalCurr; }
        const QHash<int,double> &admitTo() const { return m_admit; }

        // Elements that want a callback the moment this node's voltage moves -
        // logic devices watching an input.  Kept on the node rather than in one
        // global list so that a single node can notify them without a solve.
        void addToChangedFast( eElement *el );
        void remFromChangedFast( eElement *el );
        const QList<eElement*> &changedFast() const { return m_changedFast; }

        void notifyFast();

    private:
        QList<ePin*> m_ePinList;
        QList<eElement*> m_changedFast;

        QHash<ePin*,double> m_admitList;
        QHash<ePin*,double> m_currList;
        QHash<ePin*,int>    m_nodeList;

        // Off-diagonal totals, keyed by the node number at the far end.
        QHash<int,double> m_admit;

        double m_totalCurr;
        double m_totalAdmit;

        double m_volt;
        int m_nodeNum;
        QString m_id;

        bool m_currChanged;
        bool m_admitChanged;
        bool m_voltChanged;
        bool m_single;
        bool m_switched;
        bool m_isBus;
};

#endif // SIMU_ENODE_H
