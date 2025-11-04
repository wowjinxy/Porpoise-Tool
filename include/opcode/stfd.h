/**
 * @file stfd.h
 * @brief STFD - Store Floating-Point Double
 * 
 * Opcode: 54
 * Syntax: stfd frS, d(rA)
 */

#ifndef OPCODE_STFD_H
#define OPCODE_STFD_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_STFD 54

typedef struct {
    uint8_t frS, rA;
    int16_t d;
} STFD_Instruction;

static inline bool decode_stfd(uint32_t inst, STFD_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 54) return false;
    d->frS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = inst & 0xFFFF;
    return true;
}

static inline int transpile_stfd(const STFD_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        return snprintf(o, s, "*(double*)(mem + 0x%x) = f%u;", (uint32_t)d->d, d->frS);
    } else if (d->d == 0) {
        return snprintf(o, s, "*(double*)(mem + r%u) = f%u;", d->rA, d->frS);
    } else if (d->d > 0) {
        return snprintf(o, s, "*(double*)(mem + r%u + 0x%x) = f%u;", d->rA, (uint16_t)d->d, d->frS);
    } else {
        return snprintf(o, s, "*(double*)(mem + r%u - 0x%x) = f%u;", d->rA, (uint16_t)(-d->d), d->frS);
    }
}

static inline int comment_stfd(const STFD_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) return snprintf(o, s, "stfd f%u, 0x%x(r%u)", d->frS, (uint16_t)d->d, d->rA);
    return snprintf(o, s, "stfd f%u, -0x%x(r%u)", d->frS, (uint16_t)(-d->d), d->rA);
}

#endif

