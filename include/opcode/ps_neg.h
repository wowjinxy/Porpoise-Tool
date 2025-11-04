/**
 * @file ps_neg.h
 * @brief PS_NEG - Paired Single Negate
 * Opcode: 4 / 40
 */

#ifndef OPCODE_PS_NEG_H
#define OPCODE_PS_NEG_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frB;
    bool Rc;
} PS_NEG_Instruction;

static inline bool decode_ps_neg(uint32_t inst, PS_NEG_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x3FF) != 40) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_neg(const PS_NEG_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_neg f%u, f%u */", d->frD, d->frB);
}

static inline int comment_ps_neg(const PS_NEG_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_neg%s f%u, f%u", d->Rc?".":"", d->frD, d->frB);
}

#endif

