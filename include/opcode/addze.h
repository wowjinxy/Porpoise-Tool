/**
 * @file addze.h
 * @brief ADDZE - Add to Zero Extended
 * Opcode: 31 / 202
 */

#ifndef OPCODE_ADDZE_H
#define OPCODE_ADDZE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rD, rA;
    bool OE, Rc;
} ADDZE_Instruction;

static inline bool decode_addze(uint32_t inst, ADDZE_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 202) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->OE = (inst >> 10) & 1;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_addze(const ADDZE_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "r%u = r%u + ((xer >> 29) & 1);", d->rD, d->rA);
    if (d->OE) {
        w += snprintf(o + w, s - w, "\nif (((int32_t)r%u ^ (int32_t)r%u) < 0) { "
                     "xer |= 0xC0000000; } else { xer &= ~0x80000000; }", d->rD, d->rA);
    }
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                     "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rD, d->rD);
    }
    return w;
}

static inline int comment_addze(const ADDZE_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "addze%s%s r%u, r%u", d->OE?"o":"", d->Rc?".":"", d->rD, d->rA);
}

#endif

