/**
 * @file mfsr.h
 * @brief MFSR - Move From Segment Register
 * Opcode: 31 / 595
 */

#ifndef OPCODE_MFSR_H
#define OPCODE_MFSR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rD;
    uint8_t SR;  // Segment register (0-15)
} MFSR_Instruction;

static inline bool decode_mfsr(uint32_t inst, MFSR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 595) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->SR = (inst >> 16) & 0xF;
    return true;
}

static inline int transpile_mfsr(const MFSR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = sr[%u];  /* Move from segment register %u */", 
                   d->rD, d->SR, d->SR);
}

static inline int comment_mfsr(const MFSR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mfsr r%u, %u", d->rD, d->SR);
}

#endif

