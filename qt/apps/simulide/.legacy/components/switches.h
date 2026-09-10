/*
 * SimulIDE, ported to EwokOS - the switches.
 *
 * Upstream: src/gui/circuitwidget/components/switches/, one file per class.
 *
 * None of these derives from TwoPin, and that is the reason stampConductance()
 * is a free function rather than a TwoPin member (see twopin.h).  A switch is a
 * conductance between two pins that happens to be switchable, but a relay has a
 * coil that is one pair and two or more contacts that are others, and a DIP
 * switch is eight independent pairs in one body - none of which is "a two
 * terminal part", and all of which want the same stamp.
 *
 * AN OPEN SWITCH STAMPS 1e-12 S, NOT NOTHING.  This is the one place where the
 * obvious implementation is wrong, and it is wrong in a way that reports itself
 * as a solver failure rather than as a wrong answer.  A switch's pins declare
 * isAdmit(), because they do present a finite admittance, so a node carrying
 * only open switches is not classified single and gets a matrix row - and a row
 * that stamped nothing is a row of zeros, which makes the matrix singular.  Two
 * open switches in series would therefore stop the whole simulation.  1e12 ohm
 * passes 5 pA at 5 V, which is below anything the sheet can display, and keeps
 * every row populated.
 *
 * The Key property is wired: CircuitView offers each unmodified keystroke to
 * every component and Switch::keyPressed() claims its own, which is what makes a
 * keyboard-driven circuit possible.  Upstream does the same through its
 * KeyPress event filter.
 */

#ifndef SIMU_SWITCHES_H
#define SIMU_SWITCHES_H

#include "component.h"

// A closed contact.  0.001 ohm, matching the internal impedance e-source.h
// chose for an ideal source, so a switch in series with a rail loses the same
// fraction of the voltage a wire would.
#define SW_CLOSED_ADMIT 1e3

// An open contact, and the reason for it is in the header comment above.
#define SW_OPEN_ADMIT 1e-12

class Switch : public Component
{
    Q_OBJECT
    Q_PROPERTY( int     Poles      READ poles     WRITE setPoles     USER true )
    Q_PROPERTY( bool    DT         READ dt        WRITE setDT        USER true )
    Q_PROPERTY( QString Key        READ key       WRITE setKey       USER true )
    Q_PROPERTY( bool    Norm_Close READ normClose WRITE setNormClose USER true )

    public:
        Switch( QObject *parent, const QString &type, const QString &id );

        int poles() const { return m_poles; }
        void setPoles( int p );

        // Double throw: a changeover contact rather than a make-or-break one.
        // With it set each pole has a common and two throws, and the common is
        // on one throw while the switch rests and the other while it is thrown.
        bool dt() const { return m_dt; }
        void setDT( bool dt );

        QString key() const { return m_key; }
        void setKey( const QString &k ) { m_key = k; }

        // The state the switch is in before anything throws it.  For a single
        // throw it is whether the contacts are closed; for a double throw it is
        // which of the two the common rests on.
        bool normClose() const { return m_normClose; }
        void setNormClose( bool nc ) { m_normClose = nc; update(); }

        // Whether a pole is thrown.  Per pole rather than per switch, so a DIP
        // switch can reuse the same stamping.
        bool isThrown( int pole ) const;
        void setThrown( int pole, bool thrown );

        // Conducting, which for a single throw is thrown XOR norm-close and for
        // a double throw is not used - the common simply moves.
        bool isConducting( int pole ) const;

        virtual void stamp();

        virtual bool keyPressed( const QString &key );

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    protected:
        // Throws every pole.  A Push overrides both to make it momentary.
        virtual void toggle();

        void mousePressEvent( QGraphicsSceneMouseEvent *event );

        // Rebuilds the pins for the current pole count and throw type.  Changing
        // either is destructive, exactly as changing a Chip's package is: the
        // pins that carried the wires are not the pins that come back, so there
        // is nothing to keep the wires attached to.  New pins are registered with
        // the sheet here rather than left to Circuit::addComponent(), which ran
        // once, when the component was created.
        void buildPins();

        // The geometry both this class and SwitchDip lay out against: how far the
        // poles are spaced and how tall the body has to be.
        static int poleSpacing() { return 16; }

        QVector<bool> m_thrown;

        int m_poles;

        bool m_dt;
        bool m_normClose;

        QString m_key;
};

class Push : public Switch
{
    Q_OBJECT
    public:
        Push( QObject *parent, const QString &type, const QString &id );

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    protected:
        // Momentary: the press throws every pole, the release restores them, and
        // which state they are restored to is Norm_Close.  The release has to
        // reach the base too, or the drag the press started would never end and
        // the symbol would follow the cursor after the button came up.
        //
        // A Key is the one way a Push cannot behave as a push: the view sees only
        // presses, so Switch::keyPressed() latches it and the next press
        // releases it.  Wiring a key release through as well would mean a second
        // virtual on Component and a second walk of the sheet, for a shortcut
        // that a latching Switch already provides properly.
        void mousePressEvent( QGraphicsSceneMouseEvent *event );
        void mouseReleaseEvent( QGraphicsSceneMouseEvent *event );
};

class SwitchDip : public Component
{
    Q_OBJECT
    Q_PROPERTY( int Size READ size WRITE setSize USER true )

    public:
        SwitchDip( QObject *parent, const QString &type, const QString &id );

        int size() const { return m_size; }

        // Growing keeps the wires on the segments that are already there, which
        // is what Component::dropPins(from) is for rather than clearPins().
        void setSize( int s );

        bool isClosed( int i ) const;
        void setClosed( int i, bool on );

        virtual void stamp();

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    protected:
        void mousePressEvent( QGraphicsSceneMouseEvent *event );

    private:
        void addSegments( int count );

        // Which segment a point in the symbol's own frame falls on, or -1.  The
        // segments are close enough together that guessing from the vertical
        // position alone puts a click on the wrong one at the boundaries.
        int segmentAt( const QPointF &local ) const;

        QVector<bool> m_state;

        int m_size;
};

class RelaySPST : public Component
{
    Q_OBJECT
    Q_PROPERTY( bool    Norm_Close READ normClose WRITE setNormClose USER true )
    Q_PROPERTY( int     Poles      READ poles     WRITE setPoles     USER true )
    Q_PROPERTY( bool    DT         READ dt        WRITE setDT        USER true )
    Q_PROPERTY( double  Rcoil      READ rcoil     WRITE setRcoil     USER true )
    Q_PROPERTY( QString Unit       READ unit      WRITE setUnit      USER true )
    Q_PROPERTY( double  IOn        READ iOn       WRITE setIOn       USER true )
    Q_PROPERTY( double  IOff       READ iOff      WRITE setIOff      USER true )

    public:
        RelaySPST( QObject *parent, const QString &type, const QString &id );

        int poles() const { return m_poles; }
        void setPoles( int p );

        bool dt() const { return m_dt; }
        void setDT( bool dt );

        bool normClose() const { return m_normClose; }
        void setNormClose( bool nc ) { m_normClose = nc; }

        // Coil resistance, in the current unit - the property is Rcoil and the
        // number beside it is in whatever prefix Unit names, so 100 with " Ω" is
        // 100 ohm.
        double rcoil() const { return m_value; }
        void setRcoil( double r );

        virtual void setUnit( const QString &un );

        // Pick-up and drop-out currents, in amps, and deliberately two of them.
        // A single threshold makes a relay whose coil current sits at exactly
        // that value chatter once per step, which is a million contact
        // transitions a second and a matrix re-factorisation for every one.  The
        // gap between them is the hysteresis a real relay has anyway.
        double iOn() const { return m_Ion; }
        void setIOn( double i ) { m_Ion = i; }

        double iOff() const { return m_Ioff; }
        void setIOff( double i ) { m_Ioff = i; }

        bool isClosed() const { return m_closed; }

        // The coil current out of the last solve, read from the two coil pin
        // voltages rather than stored: the coil is a plain resistance, so the
        // current is (v_l - v_r)/Rcoil and reading it on demand cannot go stale
        // against the voltages beside it.
        double coilCurrent() const;

        virtual void stamp();

        // Advances the contact state once per step from the coil current just
        // solved.  On the reactive list for the same reason a Clock is: that list
        // is walked once per step after the solve, which is the only point at
        // which the coil current is known.
        virtual bool isReactive() const { return true; }
        virtual void updateStep();
        virtual void resetStep();

        virtual void paint( QPainter *painter,
                            const QStyleOptionGraphicsItem *option,
                            QWidget *widget );

    protected:
        void buildPins();

    private:
        void setClosed( bool on );

        // The coil's admittance, kept rather than recomputed so that a coil
        // resistance of zero - which the property panel will happily be given -
        // cannot put an infinity on the diagonal.
        double m_coilAdmit;

        double m_Ion;
        double m_Ioff;

        int m_poles;

        bool m_dt;
        bool m_normClose;
        bool m_closed;
};

// ---- factories -----------------------------------------------------------------

Component* createSwitch(    QObject *parent, const QString &type, const QString &id );
Component* createPush(      QObject *parent, const QString &type, const QString &id );
Component* createSwitchDip( QObject *parent, const QString &type, const QString &id );
Component* createRelaySPST( QObject *parent, const QString &type, const QString &id );

#endif // SIMU_SWITCHES_H
