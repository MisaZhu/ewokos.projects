/*
 * AVR Core Simulator for SimulIDE/EwokOS
 * 
 * Implements the AVR 8-bit RISC instruction set.
 * Target: ATmega328P (Arduino Uno) and compatible devices.
 *
 * Based on AVR architecture:
 * - 32 general-purpose registers (R0-R31)
 * - Status register (SREG)
 * - 16-bit word-addressed PC (22-bit byte address)
 * - Harvard architecture: separate Flash/SRAM/EEPROM
 */

#ifndef AVR_CORE_H
#define AVR_CORE_H

#include <stdint.h>
#include <string.h>

// AVR Status Register bits
#define SREG_C  0x01  // Carry
#define SREG_Z  0x02  // Zero
#define SREG_N  0x04  // Negative
#define SREG_V  0x08  // Two's complement overflow
#define SREG_S  0x10  // Sign (N xor V)
#define SREG_H  0x20  // Half carry
#define SREG_T  0x40  // Transfer bit
#define SREG_I  0x80  // Global interrupt enable

// Memory map constants for ATmega328P
#define AVR_FLASH_SIZE    (32 * 1024)   // 32KB flash (16K words)
#define AVR_SRAM_SIZE     (2 * 1024)    // 2KB SRAM
#define AVR_EEPROM_SIZE   1024          // 1KB EEPROM
#define AVR_IO_SIZE       256           // Standard I/O space
#define AVR_EXT_IO_SIZE   256           // Extended I/O space

// Data memory layout:
// 0x000-0x0FF: I/O registers (32 registers + 64 I/O)
// 0x100-0x1FF: Extended I/O
// 0x200-0x8FF: SRAM (2KB)
#define AVR_SRAM_START    0x200
#define AVR_DATA_SIZE     (AVR_IO_SIZE + AVR_EXT_IO_SIZE + AVR_SRAM_SIZE)

// I/O Register addresses (ATmega328P).
//
// These are DATA-SPACE addresses, not the 6-bit I/O addresses used by IN/OUT.
// AVR maps each I/O register twice: at its I/O address A (reachable only
// through IN/OUT/SBI/CBI/SBIC/SBIS) and at data address A+0x20 (reachable
// through LDS/STS/LD/ST).  avr->data[] is indexed by data address - that is
// what firmware actually uses, since gcc-avr emits "sts 0x45" for TCCR0B and
// "out 0x25" only when it can - so every define below carries the +0x20.
// Keeping them as bare I/O addresses put the whole peripheral model one
// window away from the CPU: a firmware write to TCCR0B landed in data[0x45]
// while the timer step read data[0x25], and Serial.print() spun forever in
// its UDRE poll because data[0xE0] was never the 0x20 that reset put in
// data[0xC0].
#define REG_PINB    0x23
#define REG_DDRB    0x24
#define REG_PORTB   0x25
#define REG_PINC    0x26
#define REG_DDRC    0x27
#define REG_PORTC   0x28
#define REG_PIND    0x29
#define REG_DDRD    0x2A
#define REG_PORTD   0x2B
#define REG_TIFR0   0x35
#define REG_TIFR1   0x36
#define REG_TIFR2   0x37
#define REG_PCIFR   0x3B
#define REG_EIFR    0x3C
#define REG_EIMSK   0x3D
#define REG_GPIOR0  0x3E
#define REG_EECR    0x3F
#define REG_EEDR    0x40
#define REG_EEARL   0x41
#define REG_EEARH   0x42
#define REG_GTCCR   0x43
#define REG_TCCR0A  0x44
#define REG_TCCR0B  0x45
#define REG_TCNT0   0x46
#define REG_OCR0A   0x47
#define REG_OCR0B   0x48
#define REG_GPIOR1  0x4A
#define REG_GPIOR2  0x4B
#define REG_SPCR    0x4C
#define REG_SPSR    0x4D
#define REG_SPDR    0x4E
#define REG_ACSR    0x50
#define REG_SMCR    0x53
#define REG_MCUSR   0x54
#define REG_MCUCR   0x55
#define REG_SPMCSR  0x57
#define REG_SPL     0x5D
#define REG_SPH     0x5E
#define REG_SREG    0x5F
#define REG_WDTCSR  0x60
#define REG_CLKPR   0x61
#define REG_PRR     0x64
#define REG_OSCCAL  0x66
#define REG_PCICR   0x68
#define REG_EICRA   0x69
#define REG_PCMSK0  0x6B
#define REG_PCMSK1  0x6C
#define REG_PCMSK2  0x6D
#define REG_TIMSK0  0x6E
#define REG_TIMSK1  0x6F
#define REG_TIMSK2  0x70
#define REG_ADCSRA  0x9A
#define REG_ADCSRB  0x9B
#define REG_ADMUX   0x9C
#define REG_ADCW    0x98  // ADCL + ADCH
#define REG_ADCL    0x98
#define REG_ADCH    0x99
#define REG_DIDR0   0x9E
#define REG_DIDR1   0x9F
#define REG_TCCR1A  0xA0
#define REG_TCCR1B  0xA1
#define REG_TCCR1C  0xA2
#define REG_TCNT1L  0xA4
#define REG_TCNT1H  0xA5
#define REG_ICR1L   0xA6
#define REG_ICR1H   0xA7
#define REG_OCR1AL  0xA8
#define REG_OCR1AH  0xA9
#define REG_OCR1BL  0xAA
#define REG_OCR1BH  0xAB
#define REG_TCCR2A  0xD0
#define REG_TCCR2B  0xD1
#define REG_TCNT2   0xD2
#define REG_OCR2A   0xD3
#define REG_OCR2B   0xD4
#define REG_ASSR    0xD6
#define REG_TWBR    0xD8
#define REG_TWSR    0xD9
#define REG_TWAR    0xDA
#define REG_TWDR    0xDB
#define REG_TWCR    0xDC
#define REG_TWAMR   0xDD
#define REG_UCSR0A  0xE0
#define REG_UCSR0B  0xE1
#define REG_UCSR0C  0xE2
#define REG_UBRR0L  0xE4
#define REG_UBRR0H  0xE5
#define REG_UDR0    0xE6

// Interrupt vectors (ATmega328P)
enum AvrVector {
    VEC_RESET = 0,
    VEC_INT0,
    VEC_INT1,
    VEC_PCINT0,
    VEC_PCINT1,
    VEC_PCINT2,
    VEC_WDT,
    VEC_TIMER2_COMPA,
    VEC_TIMER2_COMPB,
    VEC_TIMER2_OVF,
    VEC_TIMER1_CAPT,
    VEC_TIMER1_COMPA,
    VEC_TIMER1_COMPB,
    VEC_TIMER1_OVF,
    VEC_TIMER0_COMPA,
    VEC_TIMER0_COMPB,
    VEC_TIMER0_OVF,
    VEC_SPI_STC,
    VEC_USART_RX,
    VEC_USART_UDRE,
    VEC_USART_TX,
    VEC_ADC,
    VEC_EE_READY,
    VEC_ANALOG_COMP,
    VEC_TWI,
    VEC_SPM_READY,
    VEC_COUNT
};

// AVR CPU State
typedef struct AvrCore {
    // General purpose registers R0-R31
    uint8_t reg[32];
    
    // Status register
    uint8_t sreg;
    
    // Stack pointer (points to next free location)
    uint16_t sp;
    
    // Program counter (word address, so actual byte address is PC*2)
    uint32_t pc;
    
    // Clock cycle counter
    uint64_t cycle;
    
    // Memory
    uint8_t flash[AVR_FLASH_SIZE];      // Program memory (byte addressed)
    uint8_t data[AVR_DATA_SIZE];        // Data memory (I/O + SRAM)
    uint8_t eeprom[AVR_EEPROM_SIZE];    // EEPROM
    
    // Interrupt state
    bool irq_pending[VEC_COUNT];
    bool sleeping;
    bool halted;
    
    // Device configuration
    uint32_t flash_size;
    uint32_t sram_size;
    uint32_t eeprom_size;
    uint16_t sram_start;
    
    // Callback for I/O writes (peripherals)
    void (*io_write_cb)(struct AvrCore *avr, uint16_t addr, uint8_t val);
    void *io_write_data;
    
    // Callback for UART transmit
    void (*uart_tx_cb)(struct AvrCore *avr, uint8_t val);
    void *uart_tx_data;
} AvrCore;

// Initialize AVR core
void avr_core_init(AvrCore *avr);

// Reset AVR core
void avr_core_reset(AvrCore *avr);

// Execute one instruction, returns number of cycles
int avr_core_step(AvrCore *avr);

// Read data memory
//
// SREG and SP live in avr->sreg / avr->sp so the decoder can touch them
// cheaply, but firmware also reaches them through the I/O window - gcc-avr
// lowers "uint8_t s = SREG; cli(); ... SREG = s;" (used by micros(), delay()
// and every atomic section in avr-libc) to in/out on 0x3F.  Mirroring them
// here keeps the two views of the same register from disagreeing.
static inline uint8_t avr_read_data(AvrCore *avr, uint16_t addr) {
    if (addr == REG_SREG) return avr->sreg;
    if (addr == REG_SPH)  return (uint8_t)(avr->sp >> 8);
    if (addr == REG_SPL)  return (uint8_t)(avr->sp & 0xFF);
    if (addr < AVR_DATA_SIZE)
        return avr->data[addr];
    return 0;
}

// Write data memory
static inline void avr_write_data(AvrCore *avr, uint16_t addr, uint8_t val) {
    if (addr < AVR_DATA_SIZE)
        avr->data[addr] = val;

    if (addr == REG_SREG) { avr->sreg = val; return; }
    if (addr == REG_SPH)  { avr->sp = (avr->sp & 0x00FF) | ((uint16_t)val << 8); return; }
    if (addr == REG_SPL)  { avr->sp = (avr->sp & 0xFF00) | val; return; }

    if (avr->io_write_cb && addr < 0x100)
        avr->io_write_cb(avr, addr, val);
}

// Read 16-bit from data memory (little endian)
static inline uint16_t avr_read_data16(AvrCore *avr, uint16_t addr) {
    return avr_read_data(avr, addr) | (avr_read_data(avr, addr + 1) << 8);
}

// Write 16-bit to data memory (little endian)
static inline void avr_write_data16(AvrCore *avr, uint16_t addr, uint16_t val) {
    avr_write_data(avr, addr, val & 0xFF);
    avr_write_data(avr, addr + 1, (val >> 8) & 0xFF);
}

// Read flash (word addressed)
static inline uint16_t avr_read_flash16(AvrCore *avr, uint32_t word_addr) {
    uint32_t byte_addr = word_addr * 2;
    if (byte_addr + 1 < avr->flash_size)
        return avr->flash[byte_addr] | (avr->flash[byte_addr + 1] << 8);
    return 0xFFFF;
}

// Get SREG flags
static inline bool avr_get_flag(AvrCore *avr, uint8_t mask) {
    return (avr->sreg & mask) != 0;
}

// Set SREG flags
static inline void avr_set_flag(AvrCore *avr, uint8_t mask, bool val) {
    if (val)
        avr->sreg |= mask;
    else
        avr->sreg &= ~mask;
}

// Update zero flag
static inline void avr_update_z(AvrCore *avr, uint8_t result) {
    avr_set_flag(avr, SREG_Z, result == 0);
}

// Update negative flag
static inline void avr_update_n(AvrCore *avr, uint8_t result) {
    avr_set_flag(avr, SREG_N, (result & 0x80) != 0);
}

// Update sign flag (N xor V)
static inline void avr_update_s(AvrCore *avr) {
    bool n = avr_get_flag(avr, SREG_N);
    bool v = avr_get_flag(avr, SREG_V);
    avr_set_flag(avr, SREG_S, n != v);
}

#endif // AVR_CORE_H
