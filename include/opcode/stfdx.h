/**
 * @file stfdx.h
 * @brief STFDX - Store Floating-Point Double Indexed
 * 
 * Opcode: 31 / 727
 * Format: X-form
 * Syntax: stfdx frS, rA, rB
 * 
 * Description: EA = (rA|0) + rB; store frS to EA as double-precision
 */

#ifndef OPCODE_STFDX_H
#define OPCODE_STFDX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_STFDX_PRIMARY    31
#define OP_STFDX_EXTENDED   727

typedef struct {
    uint8_t frS;
    uint8_t rA;
    uint8_t rB;
} STFDX_Instruction;

static inline bool decode_stfdx(uint32_t inst, STFDX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STFDX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_STFDX_EXTENDED) return false;
    
    d->frS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_stfdx(const STFDX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        return snprintf(o, s, "*(double*)translate_address(r%u) = f%u;", d->rB, d->frS);
    }
    return snprintf(o, s, "*(double*)translate_address(r%u + r%u) = f%u;", d->rA, d->rB, d->frS);
}

static inline int comment_stfdx(const STFDX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "stfdx f%u, r%u, r%u", d->frS, d->rA, d->rB);
}

#endif // OPCODE_STFDX_H

