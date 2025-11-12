/**
 * @file lfd.h
 * @brief LFD - Load Floating-Point Double
 * 
 * Opcode: 50
 * Syntax: lfd frD, d(rA)
 */

#ifndef OPCODE_LFD_H
#define OPCODE_LFD_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LFD 50

typedef struct {
    uint8_t frD, rA;
    int16_t d;
} LFD_Instruction;

static inline bool decode_lfd(uint32_t inst, LFD_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 50) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = inst & 0xFFFF;
    return true;
}

static inline int transpile_lfd(const LFD_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        // Absolute address - should be resolved by transpiler to actual symbol/location
        uint32_t abs_addr = (uint32_t)(int16_t)d->d;
        return snprintf(o, s, "f%u = *(double*)(uintptr_t)0x%08X;", d->frD, abs_addr);
    } else if (d->d == 0) {
        return snprintf(o, s, "f%u = *(double*)(r%u);", d->frD, d->rA);
    } else if (d->d > 0) {
        return snprintf(o, s, "f%u = *(double*)(r%u + 0x%x);", d->frD, d->rA, (uint16_t)d->d);
    } else {
        return snprintf(o, s, "f%u = *(double*)(r%u - 0x%x);", d->frD, d->rA, (uint16_t)(-d->d));
    }
}

static inline int comment_lfd(const LFD_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) return snprintf(o, s, "lfd f%u, 0x%x(r%u)", d->frD, (uint16_t)d->d, d->rA);
    return snprintf(o, s, "lfd f%u, -0x%x(r%u)", d->frD, (uint16_t)(-d->d), d->rA);
}

#endif

