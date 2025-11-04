/**
 * @file fctiwz.h
 * @brief FCTIWZ - Floating Convert To Integer Word with round toward Zero
 * Opcode: 63 / 15
 */

#ifndef OPCODE_FCTIWZ_H
#define OPCODE_FCTIWZ_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t frD, frB;
    bool Rc;
} FCTIWZ_Instruction;

static inline bool decode_fctiwz(uint32_t inst, FCTIWZ_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 63 || ((inst >> 1) & 0x3FF) != 15) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_fctiwz(const FCTIWZ_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "f%u = (double)(int32_t)f%u;", d->frD, d->frB);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    return w;
}

static inline int comment_fctiwz(const FCTIWZ_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "fctiwz%s f%u, f%u", d->Rc?".":"", d->frD, d->frB);
}

#endif

