/**
 * @file lhbrx.h
 * @brief LHBRX - Load Halfword Byte-Reverse Indexed
 * 
 * Opcode: 31 / 790
 * Format: X-form
 * Syntax: lhbrx rD, rA, rB
 * 
 * Description: EA = (rA|0) + rB; load halfword from EA with bytes reversed
 * Used for endian conversion
 */

#ifndef OPCODE_LHBRX_H
#define OPCODE_LHBRX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LHBRX_PRIMARY    31
#define OP_LHBRX_EXTENDED   790

typedef struct {
    uint8_t rD;
    uint8_t rA;
    uint8_t rB;
} LHBRX_Instruction;

static inline bool decode_lhbrx(uint32_t inst, LHBRX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LHBRX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_LHBRX_EXTENDED) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lhbrx(const LHBRX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        return snprintf(o, s,
                       "{ uint16_t val = *(uint16_t*)translate_address(r%u); "
                       "r%u = ((val & 0xFF) << 8) | ((val >> 8) & 0xFF); }",
                       d->rB, d->rD);
    }
    return snprintf(o, s,
                   "{ uint16_t val = *(uint16_t*)(mem + r%u + r%u); "
                   "r%u = ((val & 0xFF) << 8) | ((val >> 8) & 0xFF); }",
                   d->rA, d->rB, d->rD);
}

static inline int comment_lhbrx(const LHBRX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lhbrx r%u, r%u, r%u", d->rD, d->rA, d->rB);
}

#endif // OPCODE_LHBRX_H

