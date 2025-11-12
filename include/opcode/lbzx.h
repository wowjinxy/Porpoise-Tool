/**
 * @file lbzx.h
 * @brief LBZX - Load Byte and Zero Indexed
 * Opcode: 31 / 87
 */

#ifndef OPCODE_LBZX_H
#define OPCODE_LBZX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rD, rA, rB;
} LBZX_Instruction;

static inline bool decode_lbzx(uint32_t inst, LBZX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 87) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lbzx(const LBZX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        // Absolute address (rB contains absolute address) - should be resolved by transpiler
        return snprintf(o, s, "r%u = *(uint8_t*)(uintptr_t)r%u;", d->rD, d->rB);
    }
    return snprintf(o, s, "r%u = *(uint8_t*)(r%u + r%u);", d->rD, d->rA, d->rB);
}

static inline int comment_lbzx(const LBZX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lbzx r%u, r%u, r%u", d->rD, d->rA, d->rB);
}

#endif

