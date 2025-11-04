/**
 * @file ps_merge01.h
 * @brief PS_MERGE01 - Paired Single Merge Direct
 * Opcode: 4 / 560
 */

#ifndef OPCODE_PS_MERGE01_H
#define OPCODE_PS_MERGE01_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frB;
    bool Rc;
} PS_MERGE01_Instruction;

static inline bool decode_ps_merge01(uint32_t inst, PS_MERGE01_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x3FF) != 560) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_merge01(const PS_MERGE01_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_merge01 f%u, f%u, f%u - merge direct */", d->frD, d->frA, d->frB);
}

static inline int comment_ps_merge01(const PS_MERGE01_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_merge01%s f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frB);
}

#endif

