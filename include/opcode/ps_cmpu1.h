/**
 * @file ps_cmpu1.h
 * @brief PS_CMPU1 - Paired Single Compare Unordered Low
 * Opcode: 4 / 64
 */

#ifndef OPCODE_PS_CMPU1_H
#define OPCODE_PS_CMPU1_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t crfD, frA, frB;
} PS_CMPU1_Instruction;

static inline bool decode_ps_cmpu1(uint32_t inst, PS_CMPU1_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x3FF) != 64) return false;
    d->crfD = (inst >> 23) & 0x7;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_ps_cmpu1(const PS_CMPU1_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_cmpu1 cr%u, f%u, f%u */", d->crfD, d->frA, d->frB);
}

static inline int comment_ps_cmpu1(const PS_CMPU1_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_cmpu1 cr%u, f%u, f%u", d->crfD, d->frA, d->frB);
}

#endif

