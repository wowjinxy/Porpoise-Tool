/**
 * @file ps_sum0.h
 * @brief PS_SUM0 - Paired Single Sum High
 * Opcode: 4 / 10
 */

#ifndef OPCODE_PS_SUM0_H
#define OPCODE_PS_SUM0_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frB, frC;
    bool Rc;
} PS_SUM0_Instruction;

static inline bool decode_ps_sum0(uint32_t inst, PS_SUM0_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 10) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->frC = (inst >> 6) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_sum0(const PS_SUM0_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_sum0 f%u, f%u, f%u, f%u */", d->frD, d->frA, d->frC, d->frB);
}

static inline int comment_ps_sum0(const PS_SUM0_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_sum0%s f%u, f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frC, d->frB);
}

#endif

