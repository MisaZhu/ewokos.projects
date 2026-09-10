/*
 * SimulIDE, ported to EwokOS - the logic family.
 *
 * Upstream: src/gui/circuitwidget/components/logic/, one file per class.  All
 * twenty-odd of them are collected here because they share one shape - read some
 * inputs, drive some outputs - and because the alternative is twenty files that
 * are each a forty line class and a paint().
 *
 * TWO DECISIONS ABOUT THE BASE CLASS, both of which upstream makes differently.
 *
 * The gates do not derive from Chip.  Upstream's Gate is a Chip whose pinout
 * comes from a .package file, which is the right shape for a 74xx part and a
 * wrong one for the single-gate symbols SimulIDE actually draws: the file it
 * would need is a DIP of the gate's own pin count, so the package carries no
 * information the class does not already have, and the DIP fallback in chip.cpp
 * numbers pins down the left side and up the right, which is the datasheet's
 * order and not the order a gate symbol wants (inputs left, output right,
 * controls below).  Placing the pins in code is shorter than placing them from
 * a file and then re-ordering them, and it is what MuxAnalog in active.cpp
 * already does.  Memory and Lm555 do derive from Chip: their pinout genuinely
 * is a datasheet DIP, pin 14 is VCC and that is the whole point of them.
 *
 * There is no multi-bit pin.  ePin in this tree carries m_isBus and m_busWidth
 * but no vector of eNodes - the comment in e-pin.h describes the upstream
 * mechanism, which this port did not reproduce - so a "bus" is N ordinary pins
 * named Data0..DataN-1.  A Bus component is a harness that joins pin i on one
 * side to pin i on the other inside the symbol, which is what a bus is
 * electrically: N independent nets drawn as one thick line.  Upstream's InBus
 * and OutBus are both aliases of it - see itemlibrary.cpp.
 *
 * HOW A LOGIC DEVICE MEETS THE STEP LOOP.  This is the part worth reading
 * before touching anything below, because getting it wrong does not produce a
 * wrong answer so much as a gate that computes once and then stops reacting.
 *
 *   - Input pins declare no admittance.  ePin::isAdmit() stays false, so a net
 *     made only of logic outputs and logic inputs classifies as "single" and
 *     never gets a matrix row.  That is the fast path e-node.h describes and it
 *     is why a sheet of gates runs at all.
 *   - Output pins are stamped with stampSource() against ELOGIC_IMPED.  The
 *     stamp is ignored while the node is single and becomes the real Norton
 *     source the moment a resistor is hung on it.
 *   - stamp() only publishes the state the device already holds.  It is called
 *     on every non-linear pass, and a pass is not a unit of time, so anything
 *     edge-sensitive that ran from stamp() would fire up to noLinAcc times per
 *     step.  A flip-flop clocked from a 1 kHz source on a circuit with diodes
 *     would count five times too fast.
 *   - updateStep() is where the state is computed.  Simulator::settleLogic()
 *     calls it on the elements whose input nodes moved, and only on those, and
 *     repeats until nothing moves (bounded by LOGIC_SETTLE, because a ring
 *     oscillator never settles and should simply advance a few gate delays per
 *     step - which is what it is doing physically anyway).
 *   - nodesUpdated() re-registers the changedFast requests every time the node
 *     graph is rebuilt, and computes once so a freshly loaded sheet shows the
 *     state its inputs imply rather than zeros.  See Component::nodesUpdated()
 *     for why it cannot be done from a constructor.
 *
 * PROPERTY NAMES.  The five on LogicBase are upstream's spellings, awkward
 * capitals and all, because they are attribute names in a .simu file.  A
 * property this port does not declare is simply not looked for - Circuit's
 * reader walks the object's properties and reads the file, not the other way
 * round - so an upstream file carrying Package="dip14" on a gate loses that
 * attribute and nothing else.
 */

#ifndef SIMU_LOGIC_H
#define SIMU_LOGIC_H

#include "chip.h"

// ---- the levels ----------------------------------------------------------------

// What an input reads as a one.  Upstream's default, and half of a 5 V supply:
// a net driven by anything at all is unambiguously above or below it.
#define LOGIC_IN_HIGH 2.5

// What an input reads as a zero.  Below LOGIC_IN_HIGH on purpose: the two
// together are a Schmitt trigger, and a single threshold would make an input
// sitting exactly on it - a net mid-slew, or one left floating at the divider
// point between two resistors - chatter once per step rather than hold.
#define LOGIC_IN_LOW 1.5

#define LOGIC_OUT_HIGH 5.0
#define LOGIC_OUT_LOW  0.0

// What a logic output drives through.  e-source.h's figure, and an order of
// magnitude stiffer than the 1e-3 an ideal source uses: a real 74xx output is
// tens of ohms, and at 10 the symbol still holds a rail up against a kilohm
// pull-up while a heavy load visibly sags it, which is the behaviour that makes
// a pull-up resistor worth drawing.
#define LOGIC_OUT_IMPED 10.0

// What an analog pin on a logic device - an ADC input, a DAC reference - leaks.
// Not zero, and for the reason switches.h gives at length: the pin declares an
// admittance, so its node gets a matrix row, and a row that stamped nothing is
// a row of zeros and a singular matrix.
#define LOGIC_ANALOG_IMPED 1e9

// Caps on the pin counts that are properties.  Sixteen inputs is a 64K-entry
// truth table for Function, which is the real limit rather than an arbitrary
// one; the rest are what a symbol can draw legibly.
#define LOGIC_MAX_IN     8
#define LOGIC_MAX_OUT    8
#define LOGIC_MAX_CHAN   16
#define LOGIC_MAX_BITS   16

// The lattice step, and the spacing between pins on one edge.  Eight, matching
// chip.cpp's CHIP_CELL and the default pin lead length, so neighbouring leads
// cannot overlap and every terminal lands on the grid a wire is routed on.
#define LOGIC_CELL 8

// Half the body width.  Inputs sit at -LOGIC_HALF-8 and outputs at
// +LOGIC_HALF+8, which puts the terminals one cell outside the box.
#define LOGIC_HALF 16

// ---- the base ------------------------------------------------------------------

class LogicBase : public Component
{
    Q_OBJECT

    // Upstream's spellings.  Bare volts and ohms, and deliberately not routed
    // through the unit machinery: a threshold is a level rather than a value
    // with an SI prefix, and Clock::freq() in sources.h is the same case.
    Q_PROPERTY( double Input_High_V  READ inputHighV  WRITE setInputHighV  USER true )
    Q_PROPERTY( double Input_Low_V   READ inputLowV   WRITE setInputLowV   USER true )
    Q_PROPERTY( double Out_High_Volt READ outHighV    WRITE setOutHighV    USER true )
    Q_PROPERTY( double Out_Low_Volt  READ outLowV     WRITE setOutLowV     USER true )
    Q_PROPERTY( double Out_Imped     READ outImped    WRITE setOutImped    USER true )

    // IEC rectangular box with the function written on it, or the distinct
    // shape - D for a buffer, shield for AND, half-moon for OR.  Upstream
    // switches drawing by loading a different .package file; this port has no
    // package file to load, so the property switches how paint() draws instead.
    // Same property, same file attribute, same two pictures.
    Q_PROPERTY( bool Logic_Symbol READ logicSymbol WRITE setLogicSymbol USER true )

    public:
        LogicBase( QObject *parent, const QString &type, const QString &id );

        double inputHighV() const { return m_inHighV; }
        void setInputHighV( double v );

        double inputLowV() const { return m_inLowV; }
        void setInputLowV( double v );

        double outHighV() const { return m_outHighV; }
        void setOutHighV( double v );

        double outLowV() const { return m_outLowV; }
        void setOutLowV( double v );

        double outImped() const { return m_outImped; }
        void setOutImped( double r );

        bool logicSymbol() const { return m_isLS; }
        virtual void setLogicSymbol( bool ls );

        // ---- the step ------------------------------------------------------

        // Publishes the state already computed, and nothing else.  See the
        // header for why the computation is not here.
        virtual void stamp();

        // Computes and publishes.  Called by settleLogic() on an input change.
        virtual void updateStep();

        virtual void nodesUpdated();

        // A logic device holds no charge and integrates nothing, so it is not
        // on the reactive list: updateStep() reaching it from settleLogic() is
        // the whole of its per-step work, and putting it on both lists would
        // run the edge detection twice.
        virtual bool isReactive() const { return false; }

    protected:
        // The computation.  Reads its inputs with readInput(), writes its
        // outputs with writeOutput(), and returns.  Called from updateStep()
        // and once from nodesUpdated().  Not called from stamp(), so an
        // edge-sensitive device may rely on running at most once per input
        // change rather than once per pass.
        virtual void compute() {}

        // ---- pins ----------------------------------------------------------
        // A subclass addPin()s every terminal in whatever order suits its
        // symbol, then calls this once.  It records which indices are inputs
        // and which are outputs, sets their electrical roles, sizes the state
        // vectors and calls Component::initPins().  The body rect is the
        // subclass's own: it knows whether its controls hang below it.
        void setPinRoles( const QVector<int> &inputs, const QVector<int> &outputs );

        // Rebuild helper for the devices whose pin count is a property.  Drops
        // everything, re-registers nothing - the caller follows it with its own
        // addPin() calls, setPinRoles() and setArea(), then publishPins().
        void rebuildPins();

        // The other half of a rebuild: puts the pins just built into the sheet's
        // map, asks the circuit for a fresh node graph, and computes once so the
        // symbol shows the state its inputs imply rather than the state it had
        // before the pins were thrown away.  Does the sheet part only when there
        // is a sheet, which there is not while a constructor is running - a
        // component is added to its circuit after it is built, so the same call
        // serves both the constructor and a live property edit.
        void publishPins();

        Pin *inPin( int i ) const { return pin( m_inIdx.value( i, -1 ) ); }
        Pin *outPin( int i ) const { return pin( m_outIdx.value( i, -1 ) ); }

        int numIn() const { return m_inIdx.size(); }
        int numOut() const { return m_outIdx.size(); }

        // ---- levels --------------------------------------------------------

        // Reads an input with hysteresis.  The previous state is what keeps a
        // level between the two thresholds from chattering - see LOGIC_IN_LOW.
        // An unwired input reads low, which is what it does on a real board
        // through the leakage of whatever is next to it and is a far better
        // default than floating at the last solved value of a deleted node.
        bool readInput( int i );
        bool readPin( Pin *p );

        // Reads an input without hysteresis, for the devices where a threshold
        // is a level rather than a state - an address bus, a BCD code.  A
        // Schmitt trigger on an address would be wrong: the address is not
        // remembered between steps and hysteresis needs something to remember.
        bool readInputHard( int i ) const;

        // Sets the state an output will drive on the next stamp, and puts the
        // level on the pin straight away so a single node picks it up without
        // waiting for the solve.
        void writeOutput( int i, bool state );

        // The level an output pin drives.
        double outLevel( int i ) const { return m_outState.value(i) ? m_outHighV : m_outLowV; }

        // Pushes every output's level onto its pin and asks for a repaint.
        void applyOutputs();

        // ---- painting ------------------------------------------------------
        // Shared by every subclass: the body, and in IEC mode the function
        // string written on it.  A subclass paints its distinct shape on top
        // when m_isLS, or nothing at all when it is happy with the box.
        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

        // The label IEC mode writes on the body: "&" for AND, ">=1" for OR,
        // "=1" for XOR, "1" for a buffer.  Empty means "no box label", which is
        // what the devices that draw their own picture return.
        virtual QString iecLabel() const { return QString(); }

        // Draws the standard body: a rounded rect over m_area, and m_isLS
        // decides whether it is the dark IC colour or the white logic-symbol
        // one.  Returns without drawing the label, which paint() does.
        void paintBody( QPainter *painter );

        // The bubble that says an output is inverted.  Drawn by the subclass at
        // the point its output lead leaves the body.
        void paintBubble( QPainter *painter, double x, double y );

        QVector<int> m_inIdx;
        QVector<int> m_outIdx;

        QVector<bool> m_outState;
        QVector<bool> m_inPrev;

        double m_inHighV;
        double m_inLowV;
        double m_outHighV;
        double m_outLowV;
        double m_outImped;

        bool m_isLS;
};

// Population parity of the low 32 bits.  Declared ahead of the gates because
// XorGate's inline gateFunction() calls it, and a namespace-scope name is not
// visible inside a class body that precedes its declaration - the deferred
// lookup a member function body gets covers the class's own members and
// nothing outside it.  Split out of XorGate at all because Function's parser
// needs the same thing for its '^' operator, and two implementations of a
// parity would eventually disagree.
int quint32BitParity( quint32 v );

// ---- the combinational gates ---------------------------------------------------

// N inputs, one output, no state.  The subclasses differ only in the function
// and in the shape they draw, which is why there is one class and four thin
// ones under it rather than four classes that each repeat the pin building.
class Gate : public LogicBase
{
    Q_OBJECT

    Q_PROPERTY( bool Inverted READ inverted WRITE setInverted USER true )
    Q_PROPERTY( int  Inputs   READ inputs   WRITE setInputs   USER true )

    public:
        Gate( QObject *parent, const QString &type, const QString &id, int inputs );

        bool inverted() const { return m_inverted; }
        void setInverted( bool inv );

        int inputs() const { return m_inputs; }
        void setInputs( int n );

        virtual void setLogicSymbol( bool ls );

    protected:
        // The function of the input bits, bit 0 being input 0.  Inversion is
        // applied by the caller and not here, so a subclass writes the function
        // it is named for and the property turns it into its complement.
        virtual bool gateFunction( quint32 bits ) const = 0;

        virtual void compute();

        // Rebuilds the symbol: m_inputs pins down the left, one on the right.
        void buildGate();

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

        // The distinct-shape outline, drawn about the origin with the output at
        // +LOGIC_HALF.  Called only when m_isLS.
        virtual void paintShape( QPainter *painter ) {}

        int m_inputs;
        bool m_inverted;
};

class Buffer : public Gate
{
    Q_OBJECT
    public:
        Buffer( QObject *parent, const QString &type, const QString &id );

    protected:
        virtual bool gateFunction( quint32 bits ) const { return ( bits & 1 ) != 0; }
        virtual QString iecLabel() const { return QStringLiteral( "1" ); }
        virtual void paintShape( QPainter *painter );
};

class AndGate : public Gate
{
    Q_OBJECT
    public:
        AndGate( QObject *parent, const QString &type, const QString &id );

    protected:
        // Every input set.  Written as a mask comparison rather than a loop so
        // that a gate asked about bits it does not have - Inputs reduced after
        // a file was read - still answers rather than reading past m_inputs.
        virtual bool gateFunction( quint32 bits ) const
            { return bits == ( ( 1u << m_inputs ) - 1u ); }

        virtual QString iecLabel() const { return QStringLiteral( "&" ); }
        virtual void paintShape( QPainter *painter );
};

class OrGate : public Gate
{
    Q_OBJECT
    public:
        OrGate( QObject *parent, const QString &type, const QString &id );

    protected:
        virtual bool gateFunction( quint32 bits ) const { return bits != 0; }
        virtual QString iecLabel() const { return QStringLiteral( "\u2265 1" ); }
        virtual void paintShape( QPainter *painter );
};

class XorGate : public Gate
{
    Q_OBJECT
    public:
        XorGate( QObject *parent, const QString &type, const QString &id );

    protected:
        // Parity, which is what an XOR of more than two inputs means in the
        // IEC symbol and what upstream computes.  A two-input XOR and a
        // four-input one are then the same gate at different widths rather
        // than two different functions.
        virtual bool gateFunction( quint32 bits ) const
            { return ( quint32BitParity( bits ) ) != 0; }

        virtual QString iecLabel() const { return QStringLiteral( "=1" ); }
        virtual void paintShape( QPainter *painter );
};

// ---- the programmable gate -----------------------------------------------------

// Eight inputs, eight outputs, and a truth table per output written as a
// boolean expression.  Upstream evaluates its Functions list on every change
// with an expression engine; here each string is parsed once, on the property
// write, into a 256-entry table, and compute() is eight array lookups.  A
// million steps a second is a million evaluations, and parsing is not something
// to be doing in one.
//
// The grammar is deliberately small:
//
//   expr  := term ( '|' term )*
//   term  := fact ( '&' fact )*
//   fact  := unary ( '^' unary )*
//   unary := '!' unary | primary
//   primary := '(' expr ')' | '0' | '1' | digit | "in" digit
//
// Precedence is the usual one with '!' binding tightest and '|' loosest, so
// "!a & b | c" reads as "((!a) & b) | c".  A single digit names an input, which
// is what makes a table writable in the property panel without a variable
// declaration step; "in3" is accepted for the same input because a bare 3 in a
// longer expression is easy to misread.
class Function : public LogicBase
{
    Q_OBJECT

    // One entry per output, in output order.  Comma-separated in the file,
    // which is what Circuit::applyAttrs() splits a QStringList property on.
    Q_PROPERTY( QStringList Functions READ functions WRITE setFunctions USER true )

    public:
        Function( QObject *parent, const QString &type, const QString &id );

        QStringList functions() const { return m_funcs; }
        void setFunctions( const QStringList &list );

        // What the last parse complained about, empty when it did not.  Shown
        // by the property panel rather than silently falling back to a zero
        // output, because an expression that does not parse is the one thing
        // about this device the user cannot see from the symbol.
        QString parseError() const { return m_error; }

    protected:
        virtual void compute();

        virtual QString iecLabel() const { return QStringLiteral( "f(x)" ); }

    private:
        void buildPins();

        // Parses one expression into table.  Returns false and sets m_error.
        bool compileOne( const QString &src, QVector<bool> &table );

        QStringList m_funcs;

        // One 256-entry truth table per output, indexed by the input byte.
        // Sized LOGIC_MAX_OUT on construction and never resized: the outputs are
        // fixed at eight, and only the number of entries a table has filled in
        // changes.
        QVector<QVector<bool> > m_table;

        QString m_error;
};

// ---- the clocked devices -------------------------------------------------------

// Set, reset, clock, Q and Q-bar.  The subclasses add their data inputs.  A
// flip-flop is edge-triggered, which is what the stamp()/updateStep() split in
// the header exists for: compute() runs once per input change and not once per
// non-linear pass, so the edge is seen exactly once.
class FlipFlop : public LogicBase
{
    Q_OBJECT

    // Whether the Set and Reset pins are live.  Off, the two pins are marked
    // unused and the state can only move on a clock edge - which is what a
    // device in a circuit that has no use for preset inputs wants, and what
    // stops a floating Set from clearing a counter that was just started.
    Q_PROPERTY( bool Set_Reset_Enabled READ setResetEnabled WRITE setSetResetEnabled USER true )

    public:
        FlipFlop( QObject *parent, const QString &type, const QString &id );

        bool setResetEnabled() const { return m_setReset; }
        void setSetResetEnabled( bool en );

        bool state() const { return m_state; }

    protected:
        // Builds the whole symbol: `nData` data pins down the left named
        // datapin0.., Set above the body, Clock and Reset below it, Q and Q-bar
        // down the right.  A subclass calls it and then reads dataIdx() for the
        // pins it cares about - which is why the data pins are built here rather
        // than by the caller, since the caller cannot know the row spacing until
        // the pin count is settled and the count is what it would be passing in.
        void buildFlipFlop( int nData );

        // The pin index of data input `i`.
        int dataIdx( int i ) const { return m_iData.value( i, -1 ); }

        virtual void compute();

        // The data inputs' contribution, evaluated on a rising clock edge.
        // D returns its input; JK returns the toggling function of J and K.
        virtual bool nextState( bool current ) = 0;

        // Rising edge detection on the clock pin, with the previous level kept
        // across calls so a level held high does not re-trigger.
        bool clockEdge();

        virtual QString iecLabel() const { return QString(); }
        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

        bool m_state;
        bool m_clkPrev;
        bool m_setReset;

        // Pin indices, cached because the layout buildFlipFlop() produced is not
        // something compute() should be rediscovering on every call - and because
        // a rising-edge detector that looked its clock pin up by name would be
        // doing a string search a million times a second.
        QVector<int> m_iData;
        int m_iSet, m_iReset, m_iClock, m_iQ, m_iQn;
};

class FlipFlopD : public FlipFlop
{
    Q_OBJECT
    public:
        FlipFlopD( QObject *parent, const QString &type, const QString &id );

    protected:
        virtual bool nextState( bool current );
        virtual QString iecLabel() const { return QStringLiteral( "D" ); }

    private:
        void buildPins();
        int m_iD;
};

class FlipFlopJK : public FlipFlop
{
    Q_OBJECT
    public:
        FlipFlopJK( QObject *parent, const QString &type, const QString &id );

    protected:
        virtual bool nextState( bool current );
        virtual QString iecLabel() const { return QStringLiteral( "JK" ); }

    private:
        void buildPins();
        int m_iJ, m_iK;
};

// A transparent latch rather than an edge-triggered one: while Enable is high
// the outputs follow the data inputs, and when it goes low they hold.  Size
// bits of it in one body, which is what a 74xx75 is.
class LatchD : public LogicBase
{
    Q_OBJECT
    Q_PROPERTY( int Size READ size WRITE setSize USER true )

    public:
        LatchD( QObject *parent, const QString &type, const QString &id );

        int size() const { return m_size; }
        void setSize( int n );

    protected:
        virtual void compute();
        virtual QString iecLabel() const { return QStringLiteral( "EN" ); }

    private:
        void buildPins();

        int m_size;
        int m_iEnable;

        QVector<bool> m_latch;
};

// ---- the counters and arithmetic -----------------------------------------------

class BinCounter : public LogicBase
{
    Q_OBJECT
    Q_PROPERTY( int Bits      READ bits     WRITE setBits     USER true )
    Q_PROPERTY( int Max_Value READ maxValue WRITE setMaxValue USER true )

    public:
        BinCounter( QObject *parent, const QString &type, const QString &id );

        int bits() const { return m_bits; }
        void setBits( int n );

        // Where the count rolls over.  Zero is taken to mean the natural width,
        // 2^Bits, so a counter loaded from a file that does not carry the
        // property counts to its full width rather than to nothing.
        int maxValue() const { return m_max; }
        void setMaxValue( int v );

        int count() const { return m_count; }

    protected:
        virtual void compute();
        virtual QString iecLabel() const { return QStringLiteral( "CTR" ); }

    private:
        void buildPins();

        int m_bits;
        int m_max;
        int m_count;

        bool m_clkPrev;

        int m_iClock, m_iReset, m_iEnable, m_iCarry;
};

class FullAdder : public LogicBase
{
    Q_OBJECT
    public:
        FullAdder( QObject *parent, const QString &type, const QString &id );

    protected:
        virtual void compute();
        virtual QString iecLabel() const { return QStringLiteral( "FA" ); }

    private:
        void buildPins();
};

class ShiftReg : public LogicBase
{
    Q_OBJECT
    Q_PROPERTY( int  Bits           READ bits          WRITE setBits          USER true )
    Q_PROPERTY( bool Input_Lsb_First READ lsbFirst      WRITE setLsbFirst      USER true )

    public:
        ShiftReg( QObject *parent, const QString &type, const QString &id );

        int bits() const { return m_bits; }
        void setBits( int n );

        // Which end a bit enters at.  A daisy-chained pair of shift registers
        // and a seven-segment driver wired to one both exist, and they want
        // opposite ends, so it is a property rather than a decision made here.
        bool lsbFirst() const { return m_lsbFirst; }
        void setLsbFirst( bool lsb );

    protected:
        virtual void compute();
        virtual QString iecLabel() const { return QStringLiteral( "SRG" ); }

    private:
        void buildPins();

        int m_bits;
        quint32 m_reg;

        bool m_clkPrev;
        bool m_lsbFirst;

        int m_iClock, m_iReset, m_iSerIn, m_iSerOut;
};

// ---- the selectors -------------------------------------------------------------

class Mux : public LogicBase
{
    Q_OBJECT
    Q_PROPERTY( int Inputs READ inputs WRITE setInputs USER true )

    public:
        Mux( QObject *parent, const QString &type, const QString &id );

        int inputs() const { return m_channels; }
        void setInputs( int n );

    protected:
        virtual void compute();
        virtual QString iecLabel() const { return QStringLiteral( "MUX" ); }

    private:
        void buildPins();

        int m_channels;
        int m_addrPins;
        int m_iOut;
};

class Demux : public LogicBase
{
    Q_OBJECT
    Q_PROPERTY( int Outputs READ outputs WRITE setOutputs USER true )

    public:
        Demux( QObject *parent, const QString &type, const QString &id );

        int outputs() const { return m_channels; }
        void setOutputs( int n );

    protected:
        virtual void compute();
        virtual QString iecLabel() const { return QStringLiteral( "DMX" ); }

    private:
        void buildPins();

        int m_channels;
        int m_addrPins;
        int m_iIn;
};

// ---- the code converters -------------------------------------------------------

// Four BCD bits in, ten decoded lines out.  A code outside 0..9 drives nothing,
// which is what a 7442 does and what stops a counter mid-rollover lighting two
// segments at once.
class BcdToDec : public LogicBase
{
    Q_OBJECT
    public:
        BcdToDec( QObject *parent, const QString &type, const QString &id );

    protected:
        virtual void compute();
        virtual QString iecLabel() const { return QStringLiteral( "DCD" ); }

    private:
        void buildPins();
};

// The other direction: ten lines in, four bits out.  More than one line high is
// a wiring mistake and the highest-numbered one wins, which is a defined answer
// rather than a latch-up.
class DecToBcd : public LogicBase
{
    Q_OBJECT
    public:
        DecToBcd( QObject *parent, const QString &type, const QString &id );

    protected:
        virtual void compute();
        virtual QString iecLabel() const { return QStringLiteral( "DTS" ); }

    private:
        void buildPins();
};

// Four BCD bits in, seven segment lines out.  Separate from SevenSegmentBCD
// below, which is the display with the decoder inside it: this one is the IC,
// and its outputs go wherever the sheet says.
class BcdTo7S : public LogicBase
{
    Q_OBJECT
    public:
        BcdTo7S( QObject *parent, const QString &type, const QString &id );

        // The segment pattern for a digit, bit 0 = segment a through bit 6 =
        // segment g.  Shared with the display below and with the SevenSegment in
        // outputs.cpp, which is the reason it is here rather than private: three
        // copies of the segment map would eventually disagree about what a 6
        // looks like.
        static quint8 segments( int digit );

    protected:
        virtual void compute();
        virtual QString iecLabel() const { return QStringLiteral( "7S" ); }

    private:
        void buildPins();
};

// The display with the decoder built in: four BCD inputs and a digit drawn on
// the symbol, no outputs at all.  A leaf device, so it stamps nothing and is on
// the element list only to be notified.
class SevenSegmentBCD : public LogicBase
{
    Q_OBJECT
    public:
        SevenSegmentBCD( QObject *parent, const QString &type, const QString &id );

        // Reads its node once per GUI tick rather than per step, exactly as
        // LogicOutput in sources.h does and for the same reason.
        virtual void updateDisplay();

        virtual bool isSimulable() const { return false; }

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    protected:
        // The digit, taken off the four inputs.  It has to be here and not only in
        // updateDisplay(): a display is also on the Simulator's changedFast list,
        // and the call settleLogic() makes reaches updateStep(), which is
        // LogicBase's and calls this.
        virtual void compute();

    private:
        void buildPins();

        quint8 m_seg;
};

// ---- the data converters -------------------------------------------------------

// Analog in, digital out.  The analog pin is the one pin in this file that
// declares an admittance, so the device is in the matrix whether the rest of
// the sheet is or not - and it has to stamp its leakage, or the node it joined
// has a row and nothing in it.
class ADC : public LogicBase
{
    Q_OBJECT
    Q_PROPERTY( int    Size READ size WRITE setSize USER true )
    Q_PROPERTY( double Vref READ vref WRITE setVref USER true )

    public:
        ADC( QObject *parent, const QString &type, const QString &id );

        int size() const { return m_bits; }
        void setSize( int n );

        double vref() const { return m_vref; }
        void setVref( double v );

        virtual void stamp();

    protected:
        virtual void compute();
        virtual QString iecLabel() const { return QStringLiteral( "ADC" ); }

    private:
        void buildPins();

        int m_bits;
        double m_vref;

        bool m_clkPrev;

        int m_iAnalog, m_iVref, m_iClock, m_iEnable;
};

// Digital in, analog out.  The output is a real voltage source behind
// ESOURCE_IMPED rather than a stiff logic level, because what hangs off a DAC
// is a filter and an amplifier and neither of them wants a ten ohm driver.
class DAC : public LogicBase
{
    Q_OBJECT
    Q_PROPERTY( int    Size READ size WRITE setSize USER true )
    Q_PROPERTY( double Vref READ vref WRITE setVref USER true )

    public:
        DAC( QObject *parent, const QString &type, const QString &id );

        int size() const { return m_bits; }
        void setSize( int n );

        double vref() const { return m_vref; }
        void setVref( double v );

        virtual void stamp();

    protected:
        virtual void compute();
        virtual QString iecLabel() const { return QStringLiteral( "DAC" ); }

    private:
        void buildPins();

        int m_bits;
        double m_vref;
        double m_vout;

        int m_iVref, m_iOut, m_iEnable;
};

// ---- the bus harness -----------------------------------------------------------

// Size single-bit pins on the left, Size on the right, and pin i joined to pin
// i+Size inside the symbol.  Electrically N wires; graphically one thick line,
// which is what a bus is.  Not a Chip and not a LogicBase: it holds no state,
// drives nothing and stamps nothing.
class Bus : public Component
{
    Q_OBJECT
    Q_PROPERTY( int Size READ size WRITE setSize USER true )

    public:
        Bus( QObject *parent, const QString &type, const QString &id );

        int size() const { return m_size; }
        void setSize( int n );

        // The joins are the device, and Circuit::updateNodes() unions them
        // exactly as it unions the two ends of a wire - so there is nothing to
        // stamp and no reason to be on the element list.
        virtual bool isSimulable() const { return false; }

        // The width, under the symbol.  Overridden rather than routed through
        // Value/Unit: a bus has no SI-prefixed quantity, and putting one on it
        // would write two more attributes into every .simu that carries a bus for
        // a number the Size attribute already says.
        virtual QString valLabelText() const;

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    private:
        void buildPins();

        int m_size;
};

// ---- the memory ----------------------------------------------------------------

// A RAM.  Chip, and genuinely one: the pinout is a datasheet DIP with pin 14
// VCC and pin 28 something else, the address and data widths are properties,
// and the DIP fallback in chip.cpp lays out exactly what is needed.
//
// The data pins are bidirectional, which is the one electrical subtlety here.
// A pin that is not being driven must present nothing at all - not a weak pull,
// not a leakage - because two memories sharing a data bus is the whole reason a
// bus is tristate, and a device that leaks onto it fights every other one on
// it.  isOutput() is set and cleared per step and isAdmit() is never set, so an
// undriven data net stays single and floats at whatever the last driver left.
class Memory : public Chip
{
    Q_OBJECT
    Q_PROPERTY( int     Addr_Bits   READ addrBits   WRITE setAddrBits   USER true )
    Q_PROPERTY( int     Data_Bits   READ dataBits   WRITE setDataBits   USER true )
    Q_PROPERTY( bool    Persistence READ persistence WRITE setPersistence USER true )

    public:
        Memory( QObject *parent, const QString &type, const QString &id );

        int addrBits() const { return m_addrBits; }
        void setAddrBits( int n );

        int dataBits() const { return m_dataBits; }
        void setDataBits( int n );

        // Whether the contents survive a Stop.  Off, startSim() clears the
        // array, which is what a RAM does when its supply goes away and what
        // makes a counter built from one start from zero.  On, it does not -
        // which is what a circuit that stores a waveform across a run needs.
        bool persistence() const { return m_persist; }
        void setPersistence( bool p ) { m_persist = p; }

        virtual void stamp();
        virtual void updateStep();
        virtual void resetStep();
        virtual void nodesUpdated();

        // The geometry rather than a value, and for the reason Bus gives on its
        // own override: a memory has no SI-prefixed quantity.  "32K x 8" is how a
        // part is named on a datasheet and how a user picks one, and both numbers
        // are already attributes, so routing this through Value/Unit would write
        // two more into every .simu that carries a memory.
        virtual QString valLabelText() const;

        // Reactive rather than changedFast, and the choice is not cosmetic: a
        // write is taken on an edge of WE, and an edge seen once per input change
        // rather than once per step is seen twice when the address and the data
        // arrive in different settle passes.  The reactive list is walked exactly
        // once per step, after the solve, which is the only place in the loop with
        // that property.
        virtual bool isReactive() const { return true; }

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    protected:
        // Rebuilds the pinout after a width change, and after Chip::initChip()
        // has run from setPackage().  Chip declares initChip() virtual for
        // exactly this.
        virtual bool initChip();

    private:
        void assignRoles();

        QVector<quint8> m_mem;

        int m_addrBits;
        int m_dataBits;

        bool m_persist;
        bool m_writePrev;

        // The address the last write used, kept so a read-back in the same step
        // returns what was just written rather than what was there before it.
        int m_lastAddr;

        int m_iCs, m_iWe, m_iOe;
        int m_addrBase, m_dataBase;
};

// ---- the I2C devices -----------------------------------------------------------

// Both of these are I2C slaves on a two-wire bus, and both therefore need the
// same state machine: START and STOP conditions detected as SDA moving while SCL
// is high, nine-bit frames with the ninth bit an acknowledge the slave drives,
// and an address byte matched against a control code.  The machine is here once
// and the two devices differ in what they do with a byte.
//
// SDA is bidirectional and open-drain, which is not a detail: a slave can only
// ever pull the line low, and two of them pulling it in opposite directions is
// how a real bus gets stuck.  So the pin is driven low or left floating, never
// driven high, and the pull-up is a resistor the user draws.
class I2CBase : public LogicBase
{
    Q_OBJECT
    Q_PROPERTY( int Control_Code READ controlCode WRITE setControlCode USER true )

    public:
        I2CBase( QObject *parent, const QString &type, const QString &id );

        int controlCode() const { return m_code; }
        void setControlCode( int c );

        virtual void stamp();

    protected:
        // Runs the bus state machine once.  Called from compute(), which is
        // once per input change and so once per edge rather than once per
        // non-linear pass - a bit clocked nine times would be a byte and a
        // third.
        void runBus();

        // The step hook.  I2CBase's is the machine and nothing else: a slave with
        // no parallel side - I2CRam - has no outputs to publish, so there is
        // nothing for a subclass to add unless it has some.  I2CToParallel does,
        // and overrides this to follow it with its port.
        virtual void compute();

        // What the slave does with a received byte.  Returns the bit to
        // acknowledge with, which is low for "accepted" and high for "not".
        virtual bool onByte( quint8 byte, bool isAddr ) = 0;

        // What it puts on the bus when the master reads.  Returning false
        // leaves SDA floating, which is a NACK on a read.
        virtual bool driveRead( quint8 &byte ) = 0;

        // Loads the next byte to transmit and puts its most significant bit on
        // SDA, already shifted out of m_shift so that the mid-frame edge drives
        // the next one rather than the same one twice.
        void loadTxByte();

        int m_iScl, m_iSda;

        int m_code;

        // The machine's state.  Kept small and explicit rather than as an enum
        // of many names: an I2C slave is a shift register, a bit counter and a
        // flag saying whether it is being read or written.
        bool m_sclPrev, m_sdaPrev;
        bool m_active;          // between START and STOP
        bool m_reading;         // the master is reading rather than writing
        bool m_addressed;       // this slave's code has been matched
        bool m_ackPending;      // the ninth bit of the current frame
        bool m_sdaLow;          // what this slave is pulling SDA to

        // Whether the eight data bits of the frame in progress are ours to drive.
        // It cannot be derived from m_reading, which describes the transaction
        // rather than the frame: an address byte is always received by the slave
        // even in a read, and the frame that carries it has to know that.
        bool m_txFrame;

        int m_bitCount;
        quint16 m_shift;

        // Address bytes seen, for the devices that take a two-byte address.
        quint8 m_addrBytes[2];
        int m_addrCount;
};

class I2CRam : public I2CBase
{
    Q_OBJECT
    Q_PROPERTY( int Size READ size WRITE setSize USER true )

    public:
        I2CRam( QObject *parent, const QString &type, const QString &id );

        int size() const { return m_size; }
        void setSize( int n );

    protected:
        virtual bool onByte( quint8 byte, bool isAddr );
        virtual bool driveRead( quint8 &byte );
        virtual QString iecLabel() const { return QStringLiteral( "I2C" ); }

    private:
        void buildPins();

        QVector<quint8> m_mem;

        int m_size;
        int m_ptr;
};

class I2CToParallel : public I2CBase
{
    Q_OBJECT
    public:
        I2CToParallel( QObject *parent, const QString &type, const QString &id );

    protected:
        // The bus machine and then the port: the byte runBus() just handed to
        // onByte() is what the eight pins have to show, and doing it here rather
        // than inside onByte() keeps the write to m_port on the same call path as
        // the repaint that follows it.
        virtual void compute();

        virtual bool onByte( quint8 byte, bool isAddr );
        virtual bool driveRead( quint8 &byte );
        virtual QString iecLabel() const { return QStringLiteral( "I2C" ); }

    private:
        void buildPins();

        quint8 m_port;
        int m_iReset;
};

// ---- the 555 -------------------------------------------------------------------

// The timer.  Chip, because pin 1 is ground and pin 8 is VCC and a 555 that does
// not look like a 555 is not worth drawing.
//
// This is the one device in the file that is properly analog: two comparators
// against a three-resistor divider, an SR latch, a discharge transistor.  It is
// not non-linear in the Newton sense - the latch is digital and the comparators
// are just thresholds - so isNonLinear() stays false and the conductance change
// when the discharge transistor switches is picked up by the matrix's own
// "did the admittance move" test rather than by an extra pass.
class Lm555 : public Chip
{
    Q_OBJECT
    public:
        Lm555( QObject *parent, const QString &type, const QString &id );

        virtual void stamp();
        virtual void updateStep();
        virtual void nodesUpdated();

        // Reactive for the same reason Memory is: the SR latch is level-driven
        // and could live in stamp(), but the discharge transistor's conductance
        // has to move once per step and not once per non-linear pass, or a
        // circuit with a diode in it discharges a timing capacitor five times per
        // step and the frequency the sheet produces depends on what else is on
        // it.
        virtual bool isReactive() const { return true; }

    protected:
        virtual bool initChip();

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    private:
        void assignRoles();

        // The SR latch, and the two comparator thresholds derived from VCC and
        // the control pin.  Kept as state rather than recomputed because the
        // latch is what makes the 555 a timer rather than a comparator with
        // hysteresis: the output holds between the two thresholds.
        bool m_latch;
        bool m_threshPrev;

        int m_iGnd, m_iTrig, m_iOut, m_iReset, m_iCtrl, m_iThres, m_iDisch, m_iVcc;
};

// ---- factories -----------------------------------------------------------------

Component* createBuffer(          QObject *parent, const QString &type, const QString &id );
Component* createAndGate(         QObject *parent, const QString &type, const QString &id );
Component* createOrGate(          QObject *parent, const QString &type, const QString &id );
Component* createXorGate(         QObject *parent, const QString &type, const QString &id );
Component* createFunction(        QObject *parent, const QString &type, const QString &id );
Component* createFlipFlopD(       QObject *parent, const QString &type, const QString &id );
Component* createFlipFlopJK(      QObject *parent, const QString &type, const QString &id );
Component* createLatchD(          QObject *parent, const QString &type, const QString &id );
Component* createBinCounter(      QObject *parent, const QString &type, const QString &id );
Component* createFullAdder(       QObject *parent, const QString &type, const QString &id );
Component* createShiftReg(        QObject *parent, const QString &type, const QString &id );
Component* createMux(             QObject *parent, const QString &type, const QString &id );
Component* createDemux(           QObject *parent, const QString &type, const QString &id );
Component* createBcdToDec(        QObject *parent, const QString &type, const QString &id );
Component* createDecToBcd(        QObject *parent, const QString &type, const QString &id );
Component* createBcdTo7S(         QObject *parent, const QString &type, const QString &id );
Component* createSevenSegmentBCD( QObject *parent, const QString &type, const QString &id );
Component* createADC(             QObject *parent, const QString &type, const QString &id );
Component* createDAC(             QObject *parent, const QString &type, const QString &id );
Component* createBus(             QObject *parent, const QString &type, const QString &id );
Component* createMemory(          QObject *parent, const QString &type, const QString &id );
Component* createI2CRam(          QObject *parent, const QString &type, const QString &id );
Component* createI2CToParallel(   QObject *parent, const QString &type, const QString &id );
Component* createLm555(           QObject *parent, const QString &type, const QString &id );

#endif // SIMU_LOGIC_H
