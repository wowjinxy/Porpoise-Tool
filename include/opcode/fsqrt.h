/**
 * @file fsqrt.h
 * @brief FSQRT - Floating-Point Square Root Double
 * Opcode: 63 / 22
 */

#ifndef OPCODE_FSQRT_H
#define OPCODE_FSQRT_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define OP_FSQRT_PRIMARY    63
#define OP_FSQRT_EXTENDED   22

typedef struct {
    uint8_t frD, frB;
    bool Rc;
} FSQRT_Instruction;

static inline bool decode_fsqrt(uint32_t inst, FSQRT_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 63 || ((inst >> 1) & 0x1F) != 22) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_fsqrt(const FSQRT_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "f%u = sqrt(f%u);", d->frD, d->frB);
    if (d->Rc) w += snprintf(o + w, s - w, "\ncr1 = (fpscr >> 28) & 0xF;");
    return w;
}

static inline int comment_fsqrt(const FSQRT_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "fsqrt%s f%u, f%u", d->Rc?".":"", d->frD, d->frB);
}

#endif

