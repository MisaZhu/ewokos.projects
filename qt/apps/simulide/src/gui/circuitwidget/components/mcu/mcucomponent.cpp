/*
 * MCU Component Implementation
 *
 * Base class for microcontroller chips in this port.  It owns the BaseProcessor
 * (an AvrProcessor for AVR/Arduino parts), loads the chip package so the part
 * has real pins, and binds every GPIO pin to an eSource so the simulated core
 * can drive outputs and sample inputs against the circuit matrix.
 *
 * Division of labour during a run:
 *   - Simulator::runCircuit() calls BaseProcessor::self()->step(), which runs
 *     the cycle-accurate CPU batch (instructions-per-sim-step set from Mhz).
 *   - Right after that it calls McuComponent::self()->updatePins(), which
 *     pushes PORT/DDR out to the driven pins and samples inputs back into PIN.
 * McuComponent::step() therefore does not run the CPU (that would double-step).
 */

#include "mcucomponent.h"
#include "avrprocessor.h"
#include "simulator.h"
#include "circuit.h"
#include "pin.h"
#include "e-source.h"
#include "simuapi_apppath.h"

#include <QFileInfo>
#include <QDir>
#include <QDebug>

McuComponent* McuComponent::m_pSelf = nullptr;

McuComponent::McuComponent(QObject *parent, QString type, QString id)
    : Chip(parent, type, id)
{
    m_pSelf = this;
    m_processor = nullptr;
    m_device = "atmega328";
    m_freq = 16.0;  // 16 MHz default
    // Auto-load the firmware named by the circuit's Program property when the
    // simulation starts (Simulator::startSim -> runAutoLoad).  Upstream default.
    m_autoLoad = true;
}

McuComponent::~McuComponent()
{
    if (m_pSelf == this)
        m_pSelf = nullptr;

    for (GpioPin &g : m_gpio)
        delete g.src;
    m_gpio.clear();

    if (m_processor) {
        delete m_processor;
        m_processor = nullptr;
    }
}

AvrProcessor* McuComponent::avrProcessor()
{
    // static_cast: this build uses -fno-rtti, so dynamic_cast is unavailable.
    // Every McuComponent in this port is backed by an AvrProcessor.
    return static_cast<AvrProcessor*>(m_processor);
}

void McuComponent::setDevice(QString device)
{
    m_device = device.toLower();

    // Create processor if not exists
    if (!m_processor)
        m_processor = new AvrProcessor();

    m_processor->setDevice(m_device);
}

void McuComponent::setFreq(double freq)
{
    m_freq = freq;
}

// ---- firmware ---------------------------------------------------------------

bool McuComponent::loadFirmware(QString file)
{
    if (file.isEmpty()) return false;

    if (!m_processor) {
        m_processor = new AvrProcessor();
        m_processor->setDevice(m_device);
    }

    // The circuit stores Program relative to the .simu folder, so resolve it
    // against the loaded circuit's directory (same convention Chip::initChip
    // uses for packages).  An already-absolute path is passed through.
    QString path = file;
    if (!QFileInfo(path).isAbsolute()) {
        QDir circuitDir = QFileInfo(Circuit::self()->getFileName()).absoluteDir();
        path = circuitDir.absoluteFilePath(file);
    }

    // Keep the original (possibly relative) value for the Program property so a
    // re-saved circuit stays portable; hand the resolved path to the core.
    m_firmwareFile = file;
    bool ok = m_processor->loadFirmware(path);
    qDebug() << "MCU: loadFirmware resolved=" << path
             << "exists=" << QFileInfo(path).exists() << "result=" << ok;
    return ok;
}

void McuComponent::setFirmwareFile(QString file)
{
    m_firmwareFile = file;
}

void McuComponent::runAutoLoad()
{
    qDebug() << "MCU: runAutoLoad autoLoad=" << m_autoLoad
             << "firmwareFile=" << m_firmwareFile;
    if (m_autoLoad && !m_firmwareFile.isEmpty()) {
        loadFirmware(m_firmwareFile);
        if (m_processor)
            qDebug() << "MCU: after autoload loadStatus="
                     << m_processor->getLoadStatus();
    }
}

// ---- package / pins ---------------------------------------------------------

void McuComponent::initMcuPins(QString pkgRelPath)
{
    // Drop any previous GPIO binding.  Arduino derives from AvrComponent, so an
    // Arduino is initialised twice (avr default, then the board package); the
    // eSources created on the first pass reference Pins that Chip::initChip()
    // is about to delete, so they must go first.
    for (GpioPin &g : m_gpio)
        delete g.src;
    m_gpio.clear();

    // Resolve the package against the shipped read-only data dir and hand the
    // absolute path to Chip::initChip().  QDir::absoluteFilePath() passes an
    // already-absolute path through unchanged, so initChip's normal
    // circuit-folder resolution is bypassed cleanly.
    QString pkg = SIMUAPI_AppPath::self()->availableDataFilePath(pkgRelPath + ".package");
    if (pkg.isEmpty()) {
        qDebug() << "MCU: package not found for" << pkgRelPath;
        return;
    }
    m_pkgeFile = pkg;

    Chip::initChip();   // Creates m_pin[] from the package XML

    // Bind every GPIO pin (package id "PB0".."PD7") to an eSource.  Chip::addPin
    // named the pin "<component id>-<package pin id>", so the port/bit is the
    // token after the last '-'.  Power/aref/reset pins are skipped: they are not
    // modelled as GPIO here.
    for (Pin *pin : m_pin) {
        if (!pin) continue;

        QString full = pin->pinId();
        QString tok  = full.section('-', -1);   // last '-'-separated token
        if (tok.size() != 3 || tok[0] != 'P') continue;

        int port = -1;
        if      (tok[1] == 'B') port = 0;
        else if (tok[1] == 'C') port = 1;
        else if (tok[1] == 'D') port = 2;
        if (port < 0) continue;

        bool ok  = false;
        int  num = tok.mid(2).toInt(&ok);
        if (!ok || num < 0 || num > 7) continue;

        GpioPin g;
        g.pin  = pin;
        g.port = port;
        g.num  = num;
        g.src  = new eSource((full + "-eSource").toStdString(), pin);
        // Start as a floating input; updatePins() sets the real state each step.
        g.src->setVoltHigh(5.0);
        g.src->setVoltLow(0.0);
        g.src->setImp(high_imp);
        m_gpio.append(g);
    }
    qDebug() << "MCU:" << m_gpio.size() << "GPIO pins bound from" << pkgRelPath;
}

void McuComponent::updatePins()
{
    AvrProcessor *avr = avrProcessor();
    if (!avr || m_gpio.isEmpty()) return;

    // Cache the three port registers once per step (port 0=B, 1=C, 2=D).
    uint8_t ddr[3]  = { avr->getGpioDdr(0),  avr->getGpioDdr(1),  avr->getGpioDdr(2)  };
    uint8_t port[3] = { avr->getGpioPort(0), avr->getGpioPort(1), avr->getGpioPort(2) };

    for (GpioPin &g : m_gpio) {
        bool isOut = (ddr[g.port]  >> g.num) & 1;
        bool val   = (port[g.port] >> g.num) & 1;

        if (isOut) {
            // Push-pull output: low impedance, drive the PORT bit.  setImp()
            // re-stamps admittance (and flags the matrix for re-factorisation),
            // so only call it when the direction actually changes.
            if (g.src->imp() != 40) g.src->setImp(40);
            g.src->setOut(val);
            g.src->stampOutput();
        } else {
            // Input.  A set PORT bit enables the AVR's internal pull-up
            // (~tens of kOhm to Vcc); a clear bit leaves the pin floating.
            if (val) {
                if (g.src->imp() != 30000) g.src->setImp(30000);
                g.src->setOut(true);
                g.src->stampOutput();
            } else {
                if (g.src->imp() != high_imp) g.src->setImp(high_imp);
            }
            // Sample the node voltage back into the AVR PIN register so the
            // firmware's next IN/SBIS sees the circuit state.
            bool state = (g.src->getVolt() > 2.5);
            avr->setGpioPin(g.port, g.num, state);
        }
    }
}

// ---- simulation control -----------------------------------------------------

void McuComponent::step()
{
    // No-op: the CPU is driven by BaseProcessor::self()->step() from
    // Simulator::runCircuit(), and the pins by updatePins() right after.  This
    // method exists for the subclass overrides; running the core here would
    // double-step it.
}

void McuComponent::reset()
{
    if (m_processor)
        m_processor->reset();
}

// ---- GPIO interface ---------------------------------------------------------

void McuComponent::setGpioPin(int port, int pin, bool value)
{
    AvrProcessor *avr = avrProcessor();
    if (avr) avr->setGpioPin(port, pin, value);
}

bool McuComponent::getGpioPin(int port, int pin)
{
    AvrProcessor *avr = avrProcessor();
    if (avr) return avr->getGpioPin(port, pin);
    return false;
}

uint8_t McuComponent::getGpioPort(int port)
{
    AvrProcessor *avr = avrProcessor();
    if (avr) return avr->getGpioPort(port);
    return 0;
}

uint8_t McuComponent::getGpioDdr(int port)
{
    AvrProcessor *avr = avrProcessor();
    if (avr) return avr->getGpioDdr(port);
    return 0;
}

// ---- USART interface --------------------------------------------------------

void McuComponent::usartReceive(uint8_t byte)
{
    AvrProcessor *avr = avrProcessor();
    if (avr) avr->usartReceive(byte);
}

// ---- EEPROM interface -------------------------------------------------------

QVector<int> McuComponent::eeprom()
{
    if (m_processor) return m_processor->eeprom();
    return QVector<int>();
}

void McuComponent::setEeprom(QVector<int> eep)
{
    if (m_processor) m_processor->setEeprom(eep);
}
