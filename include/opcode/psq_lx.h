/**
 * @file psq_lx.h
 * @brief PSQ_LX - Paired Single Quantized Load Indexed
 * Opcode: 4 / 6
 */

#ifndef OPCODE_PSQ_LX_H
#define OPCODE_PSQ_LX_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, rA, rB, W, I;
} PSQ_LX_Instruction;

static inline bool decode_psq_lx(uint32_t inst, PSQ_LX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x3F) != 6) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->W = (inst >> 10) & 1;
    d->I = (inst >> 7) & 0x7;
    return true;
}

static inline int transpile_psq_lx(const PSQ_LX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* psq_lx f%u, r%u, r%u, %u, qr%u */",
                   d->frD, d->rA, d->rB, d->W, d->I);
}

static inline int comment_psq_lx(const PSQ_LX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "psq_lx f%u, r%u, r%u, %u, qr%u",
                   d->frD, d->rA, d->rB, d->W, d->I);
}

#endif

