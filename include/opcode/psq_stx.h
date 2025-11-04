/**
 * @file psq_stx.h
 * @brief PSQ_STX - Paired Single Quantized Store Indexed
 * Opcode: 4 / 7
 */

#ifndef OPCODE_PSQ_STX_H
#define OPCODE_PSQ_STX_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frS, rA, rB, W, I;
} PSQ_STX_Instruction;

static inline bool decode_psq_stx(uint32_t inst, PSQ_STX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x3F) != 7) return false;
    d->frS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->W = (inst >> 10) & 1;
    d->I = (inst >> 7) & 0x7;
    return true;
}

static inline int transpile_psq_stx(const PSQ_STX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* psq_stx f%u, r%u, r%u, %u, qr%u */",
                   d->frS, d->rA, d->rB, d->W, d->I);
}

static inline int comment_psq_stx(const PSQ_STX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "psq_stx f%u, r%u, r%u, %u, qr%u",
                   d->frS, d->rA, d->rB, d->W, d->I);
}

#endif

