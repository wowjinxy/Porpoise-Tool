/**
 * @file ps_sel.h
 * @brief PS_SEL - Paired Single Select
 * Opcode: 4 / 23
 */

#ifndef OPCODE_PS_SEL_H
#define OPCODE_PS_SEL_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frB, frC;
    bool Rc;
} PS_SEL_Instruction;

static inline bool decode_ps_sel(uint32_t inst, PS_SEL_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 23) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->frC = (inst >> 6) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_sel(const PS_SEL_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_sel f%u, f%u, f%u, f%u */", d->frD, d->frA, d->frC, d->frB);
}

static inline int comment_ps_sel(const PS_SEL_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_sel%s f%u, f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frC, d->frB);
}

#endif

