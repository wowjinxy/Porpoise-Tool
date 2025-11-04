/**
 * @file icbi.h
 * @brief ICBI - Instruction Cache Block Invalidate  
 * Opcode: 31 / 982
 */

#ifndef OPCODE_ICBI_H
#define OPCODE_ICBI_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct { uint8_t rA, rB; } ICBI_Instruction;

static inline bool decode_icbi(uint32_t inst, ICBI_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 982) return false;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_icbi(const ICBI_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, ";  /* icbi - instruction cache invalidate (no-op in C) */");
}

static inline int comment_icbi(const ICBI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "icbi r%u, r%u", d->rA, d->rB);
}

#endif

