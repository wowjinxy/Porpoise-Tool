/**
 * @file sthx.h
 * @brief STHX - Store Halfword Indexed
 * Opcode: 31 / 407
 */

#ifndef OPCODE_STHX_H
#define OPCODE_STHX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rS, rA, rB;
} STHX_Instruction;

static inline bool decode_sthx(uint32_t inst, STHX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 407) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_sthx(const STHX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        return snprintf(o, s, "*(uint16_t*)translate_address(r%u) = (uint16_t)r%u;", d->rB, d->rS);
    }
    return snprintf(o, s, "*(uint16_t*)(mem + r%u + r%u) = (uint16_t)r%u;", d->rA, d->rB, d->rS);
}

static inline int comment_sthx(const STHX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "sthx r%u, r%u, r%u", d->rS, d->rA, d->rB);
}

#endif

