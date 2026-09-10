/*
 * AVR Processor - Bridge between SimulIDE BaseProcessor and AVR core
 */

#ifndef AVRPROCESSOR_H
#define AVRPROCESSOR_H

#include "baseprocessor.h"

#include "../../avr/avr_core.h"
#include "../../avr/avr_devs.h"
#include "../../avr/avr_ihex.h"

class AvrProcessor : public BaseProcessor
{
    Q_OBJECT
    
public:
    AvrProcessor(QObject *parent = 0);
    ~AvrProcessor();
    
    // BaseProcessor interface
    bool loadFirmware(QString file) override;
    void stepOne() override;
    void stepCpu() override;
    void reset() override;
    int pc() override;
    uint64_t cycle() override;
    int getRamValue(int address) override;
    QVector<int> eeprom() override;
    void setEeprom(QVector<int> eep) override;
    
    void setDevice(QString device) override;
    QString getDevice() override;
    
    // AVR-specific
    AvrCore* getCore() { return &m_avr; }
    
    // GPIO interface for circuit connection
    void setGpioPin(int port, int pin, bool value);
    bool getGpioPin(int port, int pin);
    uint8_t getGpioPort(int port);
    uint8_t getGpioDdr(int port);
    
    // USART interface
    void usartReceive(uint8_t byte);
    void setUsartTxCallback(void (*cb)(void*, uint8_t), void *data);
    
protected:
    int validate(int address) override;
    
private:
    AvrCore m_avr;
    QString m_deviceName;
    const AvrDevice *m_device;
    bool m_firmwareLoaded;
};

#endif // AVRPROCESSOR_H
