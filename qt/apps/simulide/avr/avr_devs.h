/*
 * AVR Device Definitions and Peripheral Simulation
 * 
 * Implements ATmega328P (Arduino Uno) peripherals:
 * - GPIO Ports B, C, D
 * - Timer/Counter 0, 1, 2
 * - USART0
 * - EEPROM interface
 */

#ifndef AVR_DEVS_H
#define AVR_DEVS_H

#include "avr_core.h"

// Forward declarations
struct AvrPeripheral;

// Peripheral callback types
typedef void (*AvrPeriphInit)(struct AvrPeripheral *p, AvrCore *avr);
typedef void (*AvrPeriphReset)(struct AvrPeripheral *p);
typedef void (*AvrPeriphStep)(struct AvrPeripheral *p, int cycles);
typedef uint8_t (*AvrPeriphRead)(struct AvrPeripheral *p, uint16_t addr);
typedef void (*AvrPeriphWrite)(struct AvrPeripheral *p, uint16_t addr, uint8_t val);

// Base peripheral structure
typedef struct AvrPeripheral {
    const char *name;
    uint16_t base_addr;
    uint16_t size;
    AvrPeriphInit init;
    AvrPeriphReset reset;
    AvrPeriphStep step;
    AvrPeriphRead read;
    AvrPeriphWrite write;
    void *data;
    struct AvrPeripheral *next;
} AvrPeripheral;

// GPIO Port state
typedef struct {
    uint8_t pin;      // PIN register (input)
    uint8_t ddr;      // DDR register (direction)
    uint8_t port;     // PORT register (output/pull-up)
    uint8_t ext_pin;  // External pin state (from circuit)
} AvrGpio;

// Timer state
typedef struct {
    uint16_t tcnt;    // Counter value
    uint8_t ocra;     // Output compare A
    uint8_t ocrb;     // Output compare B
    uint8_t ocra16h;  // High byte for 16-bit timer
    uint8_t ocrb16h;
    uint8_t tccra;    // Control register A
    uint8_t tccrb;    // Control register B
    uint8_t tccrc;    // Control register C
    uint8_t tifr;     // Interrupt flags
    uint8_t timsk;    // Interrupt mask
    uint16_t prescaler;
    uint8_t wgm;      // Waveform generation mode
    bool running;
} AvrTimer;

// USART state
typedef struct {
    uint8_t udr;      // Data register
    uint8_t ucsra;    // Control/Status A
    uint8_t ucsrb;    // Control/Status B
    uint8_t ucsrc;    // Control/Status C
    uint16_t ubrr;    // Baud rate
    uint8_t rx_buf;   // Receive buffer
    bool rx_ready;
    bool tx_busy;
} AvrUsart;

// EEPROM state
typedef struct {
    uint16_t eear;    // Address register
    uint8_t eedr;     // Data register
    uint8_t eecr;     // Control register
    bool write_pending;
    int write_cycles;
} AvrEeprom;

// Initialize all peripherals for ATmega328P
void avr_periph_init(AvrCore *avr);

// Reset all peripherals
void avr_periph_reset(AvrCore *avr);

// Step all peripherals (called each CPU cycle)
void avr_periph_step(AvrCore *avr, int cycles);

// Handle I/O register write
void avr_periph_write(AvrCore *avr, uint16_t addr, uint8_t val);

// Handle I/O register read
uint8_t avr_periph_read(AvrCore *avr, uint16_t addr);

// GPIO interface for circuit connection
void avr_gpio_set_pin(AvrCore *avr, uint8_t port, uint8_t pin, bool value);
bool avr_gpio_get_pin(AvrCore *avr, uint8_t port, uint8_t pin);
void avr_gpio_set_ddr(AvrCore *avr, uint8_t port, uint8_t ddr);
uint8_t avr_gpio_get_port(AvrCore *avr, uint8_t port);

// USART interface
void avr_usart_receive(AvrCore *avr, uint8_t byte);
typedef void (*AvrUsartTxCallback)(void *data, uint8_t byte);
void avr_usart_set_tx_callback(AvrCore *avr, AvrUsartTxCallback cb, void *data);

// Timer callback for output compare
typedef void (*AvrTimerCallback)(void *data, uint8_t timer, uint8_t channel);
void avr_timer_set_callback(AvrCore *avr, AvrTimerCallback cb, void *data);

// Interrupt request
void avr_irq_request(AvrCore *avr, int vector);
void avr_irq_clear(AvrCore *avr, int vector);

// Device configuration
typedef struct {
    const char *name;
    uint32_t flash_size;
    uint32_t sram_size;
    uint32_t eeprom_size;
    uint8_t num_ports;
    uint8_t num_timers;
    uint8_t num_usart;
} AvrDevice;

// Get device definition by name
const AvrDevice* avr_device_find(const char *name);

// List of supported devices
extern const AvrDevice avr_devices[];

#endif // AVR_DEVS_H
