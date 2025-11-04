/**
 * @file mtcrf.h
 * @brief MTCRF - Move To Condition Register Fields
 * Opcode: 31 / 144
 */

#ifndef OPCODE_MTCRF_H
#define OPCODE_MTCRF_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rS;
    uint8_t FXM;  // Field mask
} MTCRF_Instruction;

static inline bool decode_mtcrf(uint32_t inst, MTCRF_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 144) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->FXM = (inst >> 12) & 0xFF;
    return true;
}

static inline int transpile_mtcrf(const MTCRF_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "/* mtcrf 0x%02X, r%u */ cr = r%u;", d->FXM, d->rS, d->rS);
}

static inline int comment_mtcrf(const MTCRF_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mtcrf %u, r%u", d->FXM, d->rS);
}

#endif

