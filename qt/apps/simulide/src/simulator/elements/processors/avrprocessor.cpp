/*
 * AVR Processor Implementation
 */

#include "avrprocessor.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

AvrProcessor::AvrProcessor(QObject *parent) 
    : BaseProcessor(parent)
{
    m_pSelf = this;
    m_device = &avr_devices[0];  // Default to ATmega328P
    m_deviceName = "atmega328p";
    m_firmwareLoaded = false;
    
    avr_core_init(&m_avr);
    avr_periph_init(&m_avr);
}

AvrProcessor::~AvrProcessor()
{
    if (m_pSelf == this)
        m_pSelf = nullptr;
}

void AvrProcessor::setDevice(QString device)
{
    m_deviceName = device.toLower();
    
    // Find device definition
    const AvrDevice *dev = avr_device_find(m_deviceName.toLatin1().constData());
    if (dev) {
        m_device = dev;
        // The core's flash/data/eeprom arrays are fixed size, but the device
        // table carries real sizes - atmega2560 asks for 256KB of flash and
        // 8KB of SRAM.  Copying those straight through made the hex loader
        // memcpy past the end of flash[] (heap corruption) and put the stack
        // pointer above data[], where every push is silently discarded.  Clamp
        // each one to what the arrays can actually hold; a device bigger than
        // the core model still runs, just within the modelled address space.
        m_avr.flash_size   = qMin<uint32_t>(dev->flash_size,   AVR_FLASH_SIZE);
        m_avr.sram_size    = qMin<uint32_t>(dev->sram_size,    AVR_DATA_SIZE - AVR_SRAM_START);
        m_avr.eeprom_size  = qMin<uint32_t>(dev->eeprom_size,  AVR_EEPROM_SIZE);
    } else {
        qDebug() << "AVR: Unknown device" << device << ", using ATmega328P";
        m_device = &avr_devices[0];
        m_deviceName = "atmega328p";
    }
    
    BaseProcessor::setDevice(device);
}

QString AvrProcessor::getDevice()
{
    return m_deviceName;
}

bool AvrProcessor::loadFirmware(QString file)
{
    if (file.isEmpty()) {
        m_loadStatus = false;
        return false;
    }
    
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "AVR: Cannot open firmware file:" << file;
        m_loadStatus = false;
        return false;
    }
    
    QTextStream in(&f);
    QString hexData = in.readAll();
    f.close();
    
    // Load into flash
    uint32_t start_addr = 0;
    int size = ihex_load_string(hexData.toLatin1().constData(), 
                                 m_avr.flash, 
                                 m_avr.flash_size, 
                                 &start_addr);
    
    if (size < 0) {
        qDebug() << "AVR: Failed to load firmware:" << file;
        m_loadStatus = false;
        return false;
    }
    
    m_firmwareLoaded = true;

    // Reset processor (clears PC/registers/peripherals) then mark the base
    // processor initialized so BaseProcessor::step() will actually run: step()
    // early-returns unless m_loadStatus is set, and initialized() is what sets
    // it (plus zeroes m_msimStep/m_extraCycle).  Do NOT touch m_resetStatus
    // here - that flag means "external reset line held low" and is owned by
    // hardReset(); leaving it set would block stepping forever.
    reset();
    initialized();

    return true;
}

void AvrProcessor::reset()
{
    avr_core_reset(&m_avr);
    avr_periph_reset(&m_avr);
}

void AvrProcessor::stepOne()
{
    if (!m_firmwareLoaded || m_avr.halted) return;

    // Execute one instruction
    int cycles = avr_core_step(&m_avr);

    // Step peripherals
    avr_periph_step(&m_avr, cycles);
}

void AvrProcessor::stepCpu()
{
    // Same as stepOne for AVR
    stepOne();
}

int AvrProcessor::pc()
{
    return m_avr.pc;
}

uint64_t AvrProcessor::cycle()
{
    return m_avr.cycle;
}

int AvrProcessor::getRamValue(int address)
{
    int addr = validate(address);
    if (addr < 0) return 0;
    
    return avr_read_data(&m_avr, addr);
}

int AvrProcessor::validate(int address)
{
    // AVR data memory: 0x000-0x1FF (I/O), 0x200-0x8FF (SRAM)
    if (address < 0 || address >= (int)AVR_DATA_SIZE) {
        return -1;
    }
    return address;
}

QVector<int> AvrProcessor::eeprom()
{
    QVector<int> eep;
    eep.resize(m_avr.eeprom_size);
    
    for (uint32_t i = 0; i < m_avr.eeprom_size; i++) {
        eep[i] = m_avr.eeprom[i];
    }
    
    return eep;
}

void AvrProcessor::setEeprom(QVector<int> eep)
{
    int size = qMin(eep.size(), (int)m_avr.eeprom_size);
    
    for (int i = 0; i < size; i++) {
        m_avr.eeprom[i] = eep[i] & 0xFF;
    }
}

// GPIO interface
void AvrProcessor::setGpioPin(int port, int pin, bool value)
{
    avr_gpio_set_pin(&m_avr, port, pin, value);
}

bool AvrProcessor::getGpioPin(int port, int pin)
{
    return avr_gpio_get_pin(&m_avr, port, pin);
}

uint8_t AvrProcessor::getGpioPort(int port)
{
    return avr_gpio_get_port(&m_avr, port);
}

uint8_t AvrProcessor::getGpioDdr(int port)
{
    if (port > 2) return 0;
    return m_avr.data[REG_DDRB + port * 3];
}

// USART interface
void AvrProcessor::usartReceive(uint8_t byte)
{
    avr_usart_receive(&m_avr, byte);
}

void AvrProcessor::setUsartTxCallback(void (*cb)(void*, uint8_t), void *data)
{
    avr_usart_set_tx_callback(&m_avr, cb, data);
}
