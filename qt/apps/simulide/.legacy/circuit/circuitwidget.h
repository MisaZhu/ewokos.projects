/*
 * SimulIDE, ported to EwokOS - the sheet and the controls that drive it.
 *
 * Upstream: src/gui/circuitwidget/circuitwidget.h.  Upstream's CircuitWidget is
 * the largest class in its GUI: it owns the view, the scene, the component
 * selector, the property panel, the plotter, the toolbars and the file dialogs,
 * and reaches every one of them through a singleton.  This is the same object
 * with the singleton and the panels taken out.
 *
 * What is here is the sheet and what only the sheet can decide: the run controls,
 * the speed and animation settings that live in the <circuit> root of a .simu,
 * and opening and saving.  The component selector, the property panel and the
 * oscilloscope are MainWindow's, in gui/, because they are docked beside the
 * sheet rather than inside it and a sheet does not need to know they exist.
 *
 * Two deviations.
 *
 * CircuitWidget builds the Simulator.  Circuit::updateNodes() registers the nodes
 * it makes with Simulator::self(), so the Simulator has to exist first, and the
 * one place that can guarantee the order is the constructor that makes both.
 * MainWindow does not have to remember it.
 *
 * There is no undo.  Upstream implements it by snapshotting the whole
 * QDomDocument and restoring it; QDomDocument is in libQt5Xml, which this Qt build
 * does not have.  Reimplementing undo on a streamed format would mean holding a
 * serialised copy of the sheet per edit, which is a different design rather than a
 * port of this one, so the Edit menu has no Undo and this class does not pretend
 * otherwise.
 */

#ifndef SIMU_CIRCUITWIDGET_H
#define SIMU_CIRCUITWIDGET_H

#include <QWidget>

class Circuit;
class CircuitView;
class Component;
class Simulator;

class QCheckBox;
class QLabel;
class QSlider;
class QSpinBox;
class QToolButton;

class CircuitWidget : public QWidget
{
    Q_OBJECT

    public:
        explicit CircuitWidget( QWidget *parent = 0l );
        ~CircuitWidget();

        Circuit *circuit() const { return m_circuit; }
        CircuitView *view() const { return m_view; }

        // The name shown in the title bar, which is the file's base name and an
        // asterisk when the sheet has been edited since it was last written.
        QString displayName() const;

    public slots:
        // ---- file ----------------------------------------------------------
        // The no-argument forms put up a dialog; the ones that take a path do not,
        // which is what MainWindow's recent-files menu and a circuit named on the
        // command line need.

        void newCircuit();
        bool openCircuit();
        bool openCircuit( const QString &path );

        bool saveCircuit();
        bool saveCircuitAs();

        // ---- running --------------------------------------------------------

        void run();
        void pause();
        void stop();
        // One dt, leaving the simulation in whatever state it was in.  Works
        // stopped and works paused, which are the two states where it is useful:
        // it is how a circuit that runs too fast to watch is examined.  Note that
        // Run from stopped restarts the clock, so a stepped-then-run circuit does
        // not continue from where the stepping left it.
        void step();

    signals:
        void fileNameChanged( const QString &path );
        void modifiedChanged( bool modified );
        void simuStateChanged( bool running );

        // The window puts this on its status line.  Everything a user has to be
        // told that is not worth a dialog goes here.
        void statusMessage( const QString &msg );

        // Forwarded from the scene: a component wants the property panel.
        void propsRequested( Component *comp );

    private slots:
        void slotSimuState( bool running );
        void slotStepDone();

        void slotSpeedSlider( int percent );
        void slotRateSpin( int rate );
        void slotAnimate( bool on );

        void slotModified( bool modified );

    private:
        void buildUi();
        void hookUp();

        // Applies the settings a .simu carries in its <circuit> root to the
        // widgets that show them.  Called after a load and after a new sheet,
        // because both change values behind the widgets' backs.
        void syncControls();

        // Sets the four run buttons from the Simulator's two flags.  Called after
        // every run verb and from slotSimuState(), because pauseSim() and
        // resumeSim() emit nothing - the simulator's notion of "running" covers
        // a started-and-not-stopped run, and a paused run is still one.
        void syncRunButtons();

        // Asks about an edited sheet before it is thrown away, and returns false
        // if the answer was Cancel.  Shared by New and Open, which are the two
        // verbs that destroy a sheet.
        bool confirmDiscard();

        // Writes the sheet and reports the outcome.  Split out of saveCircuit()
        // and saveCircuitAs() because the two differ only in where the path came
        // from, and both have to handle a failure the same way.
        bool writeCircuit( const QString &path );

        Simulator *m_sim;
        Circuit *m_circuit;
        CircuitView *m_view;

        QToolButton *m_runButton;
        QToolButton *m_pauseButton;
        QToolButton *m_stopButton;
        QToolButton *m_stepButton;

        QSlider *m_speedSlider;
        QLabel *m_speedLabel;
        QSpinBox *m_rateSpin;
        QCheckBox *m_animateCheck;

        // The running readout: simulated time, and the rate the loop is actually
        // achieving while it is running.  There is no name label - displayName()
        // is MainWindow's title bar, and showing the same string twice on screen
        // only means the two can disagree.
        QLabel *m_timeLabel;

        // Set while a slot is pushing a value into a widget, so the widget's own
        // signal does not come back around and set the value again.  Without it
        // syncControls() and slotSimuState() fight over the same spin box.
        bool m_syncing;
};

#endif // SIMU_CIRCUITWIDGET_H
