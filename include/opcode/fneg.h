/**
 * @file fneg.h
 * @brief FNEG - Floating Negate
 * Opcode: 63 / 40
 */

#ifndef OPCODE_FNEG_H
#define OPCODE_FNEG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t frD, frB;
    bool Rc;
} FNEG_Instruction;

static inline bool decode_fneg(uint32_t inst, FNEG_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 63 || ((inst >> 1) & 0x3FF) != 40) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_fneg(const FNEG_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "f%u = -f%u;", d->frD, d->frB);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    return w;
}

static inline int comment_fneg(const FNEG_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "fneg%s f%u, f%u", d->Rc?".":"", d->frD, d->frB);
}

#endif

