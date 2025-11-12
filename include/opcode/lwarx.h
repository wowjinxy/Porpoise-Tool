/**
 * @file lwarx.h
 * @brief LWARX - Load Word And Reserve Indexed
 * Opcode: 31 / 20
 * Used for atomic operations
 */

#ifndef OPCODE_LWARX_H
#define OPCODE_LWARX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_LWARX_PRIMARY    31
#define OP_LWARX_EXTENDED   20

typedef struct {
    uint8_t rD, rA, rB;
} LWARX_Instruction;

static inline bool decode_lwarx(uint32_t inst, LWARX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 20) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lwarx(const LWARX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        // Absolute address (rB contains absolute address) - should be resolved by transpiler
        return snprintf(o, s, "r%u = *(uint32_t*)(uintptr_t)r%u;  /* reserve set */", d->rD, d->rB);
    }
    return snprintf(o, s, "r%u = *(uint32_t*)(r%u + r%u);  /* reserve set */", d->rD, d->rA, d->rB);
}

static inline int comment_lwarx(const LWARX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lwarx r%u, r%u, r%u", d->rD, d->rA, d->rB);
}

#endif

