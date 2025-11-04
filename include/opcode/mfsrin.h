/**
 * @file mfsrin.h
 * @brief MFSRIN - Move From Segment Register Indirect
 * Opcode: 31 / 659
 */

#ifndef OPCODE_MFSRIN_H
#define OPCODE_MFSRIN_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MFSRIN_PRIMARY    31
#define OP_MFSRIN_EXTENDED   659

typedef struct {
    uint8_t rD, rB;
} MFSRIN_Instruction;

static inline bool decode_mfsrin(uint32_t inst, MFSRIN_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 659) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_mfsrin(const MFSRIN_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = sr[(r%u >> 28) & 0xF];", d->rD, d->rB);
}

static inline int comment_mfsrin(const MFSRIN_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mfsrin r%u, r%u", d->rD, d->rB);
}

#endif

