/**
 * @file ps_mr.h
 * @brief PS_MR - Paired Single Move Register
 * Opcode: 4 / 72
 */

#ifndef OPCODE_PS_MR_H
#define OPCODE_PS_MR_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frB;
    bool Rc;
} PS_MR_Instruction;

static inline bool decode_ps_mr(uint32_t inst, PS_MR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x3FF) != 72) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_mr(const PS_MR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_mr f%u, f%u */", d->frD, d->frB);
}

static inline int comment_ps_mr(const PS_MR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_mr%s f%u, f%u", d->Rc?".":"", d->frD, d->frB);
}

#endif

