/**
 * @file divwu.h
 * @brief DIVWU - Divide Word Unsigned
 * Opcode: 31 / 459
 */

#ifndef OPCODE_DIVWU_H
#define OPCODE_DIVWU_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rD, rA, rB;
    bool OE, Rc;
} DIVWU_Instruction;

static inline bool decode_divwu(uint32_t inst, DIVWU_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 459) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->OE = (inst >> 10) & 1;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_divwu(const DIVWU_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "r%u = (r%u != 0) ? (r%u / r%u) : 0;",
                    d->rD, d->rB, d->rA, d->rB);
    if (d->OE) {
        w += snprintf(o + w, s - w, "\nif (r%u == 0) { xer |= 0xC0000000; } else { xer &= ~0x80000000; }", d->rB);
    }
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                     "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rD, d->rD);
    }
    return w;
}

static inline int comment_divwu(const DIVWU_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "divwu%s%s r%u, r%u, r%u", d->OE?"o":"", d->Rc?".":"", d->rD, d->rA, d->rB);
}

#endif

