/*
 * SimulIDE, ported to EwokOS - the two-terminal base.
 *
 * Upstream: src/simulator/elements/passive/e-resistor.h.  Upstream keeps this
 * as an element class separate from the component that owns it - Resistor
 * holds an eResistor, Capacitor an eCapacitor, Diode an eDiode - and all of
 * those derive from eResistor, because a capacitor and a diode are each a
 * conductance between two nodes with a current source across it.  Here the
 * component IS the element (see circuit/component.h), so that whole family
 * collapses into the one base below and the components derive from it directly.
 *
 * What it provides is the stamp for "an admittance between two pins", which is
 * the most-used thing in the library: resistors, capacitors, inductors, diodes,
 * the small-signal half of a transistor, relay contacts, closed switches and
 * analog muxes are all that, plus in some cases a current source in parallel.
 *
 * Q_OBJECT below is not optional, and not for the usual reason.  moc emits a
 * meta-object's superdata as a literal link to the FIRST base class named in
 * the declaration - SuperData::link<TwoPin::staticMetaObject>() for
 * `class Resistor : public TwoPin` - whether or not that base has Q_OBJECT
 * (qtbase-5.15/src/tools/moc/generator.cpp, purestSuperClass).  Dropping the
 * macro from an intermediate class therefore compiles cleanly and fails the
 * link of every component under it, which is not where one would look for it.
 */

#ifndef SIMU_TWO_PIN_H
#define SIMU_TWO_PIN_H

#include "component.h"

class ePin;

// Floor on a resistance, and on anything else that ends up as one.  The
// admittance is 1/R, so a zero would put an infinity on the diagonal and the
// factorisation would fail with the matrix looking perfectly well formed.
// 1e-12 ohm is a short to any precision the sheet can display.
#define RES_FLOOR 1e-12

// Stamp an admittance between two pins, both directions.  A free function
// rather than a TwoPin member because not everything that needs it is
// two-terminal: a relay stamps one between each contact and its pole, an analog
// mux between the selected channel and the common, a switch between its two
// throws.  None of those is a TwoPin and all of them want this.
//
// Both directions are load-bearing.  stampAdmitance() puts the conductance on
// the pin's own diagonal; setOtherNode() is what turns it into the
// off-diagonal term in the OTHER node's row.  The matrix is symmetric only
// because each end does both halves, and a stamp that does one gives a circuit
// that solves to something plausible and wrong.
//
// Stamps nothing at all when either end has no matrix row, which for a
// two-terminal part means that end is dangling.  See the comment in the .cpp:
// the half-stamp one would get by omitting the test is a conductance to ground,
// not an open lead.
//
// Returns whether it stamped, and the caller that has a second term to add has
// to honour it.  A capacitor's companion current source is the case: stamping
// -Is onto the one live end of an element whose other end is unwired is a
// current sink with no source, and it drags that node towards whatever the
// source can push.  So the source is stamped only when the conductance was.
bool stampConductance( ePin *a, ePin *b, double admit );

// Stamp a Thevenin source - `volt` behind an internal resistance `imped` - on
// a single pin, as its Norton equivalent: a conductance from the pin's node to
// the reference and a current source of V/R alongside it.  This is the
// single-ended form, the one a rail, a fixed voltage, a clock and every logic
// output use, where the other terminal of the source is the sheet's reference
// rather than a pin of the symbol.
//
// Deliberately the exception to the dangling-terminal rule stampConductance()
// enforces: here a conductance to the reference IS the element, not an
// accident of stamping half of a pair.  It is also what makes the "single node"
// fast path work - eNode::solveSingle() reads voltOut() straight off the pin
// and never looks at either stamp, so the same object is exact when unloaded
// and ordinary when loaded.
void stampSource( ePin *pin, double volt, double imped );

// The simulation timestep in seconds, or 1/DEF_SIMURATE when there is no
// Simulator yet - which is the state a component is in while it is being built,
// and the state a part built directly in a test is in for its whole life.  Both
// guards matter: a dt of zero makes a capacitor's companion conductance C/dt
// infinite and an inductor's dt/L zero, and neither failure is reported - the
// matrix just solves to nonsense.
//
// A free function rather than a TwoPin member because every device with a state
// that advances per step needs it and most of them are not two-terminal: a
// clock, a wave generator, a flip-flop's propagation delay, an inductor, a
// stepper's coil current, a servo's position loop.
double simuDt();

class TwoPin : public Component
{
    Q_OBJECT

    public:
        TwoPin( QObject *parent, const QString &type, const QString &id );

        // ---- the element ---------------------------------------------------
        // Named ohms() rather than resist() deliberately.  A valued component
        // declares a property whose WRITE is setResist() and whose number is in
        // whatever unit the panel is showing - "100" alongside " kΩ" - while
        // these are in base units and are what the matrix reads.  Upstream
        // keeps the two apart by putting them in different classes, one per
        // half of the split; sharing a class means the names have to differ or
        // the property setter silently shadows the electrical one, and the
        // symptom is a component whose panel edits nothing.
        double ohms() const { return m_resist; }
        void setOhms( double r );

        double admit() const { return m_admit; }

        // v(lPin) - v(rPin), out of the last solve.  Zero while either pin is
        // unwired, which is what ePin::getVolt() reports for a pin with no node.
        double pinVolt() const;

        // Current through the element, lPin to rPin, in amps.  Read off the
        // solved voltages rather than stored from updateStep(): a plain
        // resistor is on no per-step list, so nothing would ever call it, and a
        // value read on demand cannot go stale against the voltages beside it.
        double current() const;

        // The stamp.  This version is the bare conductance; a capacitor adds
        // its companion current source alongside it, a diode recomputes the
        // conductance from pinVolt() first.
        virtual void stamp();

        // A prefix change has to reach the element.  See Component::setUnit()
        // for why this is virtual there.
        virtual void setUnit( const QString &un );

        // The two terminals.  Named for the way a symbol is drawn rather than
        // numbered, because "left" and "right" survive a rotation in the
        // reader's head and "0" and "1" do not.
        Pin *lPin() const { return pin( 0 ); }
        Pin *rPin() const { return pin( 1 ); }

    protected:
        // The property-facing setter.  `val` is expressed in the unit currently
        // showing, so it goes through Component::setValue() - which renormalises
        // the prefix and may change m_unitMult under the caller - and the ohms
        // are taken from getmultValue() afterwards rather than from the argument.
        void setDisplayValue( double val );

        // The timestep in seconds.  Upstream works in microseconds here
        // (reaClock()/1e6) and scales back at each end; this engine is SI all
        // the way through - volts, amps, ohms, farads, henrys - so dt is
        // 1/simuRate and a capacitance in farads needs no conversion anywhere.
        double dt() const;

        // Stamp this element's admittance across its own two pins.  Split out
        // from stamp() so a subclass that adds a current source stamps the
        // conductance half exactly the same way, and returns whether it did -
        // the subclass has to skip its own term when it did not.
        bool stampPair( double admit );

        double m_resist;
        double m_admit;
};

#endif // SIMU_TWO_PIN_H
