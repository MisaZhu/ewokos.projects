/*
 * SimulIDE, ported to EwokOS - the firmware core interface.
 *
 * Upstream: src/simulator/elements/processors/baseprocessor.h.  Reduced to
 * the three calls the Simulator actually makes, so that neither the simulator
 * nor the circuit layer has to include an AVR or PIC header - upstream's
 * BaseProcessor carries the whole debug/flash/EEPROM surface that its editor
 * widget drives, none of which is wired up here yet.
 */

#ifndef SIMU_BASEPROCESSOR_H
#define SIMU_BASEPROCESSOR_H

#include <QString>

class BaseProcessor
{
    public:
        virtual ~BaseProcessor() {}

        // Advance the core.  The core decides how many cycles that is: it
        // knows its own clock frequency and reads the simulator's step
        // counter, so a 16 MHz Arduino and a 1 MHz ATtiny both advance by the
        // right amount from one call.
        virtual void runStep() = 0;

        // Reset the core and, for a real device, reload the firmware.
        virtual void resetMcu() = 0;

        // The firmware loaded and ready to run.  An empty string is what makes
        // an MCU component draw itself idle rather than executing.
        virtual QString firmware() const { return QString(); }
};

#endif // SIMU_BASEPROCESSOR_H
