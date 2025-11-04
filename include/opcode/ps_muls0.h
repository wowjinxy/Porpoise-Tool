/**
 * @file ps_muls0.h
 * @brief PS_MULS0 - Paired Single Multiply Scalar High
 * Opcode: 4 / 12
 */

#ifndef OPCODE_PS_MULS0_H
#define OPCODE_PS_MULS0_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frC;
    bool Rc;
} PS_MULS0_Instruction;

static inline bool decode_ps_muls0(uint32_t inst, PS_MULS0_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 12) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frC = (inst >> 6) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_muls0(const PS_MULS0_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_muls0 f%u, f%u, f%u */", d->frD, d->frA, d->frC);
}

static inline int comment_ps_muls0(const PS_MULS0_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_muls0%s f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frC);
}

#endif

