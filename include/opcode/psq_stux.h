/**
 * @file psq_stux.h
 * @brief PSQ_STUX - Paired Single Quantized Store with Update Indexed
 * Opcode: 4 / 39
 */

#ifndef OPCODE_PSQ_STUX_H
#define OPCODE_PSQ_STUX_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frS, rA, rB, W, I;
} PSQ_STUX_Instruction;

static inline bool decode_psq_stux(uint32_t inst, PSQ_STUX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x3F) != 39) return false;
    d->frS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->W = (inst >> 10) & 1;
    d->I = (inst >> 7) & 0x7;
    return true;
}

static inline int transpile_psq_stux(const PSQ_STUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* psq_stux f%u, r%u, r%u, %u, qr%u */",
                   d->frS, d->rA, d->rB, d->W, d->I);
}

static inline int comment_psq_stux(const PSQ_STUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "psq_stux f%u, r%u, r%u, %u, qr%u",
                   d->frS, d->rA, d->rB, d->W, d->I);
}

#endif

