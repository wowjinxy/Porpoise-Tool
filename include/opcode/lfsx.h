/**
 * @file lfsx.h
 * @brief LFSX - Load Floating-Point Single Indexed
 * 
 * Opcode: 31 / 535
 * Format: X-form
 * Syntax: lfsx frD, rA, rB
 * 
 * Description: EA = (rA|0) + rB; frD = single at EA (as double)
 */

#ifndef OPCODE_LFSX_H
#define OPCODE_LFSX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_LFSX_PRIMARY    31
#define OP_LFSX_EXTENDED   535

typedef struct {
    uint8_t frD;
    uint8_t rA;
    uint8_t rB;
} LFSX_Instruction;

static inline bool decode_lfsx(uint32_t inst, LFSX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LFSX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_LFSX_EXTENDED) return false;
    
    d->frD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lfsx(const LFSX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        // Absolute address (rB contains absolute address) - should be resolved by transpiler
        return snprintf(o, s, "f%u = (double)*(float*)(uintptr_t)r%u;", d->frD, d->rB);
    }
    return snprintf(o, s, "f%u = (double)*(float*)(r%u + r%u);", d->frD, d->rA, d->rB);
}

static inline int comment_lfsx(const LFSX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lfsx f%u, r%u, r%u", d->frD, d->rA, d->rB);
}

#endif // OPCODE_LFSX_H

