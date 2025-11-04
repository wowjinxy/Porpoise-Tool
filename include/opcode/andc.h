/**
 * @file andc.h
 * @brief ANDC - AND with Complement
 * Opcode: 31 / 60
 */

#ifndef OPCODE_ANDC_H
#define OPCODE_ANDC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rA, rS, rB;
    bool Rc;
} ANDC_Instruction;

static inline bool decode_andc(uint32_t inst, ANDC_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 60) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_andc(const ANDC_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "r%u = r%u & ~r%u;", d->rA, d->rS, d->rB);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                     "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rA, d->rA);
    }
    return w;
}

static inline int comment_andc(const ANDC_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "andc%s r%u, r%u, r%u", d->Rc?".":"", d->rA, d->rS, d->rB);
}

#endif

