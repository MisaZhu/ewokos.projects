/*
 * SimulIDE, ported to EwokOS - the passive components.
 *
 * Upstream: src/gui/circuitwidget/components/passive/.  Upstream's directory has
 * six files and this has one, because the split upstream makes is the one this
 * port deliberately undoes: it separates each symbol from its element (Resistor
 * owns an eResistor, Capacitor an eCapacitor) and so needs a header per class
 * pair.  Here the component is the element, and the whole category is small
 * enough to read at once, which matters more than the file count - a reader
 * comparing a Capacitor to an elCapacitor should not have to hold four files
 * open to see that they differ in one flag and four lines of paint.
 *
 * Note that upstream's Inductor and Diode are NOT here: they live under its
 * active/, and so they will live under active.h in this port.  What follows is
 * exactly upstream's passive/.
 *
 * Property names are upstream's, and so is their order.  Both are part of the
 * .simu format - Circuit reflects over the meta-object's property list to write
 * the attributes and reads them back the same way - and the order carries
 * meaning for Potentiometer, whose Value_Ohm is an ohmage that has to be divided
 * by a total resistance that only Resistance can supply.
 */

#ifndef SIMU_PASSIVE_H
#define SIMU_PASSIVE_H

#include "twopin.h"

// Upstream floors a capacitance at the same 1e-12 it floors a resistance at, and
// for the same reason: both end up as the argument of a division.  Spelled out
// separately rather than reusing RES_FLOOR so a reader does not have to work out
// whether a resistance floor is meant to apply to farads.
#define CAP_FLOOR 1e-12

// ---------------------------------------------------------------------------
// Resistor
// ---------------------------------------------------------------------------

class Resistor : public TwoPin
{
    Q_OBJECT
    Q_PROPERTY( double  Resistance READ resist  WRITE setResist  USER true )
    Q_PROPERTY( QString Unit       READ unit    WRITE setUnit    USER true )
    Q_PROPERTY( bool    Show_res   READ showVal WRITE setShowVal USER true )

    public:
        Resistor( QObject *parent, const QString &type, const QString &id );

        void paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                    QWidget *widget );

        // The number the panel shows and the file stores, in whatever unit is
        // standing next to it - not ohms.  ohms() is ohms.  Upstream's Resistor
        // could call this resist() because its element half lived in another
        // class; here both are in one and the two have to be distinguishable.
        double resist() const { return m_value; }

        void setResist( double r )
            { if( r < RES_FLOOR ) r = RES_FLOOR; setDisplayValue( r ); }
};

// ---------------------------------------------------------------------------
// CapacitorBase
// ---------------------------------------------------------------------------
//
// The backward-Euler companion model, which is what makes a capacitor solvable
// by the same linear routine that solves a resistor: at each step the element is
// a conductance G = C/dt between its terminals, in parallel with a current
// source Is = G*v_prev that carries the memory of where the voltage was.  The
// branch then satisfies i = G*v - Is = C*(v - v_prev)/dt, which is the capacitor
// equation with the derivative replaced by its backward difference.
//
// Two consequences worth stating, because both look like omissions:
//
//   G is recomputed only when C or dt changes, not per step.  solveAnalog()
//   calls stamp() once per pass, and a value that does not move must not be
//   rewritten or eNode::stampAdmitance()'s tolerance guard is defeated and the
//   LU is refactored every step.
//
//   The current source is stamped from m_volt, which updateStep() fills AFTER
//   the solve.  So the source in step n is built from the voltage of step n-1 -
//   which is exactly what backward Euler asks for, and not a lag.  Upstream gets
//   the same result the other way round, by stamping from inside updateStep();
//   it can, because its matrix is not zeroed between the stamp and the solve.
//   This one is, so a stamp made in updateStep() would never be read.

class CapacitorBase : public TwoPin
{
    Q_OBJECT
    Q_PROPERTY( double  Capacitance READ capac   WRITE setCapac   USER true )
    Q_PROPERTY( QString Unit        READ unit    WRITE setUnit    USER true )
    Q_PROPERTY( bool    Show_Cap    READ showVal WRITE setShowVal USER true )

    public:
        CapacitorBase( QObject *parent, const QString &type, const QString &id );

        double capac() const { return m_value; }
        void setCapac( double c );

        double farads() const { return m_cap; }

        virtual void stamp();
        virtual void updateStep();
        virtual void resetStep();
        virtual void setUnit( const QString &un );

        virtual bool isReactive() const { return true; }

    protected:
        // The electrical setter, in farads.  Kept apart from setCapac() for the
        // reason TwoPin keeps ohms() apart from resist(): this one is what the
        // companion conductance is computed from, and it has to be reachable
        // without going through the display unit.
        void setFarads( double c );

        double m_cap;      // farads
        double m_volt;     // v(lPin) - v(rPin) as of the end of the last step
};

// ---------------------------------------------------------------------------
// Capacitor
// ---------------------------------------------------------------------------

class Capacitor : public CapacitorBase
{
    Q_OBJECT

    public:
        Capacitor( QObject *parent, const QString &type, const QString &id );

        void paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                    QWidget *widget );
};

// ---------------------------------------------------------------------------
// elCapacitor
// ---------------------------------------------------------------------------
//
// The class name is lowerCamel because the type string upstream writes into a
// .simu is "elCapacitor", and a load that has to translate between the two is a
// load that can get it wrong.  Everything else in this tree is named for its
// type string too; this is the one case where the type string is not Capitalised.

class elCapacitor : public CapacitorBase
{
    Q_OBJECT

    public:
        elCapacitor( QObject *parent, const QString &type, const QString &id );

        void paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                    QWidget *widget );

        virtual void updateStep();
        virtual void resetStep();

    protected:
        // Set, not toggled.  Upstream flips the flag every fifth reversed step,
        // which at the default 1 MHz simuRate is a 200 kHz blink - far past
        // anything a display can show, so what it actually produces is aliasing
        // against the repaint rate rather than an indicator.  Debouncing the
        // detection and latching the result gives the thing the blink was
        // presumably reaching for: red while the part is backwards, and not
        // while it is not.
        bool m_reversed;
        int  m_counter;
};

// ---------------------------------------------------------------------------
// ResistorDip
// ---------------------------------------------------------------------------
//
// Not a TwoPin: it is m_size of them, each independently stampable and each with
// its own pair of terminals.  All segments share one resistance, which is what a
// physical DIP pack does and what upstream's single Value applies to.

class ResistorDip : public Component
{
    Q_OBJECT
    Q_PROPERTY( int     Size       READ size    WRITE setSize    USER true )
    Q_PROPERTY( double  Resistance READ resist  WRITE setResist  USER true )
    Q_PROPERTY( QString Unit       READ unit    WRITE setUnit    USER true )
    Q_PROPERTY( bool    Show_res   READ showVal WRITE setShowVal USER true )

    public:
        ResistorDip( QObject *parent, const QString &type, const QString &id );

        void paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                    QWidget *widget );

        virtual void stamp();
        virtual void setUnit( const QString &un );

        int size() const { return m_size; }
        void setSize( int s );

        double resist() const { return m_value; }
        void setResist( double r );

    private:
        // Appends `count` segments' worth of pins and puts them in the sheet's
        // pin map.  The map is the part that is easy to miss: Component::addPin
        // cannot do it, because a component's constructor runs before it is on a
        // sheet, and Circuit::addComponent does it once for the pins that exist
        // at that moment.  A segment added later by a Size edit is in neither
        // place, so a connector in a file naming it resolves to nothing.
        void addSegments( int count );

        void applyOhms();

        int m_size;

        double m_resist;
        double m_admit;
};

// ---------------------------------------------------------------------------
// Potentiometer
// ---------------------------------------------------------------------------
//
// Three terminals and two conductances, which is why this is not a TwoPin either.
//
// Upstream reaches the same circuit with a private eNode and two eResistors it
// owns, so that the wiper has a node to hang off even when nothing is wired to
// it.  This port does not need one: the wiper pin is a real pin and gets an
// eNode from Circuit::updateNodes() like any other, and the unwired case - the
// only one upstream's private node was for - is handled in stamp() by falling
// back to the whole track between A and B.  That is one fewer node in the matrix
// and one fewer thing to keep in step, and it is the same circuit either way.

class Potentiometer : public Component
{
    Q_OBJECT
    Q_PROPERTY( double  Resistance READ resist      WRITE setResist    USER true )
    Q_PROPERTY( QString Unit       READ unit        WRITE setUnit      USER true )
    Q_PROPERTY( bool    Show_res   READ showVal     WRITE setShowVal   USER true )
    // Last, and it has to be: this is an ohmage across part of the track, so
    // reading it back means dividing by a total that only Resistance provides,
    // and Circuit applies attributes in the order the file carries them.
    Q_PROPERTY( int     Value_Ohm  READ wiperOhms   WRITE setWiperOhms USER true )

    public:
        Potentiometer( QObject *parent, const QString &type, const QString &id );

        void paint( QPainter *painter, const QStyleOptionGraphicsItem *option,
                    QWidget *widget );

        virtual void stamp();
        virtual void setUnit( const QString &un );

        // Pin indices, named because stamp() reads better with them than with
        // the numbers and because "M" is upstream's name for the wiper.
        Pin *pinA() const { return pin( 0 ); }
        Pin *pinM() const { return pin( 1 ); }
        Pin *pinB() const { return pin( 2 ); }

        double resist() const { return m_value; }
        void setResist( double r );

        // The wiper as a position in 0..1000 rather than as an ohmage.  Storing
        // the position is what makes it survive a change of total resistance:
        // a pot set to its midpoint stays at its midpoint when 1k becomes 10k,
        // which is the physical thing and also what upstream's dial does.  The
        // file stores ohms because that is what upstream stores, and the two are
        // converted at the property boundary.
        int wiperPos() const { return m_pos; }
        void setWiperPos( int p );

        int  wiperOhms() const;
        void setWiperOhms( int ohms );

    protected:
        // Adds the wiper entry ahead of the standard ones.  Upstream puts a QDial
        // on the symbol in a QGraphicsProxyWidget instead; that is not done here
        // because the proxy would have to be composed against this item's own
        // transform - rotation, mirror and the view's scale - to be usable at the
        // 24 units across this symbol is drawn, and it competes for mouse presses
        // with the drag that moves the component.  Value_Ohm is in the property
        // panel either way, so this is a shortcut and not the only route.
        virtual void contextMenu( QGraphicsSceneContextMenuEvent *event,
                                  QMenu *menu );

    private slots:
        void slotSetWiper();

    private:
        void applySplit();

        int m_pos;             // 0..1000, A to B

        double m_resist;       // total track, ohms
        double m_admitA;       // A to wiper
        double m_admitB;       // wiper to B
};

// ---- factories --------------------------------------------------------------

Component* createResistor(      QObject *parent, const QString &type, const QString &id );
Component* createCapacitor(     QObject *parent, const QString &type, const QString &id );
Component* createElCapacitor(   QObject *parent, const QString &type, const QString &id );
Component* createResistorDip(   QObject *parent, const QString &type, const QString &id );
Component* createPotentiometer( QObject *parent, const QString &type, const QString &id );

#endif // SIMU_PASSIVE_H
