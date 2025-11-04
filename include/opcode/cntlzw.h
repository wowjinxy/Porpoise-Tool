/**
 * @file cntlzw.h
 * @brief CNTLZW - Count Leading Zeros Word
 * Opcode: 31 / 26
 */

#ifndef OPCODE_CNTLZW_H
#define OPCODE_CNTLZW_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rA, rS;
    bool Rc;
} CNTLZW_Instruction;

static inline bool decode_cntlzw(uint32_t inst, CNTLZW_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 26) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_cntlzw(const CNTLZW_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "r%u = __builtin_clz(r%u ? r%u : 1) + (r%u ? 0 : 1);",
                    d->rA, d->rS, d->rS, d->rS);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                     "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rA, d->rA);
    }
    return w;
}

static inline int comment_cntlzw(const CNTLZW_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "cntlzw%s r%u, r%u", d->Rc?".":"", d->rA, d->rS);
}

#endif

