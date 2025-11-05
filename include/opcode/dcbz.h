/**
 * @file dcbz.h
 * @brief DCBZ - Data Cache Block Set to Zero
 * 
 * Opcode: 31 / 1014
 * Format: X-form
 * Syntax: dcbz rA, rB
 * 
 * Description: Clear cache block to zero
 */

#ifndef OPCODE_DCBZ_H
#define OPCODE_DCBZ_H

#include <stdint.h>
#include <stdbool.h>

#define OP_DCBZ_PRIMARY    31
#define OP_DCBZ_EXTENDED   1014

typedef struct {
    uint8_t rA;
    uint8_t rB;
} DCBZ_Instruction;

static inline bool decode_dcbz(uint32_t inst, DCBZ_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_DCBZ_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_DCBZ_EXTENDED) return false;
    
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_dcbz(const DCBZ_Instruction *d, char *o, size_t s) {
    // Clear 32 bytes at EA (typical cache line size)
    // Use inline loop instead of memset to avoid compiler intrinsic conflicts
    if (d->rA == 0) {
        return snprintf(o, s, "{ uint32_t addr = r%u & ~0x1F; for (int i = 0; i < 32; i++) mem[addr + i] = 0; }", d->rB);
    }
    return snprintf(o, s, "{ uint32_t addr = (r%u + r%u) & ~0x1F; for (int i = 0; i < 32; i++) mem[addr + i] = 0; }", d->rA, d->rB);
}

static inline int comment_dcbz(const DCBZ_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "dcbz r%u, r%u", d->rA, d->rB);
}

#endif // OPCODE_DCBZ_H

