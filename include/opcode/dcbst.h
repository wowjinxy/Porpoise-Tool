/**
 * @file dcbst.h  
 * @brief DCBST - Data Cache Block Store
 * Opcode: 31 / 54
 */

#ifndef OPCODE_DCBST_H
#define OPCODE_DCBST_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct { uint8_t rA, rB; } DCBST_Instruction;

static inline bool decode_dcbst(uint32_t inst, DCBST_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 54) return false;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_dcbst(const DCBST_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, ";  /* dcbst - data cache store (no-op in C) */");
}

static inline int comment_dcbst(const DCBST_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "dcbst r%u, r%u", d->rA, d->rB);
}

#endif

