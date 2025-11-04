/**
 * @file lhax.h
 * @brief LHAX - Load Halfword Algebraic Indexed
 * 
 * Opcode: 31 / 343
 * Format: X-form
 * Syntax: lhax rD, rA, rB
 * 
 * Description: EA = (rA|0) + rB; rD = halfword at EA (sign extended)
 */

#ifndef OPCODE_LHAX_H
#define OPCODE_LHAX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LHAX_PRIMARY    31
#define OP_LHAX_EXTENDED   343

typedef struct {
    uint8_t rD;
    uint8_t rA;
    uint8_t rB;
} LHAX_Instruction;

static inline bool decode_lhax(uint32_t inst, LHAX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LHAX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_LHAX_EXTENDED) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lhax(const LHAX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        return snprintf(o, s, "r%u = (int32_t)(int16_t)*(uint16_t*)(mem + r%u);", d->rD, d->rB);
    }
    return snprintf(o, s, "r%u = (int32_t)(int16_t)*(uint16_t*)(mem + r%u + r%u);", d->rD, d->rA, d->rB);
}

static inline int comment_lhax(const LHAX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lhax r%u, r%u, r%u", d->rD, d->rA, d->rB);
}

#endif // OPCODE_LHAX_H

