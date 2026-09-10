/*
 * SimulIDE, ported to EwokOS - the electrical pin.
 *
 * Upstream: src/simulator/e-pin.h.  An ePin is the electrical half of a
 * connection: it belongs to an eElement, sits on an eNode, and carries the
 * three quantities the matrix is built from - the admittance it presents to
 * its node, the current it injects there, and, for an output, the
 * open-circuit voltage behind its internal impedance.
 *
 * The graphical Pin in circuit/pin.h derives from this, exactly as upstream's
 * does, which is what lets a component be both a QGraphicsItem and a circuit
 * element without a second bookkeeping layer.
 *
 * isAdmit() is the addition this port makes.  Upstream decides whether an
 * eNode can solve its own voltage by inspecting what got stamped into it
 * during the previous step; that works but couples the decision to stamp
 * ordering.  Here an element declares at construction time whether a pin
 * loads its node (a resistor, a diode, a capacitor all do; a logic input does
 * not), so eNode::initialize() can classify a node as "single" - solvable
 * without the matrix - before anything is stamped.  That is what keeps a
 * circuit made only of logic gates out of the matrix entirely, which is the
 * reason SimulIDE runs gate-level circuits at all.
 */

#ifndef SIMU_EPIN_H
#define SIMU_EPIN_H

#include <QString>

class eNode;
class eElement;

class ePin
{
    public:
        ePin( const QString &id, int index );
        virtual ~ePin();

        QString pinId() const { return m_pinId; }
        void setPinId( const QString &id ) { m_pinId = id; }

        int pinIndex() const { return m_index; }
        void setPinIndex( int index ) { m_index = index; }

        eElement *getElement() const { return m_element; }
        void setElement( eElement *el ) { m_element = el; }

        eNode *getEnode() const { return m_enode; }
        // Virtual because Pin repaints itself with the new node's colour.
        virtual void setEnode( eNode *enode );

        // The node voltage seen by this pin.  An output pin reports its own
        // source voltage before the matrix has run; everything else reports
        // the node it sits on.
        double getVolt() const;
        void setVolt( double volt );

        double getCurrent() const { return m_current; }
        void setCurrent( double current ) { m_current = current; }

        double getImped() const { return m_imped; }
        void setImped( double imp ) { m_imped = imp; }

        double getAdmit() const { return ( m_imped == 0 ) ? 0 : 1/m_imped; }
        void setAdmit( double admit ) { m_imped = ( admit == 0 ) ? 0 : 1/admit; }

        // Stamping: forwarded to the eNode, which is the object that owns the
        // matrix row.  No-ops when the pin is unconnected.
        void stampCurrent( double data );
        void stampAdmitance( double data );

        // Output pins: the Thevenin voltage behind m_imped.
        bool isOutput() const { return m_isOutput; }
        void setIsOutput( bool out );
        double voltOut() const { return m_voltOut; }
        void setVoltOut( double v ) { m_voltOut = v; }

        // Whether this pin presents a finite admittance to its node.
        bool isAdmit() const { return m_isAdmit; }
        void setIsAdmit( bool a ) { m_isAdmit = a; }

        // Multi-bit pins (a Bus) carry a vector of eNodes; single-bit pins
        // have a width of 1 and never allocate one.
        bool isBus() const { return m_isBus; }
        void setIsBus( bool bus ) { m_isBus = bus; }
        int busWidth() const { return m_busWidth; }
        void setBusWidth( int w ) { m_busWidth = w; }

        virtual void resetOutput() { m_isOutput = false; }

    protected:
        QString m_pinId;
        int m_index;

        eNode *m_enode;
        eElement *m_element;

        double m_current;
        double m_imped;
        double m_voltOut;

        bool m_isOutput;
        bool m_isAdmit;
        bool m_isBus;
        int m_busWidth;
};

#endif // SIMU_EPIN_H
