/**
 * @file stbx.h
 * @brief STBX - Store Byte Indexed
 * Opcode: 31 / 215
 */

#ifndef OPCODE_STBX_H
#define OPCODE_STBX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rS, rA, rB;
} STBX_Instruction;

static inline bool decode_stbx(uint32_t inst, STBX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 215) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_stbx(const STBX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        // Absolute address (rB contains absolute address) - should be resolved by transpiler
        return snprintf(o, s, "*(uint8_t*)(uintptr_t)r%u = (uint8_t)r%u;", d->rB, d->rS);
    }
    return snprintf(o, s, "*(uint8_t*)(r%u + r%u) = (uint8_t)r%u;", d->rA, d->rB, d->rS);
}

static inline int comment_stbx(const STBX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "stbx r%u, r%u, r%u", d->rS, d->rA, d->rB);
}

#endif

