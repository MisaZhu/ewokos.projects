/*
 * SimulIDE, ported to EwokOS - the voltage/current source element.
 *
 * Upstream: src/simulator/e-source.h.  Every driven pin in SimulIDE - a fixed
 * voltage source, a rail, a gate output, a DAC - is an eSource: a Thevenin
 * voltage behind an internal impedance, stamped into the matrix as its Norton
 * equivalent.
 *
 * That single mechanism is what makes the "single node" fast path possible
 * (see e-node.h): with nothing loading the node the impedance drops out and
 * the node voltage is just m_voltOut, while with a load the same object is an
 * ordinary conductance-plus-current-source pair in the matrix.
 *
 * The two impedances below are the port's, chosen rather than inherited.
 * Upstream's ideal sources use a sub-microohm internal resistance, which is
 * accurate but leaves the matrix spanning many orders of magnitude; 1e-3 ohm
 * is indistinguishable from ideal against any load a user places (a 1 ohm
 * resistor loses 0.1% of the source voltage) and keeps the condition number
 * sane for a LU solve in double.  Logic outputs get 10 ohm, which is the order
 * of a real gate's output resistance and makes an LED on a gate output dim the
 * way it should.
 */

#ifndef SIMU_ESOURCE_H
#define SIMU_ESOURCE_H

#include "e-element.h"

class ePin;

// Internal impedance of an ideal voltage source or rail.
#define ESOURCE_IMPED 1e-3

// Internal impedance of a logic output.
#define ELOGIC_IMPED 10.0

class eSource : public eElement
{
    public:
        eSource( const QString &id, ePin *epin );
        ~eSource();

        // Driving or sensing.  A sensing source stamps nothing and only reads
        // the node voltage back.
        bool out() const { return m_out; }
        void setOut( bool out );

        double voltOut() const { return m_voltOut; }
        void setVoltage( double v );

        double imped() const { return m_imped; }
        void setImped( double i );

        // The current this source pushed out on the last solve.
        double outputCurrent() const { return m_output; }

        ePin *ePin0() const { return m_ePin; }

        virtual void stamp();
        virtual void resetStep();
        virtual void updateStep();

    protected:
        ePin *m_ePin;

        double m_voltOut;
        double m_imped;
        double m_output;

        bool m_out;
};

#endif // SIMU_ESOURCE_H
