/**
 * @file fmr.h
 * @brief FMR - Floating Move Register
 * Opcode: 63 / 72
 */

#ifndef OPCODE_FMR_H
#define OPCODE_FMR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t frD, frB;
    bool Rc;
} FMR_Instruction;

static inline bool decode_fmr(uint32_t inst, FMR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 63 || ((inst >> 1) & 0x3FF) != 72) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_fmr(const FMR_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "f%u = f%u;", d->frD, d->frB);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    return w;
}

static inline int comment_fmr(const FMR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "fmr%s f%u, f%u", d->Rc?".":"", d->frD, d->frB);
}

#endif

