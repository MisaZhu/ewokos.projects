/*
 * SimulIDE, ported to EwokOS - see simulator.h.
 */

#include <QTimerEvent>

#include "simulator.h"

#include "baseprocessor.h"
#include "e-element.h"
#include "e-node.h"

// The GUI thread tick.  50 Hz is smooth enough to watch an LED blink and short
// enough that a Stop press lands within one frame.
#define TICK_MS 20

// How much of each tick the step loop may spend before it yields.  The rest is
// left for event handling and the repaint, so a heavy circuit slows the
// simulation down instead of freezing the window.
#define BUDGET_MS 10

Simulator* Simulator::m_pSelf = 0l;

Simulator::Simulator( QObject *parent )
           : QObject( parent )
           , m_running( false )
           , m_paused( false )
           , m_timerId( 0 )
           , m_simuRate( DEF_SIMURATE )
           , m_speed( 100 )
           , m_noLinAcc( DEF_NOLINACC )
           , m_reactStep( DEF_REACTSTEP )
           , m_circuitRate( 0 )
           , m_animate( false )
           , m_step( 0 )
           , m_rateSteps( 0 )
{
    m_pSelf = this;
}

Simulator::~Simulator()
{
    stopSim();

    if( m_pSelf == this ) m_pSelf = 0l;
}

void Simulator::startSim()
{
    if( m_running ) return;

    m_error.clear();
    m_step       = 0;
    m_circuitRate = 0;
    m_rateSteps   = 0;

    // Every element gets one chance to publish its initial state before the
    // first solve, so a source that has never been stamped does not read as
    // zero volts for the first step.
    setCircChanged();

    // And the reactive ones have to be asked, because two of the things they
    // publish are a function of when the run starts rather than of how the
    // sheet was built: dt, which a .simu can change under a capacitor that has
    // already computed its companion conductance from the old one, and the
    // charge a capacitor is holding, which a fresh run starts without.  m_step
    // is zeroed above for the same reason - Start means from rest, not from
    // wherever Pause left off.
    for( eElement *el : m_reactiveList ) el->resetStep();

    m_running = true;
    m_paused  = false;

    m_rateTimer.start();
    m_timerId = startTimer( TICK_MS );

    emit simuStateChanged( true );
}

void Simulator::stopSim()
{
    if( !m_running ) return;

    if( m_timerId ) { killTimer( m_timerId ); m_timerId = 0; }

    m_running = false;
    m_paused  = false;

    emit simuStateChanged( false );
}

void Simulator::pauseSim()
{
    if( !m_running || m_paused ) return;

    m_paused = true;
}

void Simulator::resumeSim()
{
    if( !m_running || !m_paused ) return;

    m_paused = false;

    // Real time kept running while paused; without this reset the next tick
    // would try to catch up on all of it at once.
    m_rateTimer.restart();
    m_rateSteps = 0;
}

void Simulator::setSimuRate( int rate )
{
    if( rate < 1000 ) rate = 1000;

    if( rate == m_simuRate ) return;

    m_simuRate = rate;

    // dt changed, so every companion model is stale.
    setCircChanged();

    for( eElement *el : m_reactiveList ) el->resetStep();
}

void Simulator::setSpeed( int percent )
{
    if( percent < 1 )   percent = 1;
    if( percent > 1000 ) percent = 1000;

    m_speed = percent;
}

void Simulator::setNoLinAcc( int acc )
{
    if( acc < 1 )  acc = 1;
    if( acc > 50 ) acc = 50;

    m_noLinAcc = acc;
}

void Simulator::setReactStep( int steps )
{
    if( steps < 1 )   steps = 1;
    if( steps > 1000 ) steps = 1000;

    m_reactStep = steps;
}

void Simulator::setCircChanged()
{
    m_matrix.setCircChanged();
}

// ---- registration -----------------------------------------------------------

void Simulator::addToEnodeList( eNode *nod )
{
    if( !nod ) return;
    if( m_eNodeList.contains( nod ) ) return;

    m_eNodeList.append( nod );
    setCircChanged();
}

void Simulator::remFromEnodeList( eNode *nod, bool del )
{
    m_eNodeList.removeAll( nod );
    setCircChanged();

    if( del ) delete nod;
}

void Simulator::addToElementList( eElement *el )
{
    if( !el ) return;
    if( m_elementList.contains( el ) ) return;

    m_elementList.append( el );
}

void Simulator::remFromElementList( eElement *el )
{
    m_elementList.removeAll( el );
}

void Simulator::addToReactiveList( eElement *el )
{
    if( !el ) return;
    if( m_reactiveList.contains( el ) ) return;

    m_reactiveList.append( el );
}

void Simulator::remFromReactiveList( eElement *el )
{
    m_reactiveList.removeAll( el );
}

void Simulator::addToNoLinList( eElement *el )
{
    if( !el ) return;
    if( m_nonLinear.contains( el ) ) return;

    m_nonLinear.append( el );
}

void Simulator::remFromNoLinList( eElement *el )
{
    m_nonLinear.removeAll( el );
}

void Simulator::addToChangedFast( eElement *el )
{
    if( !el ) return;
    if( m_changedFast.contains( el ) ) return;

    m_changedFast.append( el );
}

void Simulator::remFromChangedFast( eElement *el )
{
    m_changedFast.removeAll( el );
}

void Simulator::addToMcuList( BaseProcessor *proc )
{
    if( !proc ) return;
    if( m_mcuList.contains( proc ) ) return;

    m_mcuList.append( proc );
}

void Simulator::remFromMcuList( BaseProcessor *proc )
{
    m_mcuList.removeAll( proc );
}

// ---- the step ---------------------------------------------------------------

bool Simulator::solveAnalog()
{
    if( m_matrix.circChanged() )
        m_matrix.createMatrix( m_eNodeList, m_elementList );

    m_matrix.zeroMatrix();

    for( eElement *el : m_elementList ) el->stamp();

    // Each node says whether the admittance half of its row moved.  The OR is
    // what the matrix needs to decide between re-factoring and re-solving, and
    // it has to be collected here rather than left to the matrix: only the
    // stamps know, and they happen between the zeroing and the solve.  Getting
    // this wrong is silent - the circuit still runs, it just runs against the
    // factorisation from the first step, so a diode never finds its operating
    // point and a value edited mid-run has no effect.
    bool admitChanged = false;
    for( eNode *nod : m_eNodeList )
        if( nod->stampMatrix() ) admitChanged = true;

    m_matrix.setAdmitChanged( admitChanged );

    if( !m_matrix.solveMatrix() )
    {
        m_error = m_matrix.error();
        return false;
    }
    m_matrix.updateNodes();

    return true;
}

void Simulator::settleLogic()
{
    // A logic device whose input node moved recomputes its output, which moves
    // the node behind it, and so on down the chain.  Repeat until nothing
    // moves, bounded so an oscillator cannot eat the step.
    for( int pass=0; pass<LOGIC_SETTLE; ++pass )
    {
        bool anyChanged = false;

        for( eNode *nod : m_eNodeList )
        {
            if( !nod->voltChanged() ) continue;

            nod->notifyFast();
            anyChanged = true;
        }
        for( eNode *nod : m_eNodeList ) nod->setVoltChanged( false );

        if( !anyChanged ) break;

        bool fired = false;
        for( eElement *el : m_changedFast )
        {
            if( !el->changed() ) continue;

            el->setChanged( false );
            el->updateStep();
            fired = true;
        }
        if( !fired ) break;

        // The outputs that just moved have to reach their nodes.  Single nodes
        // take them directly; a loaded node needs the matrix again.
        if( m_matrix.size() == 0 )
        {
            for( eNode *nod : m_eNodeList )
                if( nod->isSingle() ) nod->solveSingle();
        }
        else if( !solveAnalog() ) break;
    }
}

void Simulator::advanceReactive()
{
    // Capacitors and inductors integrate over dt using the voltages just
    // solved, leaving their companion sources set up for the next step.
    for( eElement *el : m_reactiveList ) el->updateStep();
}

void Simulator::advanceMcu()
{
    for( BaseProcessor *proc : m_mcuList ) proc->runStep();
}

void Simulator::runCircuitStep()
{
    if( !m_running || m_paused ) return;

    if( !doStep() ) stopSim();
}

bool Simulator::stepOnce()
{
    const bool ok = doStep();

    // Emitted either way: a step that failed still moved the pins it got as far
    // as it did, and the sheet has to show the state it is actually in rather
    // than the state it was in before the attempt.
    emit stepDone();

    return ok;
}

bool Simulator::doStep()
{
    // Newton over the non-linear devices: each pass re-linearises them against
    // the voltages the previous pass solved, which is what converges a diode's
    // operating point.  A circuit with none of them pays a single pass.
    const int passes = m_nonLinear.isEmpty() ? 1 : m_noLinAcc;

    for( int i=0; i<passes; ++i )
    {
        if( !solveAnalog() ) return false;
    }
    advanceReactive();
    settleLogic();
    advanceMcu();

    ++m_step;

    return true;
}

void Simulator::timerEvent( QTimerEvent *event )
{
    if( event->timerId() != m_timerId ) return;

    if( !m_running || m_paused ) return;

    // Steps owed for the real time elapsed since the last measurement, at the
    // requested share of real time.
    const qint64 elapsed = m_rateTimer.elapsed();

    QElapsedTimer budget;
    budget.start();

    qint64 target = (qint64)m_simuRate*m_speed*elapsed/( 100*1000 );
    if( target < 1 ) target = 1;

    // A long stall - the window being dragged, another app having the CPU -
    // must not turn into a burst of a million steps when it clears.
    if( target > m_simuRate/10 ) target = m_simuRate/10;

    qint64 done = 0;
    while( done < target && budget.elapsed() < BUDGET_MS )
    {
        runCircuitStep();

        if( !m_running ) return;      // the solve failed and stopped us

        ++done;
    }
    m_rateSteps += done;

    // Report the achieved rate about twice a second.
    if( elapsed >= 500 )
    {
        const int rate = (int)( m_rateSteps*1000/elapsed );

        m_rateTimer.restart();
        m_rateSteps = 0;

        if( rate != m_circuitRate )
        {
            m_circuitRate = rate;
            emit rateChanged( rate );
        }
    }
    emit stepDone();
}
