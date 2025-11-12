/**
 * @file lwbrx.h
 * @brief LWBRX - Load Word Byte-Reverse Indexed
 * 
 * Opcode: 31 / 534
 * Format: X-form
 * Syntax: lwbrx rD, rA, rB
 * 
 * Description: EA = (rA|0) + rB; load word from EA with bytes reversed
 * Used for endian conversion
 */

#ifndef OPCODE_LWBRX_H
#define OPCODE_LWBRX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LWBRX_PRIMARY    31
#define OP_LWBRX_EXTENDED   534

typedef struct {
    uint8_t rD;
    uint8_t rA;
    uint8_t rB;
} LWBRX_Instruction;

static inline bool decode_lwbrx(uint32_t inst, LWBRX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LWBRX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_LWBRX_EXTENDED) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lwbrx(const LWBRX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        // Absolute address (rB contains absolute address) - should be resolved by transpiler
        return snprintf(o, s,
                       "{ uint32_t val = *(uint32_t*)(uintptr_t)r%u; "
                       "r%u = ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | "
                       "((val >> 8) & 0xFF00) | ((val >> 24) & 0xFF); }",
                       d->rB, d->rD);
    }
    return snprintf(o, s,
                   "{ uint32_t val = *(uint32_t*)(r%u + r%u); "
                   "r%u = ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | "
                   "((val >> 8) & 0xFF00) | ((val >> 24) & 0xFF); }",
                   d->rA, d->rB, d->rD);
}

static inline int comment_lwbrx(const LWBRX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lwbrx r%u, r%u, r%u", d->rD, d->rA, d->rB);
}

#endif // OPCODE_LWBRX_H

