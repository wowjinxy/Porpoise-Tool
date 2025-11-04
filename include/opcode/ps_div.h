/**
 * @file ps_div.h
 * @brief PS_DIV - Paired Single Divide
 * Opcode: 4 / 18
 */

#ifndef OPCODE_PS_DIV_H
#define OPCODE_PS_DIV_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frB;
    bool Rc;
} PS_DIV_Instruction;

static inline bool decode_ps_div(uint32_t inst, PS_DIV_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 18) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_div(const PS_DIV_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_div f%u, f%u, f%u - paired single div */", d->frD, d->frA, d->frB);
}

static inline int comment_ps_div(const PS_DIV_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_div%s f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frB);
}

#endif

