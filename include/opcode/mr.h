/**
 * @file mr.h
 * @brief MR - Move Register (pseudo-op for OR)
 * Opcode: 31 / 444 (or rA, rS, rS)
 */

#ifndef OPCODE_MR_H
#define OPCODE_MR_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t rA, rS;
    bool Rc;
} MR_Instruction;

static inline bool decode_mr(uint32_t inst, MR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 444) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    uint8_t rB = (inst >> 11) & 0x1F;
    if (rB != d->rS) return false;  // mr is or rA, rS, rS
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_mr(const MR_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "r%u = r%u;", d->rA, d->rS);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : (int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rA, d->rA);
    }
    return w;
}

static inline int comment_mr(const MR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mr%s r%u, r%u", d->Rc ? "." : "", d->rA, d->rS);
}

#endif

