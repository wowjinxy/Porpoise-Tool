/**
 * @file extsb.h
 * @brief EXTSB - Extend Sign Byte
 * Opcode: 31 / 954
 */

#ifndef OPCODE_EXTSB_H
#define OPCODE_EXTSB_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rA, rS;
    bool Rc;
} EXTSB_Instruction;

static inline bool decode_extsb(uint32_t inst, EXTSB_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 954) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_extsb(const EXTSB_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "r%u = (int32_t)(int8_t)(uint8_t)r%u;", d->rA, d->rS);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                     "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rA, d->rA);
    }
    return w;
}

static inline int comment_extsb(const EXTSB_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "extsb%s r%u, r%u", d->Rc?".":"", d->rA, d->rS);
}

#endif

