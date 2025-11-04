/**
 * @file dcbt.h
 * @brief DCBT - Data Cache Block Touch
 * 
 * Opcode: 31 / 278
 * Format: X-form
 * Syntax: dcbt rA, rB
 * 
 * Description: Touch data cache block (hint for prefetch)
 */

#ifndef OPCODE_DCBT_H
#define OPCODE_DCBT_H

#include <stdint.h>
#include <stdbool.h>

#define OP_DCBT_PRIMARY    31
#define OP_DCBT_EXTENDED   278

typedef struct {
    uint8_t rA;
    uint8_t rB;
} DCBT_Instruction;

static inline bool decode_dcbt(uint32_t inst, DCBT_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_DCBT_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_DCBT_EXTENDED) return false;
    
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_dcbt(const DCBT_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, ";  /* dcbt - data cache touch (no-op in C) */");
}

static inline int comment_dcbt(const DCBT_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "dcbt r%u, r%u", d->rA, d->rB);
}

#endif // OPCODE_DCBT_H

