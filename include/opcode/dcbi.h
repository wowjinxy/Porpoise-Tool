/**
 * @file dcbi.h
 * @brief DCBI - Data Cache Block Invalidate
 * Opcode: 31 / 470
 */

#ifndef OPCODE_DCBI_H
#define OPCODE_DCBI_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct { uint8_t rA, rB; } DCBI_Instruction;

static inline bool decode_dcbi(uint32_t inst, DCBI_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 470) return false;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_dcbi(const DCBI_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, ";  /* dcbi - data cache invalidate (no-op in C) */");
}

static inline int comment_dcbi(const DCBI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "dcbi r%u, r%u", d->rA, d->rB);
}

#endif

