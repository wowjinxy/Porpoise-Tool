/**
 * @file ps_merge11.h
 * @brief PS_MERGE11 - Paired Single Merge Low
 * Opcode: 4 / 624
 */

#ifndef OPCODE_PS_MERGE11_H
#define OPCODE_PS_MERGE11_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frB;
    bool Rc;
} PS_MERGE11_Instruction;

static inline bool decode_ps_merge11(uint32_t inst, PS_MERGE11_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x3FF) != 624) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_merge11(const PS_MERGE11_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_merge11 f%u, f%u, f%u - merge low */", d->frD, d->frA, d->frB);
}

static inline int comment_ps_merge11(const PS_MERGE11_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_merge11%s f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frB);
}

#endif

