/**
 * @file ps_mul.h
 * @brief PS_MUL - Paired Single Multiply
 * Opcode: 4 / 25
 */

#ifndef OPCODE_PS_MUL_H
#define OPCODE_PS_MUL_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frC;
    bool Rc;
} PS_MUL_Instruction;

static inline bool decode_ps_mul(uint32_t inst, PS_MUL_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 25) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frC = (inst >> 6) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_mul(const PS_MUL_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_mul f%u, f%u, f%u - paired single mul */", d->frD, d->frA, d->frC);
}

static inline int comment_ps_mul(const PS_MUL_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_mul%s f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frC);
}

#endif

