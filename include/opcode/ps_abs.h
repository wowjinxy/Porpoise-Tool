/**
 * @file ps_abs.h
 * @brief PS_ABS - Paired Single Absolute Value
 * Opcode: 4 / 264
 */

#ifndef OPCODE_PS_ABS_H
#define OPCODE_PS_ABS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t frD, frB;
    bool Rc;
} PS_ABS_Instruction;

static inline bool decode_ps_abs(uint32_t inst, PS_ABS_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 4 || ((inst >> 1) & 0x3FF) != 264) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_ps_abs(const PS_ABS_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* ps_abs f%u, f%u */", d->frD, d->frB);
}

static inline int comment_ps_abs(const PS_ABS_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ps_abs%s f%u, f%u", d->Rc?".":"", d->frD, d->frB);
}

#endif

