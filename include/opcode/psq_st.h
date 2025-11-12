/**
 * @file psq_st.h
 * @brief PSQ_ST - Paired Single Quantized Store (Gekko/Broadway specific)
 * Opcode: 60
 */

#ifndef OPCODE_PSQ_ST_H
#define OPCODE_PSQ_ST_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_PSQ_ST 60

typedef struct {
    uint8_t frS, rA;
    int16_t d;
    uint8_t W, I;  // W=0 store pair, W=1 store single; I=GQR index
} PSQ_ST_Instruction;

static inline bool decode_psq_st(uint32_t inst, PSQ_ST_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 60) return false;
    d->frS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->W = (inst >> 15) & 1;
    d->I = (inst >> 12) & 7;
    d->d = (inst & 0xFFF);
    if (d->d & 0x800) d->d |= 0xF000;  // Sign extend
    return true;
}

static inline int transpile_psq_st(const PSQ_ST_Instruction *d, char *o, size_t s) {
    // Simplified: treat as storing float data (actual implementation needs GQR quantization)
    if (d->rA == 0) {
        // Absolute address - should be resolved by transpiler to actual symbol/location
        uint32_t abs_addr = (uint32_t)(int16_t)d->d;
        return snprintf(o, s, "/* psq_st f%u, 0x%x, %u, qr%u */ "
                       "*(double*)(uintptr_t)0x%08X = f%u;", 
                       d->frS, abs_addr, d->W, d->I, abs_addr, d->frS);
    } else if (d->d >= 0) {
        return snprintf(o, s, "/* psq_st f%u, 0x%x(r%u), %u, qr%u */ "
                       "*(double*)(r%u + 0x%x) = f%u;",
                       d->frS, (uint16_t)d->d, d->rA, d->W, d->I, d->rA, (uint16_t)d->d, d->frS);
    } else {
        return snprintf(o, s, "/* psq_st f%u, -0x%x(r%u), %u, qr%u */ "
                       "*(double*)(r%u - 0x%x) = f%u;",
                       d->frS, (uint16_t)(-d->d), d->rA, d->W, d->I, d->rA, (uint16_t)(-d->d), d->frS);
    }
}

static inline int comment_psq_st(const PSQ_ST_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) {
        return snprintf(o, s, "psq_st f%u, 0x%x(r%u), %u, qr%u", 
                       d->frS, (uint16_t)d->d, d->rA, d->W, d->I);
    }
    return snprintf(o, s, "psq_st f%u, -0x%x(r%u), %u, qr%u",
                   d->frS, (uint16_t)(-d->d), d->rA, d->W, d->I);
}

#endif

