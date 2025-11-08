/**
 * @file lha.h
 * @brief LHA - Load Halfword Algebraic (sign extended)
 * Opcode: 42
 */

#ifndef OPCODE_LHA_H
#define OPCODE_LHA_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LHA 42

typedef struct {
    uint8_t rD, rA;
    int16_t d;
} LHA_Instruction;

static inline bool decode_lha(uint32_t inst, LHA_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 42) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = inst & 0xFFFF;
    return true;
}

static inline int transpile_lha(const LHA_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        return snprintf(o, s, "r%u = (int32_t)(int16_t)*(uint16_t*)translate_address(0x%x);", d->rD, (uint32_t)d->d);
    } else if (d->d == 0) {
        return snprintf(o, s, "r%u = (int32_t)(int16_t)*(uint16_t*)translate_address(r%u);", d->rD, d->rA);
    } else if (d->d > 0) {
        return snprintf(o, s, "r%u = (int32_t)(int16_t)*(uint16_t*)translate_address(r%u + 0x%x);", d->rD, d->rA, (uint16_t)d->d);
    } else {
        return snprintf(o, s, "r%u = (int32_t)(int16_t)*(uint16_t*)translate_address(r%u - 0x%x);", d->rD, d->rA, (uint16_t)(-d->d));
    }
}

static inline int comment_lha(const LHA_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) return snprintf(o, s, "lha r%u, 0x%x(r%u)", d->rD, (uint16_t)d->d, d->rA);
    return snprintf(o, s, "lha r%u, -0x%x(r%u)", d->rD, (uint16_t)(-d->d), d->rA);
}

#endif

