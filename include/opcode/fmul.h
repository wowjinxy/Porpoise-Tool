/**
 * @file fmul.h
 * @brief FMUL - Floating Multiply (Double-Precision)
 * Opcode: 63 / 25
 */

#ifndef OPCODE_FMUL_H
#define OPCODE_FMUL_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t frD, frA, frC;
    bool Rc;
} FMUL_Instruction;

static inline bool decode_fmul(uint32_t inst, FMUL_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 63 || ((inst >> 1) & 0x1F) != 25) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frC = (inst >> 6) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_fmul(const FMUL_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "f%u = f%u * f%u;", d->frD, d->frA, d->frC);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    return w;
}

static inline int comment_fmul(const FMUL_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "fmul%s f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frC);
}

#endif

