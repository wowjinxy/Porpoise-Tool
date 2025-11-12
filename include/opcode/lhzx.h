/**
 * @file lhzx.h
 * @brief LHZX - Load Halfword and Zero Indexed
 * Opcode: 31 / 279
 */

#ifndef OPCODE_LHZX_H
#define OPCODE_LHZX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rD, rA, rB;
} LHZX_Instruction;

static inline bool decode_lhzx(uint32_t inst, LHZX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 279) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lhzx(const LHZX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        // Absolute address (rB contains absolute address) - should be resolved by transpiler
        return snprintf(o, s, "r%u = *(uint16_t*)(uintptr_t)r%u;", d->rD, d->rB);
    }
    return snprintf(o, s, "r%u = *(uint16_t*)(r%u + r%u);", d->rD, d->rA, d->rB);
}

static inline int comment_lhzx(const LHZX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lhzx r%u, r%u, r%u", d->rD, d->rA, d->rB);
}

#endif

