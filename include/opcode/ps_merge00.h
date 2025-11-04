/**
 * @file ps_merge00.h
 * @brief PS_MERGE00 - Paired Single Merge High
 * Opcode: 4 / 528
 */

#ifndef OPCODE_PS_MERGE00_H
#define OPCODE_PS_MERGE00_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frB;
    bool Rc;
} PS_MERGE00_Instruction;

static inline bool decode_ps_merge00(uint32_t inst, PS_MERGE00_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x3FF) != 528) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_merge00(const PS_MERGE00_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_merge00 f%u, f%u, f%u - merge high */", d->frD, d->frA, d->frB);
}

static inline int comment_ps_merge00(const PS_MERGE00_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_merge00%s f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frB);
}

#endif

