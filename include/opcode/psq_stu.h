/**
 * @file psq_stu.h
 * @brief PSQ_STU - Paired Single Quantized Store with Update
 * Opcode: 61
 */

#ifndef OPCODE_PSQ_STU_H
#define OPCODE_PSQ_STU_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frS, rA;
    int16_t d;
    uint8_t W, I;
} PSQ_STU_Instruction;

static inline bool decode_psq_stu(uint32_t inst, PSQ_STU_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 61) return false;
    d->frS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = (inst >> 4) & 0xFFF;
    d->W = (inst >> 3) & 1;
    d->I = inst & 0x7;
    return true;
}

static inline int transpile_psq_stu(const PSQ_STU_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* psq_stu f%u, 0x%x(r%u), %u, qr%u */",
                   d->frS, d->d, d->rA, d->W, d->I);
}

static inline int comment_psq_stu(const PSQ_STU_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "psq_stu f%u, 0x%x(r%u), %u, qr%u",
                   d->frS, d->d, d->rA, d->W, d->I);
}

#endif

