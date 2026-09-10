/*
 * SimulIDE, ported to EwokOS - the sources.
 *
 * Upstream: src/gui/circuitwidget/components/sources/, one file per class.
 * They are collected here because they share one shape - a driven terminal -
 * and because splitting eight classes that are each forty lines into eight
 * files costs more in navigation than it saves.
 *
 * The family divides in two, and the division is electrical rather than
 * cosmetic:
 *
 *   Single-ended.  A Fixed Voltage, a Ground, a Logic Input, a Clock, a Wave
 *   Generator.  One pin, whose node the device drives against the sheet's
 *   reference through an internal impedance.  Stamped by stampSource(), which
 *   is the documented exception to the dangling-terminal rule: a conductance to
 *   the reference is the element here, not half of a pair.
 *
 *   Two-terminal.  A VoltSource and a CurrSource.  Two pins, and the EMF or the
 *   current is between them, so the stamp is a conductance across the pair plus
 *   a companion current source - see VoltSource::stamp() for the sign
 *   derivation, which is the part worth getting right.
 *
 * A Ground is not a Fixed Voltage of zero, and is not implemented as one.
 * Upstream's Ground declares no properties at all, and since Circuit's writer
 * walks the whole meta-object chain, a Ground that derived from VoltageBase
 * would come out of this port carrying Voltage="0" Unit=" V" Show_Volt="false"
 * that upstream's own loader has never seen.  It costs thirty lines to keep the
 * file clean.
 *
 * PROPERTY ORDER.  The classes below inherit Voltage/Unit/Show_Volt rather than
 * declaring them, so QMetaObject lists them ahead of a subclass's own - a Clock
 * writes Voltage before Freq, where upstream writes Freq last.  That is safe
 * and deliberate: Circuit::applyAttrs() looks each attribute up by name, so
 * order only matters where one setter's arithmetic depends on another having
 * already run.  None here does.  Potentiometer's Value_Ohm is the one case in
 * this tree that does, and it declares its properties to match.
 */

#ifndef SIMU_SOURCES_H
#define SIMU_SOURCES_H

#include "component.h"

// ---- the single-ended driven terminal ----------------------------------------

class VoltageBase : public Component
{
    Q_OBJECT
    Q_PROPERTY( double  Voltage   READ voltOut  WRITE setVolt    USER true )
    Q_PROPERTY( QString Unit      READ unit     WRITE setUnit    USER true )
    Q_PROPERTY( bool    Show_Volt READ showVal  WRITE setShowVal USER true )

    public:
        VoltageBase( QObject *parent, const QString &type, const QString &id );

        // The level this device drives right now, which is what the pin carries
        // and what stampSource() stamps.  Virtual because a Clock gates it on
        // its own square wave and a WaveGen on a phase table: both compute a
        // level that is not m_volt, and both would otherwise have to duplicate
        // the whole stamp.
        virtual double level() const { return m_out ? m_volt : 0.0; }

        double voltOut() const { return m_value; }
        void setVolt( double v );

        // Driving or high impedance.  A source that is off presents 1e30 ohm
        // rather than zero conductance, because a true open circuit on a node
        // that has nothing else on it leaves the matrix row empty and the solve
        // singular.  1e30 is an open to any load a user can place.
        bool out() const { return m_out; }
        virtual void setOut( bool out );

        virtual void setUnit( const QString &un );

        virtual void stamp();

        Pin *outPin() const { return pin( 0 ); }

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    protected:
        // Pushes level() onto the pin and asks for a repaint.  Called by
        // anything that changes the driven value, including the per-step
        // waveform advance - the stamp only reaches the matrix, and a stopped
        // circuit still has to show the state it is in.
        void applyOut();

        double m_imped;

        // m_volt is the electrical value in base units; m_value, which the
        // property reads and writes, is the number the panel shows.  The two
        // differ by exactly m_unitMult and are kept in step by setVolt/setUnit.
        double m_volt;

        // What is being driven right now.  For a Fixed Voltage and a Logic Input
        // it is the output; for a Clock and a Wave Generator it is the enable,
        // and the instantaneous level is the waveform's own state - upstream
        // overloads the same flag for both and a clock that toggles it stops on
        // its own first low half-cycle.
        bool m_out;
};

// "Fixed Voltage" in the library, "Rail" in files written by older releases -
// the alias is in itemlibrary.cpp.
class FixedVoltage : public VoltageBase
{
    Q_OBJECT
    public:
        FixedVoltage( QObject *parent, const QString &type, const QString &id );

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );
};

class Ground : public Component
{
    Q_OBJECT
    public:
        Ground( QObject *parent, const QString &type, const QString &id );

        virtual void stamp();

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );
};

class LogicInput : public VoltageBase
{
    Q_OBJECT
    Q_PROPERTY( bool Out READ out WRITE setOut USER true )

    public:
        LogicInput( QObject *parent, const QString &type, const QString &id );

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    protected:
        // A click toggles the level, which is the whole point of the device.
        // The base still has to see the event: without it the press would not
        // start a drag and the symbol could be switched but not moved.
        void mousePressEvent( QGraphicsSceneMouseEvent *event );
};

class LogicOutput : public Component
{
    Q_OBJECT
    public:
        LogicOutput( QObject *parent, const QString &type, const QString &id );

        // Reads the node once per GUI tick rather than per step: a lamp that
        // redrew itself a million times a second would cost more than the
        // simulation and look the same.
        virtual void updateDisplay();

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

        // Nothing to stamp, so it does not belong on the element list either.
        // Declaring this is what keeps a sheet of indicators out of the matrix.
        virtual bool isSimulable() const { return false; }

    private:
        bool m_lit;
};

class Clock : public VoltageBase
{
    Q_OBJECT
    Q_PROPERTY( bool   Always_On READ alwaysOn WRITE setAlwaysOn USER true )
    Q_PROPERTY( bool   Out       READ out      WRITE setOut      USER true )
    Q_PROPERTY( double Freq      READ freq     WRITE setFreq     USER true )

    public:
        Clock( QObject *parent, const QString &type, const QString &id );

        bool alwaysOn() const { return m_alwaysOn; }
        void setAlwaysOn( bool on ) { m_alwaysOn = on; }

        // In hertz, and deliberately not routed through the unit machinery: a
        // clock's frequency is a rate rather than a value with an SI prefix, and
        // upstream stores it as a bare number too.
        double freq() const { return m_freq; }
        void setFreq( double f );

        virtual double level() const;

        // Runs on the Simulator's reactive list.  That list exists for the
        // devices that integrate - a capacitor's updateStep() accumulates its
        // voltage - and what a clock needs is the same call at the same point in
        // the step: once, after the solve, before the logic settles.  Adding a
        // separate clock list to the Simulator would be truer to upstream's
        // naming and would change nothing about when updateStep() ran, so the
        // reactive list carries both and isReactive() means "advances per step".
        virtual bool isReactive() const { return true; }

        virtual void updateStep();
        virtual void resetStep();

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    private:
        double m_freq;
        double m_time;

        bool m_alwaysOn;

        // The square wave's own state, kept apart from the Out property for the
        // reason given on VoltageBase::m_out.
        bool m_high;
};

class WaveGen : public VoltageBase
{
    Q_OBJECT
    Q_PROPERTY( QString Wave_Type READ waveType WRITE setWaveType USER true )
    Q_PROPERTY( int     Quality   READ quality  WRITE setQuality  USER true )
    Q_PROPERTY( double  Freq      READ freq     WRITE setFreq     USER true )
    Q_PROPERTY( double  Volt_Base READ baseVolt WRITE setBaseVolt USER true )
    Q_PROPERTY( bool    Always_On READ alwaysOn WRITE setAlwaysOn USER true )
    Q_PROPERTY( bool    Out       READ out      WRITE setOut      USER true )

    public:
        WaveGen( QObject *parent, const QString &type, const QString &id );

        // "Sine", "Triangle", "Saw", "Ramp", "Square" - upstream's spellings,
        // which are the strings a .simu carries in Wave_Type.  Anything else
        // falls back to a sine rather than to silence, so a file from a release
        // that added a waveform still runs.
        QString waveType() const { return m_waveType; }
        void setWaveType( const QString &t );

        // Samples per period in the lookup table.  Upstream builds the table for
        // the same reason it is kept here: at a million steps a second the
        // generator is evaluated a million times a second, and indexing a table
        // is cheaper than a sine.  The amplitude, the offset and the shape are
        // baked in, so the table is rebuilt whenever any of them changes and
        // updateStep() only ever indexes it.
        int quality() const { return m_quality; }
        void setQuality( int q );

        double freq() const { return m_freq; }
        void setFreq( double f );

        double baseVolt() const { return m_base; }
        void setBaseVolt( double v ) { m_base = v; buildTable(); }

        bool alwaysOn() const { return m_alwaysOn; }
        void setAlwaysOn( bool on ) { m_alwaysOn = on; }

        virtual double level() const;

        virtual bool isReactive() const { return true; }

        virtual void updateStep();
        virtual void resetStep();

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    private:
        void buildTable();

        QVector<double> m_table;

        QString m_waveType;

        double m_freq;
        double m_base;

        // Position in the period, in [0,1), rather than an elapsed time: the
        // table is indexed straight from it, and it cannot drift into a range
        // where reducing it modulo the period takes more than one subtraction.
        double m_phase;

        int m_quality;

        bool m_alwaysOn;
};

// ---- the two-terminal sources --------------------------------------------------

class VoltSource : public Component
{
    Q_OBJECT
    Q_PROPERTY( double  Voltage   READ voltOut WRITE setVolt    USER true )
    Q_PROPERTY( QString Unit      READ unit    WRITE setUnit    USER true )
    Q_PROPERTY( bool    Show_Volt READ showVal WRITE setShowVal USER true )

    public:
        VoltSource( QObject *parent, const QString &type, const QString &id );

        double voltOut() const { return m_value; }
        void setVolt( double v );

        virtual void setUnit( const QString &un );

        virtual void stamp();

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

        Pin *lPin() const { return pin( 0 ); }
        Pin *rPin() const { return pin( 1 ); }

    private:
        double m_volt;
};

class CurrSource : public Component
{
    Q_OBJECT
    Q_PROPERTY( double  Current   READ currOut WRITE setCurr    USER true )
    Q_PROPERTY( QString Unit      READ unit    WRITE setUnit    USER true )
    Q_PROPERTY( bool    Show_curr READ showVal WRITE setShowVal USER true )

    public:
        CurrSource( QObject *parent, const QString &type, const QString &id );

        double currOut() const { return m_value; }
        void setCurr( double i );

        virtual void setUnit( const QString &un );

        virtual void stamp();

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

        Pin *lPin() const { return pin( 0 ); }
        Pin *rPin() const { return pin( 1 ); }

    private:
        double m_curr;
};

// ---- factories -----------------------------------------------------------------

Component* createFixedVoltage( QObject *parent, const QString &type, const QString &id );
Component* createGround(       QObject *parent, const QString &type, const QString &id );
Component* createLogicInput(   QObject *parent, const QString &type, const QString &id );
Component* createLogicOutput(  QObject *parent, const QString &type, const QString &id );
Component* createClock(        QObject *parent, const QString &type, const QString &id );
Component* createWaveGen(      QObject *parent, const QString &type, const QString &id );
Component* createVoltSource(   QObject *parent, const QString &type, const QString &id );
Component* createCurrSource(   QObject *parent, const QString &type, const QString &id );

#endif // SIMU_SOURCES_H
