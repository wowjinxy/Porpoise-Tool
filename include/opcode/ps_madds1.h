/**
 * @file ps_madds1.h
 * @brief PS_MADDS1 - Paired Single Multiply-Add Scalar Low
 * Opcode: 4 / 15
 */

#ifndef OPCODE_PS_MADDS1_H
#define OPCODE_PS_MADDS1_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frB, frC;
    bool Rc;
} PS_MADDS1_Instruction;

static inline bool decode_ps_madds1(uint32_t inst, PS_MADDS1_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 15) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->frC = (inst >> 6) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_madds1(const PS_MADDS1_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_madds1 f%u, f%u, f%u, f%u */", d->frD, d->frA, d->frC, d->frB);
}

static inline int comment_ps_madds1(const PS_MADDS1_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_madds1%s f%u, f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frC, d->frB);
}

#endif

