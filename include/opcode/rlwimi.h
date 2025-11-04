/**
 * @file rlwimi.h
 * @brief RLWIMI - Rotate Left Word Immediate then Mask Insert
 * 
 * Opcode: 20
 * Syntax: rlwimi rA, rS, SH, MB, ME
 */

#ifndef OPCODE_RLWIMI_H
#define OPCODE_RLWIMI_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_RLWIMI 20

typedef struct {
    uint8_t rA, rS, SH, MB, ME;
    bool Rc;
} RLWIMI_Instruction;

static inline bool decode_rlwimi(uint32_t inst, RLWIMI_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 20) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->SH = (inst >> 11) & 0x1F;
    d->MB = (inst >> 6) & 0x1F;
    d->ME = (inst >> 1) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline uint32_t rlwimi_mask(uint8_t MB, uint8_t ME) {
    if (MB <= ME) {
        return ((1U << (32 - MB)) - 1) & ~((1U << (31 - ME)) - 1);
    } else {
        return ((1U << (32 - MB)) - 1) | ~((1U << (31 - ME)) - 1);
    }
}

static inline int transpile_rlwimi(const RLWIMI_Instruction *d, char *o, size_t s) {
    uint32_t mask = rlwimi_mask(d->MB, d->ME);
    int w = snprintf(o, s, "{ uint32_t rot = (r%u << %u) | (r%u >> %u); "
                     "r%u = (r%u & ~0x%08X) | (rot & 0x%08X); }",
                     d->rS, d->SH, d->rS, 32 - d->SH,
                     d->rA, d->rA, mask, mask);
    if (d->Rc) w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                             "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rA, d->rA);
    return w;
}

static inline int comment_rlwimi(const RLWIMI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "rlwimi%s r%u, r%u, %u, %u, %u", d->Rc?".":"", d->rA, d->rS, d->SH, d->MB, d->ME);
}

#endif

