/**
 * @file ps_msub.h
 * @brief PS_MSUB - Paired Single Multiply-Subtract
 * Opcode: 4 / 28
 */

#ifndef OPCODE_PS_MSUB_H
#define OPCODE_PS_MSUB_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frB, frC;
    bool Rc;
} PS_MSUB_Instruction;

static inline bool decode_ps_msub(uint32_t inst, PS_MSUB_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 28) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->frC = (inst >> 6) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_msub(const PS_MSUB_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_msub f%u, f%u, f%u, f%u - paired single msub */",
                   d->frD, d->frA, d->frC, d->frB);
}

static inline int comment_ps_msub(const PS_MSUB_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_msub%s f%u, f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frC, d->frB);
}

#endif

