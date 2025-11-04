/**
 * @file ps_sub.h
 * @brief PS_SUB - Paired Single Subtract
 * Opcode: 4 / 20
 */

#ifndef OPCODE_PS_SUB_H
#define OPCODE_PS_SUB_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frB;
    bool Rc;
} PS_SUB_Instruction;

static inline bool decode_ps_sub(uint32_t inst, PS_SUB_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 20) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_sub(const PS_SUB_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_sub f%u, f%u, f%u - paired single sub */", d->frD, d->frA, d->frB);
}

static inline int comment_ps_sub(const PS_SUB_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_sub%s f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frB);
}

#endif

