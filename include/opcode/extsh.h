/**
 * @file extsh.h
 * @brief EXTSH - Extend Sign Halfword
 * Opcode: 31 / 922
 */

#ifndef OPCODE_EXTSH_H
#define OPCODE_EXTSH_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rA, rS;
    bool Rc;
} EXTSH_Instruction;

static inline bool decode_extsh(uint32_t inst, EXTSH_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 922) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_extsh(const EXTSH_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "r%u = (int32_t)(int16_t)(uint16_t)r%u;", d->rA, d->rS);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                     "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rA, d->rA);
    }
    return w;
}

static inline int comment_extsh(const EXTSH_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "extsh%s r%u, r%u", d->Rc?".":"", d->rA, d->rS);
}

#endif

