/*
 * SimulIDE, ported to EwokOS - the reactive element base.
 *
 * Upstream: https://github.com/SimulIDE/SimulIDE_SF (GPL-3.0),
 * src/simulator/e-element.h.  An eElement is anything that takes part in a
 * simulation step: it owns the ePins that attach it to the circuit's eNodes
 * and is asked to stamp its contribution into the matrix.
 *
 * Two of upstream's conveniences are not reproduced here.  Its
 * addToReactiveList()/addToNoLinList()/addToChangedFast() forward to the
 * Simulator singleton; this port keeps the same registration but spells it
 * out at the call site, so this header does not pull in the Simulator's -
 * e-element.cpp does include it, because the destructor has to undo every
 * registration an element might have made and guessing wrong there leaves the
 * step loop walking freed memory.  And upstream builds on std::vector while
 * this tree's C++ standard library is ewok_stl, so the pin list is a QVector.
 */

#ifndef SIMU_EELEMENT_H
#define SIMU_EELEMENT_H

#include <QString>
#include <QVector>

class ePin;

class eElement
{
    public:
        explicit eElement( const QString &id );
        virtual ~eElement();

        QString itemID() const { return m_elmId; }
        // Virtual because a Component's name is also the prefix of its pins'
        // ids, which the .simu file stores and connectors are keyed on.
        virtual void setId( const QString &id ) { m_elmId = id; }

        int numEpins() const { return m_numEpins; }
        virtual void setNumEpins( int n );

        virtual void setEpin( int pin, ePin *epin );
        virtual ePin *getEpin( int pin ) const;

        // Write this element's admittances and current sources into the eNodes
        // it is attached to.  Called at the start of every non-linear pass, and
        // again by any element whose linearisation depends on a solved voltage.
        virtual void stamp() {}

        // Per-step hooks.  resetStep() clears the state a step accumulates;
        // updateStep() runs after the solve, which is where C and L integrate
        // and where clocked devices advance.
        virtual void resetStep() {}
        virtual void updateStep() { stamp(); }

        // Set by the Simulator when this element asked to be revisited.
        bool changed() const { return m_changed; }
        virtual void setChanged( bool c ) { m_changed = c; }

    protected:
        int m_numEpins;
        QVector<ePin*> m_epin;
        QString m_elmId;
        bool m_changed;
};

#endif // SIMU_EELEMENT_H
