/**
 * @file ps_nmadd.h
 * @brief PS_NMADD - Paired Single Negative Multiply-Add
 * Opcode: 4 / 31
 */

#ifndef OPCODE_PS_NMADD_H
#define OPCODE_PS_NMADD_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frB, frC;
    bool Rc;
} PS_NMADD_Instruction;

static inline bool decode_ps_nmadd(uint32_t inst, PS_NMADD_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 31) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->frC = (inst >> 6) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_nmadd(const PS_NMADD_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_nmadd f%u, f%u, f%u, f%u */", d->frD, d->frA, d->frC, d->frB);
}

static inline int comment_ps_nmadd(const PS_NMADD_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_nmadd%s f%u, f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frC, d->frB);
}

#endif

