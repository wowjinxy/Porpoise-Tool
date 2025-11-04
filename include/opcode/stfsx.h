/**
 * @file stfsx.h
 * @brief STFSX - Store Floating-Point Single Indexed
 * 
 * Opcode: 31 / 663
 * Format: X-form
 * Syntax: stfsx frS, rA, rB
 * 
 * Description: EA = (rA|0) + rB; store frS to EA as single-precision
 */

#ifndef OPCODE_STFSX_H
#define OPCODE_STFSX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_STFSX_PRIMARY    31
#define OP_STFSX_EXTENDED   663

typedef struct {
    uint8_t frS;
    uint8_t rA;
    uint8_t rB;
} STFSX_Instruction;

static inline bool decode_stfsx(uint32_t inst, STFSX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STFSX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_STFSX_EXTENDED) return false;
    
    d->frS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_stfsx(const STFSX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        return snprintf(o, s, "*(float*)(mem + r%u) = (float)f%u;", d->rB, d->frS);
    }
    return snprintf(o, s, "*(float*)(mem + r%u + r%u) = (float)f%u;", d->rA, d->rB, d->frS);
}

static inline int comment_stfsx(const STFSX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "stfsx f%u, r%u, r%u", d->frS, d->rA, d->rB);
}

#endif // OPCODE_STFSX_H

