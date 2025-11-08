/**
 * @file stwx.h
 * @brief STWX - Store Word Indexed
 * Opcode: 31 / 151
 */

#ifndef OPCODE_STWX_H
#define OPCODE_STWX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rS, rA, rB;
} STWX_Instruction;

static inline bool decode_stwx(uint32_t inst, STWX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 151) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_stwx(const STWX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        return snprintf(o, s, "*(uint32_t*)translate_address(r%u) = r%u;", d->rB, d->rS);
    }
    return snprintf(o, s, "*(uint32_t*)(mem + r%u + r%u) = r%u;", d->rA, d->rB, d->rS);
}

static inline int comment_stwx(const STWX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "stwx r%u, r%u, r%u", d->rS, d->rA, d->rB);
}

#endif

