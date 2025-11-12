/**
 * @file stbu.h
 * @brief STBU - Store Byte with Update
 * Opcode: 39
 */

#ifndef OPCODE_STBU_H
#define OPCODE_STBU_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_STBU 39

typedef struct {
    uint8_t rS, rA;
    int16_t d;
} STBU_Instruction;

static inline bool decode_stbu(uint32_t inst, STBU_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 39) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = inst & 0xFFFF;
    return true;
}

static inline int transpile_stbu(const STBU_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) {
        return snprintf(o, s, "r%u = r%u + 0x%x; *(uint8_t*)(r%u) = (uint8_t)r%u;",
                       d->rA, d->rA, (uint16_t)d->d, d->rA, d->rS);
    } else {
        return snprintf(o, s, "r%u = r%u - 0x%x; *(uint8_t*)(r%u) = (uint8_t)r%u;",
                       d->rA, d->rA, (uint16_t)(-d->d), d->rA, d->rS);
    }
}

static inline int comment_stbu(const STBU_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) return snprintf(o, s, "stbu r%u, 0x%x(r%u)", d->rS, (uint16_t)d->d, d->rA);
    return snprintf(o, s, "stbu r%u, -0x%x(r%u)", d->rS, (uint16_t)(-d->d), d->rA);
}

#endif

