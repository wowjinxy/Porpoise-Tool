/**
 * @file sthu.h
 * @brief STHU - Store Halfword with Update
 * Opcode: 45
 */

#ifndef OPCODE_STHU_H
#define OPCODE_STHU_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_STHU 45

typedef struct {
    uint8_t rS, rA;
    int16_t d;
} STHU_Instruction;

static inline bool decode_sthu(uint32_t inst, STHU_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 45) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = inst & 0xFFFF;
    return true;
}

static inline int transpile_sthu(const STHU_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) {
        return snprintf(o, s, "r%u = r%u + 0x%x; *(uint16_t*)translate_address(r%u) = (uint16_t)r%u;",
                       d->rA, d->rA, (uint16_t)d->d, d->rA, d->rS);
    } else {
        return snprintf(o, s, "r%u = r%u - 0x%x; *(uint16_t*)translate_address(r%u) = (uint16_t)r%u;",
                       d->rA, d->rA, (uint16_t)(-d->d), d->rA, d->rS);
    }
}

static inline int comment_sthu(const STHU_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) return snprintf(o, s, "sthu r%u, 0x%x(r%u)", d->rS, (uint16_t)d->d, d->rA);
    return snprintf(o, s, "sthu r%u, -0x%x(r%u)", d->rS, (uint16_t)(-d->d), d->rA);
}

#endif

