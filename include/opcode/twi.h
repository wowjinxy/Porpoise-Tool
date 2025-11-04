/**
 * @file twi.h
 * @brief TWI - Trap Word Immediate
 * 
 * Opcode: 3
 * Format: D-form
 * Syntax: twi TO, rA, SIMM
 * 
 * Description: Trap if condition is met with immediate comparison
 */

#ifndef OPCODE_TWI_H
#define OPCODE_TWI_H

#include <stdint.h>
#include <stdbool.h>

#define OP_TWI 3

typedef struct {
    uint8_t TO;
    uint8_t rA;
    int16_t SIMM;
} TWI_Instruction;

static inline bool decode_twi(uint32_t inst, TWI_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_TWI) return false;
    
    d->TO = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->SIMM = inst & 0xFFFF;
    return true;
}

static inline int transpile_twi(const TWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* twi %u, r%u, 0x%x - trap immediate (no-op in C) */",
                   d->TO, d->rA, (uint16_t)d->SIMM);
}

static inline int comment_twi(const TWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "twi %u, r%u, 0x%x", d->TO, d->rA, (uint16_t)d->SIMM);
}

#endif // OPCODE_TWI_H

