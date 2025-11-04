/**
 * @file psq_l.h
 * @brief PSQ_L - Paired Single Quantized Load (Gekko/Broadway specific)
 * Opcode: 56
 */

#ifndef OPCODE_PSQ_L_H
#define OPCODE_PSQ_L_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_PSQ_L 56

typedef struct {
    uint8_t frD, rA;
    int16_t d;
    uint8_t W, I;  // W=0 load pair, W=1 load single; I=GQR index
} PSQ_L_Instruction;

static inline bool decode_psq_l(uint32_t inst, PSQ_L_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 56) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->W = (inst >> 15) & 1;
    d->I = (inst >> 12) & 7;
    d->d = (inst & 0xFFF);
    if (d->d & 0x800) d->d |= 0xF000;  // Sign extend
    return true;
}

static inline int transpile_psq_l(const PSQ_L_Instruction *d, char *o, size_t s) {
    // Simplified: treat as loading float data (actual implementation needs GQR dequantization)
    if (d->rA == 0) {
        return snprintf(o, s, "/* psq_l f%u, 0x%x, %u, qr%u */ "
                       "f%u = *(double*)(mem + 0x%x);", 
                       d->frD, (uint32_t)d->d, d->W, d->I, d->frD, (uint32_t)d->d);
    } else if (d->d >= 0) {
        return snprintf(o, s, "/* psq_l f%u, 0x%x(r%u), %u, qr%u */ "
                       "f%u = *(double*)(mem + r%u + 0x%x);",
                       d->frD, (uint16_t)d->d, d->rA, d->W, d->I, d->frD, d->rA, (uint16_t)d->d);
    } else {
        return snprintf(o, s, "/* psq_l f%u, -0x%x(r%u), %u, qr%u */ "
                       "f%u = *(double*)(mem + r%u - 0x%x);",
                       d->frD, (uint16_t)(-d->d), d->rA, d->W, d->I, d->frD, d->rA, (uint16_t)(-d->d));
    }
}

static inline int comment_psq_l(const PSQ_L_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) {
        return snprintf(o, s, "psq_l f%u, 0x%x(r%u), %u, qr%u", 
                       d->frD, (uint16_t)d->d, d->rA, d->W, d->I);
    }
    return snprintf(o, s, "psq_l f%u, -0x%x(r%u), %u, qr%u",
                   d->frD, (uint16_t)(-d->d), d->rA, d->W, d->I);
}

#endif

