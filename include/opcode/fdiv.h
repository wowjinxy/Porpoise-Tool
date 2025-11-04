/**
 * @file fdiv.h
 * @brief FDIV - Floating Divide (Double-Precision)
 * Opcode: 63 / 18
 */

#ifndef OPCODE_FDIV_H
#define OPCODE_FDIV_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t frD, frA, frB;
    bool Rc;
} FDIV_Instruction;

static inline bool decode_fdiv(uint32_t inst, FDIV_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 63 || ((inst >> 1) & 0x1F) != 18) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_fdiv(const FDIV_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "f%u = f%u / f%u;", d->frD, d->frA, d->frB);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    return w;
}

static inline int comment_fdiv(const FDIV_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "fdiv%s f%u, f%u, f%u", d->Rc?".":"", d->frD, d->frA, d->frB);
}

#endif

