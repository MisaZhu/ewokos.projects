/*
 * AVR Core Simulator - Instruction Set Implementation
 * 
 * Implements the AVR 8-bit RISC instruction set (~131 instructions).
 * Each instruction returns the number of clock cycles consumed.
 *
 * Decoding is organised as one if/else chain over mutually exclusive
 * mask/value pairs, ordered so that no instruction can be claimed by an
 * earlier, looser test.  The AVR opcode space is dense and the groups
 * interleave in non-obvious ways - the 0x94xx/0x95xx block alone holds
 * BSET/BCLR, IJMP/ICALL, JMP/CALL, the single-register ALU ops, RET/RETI/
 * SLEEP/BREAK/WDR/SPM, ADIW/SBIW, CBI/SBI/SBIC/SBIS and MUL - so every
 * mask below is the narrowest one that still covers its group.  Widening a
 * mask silently swallows a neighbouring instruction, and order matters just
 * as much: an earlier revision of this file tested the 0x94xx block early in
 * the chain, which claimed BSET/BCLR, IJMP, ICALL, JMP, CALL and the whole
 * RET/RETI/SLEEP group along with the register ALU ops, and left RETI, INC,
 * DEC, SWAP and JMP as no-ops - enough on its own to stop any firmware from
 * running at all.  The same mask appears below, but only after each of those
 * forms has been claimed by a narrower test of its own.
 */

#include "avr_core.h"
#include <stdio.h>

// Helper macros
#define REG(r)      avr->reg[(r) & 31]
#define REG16(r)    (avr->reg[(r) & 31] | (avr->reg[((r) + 1) & 31] << 8))
#define SET_REG16(r, v) do { avr->reg[(r) & 31] = (v) & 0xFF; avr->reg[((r) + 1) & 31] = ((v) >> 8) & 0xFF; } while(0)

#define FLASH16(addr) avr_read_flash16(avr, addr)
#define NEXT_WORD()   FLASH16(avr->pc++)

#define PUSH8(v) do { avr_write_data(avr, avr->sp, (v)); avr->sp--; } while(0)
#define POP8()     ({ uint8_t v = avr_read_data(avr, ++avr->sp); v; })
#define PUSH16(v)  do { PUSH8(((v) >> 8) & 0xFF); PUSH8((v) & 0xFF); } while(0)
#define POP16()    ({ uint16_t lo = POP8(); uint16_t hi = POP8(); (hi << 8) | lo; })

// Status register helpers
#define GET_C()  ((avr->sreg & SREG_C) ? 1 : 0)
#define GET_Z()  ((avr->sreg & SREG_Z) ? 1 : 0)
#define GET_N()  ((avr->sreg & SREG_N) ? 1 : 0)
#define GET_V()  ((avr->sreg & SREG_V) ? 1 : 0)
#define GET_S()  ((avr->sreg & SREG_S) ? 1 : 0)
#define GET_H()  ((avr->sreg & SREG_H) ? 1 : 0)
#define GET_T()  ((avr->sreg & SREG_T) ? 1 : 0)
#define GET_I()  ((avr->sreg & SREG_I) ? 1 : 0)

#define SET_C(v) avr_set_flag(avr, SREG_C, v)
#define SET_Z(v) avr_set_flag(avr, SREG_Z, v)
#define SET_N(v) avr_set_flag(avr, SREG_N, v)
#define SET_V(v) avr_set_flag(avr, SREG_V, v)
#define SET_S(v) avr_set_flag(avr, SREG_S, v)
#define SET_H(v) avr_set_flag(avr, SREG_H, v)
#define SET_T(v) avr_set_flag(avr, SREG_T, v)
#define SET_I(v) avr_set_flag(avr, SREG_I, v)

// Arithmetic helpers
static inline void calc_add_flags(AvrCore *avr, uint8_t rd, uint8_t rr, uint8_t result) {
    SET_H(((rd & 0x0F) + (rr & 0x0F)) > 0x0F);
    SET_C(((uint16_t)rd + rr) > 0xFF);
    SET_N(result & 0x80);
    SET_V((~(rd ^ rr) & (rd ^ result) & 0x80) != 0);
    SET_Z(result == 0);
    SET_S(GET_N() != GET_V());
}

static inline void calc_adc_flags(AvrCore *avr, uint8_t rd, uint8_t rr, uint8_t carry, uint8_t result) {
    SET_H(((rd & 0x0F) + (rr & 0x0F) + carry) > 0x0F);
    SET_C(((uint16_t)rd + rr + carry) > 0xFF);
    SET_N(result & 0x80);
    SET_V((~(rd ^ rr) & (rd ^ result) & 0x80) != 0);
    SET_Z(result == 0);
    SET_S(GET_N() != GET_V());
}

static inline void calc_sub_flags(AvrCore *avr, uint8_t rd, uint8_t rr, uint8_t result) {
    SET_H((rd & 0x0F) < (rr & 0x0F));
    SET_C(rd < rr);
    SET_N(result & 0x80);
    SET_V(((rd ^ rr) & (rd ^ result) & 0x80) != 0);
    SET_Z(result == 0);
    SET_S(GET_N() != GET_V());
}

// SBC/CPC differ from SUB/CP in one respect that matters for multi-byte
// compares: Z is left alone when the result is zero, so a chain of
// CPC/SBC/... can test a 32-bit value for equality with a single final
// BREQ.  Clearing Z here would break every such chain.
static inline void calc_sbc_flags(AvrCore *avr, uint8_t rd, uint8_t rr, uint8_t carry, uint8_t result) {
    SET_H((rd & 0x0F) < ((rr & 0x0F) + carry));
    SET_C(((uint16_t)rd - rr - carry) & 0x100);
    SET_N(result & 0x80);
    SET_V(((rd ^ rr) & (rd ^ result) & 0x80) != 0);
    if (result != 0) SET_Z(0);
    SET_S(GET_N() != GET_V());
}

static inline void calc_logic_flags(AvrCore *avr, uint8_t result) {
    SET_N(result & 0x80);
    SET_V(0);
    SET_Z(result == 0);
    SET_S(GET_N() != GET_V());
    SET_H(0);
}

void avr_core_init(AvrCore *avr) {
    memset(avr, 0, sizeof(AvrCore));
    avr->flash_size = AVR_FLASH_SIZE;
    avr->sram_size = AVR_SRAM_SIZE;
    avr->eeprom_size = AVR_EEPROM_SIZE;
    avr->sram_start = AVR_SRAM_START;
    memset(avr->eeprom, 0xFF, AVR_EEPROM_SIZE);  // EEPROM defaults to 0xFF
}

void avr_core_reset(AvrCore *avr) {
    avr->pc = 0;
    avr->sp = AVR_SRAM_START + avr->sram_size - 1;  // Stack at top of SRAM
    avr->sreg = 0;
    avr->cycle = 0;
    avr->sleeping = false;
    avr->halted = false;
    memset(avr->reg, 0, 32);
    memset(avr->irq_pending, 0, sizeof(avr->irq_pending));
    
    // Clear I/O registers (except some that have reset values)
    memset(avr->data, 0, 0x100);

    // Mirror the two registers the CPU keeps in dedicated fields, so that
    // firmware reading SREG/SP through the I/O window before writing them
    // sees the same value the core is using.
    avr->data[REG_SREG] = avr->sreg;
    avr->data[REG_SPH]  = (uint8_t)(avr->sp >> 8);
    avr->data[REG_SPL]  = (uint8_t)(avr->sp & 0xFF);
}

// Program memory is word addressed and pc is a word index, so every
// unconditional pc assignment is wrapped to the device's flash size.
static inline uint32_t pc_mask(AvrCore *avr) { return (avr->flash_size / 2) - 1; }

// Is this a two-word instruction?  The skip instructions (CPSE, SBIC, SBRS)
// must swallow the whole of the instruction they skip, and the only two-word
// forms are LDS, STS, JMP and CALL.
static inline bool instr_is_long(uint16_t instr) {
    if ((instr & 0xFE0F) == 0x9000) return true;   // LDS Rd,k
    if ((instr & 0xFE0F) == 0x9200) return true;   // STS k,Rr
    if ((instr & 0xFE0E) == 0x940C) return true;   // JMP k / CALL k
    return false;
}

// Number of cycles for a skipped instruction: 1 word -> 1 extra cycle,
// 2 words -> 2 extra cycles.
static inline int skip_cost(AvrCore *avr) {
    uint16_t next = FLASH16(avr->pc);
    if (!instr_is_long(next)) { avr->pc = (avr->pc + 1) & pc_mask(avr); return 1; }
    avr->pc = (avr->pc + 2) & pc_mask(avr);
    return 2;
}

// Take the highest priority pending interrupt.  Returns the cycles consumed
// by the interrupt response, or 0 if none was taken.
static int check_interrupts(AvrCore *avr) {
    int vec = -1;
    for (int i = 1; i < VEC_COUNT; i++) {
        if (avr->irq_pending[i]) { vec = i; break; }
    }
    if (vec < 0) return 0;

    // Any pending interrupt wakes the core from SLEEP, even one whose enable
    // bit is off or while I is clear - execution simply resumes at the
    // instruction after SLEEP in that case.
    if (avr->sleeping) avr->sleeping = false;

    if (!GET_I()) return 0;

    avr->irq_pending[vec] = false;
    PUSH16(avr->pc);
    SET_I(0);
    // Each vector slot is 2 words wide (an rjmp plus padding), so vector N
    // sits at word address 2*N.  VEC_TIMER0_OVF is 16, i.e. word 0x20, which
    // is exactly where the ATmega328P puts the Timer0 overflow handler.
    avr->pc = (uint32_t)vec * 2;
    return 4;
}

// Execute one instruction, pc already pointing at the following word.
// Returns the total cycles the instruction takes.
static int exec_instr(AvrCore *avr, uint16_t instr) {
    // Register operands common to the 0x0400-0x3FFF blocks: d is bits 8..4
    // and r is bit 9 (as the 5th bit) plus bits 3..0.
    #define RD()  ((instr >> 4) & 0x1F)
    #define RR()  ((uint8_t)(((instr >> 5) & 0x10) | (instr & 0x0F)))
    // Immediate blocks (0x3000-0x7FFF) only address R16-R31 and split K
    // across bits 11..8 and 3..0.
    #define RD16() (16 + ((instr >> 4) & 0x0F))
    #define KK()   ((uint8_t)((((instr >> 4) & 0xF0) | (instr & 0x0F))))

    /* ---- 0x0000-0x03FF: NOP, MOVW, MULS, MULSU/FMUL family ---- */
    if ((instr & 0xFF00) == 0x0100) {                       // MOVW Rd+1:Rd <- Rr+1:Rr
        uint8_t d = ((instr >> 4) & 0x0F) * 2;
        uint8_t r = (instr & 0x0F) * 2;
        REG(d) = REG(r);
        REG(d + 1) = REG(r + 1);
        return 1;
    }
    else if ((instr & 0xFF00) == 0x0200) {                  // MULS Rd,Rr (signed)
        uint8_t d = 16 + ((instr >> 4) & 0x0F);
        uint8_t r = 16 + (instr & 0x0F);
        int16_t result = (int16_t)((int8_t)REG(d) * (int8_t)REG(r));
        REG(0) = result & 0xFF;
        REG(1) = (uint8_t)((result >> 8) & 0xFF);
        SET_C((result >> 15) & 1);
        SET_Z(result == 0);
        return 1;
    }
    else if ((instr & 0xFF88) == 0x0300) {                  // MULSU / FMUL / FMULS / FMULSU
        uint8_t d = 16 + ((instr >> 4) & 0x07);
        uint8_t r = 16 + (instr & 0x07);
        int16_t result;
        if (!(instr & 0x0080)) {
            if (!(instr & 0x0008))                          // MULSU: signed * unsigned
                result = (int16_t)((int8_t)REG(d) * (uint8_t)REG(r));
            else                                            // FMUL: unsigned, shifted
                result = (int16_t)((uint16_t)(REG(d) * REG(r)) << 1);
        } else {
            if (!(instr & 0x0008))                          // FMULS: signed, shifted
                result = (int16_t)((int8_t)REG(d) * (int8_t)REG(r)) << 1;
            else                                            // FMULSU: signed*unsigned, shifted
                result = (int16_t)((int8_t)REG(d) * (uint8_t)REG(r)) << 1;
        }
        REG(0) = result & 0xFF;
        REG(1) = (uint8_t)((result >> 8) & 0xFF);
        SET_C((result >> 15) & 1);
        SET_Z(result == 0);
        return 1;
    }

    /* ---- 0x0400-0x2FFF: two-register ALU, mask 0xFC00 ---- */
    else if ((instr & 0xFC00) == 0x0400) {                  // CPC Rd,Rr
        uint8_t d = RD(), r = RR(), carry = GET_C();
        uint8_t result = REG(d) - REG(r) - carry;
        calc_sbc_flags(avr, REG(d), REG(r), carry, result);
        return 1;
    }
    else if ((instr & 0xFC00) == 0x0800) {                  // SBC Rd,Rr
        uint8_t d = RD(), r = RR(), carry = GET_C();
        uint8_t result = REG(d) - REG(r) - carry;
        calc_sbc_flags(avr, REG(d), REG(r), carry, result);
        REG(d) = result;
        return 1;
    }
    else if ((instr & 0xFC00) == 0x0C00) {                  // ADD Rd,Rr
        uint8_t d = RD(), r = RR();
        uint8_t result = REG(d) + REG(r);
        calc_add_flags(avr, REG(d), REG(r), result);
        REG(d) = result;
        return 1;
    }
    else if ((instr & 0xFC00) == 0x1000) {                  // CPSE Rd,Rr
        uint8_t d = RD(), r = RR();
        if (REG(d) == REG(r)) return 1 + skip_cost(avr);
        return 1;
    }
    else if ((instr & 0xFC00) == 0x1400) {                  // CP Rd,Rr
        uint8_t d = RD(), r = RR();
        uint8_t result = REG(d) - REG(r);
        calc_sub_flags(avr, REG(d), REG(r), result);
        return 1;
    }
    else if ((instr & 0xFC00) == 0x1800) {                  // SUB Rd,Rr
        uint8_t d = RD(), r = RR();
        uint8_t result = REG(d) - REG(r);
        calc_sub_flags(avr, REG(d), REG(r), result);
        REG(d) = result;
        return 1;
    }
    else if ((instr & 0xFC00) == 0x1C00) {                  // ADC Rd,Rr
        uint8_t d = RD(), r = RR(), carry = GET_C();
        uint8_t result = REG(d) + REG(r) + carry;
        calc_adc_flags(avr, REG(d), REG(r), carry, result);
        REG(d) = result;
        return 1;
    }
    else if ((instr & 0xFC00) == 0x2000) {                  // AND Rd,Rr
        uint8_t d = RD(), r = RR();
        REG(d) &= REG(r);
        calc_logic_flags(avr, REG(d));
        return 1;
    }
    else if ((instr & 0xFC00) == 0x2400) {                  // EOR Rd,Rr
        uint8_t d = RD(), r = RR();
        REG(d) ^= REG(r);
        calc_logic_flags(avr, REG(d));
        return 1;
    }
    else if ((instr & 0xFC00) == 0x2800) {                  // OR Rd,Rr
        uint8_t d = RD(), r = RR();
        REG(d) |= REG(r);
        calc_logic_flags(avr, REG(d));
        return 1;
    }
    else if ((instr & 0xFC00) == 0x2C00) {                  // MOV Rd,Rr
        REG(RD()) = REG(RR());
        return 1;
    }

    /* ---- 0x3000-0x7FFF: immediate ALU, mask 0xF000 ---- */
    else if ((instr & 0xF000) == 0x3000) {                  // CPI Rd,K
        uint8_t d = RD16(), k = KK();
        uint8_t result = REG(d) - k;
        calc_sub_flags(avr, REG(d), k, result);
        return 1;
    }
    else if ((instr & 0xF000) == 0x4000) {                  // SBCI Rd,K
        uint8_t d = RD16(), k = KK(), carry = GET_C();
        uint8_t result = REG(d) - k - carry;
        calc_sbc_flags(avr, REG(d), k, carry, result);
        REG(d) = result;
        return 1;
    }
    else if ((instr & 0xF000) == 0x5000) {                  // SUBI Rd,K
        uint8_t d = RD16(), k = KK();
        uint8_t result = REG(d) - k;
        calc_sub_flags(avr, REG(d), k, result);
        REG(d) = result;
        return 1;
    }
    else if ((instr & 0xF000) == 0x6000) {                  // ORI/SBR Rd,K
        uint8_t d = RD16(), k = KK();
        REG(d) |= k;
        calc_logic_flags(avr, REG(d));
        return 1;
    }
    else if ((instr & 0xF000) == 0x7000) {                  // ANDI/CBR Rd,K
        uint8_t d = RD16(), k = KK();
        REG(d) &= k;
        calc_logic_flags(avr, REG(d));
        return 1;
    }

    /* ---- LDD/STD with 6-bit displacement (0x8000-0x8FFF, 0xA000-0xAFFF) ----
     * 10q0 qqsd dddd Yqqq : bit3 selects Y(1)/Z(0), bit9 selects store(1).
     * The displacement is scattered: q5=bit13, q4=bit11, q3=bit10, q2..q0=bits2..0.
     * Only bits 15, 14 and 12 are constant, hence the 0xD000 mask; pinning bit9
     * or bit3 in it would turn every STD and every Y-indexed form into a NOP.
     */
    else if ((instr & 0xD000) == 0x8000) {
        uint8_t d = RD();
        uint8_t q = (uint8_t)(((instr >> 8) & 0x20) | ((instr >> 7) & 0x18) | (instr & 0x07));
        uint16_t base = (instr & 0x0008) ? REG16(28) : REG16(30);
        uint16_t addr = (uint16_t)(base + q);
        if (instr & 0x0200) avr_write_data(avr, addr, REG(d));   // STD
        else                REG(d) = avr_read_data(avr, addr);   // LDD
        return 2;
    }

    /* ---- 0x9000-0x91FF: LDS and the direct/indirect loads ----
     * Mask is 0xFE00, not 0xFE0F: the low nibble is the sub-opcode
     * (0=LDS, 1=LD Z+, 4=LPM, 9=LD Y+, C=LD X, F=POP, ...) and must survive
     * the group test to reach the switch below.
     */
    else if ((instr & 0xFE00) == 0x9000) {
        uint8_t d = RD();
        switch (instr & 0x000F) {
        case 0x00: {                                        // LDS Rd,k (two words)
            uint16_t k = NEXT_WORD();
            REG(d) = avr_read_data(avr, k);
            return 2;
        }
        case 0x01:                                          // LD Rd,Z+
            REG(d) = avr_read_data(avr, REG16(30));
            SET_REG16(30, REG16(30) + 1);
            return 2;
        case 0x02:                                          // LD Rd,-Z
            SET_REG16(30, REG16(30) - 1);
            REG(d) = avr_read_data(avr, REG16(30));
            return 2;
        case 0x04:                                          // LPM Rd,Z
            REG(d) = avr->flash[REG16(30) & (avr->flash_size - 1)];
            return 3;
        case 0x05: {                                        // LPM Rd,Z+
            uint16_t z = REG16(30);
            REG(d) = avr->flash[z & (avr->flash_size - 1)];
            SET_REG16(30, z + 1);
            return 3;
        }
        case 0x06:                                          // ELPM Rd,Z (RAMPZ not modelled)
            REG(d) = avr->flash[REG16(30) & (avr->flash_size - 1)];
            return 3;
        case 0x07: {                                        // ELPM Rd,Z+
            uint16_t z = REG16(30);
            REG(d) = avr->flash[z & (avr->flash_size - 1)];
            SET_REG16(30, z + 1);
            return 3;
        }
        case 0x09:                                          // LD Rd,Y+
            REG(d) = avr_read_data(avr, REG16(28));
            SET_REG16(28, REG16(28) + 1);
            return 2;
        case 0x0A:                                          // LD Rd,-Y
            SET_REG16(28, REG16(28) - 1);
            REG(d) = avr_read_data(avr, REG16(28));
            return 2;
        case 0x0C:                                          // LD Rd,X
            REG(d) = avr_read_data(avr, REG16(26));
            return 2;
        case 0x0D:                                          // LD Rd,X+
            REG(d) = avr_read_data(avr, REG16(26));
            SET_REG16(26, REG16(26) + 1);
            return 2;
        case 0x0E:                                          // LD Rd,-X
            SET_REG16(26, REG16(26) - 1);
            REG(d) = avr_read_data(avr, REG16(26));
            return 2;
        case 0x0F:                                          // POP Rd
            REG(d) = POP8();
            return 2;
        }
    }

    /* ---- 0x9200-0x93FF: STS and the direct/indirect stores ---- */
    else if ((instr & 0xFE00) == 0x9200) {
        uint8_t d = RD();
        switch (instr & 0x000F) {
        case 0x00: {                                        // STS k,Rr (two words)
            uint16_t k = NEXT_WORD();
            avr_write_data(avr, k, REG(d));
            return 2;
        }
        case 0x01:                                          // ST Z+,Rr
            avr_write_data(avr, REG16(30), REG(d));
            SET_REG16(30, REG16(30) + 1);
            return 2;
        case 0x02:                                          // ST -Z,Rr
            SET_REG16(30, REG16(30) - 1);
            avr_write_data(avr, REG16(30), REG(d));
            return 2;
        case 0x09:                                          // ST Y+,Rr
            avr_write_data(avr, REG16(28), REG(d));
            SET_REG16(28, REG16(28) + 1);
            return 2;
        case 0x0A:                                          // ST -Y,Rr
            SET_REG16(28, REG16(28) - 1);
            avr_write_data(avr, REG16(28), REG(d));
            return 2;
        case 0x0C:                                          // ST X,Rr
            avr_write_data(avr, REG16(26), REG(d));
            return 2;
        case 0x0D:                                          // ST X+,Rr
            avr_write_data(avr, REG16(26), REG(d));
            SET_REG16(26, REG16(26) + 1);
            return 2;
        case 0x0E:                                          // ST -X,Rr
            SET_REG16(26, REG16(26) - 1);
            avr_write_data(avr, REG16(26), REG(d));
            return 2;
        case 0x0F:                                          // PUSH Rr
            PUSH8(REG(d));
            return 2;
        }
    }

    /* ---- RET / RETI / SLEEP / BREAK / WDR / SPM (0x95x8) ---- */
    else if ((instr & 0xFF0F) == 0x9508) {
        switch ((instr >> 4) & 0x0F) {
        case 0x00:                                          // RET
            avr->pc = POP16();
            return 4;
        case 0x01:                                          // RETI
            avr->pc = POP16();
            SET_I(1);
            return 4;
        case 0x08:                                          // SLEEP
            avr->sleeping = true;
            return 1;
        case 0x09:                                          // BREAK
            avr->halted = true;
            return 1;
        case 0x0A:                                          // WDR (watchdog not modelled)
            return 1;
        case 0x0C:                                          // LPM (R0 <- flash[Z])
            REG(0) = avr->flash[REG16(30) & (avr->flash_size - 1)];
            return 3;
        case 0x0D:                                          // ELPM (RAMPZ not modelled)
            REG(0) = avr->flash[REG16(30) & (avr->flash_size - 1)];
            return 3;
        case 0x0E:                                          // SPM
        case 0x0F:                                          // SPM Z+
            return 1;
        }
    }

    /* ---- BSET/BCLR (SEI, CLI, SEC, CLZ, ...): 0x9408 | s<<4, bit7 = clear ---- */
    else if ((instr & 0xFF8F) == 0x9408) {
        uint8_t s = (instr >> 4) & 0x07;
        if (instr & 0x0080) avr->sreg &= (uint8_t)~(1 << s);
        else                avr->sreg |=  (uint8_t)(1 << s);
        return 1;
    }
    else if (instr == 0x9409) {                             // IJMP
        avr->pc = REG16(30) & pc_mask(avr);
        return 2;
    }
    else if (instr == 0x940D) {                             // ICALL
        PUSH16(avr->pc);
        avr->pc = REG16(30) & pc_mask(avr);
        return 3;
    }
    /* ---- JMP / CALL (two words).  bit1 tells them apart, bit0 is k16. ---- */
    else if ((instr & 0xFE0E) == 0x940C) {
        uint32_t addr = ((uint32_t)(instr & 0x01F0) << 13)
                      | ((uint32_t)(instr & 0x0001) << 16)
                      | NEXT_WORD();
        addr &= pc_mask(avr);
        if (instr & 0x0002) {                               // CALL k
            PUSH16(avr->pc);
            avr->pc = addr;
            return 4;
        }
        avr->pc = addr;                                     // JMP k
        return 3;
    }
    /* ---- single-register ALU: 0x9400 | d<<4 | subop ---- */
    else if ((instr & 0xFE00) == 0x9400) {
        uint8_t d = RD();
        switch (instr & 0x000F) {
        case 0x00:                                          // COM Rd
            REG(d) = (uint8_t)(0xFF - REG(d));
            SET_N(REG(d) & 0x80);
            SET_V(0);
            SET_Z(REG(d) == 0);
            SET_S(GET_N() != GET_V());
            SET_C(1);
            return 1;
        case 0x01: {                                        // NEG Rd
            uint8_t old = REG(d);
            REG(d) = (uint8_t)(0 - old);
            SET_H(((REG(d) | old) & 0x08) != 0);
            SET_C(REG(d) != 0);
            SET_V(REG(d) == 0x80);
            SET_N(REG(d) & 0x80);
            SET_Z(REG(d) == 0);
            SET_S(GET_N() != GET_V());
            return 1;
        }
        case 0x02:                                          // SWAP Rd
            REG(d) = (uint8_t)(((REG(d) >> 4) & 0x0F) | ((REG(d) << 4) & 0xF0));
            return 1;
        case 0x03:                                          // INC Rd
            REG(d)++;
            SET_V(REG(d) == 0x80);
            SET_N(REG(d) & 0x80);
            SET_Z(REG(d) == 0);
            SET_S(GET_N() != GET_V());
            return 1;
        case 0x05:                                          // ASR Rd
            SET_C(REG(d) & 0x01);
            REG(d) = (uint8_t)((REG(d) >> 1) | (REG(d) & 0x80));
            SET_N(REG(d) & 0x80);
            SET_V(GET_N() != GET_C());
            SET_Z(REG(d) == 0);
            SET_S(GET_N() != GET_V());
            return 1;
        case 0x06:                                          // LSR Rd
            SET_C(REG(d) & 0x01);
            REG(d) = (uint8_t)(REG(d) >> 1);
            SET_N(0);
            SET_V(GET_C());
            SET_Z(REG(d) == 0);
            SET_S(GET_N() != GET_V());
            return 1;
        case 0x07: {                                        // ROR Rd
            uint8_t old_c = GET_C();
            SET_C(REG(d) & 0x01);
            REG(d) = (uint8_t)((REG(d) >> 1) | (old_c << 7));
            SET_N(REG(d) & 0x80);
            SET_V(GET_N() != GET_C());
            SET_Z(REG(d) == 0);
            SET_S(GET_N() != GET_V());
            return 1;
        }
        case 0x0A:                                          // DEC Rd
            REG(d)--;
            SET_V(REG(d) == 0x7F);
            SET_N(REG(d) & 0x80);
            SET_Z(REG(d) == 0);
            SET_S(GET_N() != GET_V());
            return 1;
        }
    }
    /* ---- ADIW / SBIW: only R24-R31, 6-bit immediate ---- */
    else if ((instr & 0xFF00) == 0x9600 || (instr & 0xFF00) == 0x9700) {
        bool     sub = (instr & 0x0100) != 0;
        uint8_t  d   = (uint8_t)(24 + ((instr >> 4) & 0x03) * 2);
        uint8_t  k   = (uint8_t)(((instr >> 2) & 0x30) | (instr & 0x0F));
        uint16_t old = REG16(d);
        uint16_t res = sub ? (uint16_t)(old - k) : (uint16_t)(old + k);
        SET_REG16(d, res);
        SET_N((res >> 15) & 1);
        SET_Z(res == 0);
        if (sub) { SET_V(((~res) & old) >> 15);  SET_C((res & (~old)) >> 15); }
        else     { SET_V((res & (~old)) >> 15);  SET_C(((~res) & old) >> 15); }
        SET_S(GET_N() != GET_V());
        return 2;
    }
    /* ---- CBI / SBIC / SBI / SBIS: 5-bit I/O address, so 0x20 + bits 7..3 ---- */
    else if ((instr & 0xFC00) == 0x9800) {
        uint16_t io  = (uint16_t)(0x20 + ((instr >> 3) & 0x1F));
        uint8_t  bit = (uint8_t)(instr & 0x07);
        uint8_t  val = avr_read_data(avr, io);
        switch ((instr >> 8) & 0x03) {
        case 0x00:                                          // CBI A,b
            avr_write_data(avr, io, (uint8_t)(val & ~(1 << bit)));
            return 2;
        case 0x01:                                          // SBIC A,b (skip if clear)
            if (!(val & (1 << bit))) return 1 + skip_cost(avr);
            return 1;
        case 0x02:                                          // SBI A,b
            avr_write_data(avr, io, (uint8_t)(val | (1 << bit)));
            return 2;
        case 0x03:                                          // SBIS A,b (skip if set)
            if (val & (1 << bit)) return 1 + skip_cost(avr);
            return 1;
        }
    }
    else if ((instr & 0xFC00) == 0x9C00) {                  // MUL Rd,Rr (unsigned)
        uint16_t result = (uint16_t)(REG(RD()) * REG(RR()));
        REG(0) = result & 0xFF;
        REG(1) = (uint8_t)((result >> 8) & 0xFF);
        SET_C((result >> 15) & 1);
        SET_Z(result == 0);
        return 2;
    }

    /* ---- IN / OUT: 6-bit I/O address, bits 9 and 3..0 ---- */
    else if ((instr & 0xF800) == 0xB000) {                  // IN Rd,A
        uint8_t a = (uint8_t)(((instr >> 5) & 0x30) | (instr & 0x0F));
        REG(RD()) = avr_read_data(avr, (uint16_t)(0x20 + a));
        return 1;
    }
    else if ((instr & 0xF800) == 0xB800) {                  // OUT A,Rr
        uint8_t a = (uint8_t)(((instr >> 5) & 0x30) | (instr & 0x0F));
        avr_write_data(avr, (uint16_t)(0x20 + a), REG(RD()));
        return 1;
    }

    /* ---- relative jumps and LDI ---- */
    else if ((instr & 0xF000) == 0xC000) {                  // RJMP k
        int16_t off = (int16_t)(instr << 4) >> 4;           // sign extend 12 bits
        avr->pc = (avr->pc + off) & pc_mask(avr);
        return 2;
    }
    else if ((instr & 0xF000) == 0xD000) {                  // RCALL k
        int16_t off = (int16_t)(instr << 4) >> 4;
        PUSH16(avr->pc);
        avr->pc = (avr->pc + off) & pc_mask(avr);
        return 3;
    }
    else if ((instr & 0xF000) == 0xE000) {                  // LDI Rd,K (R16-R31)
        REG(RD16()) = KK();
        return 1;
    }

    /* ---- conditional branches: 7-bit signed offset in bits 9..3 ---- */
    else if ((instr & 0xFC00) == 0xF000 || (instr & 0xFC00) == 0xF400) {
        int8_t  off = (int8_t)((instr >> 3) & 0x7F);
        if (off & 0x40) off = (int8_t)(off - 0x80);
        uint8_t s    = (uint8_t)(instr & 0x07);
        // bit10 = 0 -> BRBS (branch if the flag is Set),
        // bit10 = 1 -> BRBC (branch if the flag is Cleared).
        // This is the reverse of what the mnemonic order suggests, and getting
        // it backwards inverts every conditional branch in the machine: the
        // crt0 data-copy loop falls straight through, main() returns at once
        // and the core parks in the exit loop's "rjmp .-1".
        bool    set  = (instr & 0x0400) == 0;
        if (((avr->sreg >> s) & 1) == (set ? 1 : 0)) {
            avr->pc = (avr->pc + off) & pc_mask(avr);
            return 2;
        }
        return 1;
    }
    /* ---- BLD / BST ---- */
    else if ((instr & 0xFE08) == 0xF800 || (instr & 0xFE08) == 0xFA00) {
        uint8_t d = RD(), b = (uint8_t)(instr & 0x07);
        if (instr & 0x0200) SET_T((REG(d) >> b) & 1);                        // BST
        else REG(d) = (uint8_t)((REG(d) & ~(1 << b)) | (GET_T() ? (1 << b) : 0));  // BLD
        return 1;
    }
    /* ---- SBRC / SBRS ---- */
    else if ((instr & 0xFE08) == 0xFC00 || (instr & 0xFE08) == 0xFE00) {
        uint8_t r = RD(), b = (uint8_t)(instr & 0x07);
        bool    bit_set = ((REG(r) >> b) & 1) != 0;
        bool    is_sbrs = (instr & 0x0200) != 0;
        if (bit_set == is_sbrs) return 1 + skip_cost(avr);
        return 1;
    }

    #undef RD
    #undef RR
    #undef RD16
    #undef KK

    // Unimplemented (XBREAK, DES, XMEGA-only XCH/LAS/LAC/LAT, ...).  Counted
    // as a plain cycle so a stray one cannot wedge the core.
    return 1;
}

// Main instruction execution
int avr_core_step(AvrCore *avr) {
    if (avr->halted) return 1;
    
    // Check interrupts first
    int irq_cycles = check_interrupts(avr);
    if (irq_cycles) {
        avr->cycle += irq_cycles;
        return irq_cycles;
    }
    
    if (avr->sleeping) {
        avr->cycle++;
        return 1;
    }
    
    uint16_t instr = FLASH16(avr->pc);
    avr->pc = (avr->pc + 1) & pc_mask(avr);

    int cycles = exec_instr(avr, instr);

    avr->cycle += cycles;
    return cycles;
}
