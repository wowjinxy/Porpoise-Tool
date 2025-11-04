/**
 * @file mulhw.h
 * @brief MULHW - Multiply High Word (signed)
 * Opcode: 31 / 75
 */

#ifndef OPCODE_MULHW_H
#define OPCODE_MULHW_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rD, rA, rB;
    bool Rc;
} MULHW_Instruction;

static inline bool decode_mulhw(uint32_t inst, MULHW_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 75) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_mulhw(const MULHW_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "r%u = (uint32_t)(((int64_t)(int32_t)r%u * (int64_t)(int32_t)r%u) >> 32);",
                    d->rD, d->rA, d->rB);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                     "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rD, d->rD);
    }
    return w;
}

static inline int comment_mulhw(const MULHW_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mulhw%s r%u, r%u, r%u", d->Rc?".":"", d->rD, d->rA, d->rB);
}

#endif

