/**
 * @file dcbf.h
 * @brief DCBF - Data Cache Block Flush
 * Opcode: 31 / 86
 */

#ifndef OPCODE_DCBF_H
#define OPCODE_DCBF_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct { uint8_t rA, rB; } DCBF_Instruction;

static inline bool decode_dcbf(uint32_t inst, DCBF_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 86) return false;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_dcbf(const DCBF_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, ";  /* dcbf - data cache flush (no-op in C) */");
}

static inline int comment_dcbf(const DCBF_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "dcbf r%u, r%u", d->rA, d->rB);
}

#endif

