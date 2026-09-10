/*
 * SimulIDE, ported to EwokOS - see circuitwidget.h.
 */

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "circuitwidget.h"

#include "circuit.h"
#include "circuitview.h"
#include "simulator.h"

// The slider is percent of real time and Simulator::setSpeed() clamps to the
// same range, so the track runs end to end with no dead travel.  Linear rather
// than logarithmic: the band that matters is 10..200 and a log scale would put
// the default somewhere the user cannot see it move.
#define SPEED_MIN 1
#define SPEED_MAX 1000

// setSimuRate() floors at 1000 and the default is a million.  The ceiling is
// 100 MHz - far past what this loop achieves, and past the point where a dt of
// 10 ns means anything to the parts on offer.
#define RATE_MIN 1000
#define RATE_MAX 100000000

// The right-hand readout.  A free function rather than a member because it only
// reads the Simulator and both syncControls() and slotStepDone() want it.
static QString readoutText( const Simulator *sim )
{
    if( !sim ) return QString();

    // Fixed 'f' format and not QString::number( double ) on its own: the default
    // switches to exponential below 1e-5, which is exactly where a single-stepped
    // circuit lives and where the digits are the whole point.
    QString s = QString::number( sim->simTime(), 'f', 6 );
    s += QLatin1String( " s" );

    // The achieved rate is only meaningful while the loop is turning; a stopped
    // circuit showing "0 steps/s" reads like an error rather than like a pause.
    if( sim->isRunning() )
        s += QObject::tr( ", %1 steps/s" ).arg( sim->circuitRate() );

    return s;
}

// A thin vertical rule between the groups of controls.  QBoxLayout has no
// addSeparator() - that is QToolBar and QMenu - and a toolbar this dense needs
// the groups visible or Run and Speed read as one control.
static void addSep( QBoxLayout *box, QWidget *parent )
{
    QFrame *line = new QFrame( parent );
    line->setFrameShape( QFrame::VLine );
    line->setFrameShadow( QFrame::Sunken );
    box->addWidget( line );
}

CircuitWidget::CircuitWidget( QWidget *parent )
           : QWidget( parent )
           , m_sim( 0l )
           , m_circuit( 0l )
           , m_view( 0l )
           , m_runButton( 0l )
           , m_pauseButton( 0l )
           , m_stopButton( 0l )
           , m_stepButton( 0l )
           , m_speedSlider( 0l )
           , m_speedLabel( 0l )
           , m_rateSpin( 0l )
           , m_animateCheck( 0l )
           , m_timeLabel( 0l )
           , m_syncing( false )
{
    // The Simulator before the Circuit, and both before anything that can put a
    // component on the sheet.  Circuit::updateNodes() registers the nodes it
    // builds with Simulator::self(), so a sheet built first would silently drop
    // every node on the floor and the first solve would run against an empty
    // matrix.  Both are children of this widget, and QObject destroys children
    // in reverse order of creation, so the sheet is torn down while the
    // simulator is still there to take its elements and nodes off the lists.
    m_sim     = new Simulator( this );
    m_circuit = new Circuit( this );

    buildUi();
    hookUp();
    syncControls();
}

CircuitWidget::~CircuitWidget()
{
    // Stop before the children start going.  The tick timer would otherwise
    // still be live while the scene is being destroyed, and the step it runs
    // walks the element and node lists that ~Circuit is emptying.  ~Simulator
    // stops as well, but it is the last of the three to be destroyed and by then
    // the scene is already gone.
    if( m_sim ) m_sim->stopSim();
}

// ---- the ui ---------------------------------------------------------------------

void CircuitWidget::buildUi()
{
    m_view = new CircuitView( m_circuit, this );

    QHBoxLayout *bar = new QHBoxLayout();
    bar->setContentsMargins( 3, 2, 3, 2 );
    bar->setSpacing( 4 );

    // Text only.  There is no .qrc in this build and shipping icons would mean
    // a resource step the rest of the tree does not have, and a four-word run
    // control is legible without them.
    m_runButton = new QToolButton( this );
    m_runButton->setText( tr( "Run" ) );
    m_runButton->setToolTip( tr( "Start the simulation, or resume it from a pause" ) );
    m_runButton->setAutoRaise( true );
    bar->addWidget( m_runButton );

    m_pauseButton = new QToolButton( this );
    m_pauseButton->setText( tr( "Pause" ) );
    m_pauseButton->setToolTip( tr( "Freeze the simulation without resetting its clock" ) );
    m_pauseButton->setAutoRaise( true );
    bar->addWidget( m_pauseButton );

    m_stopButton = new QToolButton( this );
    m_stopButton->setText( tr( "Stop" ) );
    m_stopButton->setToolTip( tr( "Stop the simulation and reset its clock" ) );
    m_stopButton->setAutoRaise( true );
    bar->addWidget( m_stopButton );

    m_stepButton = new QToolButton( this );
    m_stepButton->setText( tr( "Step" ) );
    m_stepButton->setToolTip( tr( "Advance one dt while stopped or paused" ) );
    m_stepButton->setAutoRaise( true );
    bar->addWidget( m_stepButton );

    addSep( bar, this );

    bar->addWidget( new QLabel( tr( "Speed:" ), this ) );

    m_speedSlider = new QSlider( Qt::Horizontal, this );
    m_speedSlider->setRange( SPEED_MIN, SPEED_MAX );
    m_speedSlider->setPageStep( 10 );
    m_speedSlider->setFixedWidth( 110 );
    m_speedSlider->setToolTip( tr( "Percent of real time the simulation advances at.\n"
                                   "A runtime preference - it is not saved in the circuit." ) );
    bar->addWidget( m_speedSlider );

    m_speedLabel = new QLabel( this );
    m_speedLabel->setMinimumWidth( 46 );
    m_speedLabel->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
    bar->addWidget( m_speedLabel );

    addSep( bar, this );

    bar->addWidget( new QLabel( tr( "Steps/s:" ), this ) );

    m_rateSpin = new QSpinBox( this );
    m_rateSpin->setRange( RATE_MIN, RATE_MAX );
    m_rateSpin->setSingleStep( 100000 );
    m_rateSpin->setToolTip( tr( "Simulation steps per second of simulated time: dt is 1/this.\n"
                                "Saved in the circuit as the speed attribute." ) );
    bar->addWidget( m_rateSpin );

    addSep( bar, this );

    m_animateCheck = new QCheckBox( tr( "Animate" ), this );
    m_animateCheck->setToolTip( tr( "Colour wires and pins by voltage while running" ) );
    bar->addWidget( m_animateCheck );

    bar->addStretch( 1 );

    m_timeLabel = new QLabel( this );
    m_timeLabel->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
    bar->addWidget( m_timeLabel );

    QVBoxLayout *box = new QVBoxLayout( this );
    box->setContentsMargins( 0, 0, 0, 0 );
    box->setSpacing( 0 );
    box->addLayout( bar );
    box->addWidget( m_view, 1 );
}

void CircuitWidget::hookUp()
{
    connect( m_runButton,   SIGNAL( clicked() ), this, SLOT( run() ) );
    connect( m_pauseButton, SIGNAL( clicked() ), this, SLOT( pause() ) );
    connect( m_stopButton,  SIGNAL( clicked() ), this, SLOT( stop() ) );
    connect( m_stepButton,  SIGNAL( clicked() ), this, SLOT( step() ) );

    connect( m_speedSlider,  SIGNAL( valueChanged( int ) ), this, SLOT( slotSpeedSlider( int ) ) );
    connect( m_rateSpin,     SIGNAL( valueChanged( int ) ), this, SLOT( slotRateSpin( int ) ) );
    // toggled() and not clicked(), so setChecked() from syncControls() reaches
    // the slot and is caught by the m_syncing guard rather than slipping past it.
    connect( m_animateCheck, SIGNAL( toggled( bool ) ),     this, SLOT( slotAnimate( bool ) ) );

    connect( m_sim, SIGNAL( simuStateChanged( bool ) ), this, SLOT( slotSimuState( bool ) ) );
    connect( m_sim, SIGNAL( stepDone() ),               this, SLOT( slotStepDone() ) );

    // Two listeners on stepDone() and they do different jobs: this widget
    // refreshes the readout, the scene pushes the solved voltages out to the pin
    // colours.  The scene is connected to the simulator rather than called from
    // here so that it keeps working with no widget above it at all.
    connect( m_sim, SIGNAL( stepDone() ), m_circuit, SLOT( slotStepDone() ) );

    // The scene's two outward signals become this widget's own: MainWindow talks
    // to the sheet through this object and never has to reach past it.
    connect( m_circuit, SIGNAL( circuitModified( bool ) ), this, SLOT( slotModified( bool ) ) );
    connect( m_circuit, SIGNAL( propsRequested( Component* ) ),
                        this,     SIGNAL( propsRequested( Component* ) ) );
}

void CircuitWidget::syncControls()
{
    if( !m_sim || !m_circuit ) return;

    // Pushing values out to the widgets, so the slots they fire on the way must
    // not push the same values back in.
    m_syncing = true;

    m_speedSlider->setValue( m_sim->speed() );
    m_speedLabel->setText( QString::number( m_sim->speed() ) + QLatin1String( " %" ) );
    m_rateSpin->setValue( m_sim->simuRate() );
    m_animateCheck->setChecked( m_sim->animate() );

    m_syncing = false;

    m_timeLabel->setText( readoutText( m_sim ) );

    syncRunButtons();
}

void CircuitWidget::syncRunButtons()
{
    const bool running = m_sim && m_sim->isRunning();
    const bool paused  = running && m_sim->isPaused();

    // Run doubles as resume, so it is available in exactly the two states where
    // it would do something: stopped, and running-but-paused.  Step is the same,
    // because stepping a loop that is already running unpaused adds nothing the
    // timer is not doing faster.
    m_runButton->setEnabled( !running || paused );
    m_pauseButton->setEnabled( running && !paused );
    m_stopButton->setEnabled( running );
    m_stepButton->setEnabled( !running || paused );
}

// ---- running --------------------------------------------------------------------

void CircuitWidget::run()
{
    if( !m_sim ) return;

    if( m_sim->isRunning() ) m_sim->resumeSim();
    else                     m_sim->startSim();

    // startSim() emits and lands in slotSimuState(); resumeSim() emits nothing at
    // all, so without this the buttons stay in their paused arrangement and the
    // readout never restarts.
    syncRunButtons();
    m_timeLabel->setText( readoutText( m_sim ) );
}

void CircuitWidget::pause()
{
    if( !m_sim ) return;

    m_sim->pauseSim();

    // No signal from pauseSim() either - the simulator's "running" means started
    // and not stopped, and a paused run is still one.
    syncRunButtons();
}

void CircuitWidget::stop()
{
    if( !m_sim ) return;

    m_sim->stopSim();

    syncRunButtons();
}

void CircuitWidget::step()
{
    if( !m_sim ) return;

    if( !m_sim->stepOnce() )
    {
        // Nothing to stop - the simulation was not running, or was paused - so
        // the failure is only reportable, not recoverable by tearing a run down.
        emit statusMessage( m_sim->error() );
        return;
    }

    // stepOnce() emits stepDone(), which both listeners hear, so the sheet and
    // the readout are already current by the time it returns.
}

void CircuitWidget::slotSimuState( bool running )
{
    syncRunButtons();

    m_timeLabel->setText( readoutText( m_sim ) );

    emit simuStateChanged( running );

    // A false here can mean the user pressed Stop or that the matrix refused to
    // solve and the simulator stopped itself.  The second case has a reason in
    // error() and no other way out: a floating node or a shorted source lands
    // here and the sheet just freezes.  It goes to the status line rather than a
    // dialog because this is reached from inside the tick timer, and a modal
    // dialog there would open a nested event loop under the step that emitted it.
    if( !running && m_sim && !m_sim->error().isEmpty() )
        emit statusMessage( m_sim->error() );
}

void CircuitWidget::slotStepDone()
{
    m_timeLabel->setText( readoutText( m_sim ) );
}

void CircuitWidget::slotSpeedSlider( int percent )
{
    // The label follows the thumb even while syncing: syncControls() has just set
    // both and the text has to agree with the position it set.
    if( m_speedLabel )
        m_speedLabel->setText( QString::number( percent ) + QLatin1String( " %" ) );

    if( m_syncing ) return;

    if( m_sim ) m_sim->setSpeed( percent );
}

void CircuitWidget::slotRateSpin( int rate )
{
    if( m_syncing ) return;

    // Through the scene rather than straight at the simulator: simuRate is one of
    // the five attributes of the <circuit> root, and Circuit is what round-trips
    // them.  It forwards to the same setter, so the effect is identical, but a
    // future change to how the sheet treats a rate change lands in one place.
    if( m_circuit ) m_circuit->setCircSpeed( rate );
}

void CircuitWidget::slotAnimate( bool on )
{
    if( m_syncing ) return;

    // Circuit::setAnimate() and not the Simulator's: the Simulator's is a bare
    // flag, while the scene's also walks the pins once when animation goes off,
    // because there will be no further step to tell them to stop showing voltage.
    if( m_circuit ) m_circuit->setAnimate( on );
}

void CircuitWidget::slotModified( bool modified )
{
    // Just forwarded.  displayName() carries the asterisk, so MainWindow rebuilds
    // its title from this without the widget having to hold a label of its own.
    emit modifiedChanged( modified );
}

// ---- file -----------------------------------------------------------------------

QString CircuitWidget::displayName() const
{
    QString base;

    if( m_circuit && !m_circuit->fileName().isEmpty() )
        base = QFileInfo( m_circuit->fileName() ).fileName();
    else
        base = tr( "Untitled" );

    if( m_circuit && m_circuit->isModified() ) base.append( QLatin1String( " *" ) );

    return base;
}

bool CircuitWidget::confirmDiscard()
{
    if( !m_circuit || !m_circuit->isModified() ) return true;

    const QMessageBox::StandardButton ans = QMessageBox::question(
                this, tr( "Unsaved Changes" ),
                tr( "The circuit \"%1\" has been modified.\nDo you want to save it?" )
                    .arg( displayName() ),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Save );

    if( ans == QMessageBox::Cancel )  return false;
    if( ans == QMessageBox::Discard ) return true;

    // Save, and treat a failed save as a Cancel: going on would throw away the
    // only copy of what the user just asked to keep.
    return saveCircuit();
}

void CircuitWidget::newCircuit()
{
    if( !confirmDiscard() ) return;

    stop();

    m_circuit->clearAll();
    m_circuit->setFileName( QString() );

    // The five root attributes went out with the old sheet.  Without this a New
    // after an Open inherits the opened file's rate and animation and writes them
    // into whatever is saved next.
    m_circuit->resetSettings();

    // Not a root attribute - a runtime preference - so resetSettings() does not
    // touch it and the sheet would otherwise keep the last slider position.
    if( m_sim ) m_sim->setSpeed( 100 );

    syncControls();

    emit fileNameChanged( QString() );
    emit statusMessage( tr( "New circuit" ) );
}

bool CircuitWidget::openCircuit()
{
    if( !confirmDiscard() ) return false;

    // Start where the user already is, and in the shipped examples before that:
    // the first thing anyone does with this program is open one of those.
    QString dir;

    if( m_circuit && !m_circuit->fileName().isEmpty() )
        dir = QFileInfo( m_circuit->fileName() ).absolutePath();
    else
        dir = QLatin1String( SIMULIDE_EXAMPLES_DIR );

    const QString path = QFileDialog::getOpenFileName(
                this, tr( "Open Circuit" ), dir,
                tr( "SimulIDE Circuits (*.simu);;All Files (*)" ) );

    if( path.isEmpty() ) return false;

    return openCircuit( path );
}

bool CircuitWidget::openCircuit( const QString &path )
{
    if( !m_circuit || path.isEmpty() ) return false;

    // No dialog: this is the form a recent-files menu and a circuit named on the
    // command line use, and neither wants to be interrupted.  The caller has
    // already decided the current sheet can go.
    stop();

    if( !m_circuit->openFromFile( path ) )
    {
        // openFromFile() cleared the sheet before it found out the file was bad,
        // so what is on screen now is an empty untitled sheet and the name has to
        // say so rather than point at a file that did not load.
        m_circuit->setFileName( QString() );
        syncControls();

        emit fileNameChanged( QString() );
        emit statusMessage( m_circuit->error() );

        QMessageBox::critical( this, tr( "Open Circuit" ), m_circuit->error() );
        return false;
    }

    // A sheet that loaded but used parts this build has no class for still loads:
    // the unknown items are kept in extraItems() so saving puts them back.  That
    // is a warning and not a failure, but saying nothing would look like a
    // component had quietly vanished.
    const QString warn = m_circuit->error();

    syncControls();

    emit fileNameChanged( m_circuit->fileName() );

    if( warn.isEmpty() )
    {
        emit statusMessage( tr( "Opened %1" ).arg( path ) );
    }
    else
    {
        emit statusMessage( warn );
        QMessageBox::warning( this, tr( "Open Circuit" ), warn );
    }

    return true;
}

bool CircuitWidget::saveCircuit()
{
    if( !m_circuit ) return false;

    if( m_circuit->fileName().isEmpty() ) return saveCircuitAs();

    return writeCircuit( m_circuit->fileName() );
}

bool CircuitWidget::saveCircuitAs()
{
    if( !m_circuit ) return false;

    QString dir;

    if( !m_circuit->fileName().isEmpty() )
        dir = QFileInfo( m_circuit->fileName() ).absolutePath();
    else
        dir = QLatin1String( SIMULIDE_EXAMPLES_DIR );

    const QString path = QFileDialog::getSaveFileName(
                this, tr( "Save Circuit As" ), dir,
                tr( "SimulIDE Circuits (*.simu);;All Files (*)" ) );

    if( path.isEmpty() ) return false;

    return writeCircuit( path );
}

bool CircuitWidget::writeCircuit( const QString &path )
{
    if( !m_circuit ) return false;

    if( !m_circuit->saveToFile( path ) )
    {
        emit statusMessage( m_circuit->error() );

        QMessageBox::critical( this, tr( "Save Circuit" ), m_circuit->error() );
        return false;
    }

    // Read the name back rather than echoing the argument: saveToFile() appends
    // .simu when it is missing, so the path on the sheet can differ from the one
    // the dialog returned, and a recent-files menu has to be given the real one.
    emit fileNameChanged( m_circuit->fileName() );
    emit statusMessage( tr( "Saved %1" ).arg( m_circuit->fileName() ) );

    return true;
}
