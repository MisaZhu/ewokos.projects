/*
 * SimulIDE, ported to EwokOS - see itemlibrary.h.
 */

#include "itemlibrary.h"

// ---- the factory functions ----------------------------------------------------
//
// One per component, declared here and defined in the component's own file.  The
// list below is the only place a new component has to be added, and that is
// deliberate: this tree is built -fno-use-cxa-atexit against a statically linked
// Qt, so a file-scope object that registered itself in its constructor would never
// run.  An explicit table also makes it possible to see the whole catalogue in one
// screen rather than by grepping forty files.

Component* createNode( QObject *parent, const QString &type, const QString &id );

Component* createPotentiometer( QObject *parent, const QString &type, const QString &id );
Component* createResistor(      QObject *parent, const QString &type, const QString &id );
Component* createResistorDip(   QObject *parent, const QString &type, const QString &id );
Component* createCapacitor(     QObject *parent, const QString &type, const QString &id );
Component* createElCapacitor(   QObject *parent, const QString &type, const QString &id );

Component* createLogicInput(    QObject *parent, const QString &type, const QString &id );
Component* createClock(         QObject *parent, const QString &type, const QString &id );
Component* createWaveGen(       QObject *parent, const QString &type, const QString &id );
Component* createVoltSource(    QObject *parent, const QString &type, const QString &id );
Component* createCurrSource(    QObject *parent, const QString &type, const QString &id );
Component* createFixedVoltage(  QObject *parent, const QString &type, const QString &id );
Component* createGround(        QObject *parent, const QString &type, const QString &id );
Component* createLogicOutput(   QObject *parent, const QString &type, const QString &id );

Component* createPush(          QObject *parent, const QString &type, const QString &id );
Component* createSwitch(        QObject *parent, const QString &type, const QString &id );
Component* createSwitchDip(     QObject *parent, const QString &type, const QString &id );
Component* createRelaySPST(     QObject *parent, const QString &type, const QString &id );

Component* createDiode(         QObject *parent, const QString &type, const QString &id );
Component* createInductor(      QObject *parent, const QString &type, const QString &id );
Component* createVoltReg(       QObject *parent, const QString &type, const QString &id );
Component* createOpAmp(         QObject *parent, const QString &type, const QString &id );
Component* createMosfet(        QObject *parent, const QString &type, const QString &id );
Component* createBJT(           QObject *parent, const QString &type, const QString &id );
Component* createMuxAnalog(     QObject *parent, const QString &type, const QString &id );

namespace {

struct RegEntry
{
    const char *type;
    const char *category;
    const char *help;
    createItemPtr fn;
};

// The order within a category is the order the component selector shows, and the
// order of first appearance of a category is the order of its tab.  Both follow
// upstream's ItemLibrary::loadItems(), so that a sheet laid out against its
// selector can be rebuilt against this one without hunting.
//
// Upstream's full order, for reference as the rest is ported - the category each
// line belongs to is the comment above it there, and the entries are listed here
// as they are added:
//
//   Meters      Probe Voltmeter Amperimeter Frequencimeter Oscope
//   Sources     LogicInput Clock WaveGen VoltSource CurrSource Rail Ground
//   Switches    Push Switch SwitchDip RelaySPST
//   Passive     Potentiometer Resistor ResistorDip Capacitor elCapacitor Inductor
//   Active      Diode VoltReg OpAmp Mosfet BJT MuxAnalog
//   Outputs     Led LedBar LedMatrix SevenSegment KeyPad Hd44780 Pcd8544 Ks0108
//               Stepper Servo AudioOut
//   Micro       PICComponent AVRComponent Arduino SR04
//   Logic       Buffer AndGate OrGate XorGate Function FlipFlopD FlipFlopJK
//               BinCounter FullAdder LatchD ShiftReg Mux Demux BcdToDec DecToBcd
//               BcdTo7S ADC DAC Bus SevenSegmentBCD Memory I2CRam I2CToParallel
//               Lm555
//   Subcircuits SubCircuit
//   Other       TextComponent Rectangle Ellipse Line Node SubPackage
//
// Note that the category a component registers under is not always the directory
// upstream keeps it in: Inductor sits in active/ and registers as Passive.  The
// file it lands in here follows the directory, the string below follows the tab.
const RegEntry kItems[] =
{
    { "LogicInput", "Sources",
      "Logic input: a manually set high or low.\n"
      "Click the symbol to toggle it.  Voltage is the high level; when Out is off\n"
      "the pin is driven to 0 V through the same impedance.",
      createLogicInput },

    { "Clock", "Sources",
      "Clock: a square wave at Freq hertz.\n"
      "Always_On runs it whether the simulation is started or not; Out enables it\n"
      "while running.  The duty cycle is 50%.",
      createClock },

    { "WaveGen", "Sources",
      "Wave generator: sine, triangle, saw, ramp or square at Freq hertz.\n"
      "The waveform swings around Volt_Base by plus and minus Voltage, and is\n"
      "built into a Quality-sample lookup table so a million steps a second do\n"
      "not cost a million sine evaluations.",
      createWaveGen },

    { "VoltSource", "Sources",
      "Voltage source: an EMF between two terminals.\n"
      "The positive terminal is the one on the left.  Internally an ideal source,\n"
      "so it will supply whatever current the circuit asks for.",
      createVoltSource },

    { "CurrSource", "Sources",
      "Current source: drives Current from the left terminal to the right.\n"
      "Ideally stiff, so an open circuit on either side leaves the voltage to be\n"
      "decided by whatever else is on that node.",
      createCurrSource },

    { "Fixed Voltage", "Sources",
      "Fixed voltage / rail: drives Voltage on one terminal.\n"
      "Older files name this part Rail; the alias table folds the two together.",
      createFixedVoltage },

    { "Ground", "Sources",
      "Ground: ties a node to the sheet's reference.\n"
      "Every circuit needs at least one, or the matrix has no reference and does\n"
      "not solve.  Older files name this part \"Ground (0 V)\".",
      createGround },

    { "LogicOutput", "Sources",
      "Logic output: an indicator that reads its node.\n"
      "It presents no load and stamps nothing, so it can be hung on any signal\n"
      "without changing what it is measuring.",
      createLogicOutput },

    { "Push", "Switches",
      "Push button: momentary.\n"
      "Pressing throws every pole, releasing restores Norm_Close.  A Key makes it\n"
      "latching instead, because the sheet sees key presses but not releases.",
      createPush },

    { "Switch", "Switches",
      "Switch: Poles single- or double-throw contacts.\n"
      "Click the symbol or press Key to throw it.  An open contact is not an\n"
      "infinite resistance but a very large one, so a node made only of open\n"
      "switches still has a solvable matrix row.",
      createSwitch },

    { "SwitchDip", "Switches",
      "DIP switch: Size independent single-pole contacts in one body.\n"
      "Click an individual segment to close or open it.  Resizing keeps the wires\n"
      "on the segments that survive.",
      createSwitchDip },

    { "RelaySPST", "Switches",
      "Relay: a coil that throws Poles contacts.\n"
      "The contacts close above IOn amps of coil current and open again below\n"
      "IOff, the gap being the hysteresis a real relay has - a single threshold\n"
      "would make a coil sitting exactly on it chatter once per step.",
      createRelaySPST },

    { "Potentiometer", "Passive",
      "Potentiometer: three-terminal adjustable resistor.\n"
      "The track between A and B is the Resistance; the wiper M taps part of it,\n"
      "set in ohms by Value_Ohm or from the right-click menu.  A pot with its\n"
      "wiper left unwired is still a resistor of the full track value.",
      createPotentiometer },

    { "Resistor", "Passive",
      "Resistor: a fixed resistance between two terminals.\n"
      "Resistance is the number shown next to Unit, so 100 with \" k\" is 100 kohm.\n"
      "The value is read by the solver on every pass, so it can be changed while\n"
      "the simulation is running.",
      createResistor },

    { "ResistorDip", "Passive",
      "Resistor pack: Size independent resistors in one body, as in a physical\n"
      "SIP or DIP network.  All segments share one Resistance.  Resizing keeps\n"
      "the wires on the segments that survive.",
      createResistorDip },

    { "Capacitor", "Passive",
      "Capacitor: non-polarised, two parallel plates.\n"
      "Solved by backward Euler, so it appears to the matrix as a conductance of\n"
      "C/dt in parallel with a current source carrying the previous step's\n"
      "voltage.  The timestep comes from the simulation rate.",
      createCapacitor },

    { "elCapacitor", "Passive",
      "Electrolytic capacitor: polarised.\n"
      "Electrically identical to a Capacitor, but it draws red when the voltage\n"
      "across it has been reversed for more than a few steps, which is how a real\n"
      "one fails.",
      createElCapacitor },

    { "Diode", "Active",
      "Diode: conducts from the left terminal to the right.\n"
      "Solved by Newton iteration rather than by a switch, so it conducts a\n"
      "small reverse current and its forward drop settles where the exponential\n"
      "and the rest of the circuit agree.  Forward_Volt is the drop at which it\n"
      "is taken to be fully on.",
      createDiode },

    { "Inductor", "Passive",
      "Inductor: opposes a change of current.\n"
      "Solved by backward Euler, so it appears to the matrix as a conductance of\n"
      "dt/L in parallel with a current source carrying the previous step's\n"
      "current.  At dc it settles to its Resistance.",
      createInductor },

    { "VoltReg", "Active",
      "Voltage regulator: holds Voltage between output and ground.\n"
      "Drawn as the three-terminal TO-220 it usually is - input on the left,\n"
      "ground below, output on the right - and behaves as an ideal source until\n"
      "the input drops below the output.",
      createVoltReg },

    { "OpAmp", "Active",
      "Operational amplifier: amplifies the difference between its inputs.\n"
      "The gain is set high enough that with feedback the two inputs are at the\n"
      "same voltage, which is how every textbook circuit around one works.",
      createOpAmp },

    { "Mosfet", "Active",
      "MOSFET: a voltage-controlled channel.\n"
      "P_Channel inverts the sense of the gate threshold.  The model is the\n"
      "square-law one, with RDSon as the fully-enhanced channel resistance.",
      createMosfet },

    { "BJT", "Active",
      "Bipolar transistor: a current-controlled junction.\n"
      "PNP inverts both the junction polarities.  Gain is the common-emitter\n"
      "current gain, and the base-emitter drop is the silicon one.",
      createBJT },

    { "MuxAnalog", "Active",
      "Analog multiplexer: routes one of Channels inputs to the common.\n"
      "The selected channel is a low resistance and every other channel an open\n"
      "circuit, which is what makes it usable as a sample-and-hold switch as\n"
      "well as a signal router.",
      createMuxAnalog },

    { "Node", "Other",
      "Junction: joins three wires into one node.\n"
      "A junction with fewer than three wires dissolves itself, splicing the two\n"
      "remaining wires together, so it is never left standing as a dot that joins\n"
      "nothing.",
      createNode },
};

// Names older releases of SimulIDE wrote for a type that has since been renamed,
// and names upstream's own loader folds together.  Both halves of the examples
// folder have to load.
const char *const kAliases[][2] =
{
    { "Rail",          "Fixed Voltage" },
    { "Ground (0 V)",  "Ground"        },
    { "InBus",         "Bus"           },
    { "OutBus",        "Bus"           },
    { "Ram8bit",       "Memory"        },
};

} // namespace

// ---- the library --------------------------------------------------------------

ItemLibrary::ItemLibrary()
{
    const int n = int( sizeof(kItems)/sizeof(kItems[0]) );
    for( int i = 0; i < n; i++ )
        addItem( QString::fromUtf8( kItems[i].type ),
                 QString::fromUtf8( kItems[i].category ),
                 QString::fromUtf8( kItems[i].help ),
                 kItems[i].fn );

    const int a = int( sizeof(kAliases)/sizeof(kAliases[0]) );
    for( int i = 0; i < a; i++ )
        addAlias( QString::fromUtf8( kAliases[i][0] ),
                  QString::fromUtf8( kAliases[i][1] ) );
}

ItemLibrary* ItemLibrary::self()
{
    // A function-local static is initialised on first call, which is the one form
    // of static initialisation this runtime does perform - and there is no
    // teardown to worry about, since nothing here owns a file or a socket.
    static ItemLibrary lib;
    return &lib;
}

void ItemLibrary::addItem( const QString &type, const QString &category,
                           const QString &help, createItemPtr fn )
{
    LibraryItem *item = m_map.value( type, 0l );

    if( !item )
    {
        item = new LibraryItem;
        item->type = type;
        m_map.insert( type, item );
        m_items.append( item );
    }

    item->category = category;
    item->help     = help;
    item->createFn = fn;
}

void ItemLibrary::addAlias( const QString &from, const QString &to )
{
    m_alias.insert( from, to );
}

QString ItemLibrary::resolve( const QString &type ) const
{
    return m_alias.value( type, type );
}

LibraryItem* ItemLibrary::libraryItem( const QString &type )
{
    return m_map.value( resolve( type ), 0l );
}

createItemPtr ItemLibrary::createFn( const QString &type )
{
    LibraryItem *item = libraryItem( type );
    return item ? item->createFn : 0l;
}

QStringList ItemLibrary::categories() const
{
    QStringList cats;

    for( const LibraryItem *item : m_items )
        if( !cats.contains( item->category ) ) cats.append( item->category );

    return cats;
}

QList<LibraryItem*> ItemLibrary::itemsIn( const QString &category ) const
{
    QList<LibraryItem*> list;

    for( LibraryItem *item : m_items )
        if( item->category == category ) list.append( item );

    return list;
}
