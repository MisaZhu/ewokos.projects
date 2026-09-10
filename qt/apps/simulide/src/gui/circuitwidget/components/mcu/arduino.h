/*
 * Arduino Component - Arduino Uno/Nano board wrapper
 */

#ifndef ARDUINO_H
#define ARDUINO_H

#include "avrcomponent.h"

class LibraryItem;
class Component;

class Arduino : public AvrComponent
{
    Q_OBJECT

public:
    Arduino(QObject *parent = 0, QString type = "Arduino", QString id = "Arduino-1");
    ~Arduino();

    static LibraryItem* libraryItem();
    static Component* construct(QObject *parent, QString type, QString id);

private:
    QString m_boardType;  // "uno", "nano", "mega"
};

#endif // ARDUINO_H
