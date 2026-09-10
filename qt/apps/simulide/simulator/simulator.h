/*
 * SimulIDE, ported to EwokOS - the step loop.
 *
 * Upstream: src/simulator/simulator.h.  The list structure is upstream's -
 * separate lists for the nodes, the stamping elements, the reactive elements,
 * the non-linear ones and the logic devices watching node changes - because
 * each is walked at a different point in the step and walking the wrong set is
 * the difference between a simulator that runs and one that crawls.
 *
 * The threading is not.  Upstream runs runCircuit() on a QFuture via
 * QtConcurrent and marshals graphic updates back with signals; this Qt build
 * is configured -no-feature-concurrent (qconfig.h defines QT_NO_CONCURRENT)
 * and there is no QFuture to be had, so the loop is driven by a QTimer in the
 * GUI thread instead.  That changes how much work a step may take: each tick
 * runs steps until either the step target for the elapsed real time is met or
 * a CPU budget is spent, whichever comes first, and reports the rate it
 * actually achieved.  A circuit that cannot keep up with its own simuRate
 * therefore runs in slow motion rather than starving the event loop - which is
 * also what upstream's timerSc scaling was for.
 *
 * m_reactStep is read from and written back to .simu files so that circuits
 * authored upstream survive a round trip unchanged, but it does not batch steps
 * here: this port's step is one dt with the non-linear iteration inside it, so
 * there is no separate reactive batch for it to count.
 */

#ifndef SIMU_SIMULATOR_H
#define SIMU_SIMULATOR_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>

#include "circmatrix.h"

class BaseProcessor;
class eElement;
class eNode;

// Simulation steps per second of simulated time: dt is 1/this.  Upstream's
// default, and the value its own example circuits are written against.
#define DEF_SIMURATE 1000000

// Newton passes over the non-linear devices per step.
#define DEF_NOLINACC 5

// Reactive steps per non-linear batch, as upstream's files record it.
#define DEF_REACTSTEP 50

// Bound on the logic settling loop.  A ring oscillator never settles, and
// without a bound it would consume the whole step; with one it simply advances
// a few gate delays per step, which is what it is doing physically anyway.
#define LOGIC_SETTLE 8

class Simulator : public QObject
{
    Q_OBJECT
    public:
        explicit Simulator( QObject *parent = 0 );
        ~Simulator();

        static Simulator* self() { return m_pSelf; }

        void startSim();
        void stopSim();
        void pauseSim();
        void resumeSim();

        bool isRunning() const { return m_running; }
        bool isPaused()  const { return m_paused; }

        // Advance the circuit by one dt.  Does nothing unless the simulation is
        // running and unpaused, which is what makes it safe to call from the
        // timer callback without racing a Stop.
        void runCircuitStep();

        // One dt on demand, whether the simulation is running or not.  It leaves
        // the timer and both flags alone and emits stepDone(), so a stopped
        // circuit can be advanced a step at a time - which is how one that
        // oscillates too fast to watch is examined.  Returns false and sets
        // error() if the matrix did not solve; there is no run to stop.
        bool stepOnce();

        // Simulation clock, in steps.
        quint64 step() const { return m_step; }
        // Simulated time in seconds.
        double simTime() const { return m_step/(double)m_simuRate; }

        int  simuRate() const { return m_simuRate; }
        void setSimuRate( int rate );

        // Percent of real time the simulation advances at.
        int  speed() const { return m_speed; }
        void setSpeed( int percent );

        // What the loop actually achieved, steps per second.
        int circuitRate() const { return m_circuitRate; }

        int  noLinAcc() const { return m_noLinAcc; }
        void setNoLinAcc( int acc );

        int  reactStep() const { return m_reactStep; }
        void setReactStep( int steps );

        bool animate() const { return m_animate; }
        void setAnimate( bool a ) { m_animate = a; }

        // Set by anything that changes the topology - a component added or
        // removed, a connector made or broken, a node merged or split.
        void setCircChanged();

        QString error() const { return m_error; }
        void setError( const QString &e ) { m_error = e; }

        // ---- registration -------------------------------------------------
        QList<eNode*> &enodeList() { return m_eNodeList; }

        void addToEnodeList( eNode *nod );
        void remFromEnodeList( eNode *nod, bool del );

        void addToElementList( eElement *el );
        void remFromElementList( eElement *el );

        void addToReactiveList( eElement *el );
        void remFromReactiveList( eElement *el );

        void addToNoLinList( eElement *el );
        void remFromNoLinList( eElement *el );

        void addToChangedFast( eElement *el );
        void remFromChangedFast( eElement *el );

        void addToMcuList( BaseProcessor *proc );
        void remFromMcuList( BaseProcessor *proc );

    signals:
        void simuStateChanged( bool running );
        // Emitted once per timer tick, which is the cue to repaint the sheet.
        void stepDone();
        void rateChanged( int circuitRate );

    protected:
        void timerEvent( QTimerEvent *event );

    private:
        static Simulator* m_pSelf;

        // The step body, shared by both public entry points.  solveAnalog()
        // rebuilds the matrix when the topology has been flagged changed, so this
        // is safe to call with no run in progress.
        bool doStep();

        bool solveAnalog();
        void settleLogic();
        void advanceReactive();
        void advanceMcu();

        CircMatrix m_matrix;

        QList<eNode*> m_eNodeList;
        QList<eElement*> m_elementList;
        QList<eElement*> m_reactiveList;
        QList<eElement*> m_nonLinear;
        QList<eElement*> m_changedFast;
        QList<BaseProcessor*> m_mcuList;

        bool m_running;
        bool m_paused;
        int  m_timerId;

        int m_simuRate;
        int m_speed;
        int m_noLinAcc;
        int m_reactStep;
        int m_circuitRate;
        bool m_animate;

        quint64 m_step;

        // Rate measurement across timer ticks.
        QElapsedTimer m_rateTimer;
        qint64 m_rateSteps;

        QString m_error;
};

#endif // SIMU_SIMULATOR_H
