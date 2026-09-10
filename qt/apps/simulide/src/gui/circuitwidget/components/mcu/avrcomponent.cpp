/*
 * AVR Component Implementation
 */

#include "avrcomponent.h"
#include "itemlibrary.h"

AvrComponent::AvrComponent(QObject *parent, QString type, QString id)
    : McuComponent(parent, type, id)
{
    // A generic AVR dropped from the palette defaults to the ATmega328 - the
    // same silicon as the Arduino Uno - at its 16 MHz crystal speed.  Concrete
    // boards (Arduino) refine this in their own constructor.
    setDevice("atmega328");
    setFreq(16.0);
}

AvrComponent::~AvrComponent()
{
}

LibraryItem* AvrComponent::libraryItem()
{
    return new LibraryItem(
        tr("AVR"),
        tr("Micro"),
        "ic2.png",     // generic chip icon; no avr.png is compiled into the qrc
        "AVR",
        AvrComponent::construct);
}

Component* AvrComponent::construct(QObject *parent, QString type, QString id)
{
    AvrComponent* avr = new AvrComponent(parent, type, id);
    // Load the pin layout here rather than in the constructor: construct() knows
    // the concrete type, so each part initialises exactly once with its own
    // package.  Doing it in the constructor would hit the virtual-call-during-
    // construction pitfall (an Arduino's AvrComponent base would run this with
    // AVR dispatch) and initialise the pins twice.
    avr->initMcuPins("avr/atmega328");
    return avr;
}
