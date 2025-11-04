/**
 * @file ps_nmsub.h
 * @brief PS_NMSUB - Paired Single Negative Multiply-Subtract
 * Opcode: 4 / 30
 */

#ifndef OPCODE_PS_NMSUB_H
#define OPCODE_PS_NMSUB_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frB, frC;
    bool Rc;
} PS_NMSUB_Instruction;

static inline bool decode_ps_nmsub(uint32_t inst, PS_NMSUB_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 30) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->frC = (inst >> 6) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_nmsub(const PS_NMSUB_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_nmsub f%u, f%u, f%u, f%u */", d->frD, d->frA, d->frC, d->frB);
}

static inline int comment_ps_nmsub(const PS_NMSUB_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_nmsub%s f%u, f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frC, d->frB);
}

#endif

