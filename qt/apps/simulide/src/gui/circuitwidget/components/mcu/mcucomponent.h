/*
 * MCU Component - Base class for microcontroller components
 * 
 * This replaces the stub version with a real implementation that
 * supports AVR processors (ATmega328P, Arduino Uno, etc.)
 */

#ifndef MCUCOMPONENT_H
#define MCUCOMPONENT_H

#include "chip.h"
#include "memdata.h"

#include <QList>

class BaseProcessor;
class AvrProcessor;
class eSource;
class Pin;

class MAINMODULE_EXPORT McuComponent : public Chip, public MemData
{
    Q_OBJECT
    
public:
    McuComponent(QObject *parent = 0, QString type = "McuComponent", QString id = "McuComponent-1");
    ~McuComponent();
    
    static McuComponent* self() { return m_pSelf; }
    
    // MCU interface
    BaseProcessor* processor() { return m_processor; }
    AvrProcessor* avrProcessor();
    
    QString device() { return m_device; }
    void setDevice(QString device);
    
    double freq() { return m_freq; }
    void setFreq(double freq);
    
    bool loadFirmware(QString file);
    void load(QString file) { loadFirmware(file); }  // Alias for compatibility
    QString firmwareFile() { return m_firmwareFile; }
    void setFirmwareFile(QString file);
    
    void runAutoLoad();

    // Load the chip package (pin layout) for this MCU.  pkgRelPath is relative
    // to the shipped data dir (e.g. "arduino/arduino_uno" or "avr/atmega328");
    // it resolves to an absolute path so Chip::initChip() - which normally
    // resolves m_pkgeFile against the circuit file's folder - reads it from the
    // app's res/data tree instead.  After the pins exist, an eSource is created
    // for every GPIO pin (P[B-D][0-7]) so the AVR core can drive/read them.
    void initMcuPins(QString pkgRelPath);

    // Sync the AVR GPIO registers with the circuit: drive output pins from
    // PORT/DDR, and sample input pin voltages back into the AVR PIN registers.
    // Called by Simulator::runCircuit() right after the MCU CPU batch runs.
    void updatePins();

    // GPIO interface for circuit connection
    void setGpioPin(int port, int pin, bool value);
    bool getGpioPin(int port, int pin);
    uint8_t getGpioPort(int port);
    uint8_t getGpioDdr(int port);

    // USART interface
    void usartReceive(uint8_t byte);
    
    // EEPROM interface
    QVector<int> eeprom();
    void setEeprom(QVector<int> eep);
    
    // Simulation control
    void step();
    void reset();
    
    // Properties
    Q_PROPERTY(QString Program READ firmwareFile WRITE setFirmwareFile)
    Q_PROPERTY(double Mhz READ freq WRITE setFreq)
    
protected:
    static McuComponent* m_pSelf;

    // One GPIO pin bound to the circuit: the AVR port index (0=B,1=C,2=D), the
    // bit number within that port, the GUI Pin, and the eSource that drives /
    // samples it.  Built by initMcuPins() from the package pin ids (PB0..PD7).
    struct GpioPin {
        Pin*     pin;
        eSource* src;
        int      port;
        int      num;
    };
    QList<GpioPin> m_gpio;

    BaseProcessor *m_processor;
    QString m_device;
    QString m_firmwareFile;
    double m_freq;  // MHz
    bool m_autoLoad;
};

#endif // MCUCOMPONENT_H
