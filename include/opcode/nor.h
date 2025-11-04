/**
 * @file nor.h
 * @brief NOR - Logical NOR
 * Opcode: 31 / 124
 */

#ifndef OPCODE_NOR_H
#define OPCODE_NOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rA, rS, rB;
    bool Rc;
} NOR_Instruction;

static inline bool decode_nor(uint32_t inst, NOR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 124) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_nor(const NOR_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "r%u = ~(r%u | r%u);", d->rA, d->rS, d->rB);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                     "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rA, d->rA);
    }
    return w;
}

static inline int comment_nor(const NOR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "nor%s r%u, r%u, r%u", d->Rc?".":"", d->rA, d->rS, d->rB);
}

#endif

