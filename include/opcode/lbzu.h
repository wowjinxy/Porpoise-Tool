/**
 * @file lbzu.h
 * @brief LBZU - Load Byte and Zero with Update
 * Opcode: 35
 */

#ifndef OPCODE_LBZU_H
#define OPCODE_LBZU_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LBZU 35

typedef struct {
    uint8_t rD, rA;
    int16_t d;
} LBZU_Instruction;

static inline bool decode_lbzu(uint32_t inst, LBZU_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 35) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = inst & 0xFFFF;
    return true;
}

static inline int transpile_lbzu(const LBZU_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) {
        return snprintf(o, s, "r%u = r%u + 0x%x; r%u = *(uint8_t*)translate_address(r%u);",
                       d->rA, d->rA, (uint16_t)d->d, d->rD, d->rA);
    } else {
        return snprintf(o, s, "r%u = r%u - 0x%x; r%u = *(uint8_t*)translate_address(r%u);",
                       d->rA, d->rA, (uint16_t)(-d->d), d->rD, d->rA);
    }
}

static inline int comment_lbzu(const LBZU_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) return snprintf(o, s, "lbzu r%u, 0x%x(r%u)", d->rD, (uint16_t)d->d, d->rA);
    return snprintf(o, s, "lbzu r%u, -0x%x(r%u)", d->rD, (uint16_t)(-d->d), d->rA);
}

#endif

