/**
 * @file subfc.h
 * @brief SUBFC - Subtract From Carrying
 * 
 * Opcode: 31 / 8
 * Syntax: subfc rD, rA, rB
 * Description: rD = rB - rA, set CA if no borrow
 */

#ifndef OPCODE_SUBFC_H
#define OPCODE_SUBFC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_SUBFC_PRIMARY    31
#define OP_SUBFC_EXTENDED   8

typedef struct {
    uint8_t rD, rA, rB;
    bool OE, Rc;
} SUBFC_Instruction;

static inline bool decode_subfc(uint32_t inst, SUBFC_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 8) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->OE = (inst >> 10) & 1;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_subfc(const SUBFC_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "r%u = r%u - r%u; "
                     "if (r%u >= r%u) xer |= 0x20000000; else xer &= ~0x20000000;",
                     d->rD, d->rB, d->rA, d->rB, d->rA);
    if (d->Rc) w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                             "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rD, d->rD);
    return w;
}

static inline int comment_subfc(const SUBFC_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "subfc%s%s r%u, r%u, r%u", d->OE?"o":"", d->Rc?".":"", d->rD, d->rA, d->rB);
}

#endif

