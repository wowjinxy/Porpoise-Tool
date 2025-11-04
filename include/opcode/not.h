/**
 * @file not.h
 * @brief NOT - Complement Register (pseudo-op for NOR)
 * Opcode: 31 / 124 (nor rA, rS, rS)
 */

#ifndef OPCODE_NOT_H
#define OPCODE_NOT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t rA, rS;
    bool Rc;
} NOT_Instruction;

static inline bool decode_not(uint32_t inst, NOT_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 124) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    uint8_t rB = (inst >> 11) & 0x1F;
    if (rB != d->rS) return false;  // not is nor rA, rS, rS
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_not(const NOT_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "r%u = ~r%u;", d->rA, d->rS);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : (int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rA, d->rA);
    }
    return w;
}

static inline int comment_not(const NOT_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "not%s r%u, r%u", d->Rc ? "." : "", d->rA, d->rS);
}

#endif

