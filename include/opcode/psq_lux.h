/**
 * @file psq_lux.h
 * @brief PSQ_LUX - Paired Single Quantized Load with Update Indexed
 * Opcode: 4 / 38
 */

#ifndef OPCODE_PSQ_LUX_H
#define OPCODE_PSQ_LUX_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, rA, rB, W, I;
} PSQ_LUX_Instruction;

static inline bool decode_psq_lux(uint32_t inst, PSQ_LUX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x3F) != 38) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->W = (inst >> 10) & 1;
    d->I = (inst >> 7) & 0x7;
    return true;
}

static inline int transpile_psq_lux(const PSQ_LUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* psq_lux f%u, r%u, r%u, %u, qr%u */",
                   d->frD, d->rA, d->rB, d->W, d->I);
}

static inline int comment_psq_lux(const PSQ_LUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "psq_lux f%u, r%u, r%u, %u, qr%u",
                   d->frD, d->rA, d->rB, d->W, d->I);
}

#endif

