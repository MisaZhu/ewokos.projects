/*
 * Arduino Component Implementation
 */

#include "arduino.h"
#include "itemlibrary.h"

Arduino::Arduino(QObject *parent, QString type, QString id)
    : AvrComponent(parent, type, id)
{
    // Arduino Uno: ATmega328 clocked by a 16 MHz crystal.  The AvrComponent
    // base already selects atmega328/16 MHz; these are stated again so the
    // board's spec is explicit and independent of the generic AVR default.
    m_boardType = "uno";
    setDevice("atmega328");
    setFreq(16.0);
}

Arduino::~Arduino()
{
}

LibraryItem* Arduino::libraryItem()
{
    return new LibraryItem(
        tr("Arduino Uno"),
        tr("Micro"),
        "arduinoUno.png",
        "Arduino",          // must match itemtype="Arduino" in the .simu files
        Arduino::construct);
}

Component* Arduino::construct(QObject *parent, QString type, QString id)
{
    Arduino* ard = new Arduino(parent, type, id);
    // Uno board package: its pin ids are the AVR port names (PD0..PD7, PB0..PB5,
    // PC0..PC5) plus the power/aref/reset pins, matching the connector endpoints
    // the example circuits store (e.g. "Arduino Uno-36-PB3").
    ard->initMcuPins("arduino/arduino_uno");
    return ard;
}
