/**
 * @file sraw.h
 * @brief SRAW - Shift Right Algebraic Word
 * Opcode: 31 / 792
 */

#ifndef OPCODE_SRAW_H
#define OPCODE_SRAW_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rA, rS, rB;
    bool Rc;
} SRAW_Instruction;

static inline bool decode_sraw(uint32_t inst, SRAW_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 792) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_sraw(const SRAW_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "{ uint32_t sh = r%u & 0x3F; "
                   "r%u = (sh < 32) ? ((int32_t)r%u >> sh) : ((int32_t)r%u >> 31); "
                   "xer = (xer & ~0x20000000) | (((int32_t)r%u < 0 && ((r%u << (32 - sh)) != 0)) ? 0x20000000 : 0); }",
                   d->rB, d->rA, d->rS, d->rS, d->rS, d->rS);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                     "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rA, d->rA);
    }
    return w;
}

static inline int comment_sraw(const SRAW_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "sraw%s r%u, r%u, r%u", d->Rc?".":"", d->rA, d->rS, d->rB);
}

#endif

