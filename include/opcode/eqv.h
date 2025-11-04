/**
 * @file eqv.h
 * @brief EQV - Equivalent (XNOR)
 * Opcode: 31 / 284
 */

#ifndef OPCODE_EQV_H
#define OPCODE_EQV_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rA, rS, rB;
    bool Rc;
} EQV_Instruction;

static inline bool decode_eqv(uint32_t inst, EQV_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 284) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_eqv(const EQV_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "r%u = ~(r%u ^ r%u);", d->rA, d->rS, d->rB);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                     "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rA, d->rA);
    }
    return w;
}

static inline int comment_eqv(const EQV_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "eqv%s r%u, r%u, r%u", d->Rc?".":"", d->rA, d->rS, d->rB);
}

#endif

