/**
 * @file stfs.h
 * @brief STFS - Store Floating-Point Single
 * 
 * Opcode: 52
 * Format: D-form
 * Syntax: stfs frS, d(rA)
 * 
 * Description: EA = (rA|0) + d; store frS to EA as single-precision
 */

#ifndef OPCODE_STFS_H
#define OPCODE_STFS_H

#include <stdint.h>
#include <stdbool.h>

#define OP_STFS 52

typedef struct {
    uint8_t frS;
    uint8_t rA;
    int16_t d;
} STFS_Instruction;

static inline bool decode_stfs(uint32_t inst, STFS_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STFS) return false;
    
    d->frS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = inst & 0xFFFF;
    return true;
}

static inline int transpile_stfs(const STFS_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        return snprintf(o, s, "*(float*)(mem + (int16_t)0x%x) = (float)f%u;",
                       (uint16_t)d->d, d->frS);
    } else if (d->d == 0) {
        return snprintf(o, s, "*(float*)translate_address(r%u) = (float)f%u;",
                       d->rA, d->frS);
    } else if (d->d > 0) {
        return snprintf(o, s, "*(float*)translate_address(r%u + 0x%x) = (float)f%u;",
                       d->rA, (uint16_t)d->d, d->frS);
    } else {
        return snprintf(o, s, "*(float*)translate_address(r%u - 0x%x) = (float)f%u;",
                       d->rA, (uint16_t)(-d->d), d->frS);
    }
}

static inline int comment_stfs(const STFS_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) return snprintf(o, s, "stfs f%u, 0x%x(r%u)", d->frS, (uint16_t)d->d, d->rA);
    return snprintf(o, s, "stfs f%u, -0x%x(r%u)", d->frS, (uint16_t)(-d->d), d->rA);
}

#endif // OPCODE_STFS_H

