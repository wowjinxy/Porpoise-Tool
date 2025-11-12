/**
 * @file stwbrx.h
 * @brief STWBRX - Store Word Byte-Reverse Indexed
 * 
 * Opcode: 31 / 662
 * Format: X-form
 * Syntax: stwbrx rS, rA, rB
 * 
 * Description: EA = (rA|0) + rB; store word to EA with bytes reversed
 * Used for endian conversion
 */

#ifndef OPCODE_STWBRX_H
#define OPCODE_STWBRX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_STWBRX_PRIMARY    31
#define OP_STWBRX_EXTENDED   662

typedef struct {
    uint8_t rS;
    uint8_t rA;
    uint8_t rB;
} STWBRX_Instruction;

static inline bool decode_stwbrx(uint32_t inst, STWBRX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STWBRX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_STWBRX_EXTENDED) return false;
    
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_stwbrx(const STWBRX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        // Absolute address (rB contains absolute address) - should be resolved by transpiler
        return snprintf(o, s,
                       "{ uint32_t val = r%u; "
                       "*(uint32_t*)(uintptr_t)r%u = ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | "
                       "((val >> 8) & 0xFF00) | ((val >> 24) & 0xFF); }",
                       d->rS, d->rB);
    }
    return snprintf(o, s,
                   "{ uint32_t val = r%u; "
                   "*(uint32_t*)(r%u + r%u) = ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | "
                   "((val >> 8) & 0xFF00) | ((val >> 24) & 0xFF); }",
                   d->rS, d->rA, d->rB);
}

static inline int comment_stwbrx(const STWBRX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "stwbrx r%u, r%u, r%u", d->rS, d->rA, d->rB);
}

#endif // OPCODE_STWBRX_H

