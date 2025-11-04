/**
 * @file subfic.h
 * @brief SUBFIC - Subtract From Immediate Carrying
 * Opcode: 8
 */

#ifndef OPCODE_SUBFIC_H
#define OPCODE_SUBFIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_SUBFIC 8

typedef struct {
    uint8_t rD, rA;
    int16_t SIMM;
} SUBFIC_Instruction;

static inline bool decode_subfic(uint32_t inst, SUBFIC_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 8) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->SIMM = inst & 0xFFFF;
    return true;
}

static inline int transpile_subfic(const SUBFIC_Instruction *d, char *o, size_t s) {
    // rD = SIMM - rA, set CA
    return snprintf(o, s, "r%u = 0x%x - r%u; "
                   "xer = (xer & ~0x20000000) | ((r%u <= 0x%x) ? 0x20000000 : 0);",
                   d->rD, (uint16_t)d->SIMM, d->rA, d->rA, (uint16_t)d->SIMM);
}

static inline int comment_subfic(const SUBFIC_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "subfic r%u, r%u, 0x%x", d->rD, d->rA, (uint16_t)d->SIMM);
}

#endif

