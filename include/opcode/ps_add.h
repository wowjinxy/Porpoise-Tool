/**
 * @file ps_add.h
 * @brief PS_ADD - Paired Single Add
 * Opcode: 4 / 21 (Gekko-specific)
 */

#ifndef OPCODE_PS_ADD_H
#define OPCODE_PS_ADD_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frA, frB;
    bool Rc;
} PS_ADD_Instruction;

static inline bool decode_ps_add(uint32_t inst, PS_ADD_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x1F) != 21) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_add(const PS_ADD_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_add f%u, f%u, f%u - paired single add */", d->frD, d->frA, d->frB);
}

static inline int comment_ps_add(const PS_ADD_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_add%s f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frB);
}

#endif

