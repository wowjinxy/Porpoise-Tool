/**
 * @file ps_rsqrte.h
 * @brief PS_RSQRTE - Paired Single Reciprocal Square Root Estimate
 * Opcode: 4 / 26
 */

#ifndef OPCODE_PS_RSQRTE_H
#define OPCODE_PS_RSQRTE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frB;
    bool Rc;
} PS_RSQRTE_Instruction;

static inline bool decode_ps_rsqrte(uint32_t inst, PS_RSQRTE_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 26) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_rsqrte(const PS_RSQRTE_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_rsqrte f%u, f%u */", d->frD, d->frB);
}

static inline int comment_ps_rsqrte(const PS_RSQRTE_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_rsqrte%s f%u, f%u", d->Rc?".":"", d->frD, d->frB);
}

#endif

