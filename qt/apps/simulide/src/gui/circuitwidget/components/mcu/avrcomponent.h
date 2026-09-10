/*
 * AVR Component - GUI wrapper for AVR microcontrollers
 */

#ifndef AVRCOMPONENT_H
#define AVRCOMPONENT_H

#include "mcucomponent.h"

class LibraryItem;
class Component;

class AvrComponent : public McuComponent
{
    Q_OBJECT

public:
    AvrComponent(QObject *parent = 0, QString type = "AVR", QString id = "AVR-1");
    ~AvrComponent();

    static LibraryItem* libraryItem();
    static Component* construct(QObject *parent, QString type, QString id);
};

#endif // AVRCOMPONENT_H
