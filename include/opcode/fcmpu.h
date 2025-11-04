/**
 * @file fcmpu.h
 * @brief FCMPU - Floating Compare Unordered
 * Opcode: 63 / 0
 */

#ifndef OPCODE_FCMPU_H
#define OPCODE_FCMPU_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t crfD, frA, frB;
} FCMPU_Instruction;

static inline bool decode_fcmpu(uint32_t inst, FCMPU_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 63 || ((inst >> 1) & 0x3FF) != 0) return false;
    d->crfD = (inst >> 23) & 0x7;
    d->frA = (inst >> 16) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_fcmpu(const FCMPU_Instruction *d, char *o, size_t s) {
    if (d->crfD == 0) {
        return snprintf(o, s, "cr0 = (f%u < f%u ? 0x8 : f%u > f%u ? 0x4 : f%u == f%u ? 0x2 : 0x1);",
                       d->frA, d->frB, d->frA, d->frB, d->frA, d->frB);
    }
    return snprintf(o, s, "cr%u = (f%u < f%u ? 0x8 : f%u > f%u ? 0x4 : f%u == f%u ? 0x2 : 0x1);",
                   d->crfD, d->frA, d->frB, d->frA, d->frB, d->frA, d->frB);
}

static inline int comment_fcmpu(const FCMPU_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "fcmpu cr%u, f%u, f%u", d->crfD, d->frA, d->frB);
}

#endif

