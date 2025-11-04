/**
 * @file fsqrts.h
 * @brief FSQRTS - Floating-Point Square Root Single
 * Opcode: 59 / 22
 */

#ifndef OPCODE_FSQRTS_H
#define OPCODE_FSQRTS_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define OP_FSQRTS_PRIMARY    59
#define OP_FSQRTS_EXTENDED   22

typedef struct {
    uint8_t frD, frB;
    bool Rc;
} FSQRTS_Instruction;

static inline bool decode_fsqrts(uint32_t inst, FSQRTS_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 59 || ((inst >> 1) & 0x1F) != 22) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_fsqrts(const FSQRTS_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "f%u = (float)sqrt(f%u);", d->frD, d->frB);
    if (d->Rc) w += snprintf(o + w, s - w, "\ncr1 = (fpscr >> 28) & 0xF;");
    return w;
}

static inline int comment_fsqrts(const FSQRTS_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "fsqrts%s f%u, f%u", d->Rc?".":"", d->frD, d->frB);
}

#endif

