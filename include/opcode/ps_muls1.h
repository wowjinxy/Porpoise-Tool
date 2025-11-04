/**
 * @file ps_muls1.h
 * @brief PS_MULS1 - Paired Single Multiply Scalar Low
 * Opcode: 4 / 13
 */

#ifndef OPCODE_PS_MULS1_H
#define OPCODE_PS_MULS1_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frC;
    bool Rc;
} PS_MULS1_Instruction;

static inline bool decode_ps_muls1(uint32_t inst, PS_MULS1_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 13) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frC = (inst >> 6) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_muls1(const PS_MULS1_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_muls1 f%u, f%u, f%u */", d->frD, d->frA, d->frC);
}

static inline int comment_ps_muls1(const PS_MULS1_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_muls1%s f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frC);
}

#endif

