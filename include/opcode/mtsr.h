/**
 * @file mtsr.h
 * @brief MTSR - Move To Segment Register
 * Opcode: 31 / 210
 */

#ifndef OPCODE_MTSR_H
#define OPCODE_MTSR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rS;
    uint8_t SR;  // Segment register (0-15)
} MTSR_Instruction;

static inline bool decode_mtsr(uint32_t inst, MTSR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 210) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->SR = (inst >> 16) & 0xF;
    return true;
}

static inline int transpile_mtsr(const MTSR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "sr[%u] = r%u;  /* Move to segment register %u */", 
                   d->SR, d->rS, d->SR);
}

static inline int comment_mtsr(const MTSR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mtsr %u, r%u", d->SR, d->rS);
}

#endif

