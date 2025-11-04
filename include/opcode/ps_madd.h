/**
 * @file ps_madd.h
 * @brief PS_MADD - Paired Single Multiply-Add
 * Opcode: 4 / 29
 */

#ifndef OPCODE_PS_MADD_H
#define OPCODE_PS_MADD_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frB, frC;
    bool Rc;
} PS_MADD_Instruction;

static inline bool decode_ps_madd(uint32_t inst, PS_MADD_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 29) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->frC = (inst >> 6) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_madd(const PS_MADD_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_madd f%u, f%u, f%u, f%u - paired single madd */",
                   d->frD, d->frA, d->frC, d->frB);
}

static inline int comment_ps_madd(const PS_MADD_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_madd%s f%u, f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frC, d->frB);
}

#endif

