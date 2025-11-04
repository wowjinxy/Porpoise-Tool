/**
 * @file psq_lu.h
 * @brief PSQ_LU - Paired Single Quantized Load with Update
 * Opcode: 57
 */

#ifndef OPCODE_PSQ_LU_H
#define OPCODE_PSQ_LU_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, rA;
    int16_t d;
    uint8_t W, I;
} PSQ_LU_Instruction;

static inline bool decode_psq_lu(uint32_t inst, PSQ_LU_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 57) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = (inst >> 4) & 0xFFF;
    d->W = (inst >> 3) & 1;
    d->I = inst & 0x7;
    return true;
}

static inline int transpile_psq_lu(const PSQ_LU_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* psq_lu f%u, 0x%x(r%u), %u, qr%u */", 
                   d->frD, d->d, d->rA, d->W, d->I);
}

static inline int comment_psq_lu(const PSQ_LU_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "psq_lu f%u, 0x%x(r%u), %u, qr%u", 
                   d->frD, d->d, d->rA, d->W, d->I);
}

#endif

