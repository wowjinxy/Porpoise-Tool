/**
 * @file ps_cmpo1.h
 * @brief PS_CMPO1 - Paired Single Compare Ordered Low
 * Opcode: 4 / 96
 */

#ifndef OPCODE_PS_CMPO1_H
#define OPCODE_PS_CMPO1_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t crfD, frA, frB;
} PS_CMPO1_Instruction;

static inline bool decode_ps_cmpo1(uint32_t inst, PS_CMPO1_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x3FF) != 96) return false;
    d->crfD = (inst >> 23) & 0x7;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_ps_cmpo1(const PS_CMPO1_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_cmpo1 cr%u, f%u, f%u */", d->crfD, d->frA, d->frB);
}

static inline int comment_ps_cmpo1(const PS_CMPO1_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_cmpo1 cr%u, f%u, f%u", d->crfD, d->frA, d->frB);
}

#endif

