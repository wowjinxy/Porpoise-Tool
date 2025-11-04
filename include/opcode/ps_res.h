/**
 * @file ps_res.h
 * @brief PS_RES - Paired Single Reciprocal Estimate
 * Opcode: 4 / 24
 */

#ifndef OPCODE_PS_RES_H
#define OPCODE_PS_RES_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frB;
    bool Rc;
} PS_RES_Instruction;

static inline bool decode_ps_res(uint32_t inst, PS_RES_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 24) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_res(const PS_RES_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_res f%u, f%u */", d->frD, d->frB);
}

static inline int comment_ps_res(const PS_RES_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_res%s f%u, f%u", d->Rc?".":"", d->frD, d->frB);
}

#endif

