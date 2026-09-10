/*
 * SimulIDE, ported to EwokOS - the active components.
 *
 * Upstream: src/gui/circuitwidget/components/active/.  Upstream's directory
 * holds the parts whose behaviour depends on a voltage or current they have to
 * solve for first, and it puts the Inductor here rather than with the other
 * passives; this file follows that split exactly, which is why an Inductor is
 * declared here and registers itself under the Passive tab.
 *
 * Everything in here but the Inductor is non-linear, and the way this engine
 * handles non-linearity is Newton: Simulator::doStep() runs noLinAcc passes over
 * the matrix per step and every element re-stamps between them, so a device
 * linearises itself against the voltages the previous pass produced.  What that
 * means for a class here is narrow and worth stating, because it is the one
 * design rule the whole file follows:
 *
 *   stamp() may READ the solved voltages, and must not assume they are from
 *   this step.  On the first pass of the first step they are zeros.
 *
 * Every device below therefore computes its companion conductance and current
 * source from getVolt() on its own pins, which are the values the last solve
 * left there, and converges over the passes.  A device that instead kept its
 * own idea of the operating point and advanced it once per step would work for
 * slowly-moving circuits and silently fail on a fast one.
 *
 * The two junction helpers are declared here rather than in twopin.h because
 * nothing outside this category needs them: they are the exponential-junction
 * linearisation, which is the diode and the base-emitter junction of the BJT and
 * nothing else.
 */

#ifndef SIMU_ACTIVE_H
#define SIMU_ACTIVE_H

#include "twopin.h"

// Thermal voltage at 300 K, kT/q.  Every exponential junction in here is
// written in terms of it, and it is the single constant that decides how sharply
// they turn on: the diode's conductance rises by e for every 26 mV of forward
// bias, which is why the Newton iteration converges in a handful of passes
// rather than in hundreds.
#define VT_300K 0.02585

// Floor on a junction conductance, and on the channel of an off MOSFET.  The
// reason is the same one switches.h gives for SW_OPEN_ADMIT: a pin that declares
// isAdmit() keeps its node out of the "single" fast path, so the node gets a
// matrix row, and a row that stamps nothing is a row of zeros.  1e-12 S passes
// 5 pA at 5 V, which is below anything the sheet displays.
#define ACT_OPEN_ADMIT 1e-12

// Linearise an exponential junction i = Isat*(exp(v/Vt)-1) about the voltage
// `v` last solved.  Fills the companion pair for the stamp idiom twopin.h
// documents - i = g*v - is - so that the branch current and its slope both match
// the exponential at `v`, which is what makes the next Newton pass land closer.
//
// The exponent is capped.  An unconverged first pass can see a forward bias of
// several volts across a junction whose saturation current is 1e-15, and an
// unbounded exponential is not a number this solver recovers from.  The cap has
// to sit above every operating point the model is asked about, though, or it
// stops being a backstop and starts being the model: a junction normalised at
// Forward_Volt evaluates exp(Vf/Vt) at its own knee, and 2.2 V - a red LED - is
// an argument of 85.  So the cap is 200, which is where exp() still leaves room
// in a double for the products around it: exp(200) is 7.2e86 and the saturation
// current a 5 V junction implies is 1.4e-90, so the conductance at the cap is
// 0.04 S rather than an infinity.
//
// `icur` may be null; when it is not, it receives the junction current at `v`,
// which is what the BJT multiplies by its gain.
void lineariseJunction( double v, double isat, double vt,
                        double &g, double &is, double *icur = 0l );

// The junction current on its own, for a caller that wants the reading rather
// than the stamp - a diode's displayed forward current, a transistor's base
// drive.
double junctionCurrent( double v, double isat, double vt );

// ---------------------------------------------------------------------------
// Diode
// ---------------------------------------------------------------------------

class Diode : public TwoPin
{
    Q_OBJECT
    Q_PROPERTY( double Forward_Volt READ fwdVolt WRITE setFwdVolt USER true )

    public:
        Diode( QObject *parent, const QString &type, const QString &id );

        // The drop at which the junction is taken to be fully on.  It is not a
        // threshold - there is no switching in this model - but the point the
        // saturation current is normalised to, which is the same thing a
        // datasheet's Vf is: the forward bias at a stated test current.
        double fwdVolt() const { return m_fwdVolt; }
        void setFwdVolt( double v );

        Pin *anode()   const { return lPin(); }
        Pin *cathode() const { return rPin(); }

        // Forward current out of the last solve, in amps.  Negative in reverse
        // bias, where it is the saturation current and no more.
        double forwardCurrent() const;

        virtual void stamp();

        // Its conductance depends on the voltage across it, so it has to be
        // re-stamped between passes.  Declaring this is what makes the operating
        // point converge instead of being frozen at the first pass's guess.
        virtual bool isNonLinear() const { return true; }

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    protected:
        // Saturation current, derived from Forward_Volt at DIODE_IFWD.  Kept
        // rather than recomputed per stamp because it is a constant of the part
        // and the exponential is expensive.
        void updateIsat();

        // Walks m_vlin towards the solved voltage by at most DIODE_VLIM and
        // returns the (g, is) pair for the junction at the point it reached, in
        // the i = g*v - is form the stamp idiom uses.  Split out of stamp()
        // because Led needs the pair for a stamp of a different shape: a
        // grounded LED's cathode is the sheet's reference and has no matrix row,
        // so the two-terminal stampPair() would decline and the part would not
        // light - it has to stamp the conductance and the companion source onto
        // the anode alone, which is stampSource()'s shape rather than
        // stampConductance()'s.
        void walkJunction( double &g, double &is );

        double m_fwdVolt;
        double m_isat;

    private:
        // The voltage the junction is linearised about, which is the solved
        // voltage clamped to within DIODE_VLIM of where it was last time.  SPICE
        // limits a junction the same way and for the same reason: an exponential
        // whose conductance rises by e every 26 mV is not a function Newton
        // converges on from an arbitrary start.  A diode that is off and then
        // sees the full supply on the first pass would linearise at that
        // voltage, stamp a conductance of tens of siemens and a companion source
        // of hundreds of amps, and the solve would throw it to the other rail -
        // and then do the same again, forever.  Walking the point a few kT at a
        // time costs two steps to reach the operating point and makes the
        // iteration monotone.
        double m_vlin;
};

// The forward current at which Forward_Volt is quoted, and so the point the
// saturation current is normalised to.  1 mA is a datasheet test current for a
// small-signal diode and is what makes Vf read 0.7 rather than 0.6 or 0.8.
#define DIODE_IFWD 1e-3

// How far the linearisation point may move per Newton pass, in volts.  Four
// thermal voltages: SPICE uses two, which is the conservative figure, and four
// reaches a 0.7 V operating point from a cold start inside one step's five
// passes rather than needing two steps to do it.
#define DIODE_VLIM 0.1

// ---------------------------------------------------------------------------
// Inductor
// ---------------------------------------------------------------------------
//
// Backward Euler, exactly as the capacitor is, and for the same reason: it turns
// v = L di/dt into a conductance of dt/L in parallel with a current source
// carrying the previous step's current, so the same linear solve handles it.
//
// The series resistance is folded into the companion pair rather than being a
// separate element.  A branch of R in series with L gives
//
//   i_n = i_{n-1}*L/(L+R*dt) + dt/(L+R*dt) * v_n
//
// which is the same shape with G = dt/(L+R*dt) and a decayed memory term.  Two
// elements would cost a matrix node between them for no electrical benefit.
//
// NOT a TwoPin, and that is a deliberate break with the rest of this file.  A
// TwoPin owns one resistance, m_resist, and TwoPin::current() is pinVolt() over
// it; an inductor's companion conductance is dt/(L+R*dt), which is not its
// resistance, and the branch current is not v over it either.  Deriving from
// TwoPin would leave both accessors returning a number that looks plausible and
// is wrong, which is worse than not having them.  So it derives from Component
// and says what it means.

class Inductor : public Component
{
    Q_OBJECT
    Q_PROPERTY( double  Inductance READ induct  WRITE setInduct  USER true )
    Q_PROPERTY( QString Unit       READ unit    WRITE setUnit    USER true )
    Q_PROPERTY( bool    Show_ind   READ showVal WRITE setShowVal USER true )
    // Last, and it has to be: the winding resistance is a bare ohmage that the
    // unit prefix does not apply to, so it must not be read before Inductance has
    // taken the prefix for itself.  See Potentiometer's Value_Ohm for the same
    // ordering argument.
    Q_PROPERTY( double  Resistance READ resist  WRITE setResist  USER true )
    Q_PROPERTY( bool    Show_res   READ showRes WRITE setShowRes USER true )

    public:
        Inductor( QObject *parent, const QString &type, const QString &id );

        Pin *lPin() const { return pin( 0 ); }
        Pin *rPin() const { return pin( 1 ); }

        double induct() const { return m_value; }
        void setInduct( double l );

        double henrys() const { return m_ind; }

        // The winding resistance in ohms.  A bare number rather than a valued one:
        // the component's unit prefix belongs to the inductance, and two valued
        // properties sharing one prefix is how a file carrying Unit=" m" comes to
        // scale a resistance by a thousand.
        double resist() const { return m_res; }
        void setResist( double r );

        bool showRes() const { return m_showRes; }
        void setShowRes( bool s ) { m_showRes = s; }

        // Voltage across the winding, lPin to rPin, out of the last solve.
        double pinVolt() const;

        // Current through the winding, lPin to rPin, as of the end of the last
        // step.  Stored rather than read on demand the way a resistor's is,
        // because the winding current is the state variable of the companion
        // model: it is what the next step's current source is built from, and it
        // has to be the value at the end of a step rather than a function of the
        // voltage currently on the pins.
        double curr() const { return m_curr; }

        virtual void stamp();
        virtual void updateStep();
        virtual void resetStep();
        virtual void setUnit( const QString &un );

        virtual bool isReactive() const { return true; }

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    private:
        // Recomputes the companion conductance from L, R and dt.  All three can
        // move - the value from the panel, the prefix from the file, the timestep
        // from the simulation rate - so it is one function called from all three
        // places rather than three copies of the arithmetic.
        void updateAdmit();

        double m_ind;       // henrys
        double m_res;       // ohms
        double m_admit;     // dt/(L + R*dt)
        double m_curr;      // amps, lPin to rPin, as of the end of the last step

        bool m_showRes;
};

// ---------------------------------------------------------------------------
// VoltReg
// ---------------------------------------------------------------------------
//
// A three-terminal regulator: input, ground, output.  The output is a voltage
// source between itself and the GROUND pin rather than between itself and the
// sheet's reference, which is the difference between a part that can regulate a
// negative rail and one that cannot - and the reason the ground pin exists on the
// symbol at all.
//
// The input pin is a sense, not a supply.  The output source delivers whatever
// current the load asks for and the input passes only a gigaohm of leakage, which
// is the standard simulator simplification and is what upstream does too.  What
// the input does decide is whether regulation holds: below the output voltage
// plus the dropout the regulator cannot make its number, and the output follows
// the input down instead.  Without that half the part would be an ideal source
// that works from a dead battery.

class VoltReg : public Component
{
    Q_OBJECT
    Q_PROPERTY( double  Voltage   READ voltOut WRITE setVolt    USER true )
    Q_PROPERTY( QString Unit      READ unit    WRITE setUnit    USER true )
    Q_PROPERTY( bool    Show_Volt READ showVal WRITE setShowVal USER true )

    public:
        VoltReg( QObject *parent, const QString &type, const QString &id );

        Pin *inPin()  const { return pin( 0 ); }
        Pin *outPin() const { return pin( 1 ); }
        Pin *gndPin() const { return pin( 2 ); }

        double voltOut() const { return m_value; }
        void setVolt( double v );

        virtual void setUnit( const QString &un );

        // Dropout, in volts.  Fixed at 2 V, the figure a 78xx datasheet quotes,
        // and not a property: upstream does not expose it and a user who needs a
        // low-dropout part needs a different symbol rather than a different
        // number in the panel.
        double dropout() const { return m_dropout; }

        virtual void stamp();

        virtual bool isNonLinear() const { return true; }

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    private:
        double m_volt;
        double m_dropout;
};

// ---------------------------------------------------------------------------
// OpAmp
// ---------------------------------------------------------------------------
//
// Five pins: output, inverting, non-inverting, V+ and V-.  The output is an
// eSource driving A*(v+ - v-) behind ESOURCE_IMPED, clamped to the supply rails,
// and the two inputs draw a gigaohm each.
//
// Declaring the output as a source against the sheet's reference rather than as a
// VCVS between two pins costs one matrix row and buys the single-node fast path:
// an op-amp driving a logic input and nothing else never enters the matrix at
// all.  It is also what makes the supply rails meaningful - the clamp is against
// the voltage the V+ pin is actually at, not against a fixed number.
//
// It is non-linear, and for the clamp rather than for the gain: a rail-limited
// output whose input difference is read from the previous pass is a device whose
// own output moves its inputs when there is feedback, and one pass is not enough
// to settle it.

class OpAmp : public Component
{
    Q_OBJECT
    Q_PROPERTY( double Gain READ gain WRITE setGain USER true )

    public:
        OpAmp( QObject *parent, const QString &type, const QString &id );

        Pin *outPin()    const { return pin( 0 ); }
        Pin *invPin()    const { return pin( 1 ); }
        Pin *noninvPin() const { return pin( 2 ); }
        Pin *vccPin()    const { return pin( 3 ); }
        Pin *veePin()    const { return pin( 4 ); }

        // Open-loop gain.  Upstream hardcodes 1e5; this exposes it because a
        // comparator built from an op-amp wants a lower figure to converge and a
        // textbook non-inverting amplifier wants the ideal one.
        double gain() const { return m_gain; }
        void setGain( double g );

        virtual void stamp();

        virtual bool isNonLinear() const { return true; }

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    private:
        double m_gain;
};

// ---------------------------------------------------------------------------
// Mosfet
// ---------------------------------------------------------------------------
//
// Gate, drain, source.  The gate is a capacitor of nothing at all - an infinite
// impedance in this model, which is the DC behaviour a MOSFET actually has and
// the reason the part is usable as a voltage-controlled switch.
//
// The channel is the square-law one the help text promises, linearised for the
// matrix.  In the triode region the drain current is k*(2*Vov*Vds - Vds^2) and
// its slope with respect to Vds is a conductance; in saturation the current
// stops depending on Vds at all, so the conductance falls away to the floor and
// the whole of the current rides in the companion source.  The two meet
// continuously at Vds = Vov, which is what keeps the Newton iteration from
// oscillating between regions.

class Mosfet : public Component
{
    Q_OBJECT
    Q_PROPERTY( bool    P_Channel READ pChannel WRITE setPChannel USER true )
    Q_PROPERTY( double  Threshold READ threshold WRITE setThreshold USER true )
    Q_PROPERTY( double  RDSon     READ rdsOn     WRITE setRdsOn     USER true )
    Q_PROPERTY( QString Unit      READ unit      WRITE setUnit      USER true )

    public:
        Mosfet( QObject *parent, const QString &type, const QString &id );

        Pin *gatePin()   const { return pin( 0 ); }
        Pin *drainPin()  const { return pin( 1 ); }
        Pin *sourcePin() const { return pin( 2 ); }

        bool pChannel() const { return m_pChan; }
        void setPChannel( bool p );

        double threshold() const { return m_vth; }
        void setThreshold( double v );

        // The fully-enhanced channel resistance, in the current unit - which for
        // a MOSFET is ohms, so RDSon="0.1" beside " " is a tenth of an ohm.
        double rdsOn() const { return m_value; }
        void setRdsOn( double r );

        virtual void setUnit( const QString &un );

        double rdsOhms() const { return m_rds; }

        virtual void stamp();

        virtual bool isNonLinear() const { return true; }

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    private:
        // The transconductance parameter k = 1/(RDSon*VOV_REF), chosen so that
        // the channel presents exactly RDSon at one volt of overdrive.  One volt
        // is arbitrary but it fixes the meaning of RDSon, which would otherwise
        // be a number with no operating point attached to it.
        void updateK();

        double m_vth;
        double m_rds;
        double m_k;

        bool m_pChan;
};

#define MOSFET_OV_REF 1.0

// ---------------------------------------------------------------------------
// BJT
// ---------------------------------------------------------------------------
//
// Base, collector, emitter.  Two junctions: base-emitter is the exponential one
// lineariseJunction() handles, and collector-emitter is a current source of
// Gain*Ib with a saturation clamp on it.
//
// The clamp is the part that is easy to get wrong.  An unclamped CCCS will drive
// the collector below the emitter when the base current asks for more than the
// external circuit can supply, and the model then reports a transistor that is
// amplifying a signal it has already run out of headroom for.  Below Vce_sat the
// device is instead modelled as a resistance of Vce_sat/(Gain*Ib), which meets
// the current-source branch continuously at the knee and cannot push Vce
// negative.

class BJT : public Component
{
    Q_OBJECT
    Q_PROPERTY( bool   PNP  READ pnp  WRITE setPnp  USER true )
    Q_PROPERTY( double Gain READ gain WRITE setGain USER true )

    public:
        BJT( QObject *parent, const QString &type, const QString &id );

        Pin *basePin()      const { return pin( 0 ); }
        Pin *collectorPin() const { return pin( 1 ); }
        Pin *emitterPin()   const { return pin( 2 ); }

        bool pnp() const { return m_pnp; }
        void setPnp( bool p );

        double gain() const { return m_gain; }
        void setGain( double g );

        // Base current out of the last solve, in amps, flowing into the base of
        // an NPN and out of the base of a PNP.
        double baseCurrent() const { return m_ib; }

        virtual void stamp();

        virtual bool isNonLinear() const { return true; }

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    private:
        double m_gain;
        double m_ib;      // base current at the last linearisation
        double m_isat;    // BE saturation current, from BJT_VBE_ON

        bool m_pnp;

        // The base-emitter linearisation point, walked rather than jumped, which
        // is Diode::m_vlin under another owner and for the identical reason: the
        // BE junction is the same exponential, and a cold start that linearises
        // it at the full supply voltage stamps a conductance and a companion
        // source that throw the base to the other rail - and then do it again.
        // Without this a transistor whose base resistor is fed from a rail never
        // converges, and the failure is an oscillating base rather than a wrong
        // steady state.
        double m_vlin;
};

// The base-emitter bias at which the base current is quoted, and so the point the
// saturation current is normalised to.  0.7 V at 10 uA is a small-signal
// transistor's datasheet point.
#define BJT_VBE_ON 0.7
#define BJT_IB_ON  1e-5

// Collector-emitter saturation voltage, below which the device stops being a
// current source.  0.2 V is the figure every datasheet tables.
#define BJT_VCE_SAT 0.2

// ---------------------------------------------------------------------------
// MuxAnalog
// ---------------------------------------------------------------------------
//
// One common, Channels inputs, and log2(Channels) address pins.  The selected
// channel is a low resistance to the common and every other channel is
// ACT_OPEN_ADMIT, so all of them are stamped on every pass - an unstamped
// channel pin would leave its node's matrix row empty, and the failure would be
// a singular matrix on a circuit that is merely routing a signal.
//
// It is not on the non-linear list.  Its conductances do not depend on a voltage
// it is solving for; they depend on the address, which is a logic level.  So it
// watches its address nodes through changedFast instead, which is the mechanism
// Component::nodesUpdated() documents: the address moves during settleLogic(),
// the mux is flagged, and settleLogic() re-solves the matrix once more.  Putting
// it on the non-linear list would pay five passes a step for a value that changes
// at most once.

class MuxAnalog : public Component
{
    Q_OBJECT
    Q_PROPERTY( int    Channels   READ channels   WRITE setChannels   USER true )
    Q_PROPERTY( double Impedance READ impedance  WRITE setImpedance  USER true )
    Q_PROPERTY( QString Unit     READ unit       WRITE setUnit       USER true )

    public:
        MuxAnalog( QObject *parent, const QString &type, const QString &id );

        Pin *commonPin() const { return pin( 0 ); }
        Pin *channelPin( int i ) const { return pin( 1 + i ); }
        Pin *addressPin( int i ) const { return pin( 1 + m_channels + i ); }

        int channels() const { return m_channels; }
        void setChannels( int n );

        // The on-resistance of the selected channel, in the current unit.
        double impedance() const { return m_value; }
        void setImpedance( double r );

        virtual void setUnit( const QString &un );

        // Which channel the address pins select, as a number.
        int selected() const;

        virtual void stamp();

        // The address pins have to be re-watched every time the node graph is
        // rebuilt, because the eNodes they were registered against are deleted
        // and made again.  See Component::nodesUpdated().
        virtual void nodesUpdated();

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    private:
        void buildPins();

        int m_channels;
        int m_addrPins;

        double m_ohms;
};

// ---- factories --------------------------------------------------------------

Component* createDiode(     QObject *parent, const QString &type, const QString &id );
Component* createInductor(  QObject *parent, const QString &type, const QString &id );
Component* createVoltReg(   QObject *parent, const QString &type, const QString &id );
Component* createOpAmp(     QObject *parent, const QString &type, const QString &id );
Component* createMosfet(    QObject *parent, const QString &type, const QString &id );
Component* createBJT(       QObject *parent, const QString &type, const QString &id );
Component* createMuxAnalog( QObject *parent, const QString &type, const QString &id );

#endif // SIMU_ACTIVE_H
