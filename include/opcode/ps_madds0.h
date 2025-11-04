/**
 * @file ps_madds0.h
 * @brief PS_MADDS0 - Paired Single Multiply-Add Scalar High
 * Opcode: 4 / 14
 */

#ifndef OPCODE_PS_MADDS0_H
#define OPCODE_PS_MADDS0_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frB, frC;
    bool Rc;
} PS_MADDS0_Instruction;

static inline bool decode_ps_madds0(uint32_t inst, PS_MADDS0_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 14) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->frC = (inst >> 6) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_madds0(const PS_MADDS0_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_madds0 f%u, f%u, f%u, f%u */", d->frD, d->frA, d->frC, d->frB);
}

static inline int comment_ps_madds0(const PS_MADDS0_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_madds0%s f%u, f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frC, d->frB);
}

#endif

