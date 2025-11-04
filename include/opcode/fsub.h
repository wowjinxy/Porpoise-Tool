/**
 * @file fsub.h
 * @brief FSUB - Floating Subtract (Double-Precision)
 * Opcode: 63 / 20
 */

#ifndef OPCODE_FSUB_H
#define OPCODE_FSUB_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t frD, frA, frB;
    bool Rc;
} FSUB_Instruction;

static inline bool decode_fsub(uint32_t inst, FSUB_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 63 || ((inst >> 1) & 0x1F) != 20) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_fsub(const FSUB_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "f%u = f%u - f%u;", d->frD, d->frA, d->frB);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    return w;
}

static inline int comment_fsub(const FSUB_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "fsub%s f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frB);
}

#endif

