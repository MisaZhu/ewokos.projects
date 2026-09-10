/*
 * AVR Device Definitions and Peripheral Simulation Implementation
 */

#include "avr_devs.h"
#include <string.h>

// Device definitions
const AvrDevice avr_devices[] = {
    { "atmega328p",  32768, 2048, 1024, 3, 3, 1 },
    { "atmega328",   32768, 2048, 1024, 3, 3, 1 },
    { "atmega168",   16384, 1024,  512, 3, 3, 1 },
    { "atmega88",     8192, 1024,  512, 3, 3, 1 },
    { "atmega48",     4096,  512,  256, 3, 3, 1 },
    { "atmega2560", 262144, 8192, 4096, 5, 6, 4 },
    { "atmega1280", 131072, 8192, 4096, 5, 6, 4 },
    { NULL, 0, 0, 0, 0, 0, 0 }
};

const AvrDevice* avr_device_find(const char *name) {
    if (!name) return &avr_devices[0];  // Default to ATmega328P
    
    // Simple case-insensitive comparison
    for (int i = 0; avr_devices[i].name; i++) {
        const char *a = name;
        const char *b = avr_devices[i].name;
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
            if (ca != cb) break;
            a++; b++;
        }
        if (*a == 0 && *b == 0) {
            return &avr_devices[i];
        }
    }
    return NULL;
}

// Peripheral data stored in AVR core's data array
// We use a simple approach: store peripheral state in the I/O registers directly

// GPIO helper functions
void avr_gpio_set_pin(AvrCore *avr, uint8_t port, uint8_t pin, bool value) {
    if (port > 2) return;  // Only ports B, C, D
    
    uint8_t pin_reg = REG_PINB + port * 3;
    uint8_t ddr_reg = REG_DDRB + port * 3;

    uint8_t ddr = avr->data[ddr_reg];

    // Only inputs are driven from the circuit; on an output pin the MCU owns
    // the pad and an external value must not overwrite it.  The PORT bit for
    // an input selects the internal pull-up, which the circuit resolves before
    // calling this - it is deliberately not consulted here.
    if (!(ddr & (1 << pin))) {
        if (value)
            avr->data[pin_reg] |= (1 << pin);
        else
            avr->data[pin_reg] &= ~(1 << pin);
    }
}

bool avr_gpio_get_pin(AvrCore *avr, uint8_t port, uint8_t pin) {
    if (port > 2) return false;
    
    uint8_t ddr_reg = REG_DDRB + port * 3;
    uint8_t port_reg = REG_PORTB + port * 3;
    uint8_t pin_reg = REG_PINB + port * 3;
    
    uint8_t ddr = avr->data[ddr_reg];
    
    // If output, return PORT value; if input, return PIN value
    if (ddr & (1 << pin)) {
        return (avr->data[port_reg] >> pin) & 1;
    } else {
        return (avr->data[pin_reg] >> pin) & 1;
    }
}

void avr_gpio_set_ddr(AvrCore *avr, uint8_t port, uint8_t ddr) {
    if (port > 2) return;
    avr->data[REG_DDRB + port * 3] = ddr;
}

uint8_t avr_gpio_get_port(AvrCore *avr, uint8_t port) {
    if (port > 2) return 0;
    return avr->data[REG_PORTB + port * 3];
}

// USART functions
void avr_usart_receive(AvrCore *avr, uint8_t byte) {
    // Store in UDR and set RXC flag
    avr->data[REG_UDR0] = byte;
    avr->data[REG_UCSR0A] |= 0x80;  // RXC0
    
    // Request interrupt if enabled
    if (avr->data[REG_UCSR0B] & 0x80) {  // RXCIE0
        avr_irq_request(avr, VEC_USART_RX);
    }
}

static AvrUsartTxCallback usart_tx_cb = NULL;
static void *usart_tx_data = NULL;

void avr_usart_set_tx_callback(AvrCore *avr, AvrUsartTxCallback cb, void *data) {
    (void)avr;
    usart_tx_cb = cb;
    usart_tx_data = data;
}

// Remaining cycles until the byte currently in the shift register has been
// clocked out, plus the byte itself.  0 means the transmitter is idle.
static uint32_t usart_tx_cycles = 0;
static uint8_t  usart_tx_byte   = 0;
static bool     usart_tx_emit   = true;

// One 10-bit frame (start + 8 data + stop) at the programmed baud rate.
// The bit time is 16 * (UBRR + 1) CPU cycles, or 8 * (UBRR + 1) in U2X mode;
// at 16MHz that makes UBRR=207 exactly the 9600 baud Arduino default.
static uint32_t usart_frame_cycles(AvrCore *avr) {
    uint16_t ubrr = (uint16_t)(avr->data[REG_UBRR0H] << 8) | avr->data[REG_UBRR0L];
    uint32_t per_bit = (avr->data[REG_UCSR0A] & 0x02) ? 8u : 16u;   // U2X0
    per_bit *= (uint32_t)ubrr + 1u;
    if (per_bit == 0) per_bit = 16;                                 // UBRR unset
    return per_bit * 10u;
}

// Timer functions
static AvrTimerCallback timer_cb = NULL;
static void *timer_cb_data = NULL;

void avr_timer_set_callback(AvrCore *avr, AvrTimerCallback cb, void *data) {
    (void)avr;
    timer_cb = cb;
    timer_cb_data = data;
}

// Interrupt functions
void avr_irq_request(AvrCore *avr, int vector) {
    if (vector > 0 && vector < VEC_COUNT) {
        avr->irq_pending[vector] = true;
    }
}

void avr_irq_clear(AvrCore *avr, int vector) {
    if (vector > 0 && vector < VEC_COUNT) {
        avr->irq_pending[vector] = false;
    }
}

// Prescaler phase counters.  Declared up here, ahead of avr_periph_reset(),
// which zeroes them: leaving them dirty across a restart gives the timers a
// head start on the next run.
static uint32_t timer0_presc_counter = 0;
static uint32_t timer1_presc_counter = 0;
static uint32_t timer2_presc_counter = 0;

// Peripheral initialization
void avr_periph_init(AvrCore *avr) {
    // Set up I/O write callback
    avr->io_write_cb = avr_periph_write;
    avr->io_write_data = avr;
    
    // Initialize USART TX callback
    avr->uart_tx_cb = NULL;
    avr->uart_tx_data = NULL;
}

void avr_periph_reset(AvrCore *avr) {
    // Reset I/O registers to default values
    // Most registers reset to 0, some have specific reset values
    
    // Timer registers
    avr->data[REG_TCCR0A] = 0;
    avr->data[REG_TCCR0B] = 0;
    avr->data[REG_TCNT0] = 0;
    avr->data[REG_OCR0A] = 0;
    avr->data[REG_OCR0B] = 0;
    avr->data[REG_TIMSK0] = 0;
    avr->data[REG_TIFR0] = 0;
    
    avr->data[REG_TCCR1A] = 0;
    avr->data[REG_TCCR1B] = 0;
    avr->data[REG_TCNT1L] = 0;
    avr->data[REG_TCNT1H] = 0;
    avr->data[REG_OCR1AL] = 0;
    avr->data[REG_OCR1AH] = 0;
    avr->data[REG_OCR1BL] = 0;
    avr->data[REG_OCR1BH] = 0;
    avr->data[REG_TIMSK1] = 0;
    avr->data[REG_TIFR1] = 0;
    
    avr->data[REG_TCCR2A] = 0;
    avr->data[REG_TCCR2B] = 0;
    avr->data[REG_TCNT2] = 0;
    avr->data[REG_OCR2A] = 0;
    avr->data[REG_OCR2B] = 0;
    avr->data[REG_TIMSK2] = 0;
    avr->data[REG_TIFR2] = 0;
    
    // USART
    // These are file-static so a reset has to clear them explicitly, otherwise
    // restarting a simulation leaves the previous run's prescaler phase and a
    // half-shifted TX byte in place.
    timer0_presc_counter = 0;
    timer1_presc_counter = 0;
    timer2_presc_counter = 0;
    usart_tx_cycles = 0;
    usart_tx_byte   = 0;
    usart_tx_emit   = true;

    avr->data[REG_UCSR0A] = 0x20;  // UDRE0 is set on reset
    avr->data[REG_UCSR0B] = 0;
    avr->data[REG_UCSR0C] = 0x06;  // UCSZ01:0 = 11 (8-bit)
    avr->data[REG_UBRR0L] = 0;
    avr->data[REG_UBRR0H] = 0;
    
    // GPIO
    avr->data[REG_DDRB] = 0;
    avr->data[REG_PORTB] = 0;
    avr->data[REG_PINB] = 0;
    avr->data[REG_DDRC] = 0;
    avr->data[REG_PORTC] = 0;
    avr->data[REG_PINC] = 0;
    avr->data[REG_DDRD] = 0;
    avr->data[REG_PORTD] = 0;
    avr->data[REG_PIND] = 0;
}

// Timer prescaler values
static const uint16_t timer0_prescaler[] = { 0, 1, 8, 64, 256, 1024, 0, 0 };
static const uint16_t timer1_prescaler[] = { 0, 1, 8, 64, 256, 1024, 0, 0 };
static const uint16_t timer2_prescaler[] = { 0, 1, 8, 32, 64, 128, 256, 1024 };

void avr_periph_step(AvrCore *avr, int cycles) {
    // Step Timer 0
    uint8_t tccr0b = avr->data[REG_TCCR0B];
    uint8_t cs0 = tccr0b & 0x07;
    if (cs0 > 0 && cs0 < 6) {
        timer0_presc_counter += cycles;
        uint16_t presc = timer0_prescaler[cs0];
        while (timer0_presc_counter >= presc) {
            timer0_presc_counter -= presc;
            avr->data[REG_TCNT0]++;
            
            // Check for overflow
            if (avr->data[REG_TCNT0] == 0) {
                avr->data[REG_TIFR0] |= 0x01;  // TOV0
                if (avr->data[REG_TIMSK0] & 0x01) {
                    avr_irq_request(avr, VEC_TIMER0_OVF);
                }
            }
            
            // Check compare match A
            if (avr->data[REG_TCNT0] == avr->data[REG_OCR0A]) {
                avr->data[REG_TIFR0] |= 0x02;  // OCF0A
                if (avr->data[REG_TIMSK0] & 0x02) {
                    avr_irq_request(avr, VEC_TIMER0_COMPA);
                }
                // CTC mode clears the counter on a compare match A.  WGM02:0
                // = 010 selects it: WGM01:0 are TCCR0A bits 1:0 and WGM02 is
                // TCCR0B bit 3, so CTC means that bit CLEAR.  Testing it as
                // set selected Fast PWM instead, where the counter runs free
                // to 0xFF, and the timer never restarted at OCR0A.
                if (!(tccr0b & 0x08) && (avr->data[REG_TCCR0A] & 0x03) == 2) {
                    avr->data[REG_TCNT0] = 0;
                }
            }
            
            // Check compare match B
            if (avr->data[REG_TCNT0] == avr->data[REG_OCR0B]) {
                avr->data[REG_TIFR0] |= 0x04;  // OCF0B
                if (avr->data[REG_TIMSK0] & 0x04) {
                    avr_irq_request(avr, VEC_TIMER0_COMPB);
                }
            }
        }
    }
    
    // Step Timer 1 (16-bit)
    uint8_t tccr1b = avr->data[REG_TCCR1B];
    uint8_t cs1 = tccr1b & 0x07;
    if (cs1 > 0 && cs1 < 6) {
        timer1_presc_counter += cycles;
        uint16_t presc = timer1_prescaler[cs1];
        while (timer1_presc_counter >= presc) {
            timer1_presc_counter -= presc;
            uint16_t tcnt1 = avr->data[REG_TCNT1L] | (avr->data[REG_TCNT1H] << 8);
            tcnt1++;
            avr->data[REG_TCNT1L] = tcnt1 & 0xFF;
            avr->data[REG_TCNT1H] = (tcnt1 >> 8) & 0xFF;
            
            // Check for overflow
            if (tcnt1 == 0) {
                avr->data[REG_TIFR1] |= 0x01;  // TOV1
                if (avr->data[REG_TIMSK1] & 0x01) {
                    avr_irq_request(avr, VEC_TIMER1_OVF);
                }
            }
            
            // Check compare match A
            uint16_t ocra1 = avr->data[REG_OCR1AL] | (avr->data[REG_OCR1AH] << 8);
            if (tcnt1 == ocra1) {
                avr->data[REG_TIFR1] |= 0x02;  // OCF1A
                if (avr->data[REG_TIMSK1] & 0x02) {
                    avr_irq_request(avr, VEC_TIMER1_COMPA);
                }
                // CTC mode
                if ((tccr1b & 0x18) == 0x08) {
                    tcnt1 = 0;
                    avr->data[REG_TCNT1L] = 0;
                    avr->data[REG_TCNT1H] = 0;
                }
            }
            
            // Check compare match B
            uint16_t ocrb1 = avr->data[REG_OCR1BL] | (avr->data[REG_OCR1BH] << 8);
            if (tcnt1 == ocrb1) {
                avr->data[REG_TIFR1] |= 0x04;  // OCF1B
                if (avr->data[REG_TIMSK1] & 0x04) {
                    avr_irq_request(avr, VEC_TIMER1_COMPB);
                }
            }
        }
    }
    
    // Step Timer 2
    uint8_t tccr2b = avr->data[REG_TCCR2B];
    uint8_t cs2 = tccr2b & 0x07;
    if (cs2 > 0) {
        timer2_presc_counter += cycles;
        uint16_t presc = timer2_prescaler[cs2];
        while (timer2_presc_counter >= presc) {
            timer2_presc_counter -= presc;
            avr->data[REG_TCNT2]++;
            
            // Check for overflow
            if (avr->data[REG_TCNT2] == 0) {
                avr->data[REG_TIFR2] |= 0x01;  // TOV2
                if (avr->data[REG_TIMSK2] & 0x01) {
                    avr_irq_request(avr, VEC_TIMER2_OVF);
                }
            }
            
            // Check compare match A
            if (avr->data[REG_TCNT2] == avr->data[REG_OCR2A]) {
                avr->data[REG_TIFR2] |= 0x02;  // OCF2A
                if (avr->data[REG_TIMSK2] & 0x02) {
                    avr_irq_request(avr, VEC_TIMER2_COMPA);
                }
            }
            
            // Check compare match B
            if (avr->data[REG_TCNT2] == avr->data[REG_OCR2B]) {
                avr->data[REG_TIFR2] |= 0x04;  // OCF2B
                if (avr->data[REG_TIMSK2] & 0x04) {
                    avr_irq_request(avr, VEC_TIMER2_COMPB);
                }
            }
        }
    }

    // Step USART0 transmit.  The byte sits in the shift register for one full
    // frame, then UDRE0 comes back and - if firmware enabled UDRIE0 - the UDRE
    // interrupt fires.  That interrupt is what Arduino's HardwareSerial uses
    // to drain its ring buffer, so without this Serial.print() stalls after
    // the first byte no matter how correct the decoder is.
    if (usart_tx_cycles > 0) {
        if ((uint32_t)cycles >= usart_tx_cycles) {
            usart_tx_cycles = 0;
            if (!usart_tx_emit) {
                // Hand the byte to the circuit only once the frame is out.
                usart_tx_emit = true;
                if (usart_tx_cb) usart_tx_cb(usart_tx_data, usart_tx_byte);
            }
            avr->data[REG_UCSR0A] |= 0x20 | 0x40;      // UDRE0, TXC0
            avr->irq_pending[VEC_USART_UDRE] = false;  // re-arm, then request
            if (avr->data[REG_UCSR0B] & 0x40)          // TXCIE0
                avr_irq_request(avr, VEC_USART_TX);
        } else {
            usart_tx_cycles -= (uint32_t)cycles;
            return;
        }
    }

    // The UDRE interrupt is level-triggered: it stays pending for as long as
    // the data register is empty and its enable bit is set.  Re-requesting it
    // every step is what hardware does, and is harmless because taking the
    // vector clears the pending bit.
    if ((avr->data[REG_UCSR0B] & 0x20) && (avr->data[REG_UCSR0A] & 0x20))
        avr_irq_request(avr, VEC_USART_UDRE);
}

void avr_periph_write(AvrCore *avr, uint16_t addr, uint8_t val) {
    // Handle special register writes
    
    // USART
    if (addr == REG_UDR0) {
        // The byte moves into the shift register, so UDR is no longer empty
        // and the previous frame is no longer complete.  avr_periph_step()
        // counts the frame down and restores UDRE0 when it ends.
        avr->data[REG_UDR0] = val;
        avr->data[REG_UCSR0A] &= (uint8_t)~(0x20 | 0x40);
        avr_irq_clear(avr, VEC_USART_UDRE);
        usart_tx_cycles = usart_frame_cycles(avr);
        usart_tx_byte   = val;
        usart_tx_emit   = false;
        return;
    }
    
    if (addr == REG_UCSR0A) {
        // UDRE0 (bit5) and RXC0 (bit7) are read-only: hardware sets them and
        // firmware cannot clear them by writing.  Arduino's USART0_init()
        // does "UCSR0A = 1<<U2X0", which writes a 0 into UDRE0 - real silicon
        // ignores it, and HardwareSerial::write() then relies on UDRE0 still
        // being set to take its "buffer empty and register ready" fast path.
        // Masking the bit OFF instead of preserving it deadlocked every
        // Serial.print(): write() buffered the byte, enabled UDRIE0 and spun
        // waiting for an UDRE interrupt that could only follow a UDR0 write
        // the spin loop never reached.
        uint8_t ro = (uint8_t)(avr->io_prev & (0x20 | 0x80));
        avr->data[REG_UCSR0A] = (uint8_t)((val & ~(0x20 | 0x80)) | ro);
        return;
    }
    
    if (addr == REG_UCSR0B) {
        avr->data[REG_UCSR0B] = val;
        // Datasheet: the UDRE interrupt is executed immediately if UDRE0 is
        // set at the moment UDRIE0 is written to one.
        if ((val & 0x20) && (avr->data[REG_UCSR0A] & 0x20))
            avr_irq_request(avr, VEC_USART_UDRE);
        if ((val & 0x80) && (avr->data[REG_UCSR0A] & 0x80))
            avr_irq_request(avr, VEC_USART_RX);
        return;
    }

    // Timer interrupt flags - write 1 to clear.  The new byte has already been
    // stored, so this has to work from io_prev: data[addr] & ~val would be
    // val & ~val, i.e. 0, wiping every flag whether firmware asked or not.
    if (addr == REG_TIFR0 || addr == REG_TIFR1 || addr == REG_TIFR2) {
        avr->data[addr] = (uint8_t)(avr->io_prev & ~val);
        return;
    }
    
    // EEPROM control
    if (addr == REG_EECR) {
        avr->data[REG_EECR] = val;
        
        // EERE - EEPROM Read Enable
        if (val & 0x01) {
            uint16_t eear = avr->data[REG_EEARL] | (avr->data[REG_EEARH] << 8);
            if (eear < AVR_EEPROM_SIZE) {
                avr->data[REG_EEDR] = avr->eeprom[eear];
            }
            avr->data[REG_EECR] &= ~0x01;  // Auto-clear EERE
        }
        
        // EEPE - EEPROM Program Enable
        if (val & 0x02) {
            uint16_t eear = avr->data[REG_EEARL] | (avr->data[REG_EEARH] << 8);
            if (eear < AVR_EEPROM_SIZE) {
                // Check EEMPE (Master Program Enable)
                if (val & 0x04) {
                    avr->eeprom[eear] = avr->data[REG_EEDR];
                }
            }
            avr->data[REG_EECR] &= ~0x02;  // Auto-clear EEPE
        }
        return;
    }
    
    // GPIO PORT registers - update PIN for output pins
    if (addr >= REG_PORTB && addr <= REG_PORTD) {
        uint8_t port_idx = (addr - REG_PORTB) / 3;
        uint8_t pin_reg = REG_PINB + port_idx * 3;
        uint8_t ddr_reg = REG_DDRB + port_idx * 3;
        
        avr->data[addr] = val;
        
        // For output pins, update PIN register
        uint8_t ddr = avr->data[ddr_reg];
        avr->data[pin_reg] = (avr->data[pin_reg] & ~ddr) | (val & ddr);
        return;
    }
    
    // Default: just write to data array
    // (already done by avr_write_data before callback)
}

uint8_t avr_periph_read(AvrCore *avr, uint16_t addr) {
    // avr_read_data() serves the CPU straight from avr->data[] and never calls
    // this, so nothing here may paper over a stale flag.  In particular the old
    // "UCSR0A always reports UDRE0 set" shortcut hid the fact that no USART
    // step existed: a polling driver appeared to work while an interrupt-driven
    // one (every Arduino sketch) deadlocked.  Read the modelled value.
    return avr_read_data(avr, addr);
}
