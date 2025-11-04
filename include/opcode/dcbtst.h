/**
 * @file dcbtst.h
 * @brief DCBTST - Data Cache Block Touch for Store
 * 
 * Opcode: 31 / 246
 * Format: X-form
 * Syntax: dcbtst rA, rB
 * 
 * Description: Touch data cache block for store (hint for prefetch)
 */

#ifndef OPCODE_DCBTST_H
#define OPCODE_DCBTST_H

#include <stdint.h>
#include <stdbool.h>

#define OP_DCBTST_PRIMARY    31
#define OP_DCBTST_EXTENDED   246

typedef struct {
    uint8_t rA;
    uint8_t rB;
} DCBTST_Instruction;

static inline bool decode_dcbtst(uint32_t inst, DCBTST_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_DCBTST_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_DCBTST_EXTENDED) return false;
    
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_dcbtst(const DCBTST_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, ";  /* dcbtst - data cache touch for store (no-op in C) */");
}

static inline int comment_dcbtst(const DCBTST_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "dcbtst r%u, r%u", d->rA, d->rB);
}

#endif // OPCODE_DCBTST_H

