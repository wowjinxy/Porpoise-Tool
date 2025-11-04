/**
 * @file lfdx.h
 * @brief LFDX - Load Floating-Point Double Indexed
 * 
 * Opcode: 31 / 599
 * Format: X-form
 * Syntax: lfdx frD, rA, rB
 * 
 * Description: EA = (rA|0) + rB; frD = double at EA
 */

#ifndef OPCODE_LFDX_H
#define OPCODE_LFDX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_LFDX_PRIMARY    31
#define OP_LFDX_EXTENDED   599

typedef struct {
    uint8_t frD;
    uint8_t rA;
    uint8_t rB;
} LFDX_Instruction;

static inline bool decode_lfdx(uint32_t inst, LFDX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LFDX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_LFDX_EXTENDED) return false;
    
    d->frD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lfdx(const LFDX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        return snprintf(o, s, "f%u = *(double*)(mem + r%u);", d->frD, d->rB);
    }
    return snprintf(o, s, "f%u = *(double*)(mem + r%u + r%u);", d->frD, d->rA, d->rB);
}

static inline int comment_lfdx(const LFDX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lfdx f%u, r%u, r%u", d->frD, d->rA, d->rB);
}

#endif // OPCODE_LFDX_H

