/**
 * @file subfe.h
 * @brief SUBFE - Subtract From Extended
 * 
 * Opcode: 31 / 136
 * Syntax: subfe rD, rA, rB
 * Description: rD = rB - rA + CA - 1
 */

#ifndef OPCODE_SUBFE_H
#define OPCODE_SUBFE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_SUBFE_PRIMARY    31
#define OP_SUBFE_EXTENDED   136

typedef struct {
    uint8_t rD, rA, rB;
    bool OE, Rc;
} SUBFE_Instruction;

static inline bool decode_subfe(uint32_t inst, SUBFE_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 136) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->OE = (inst >> 10) & 1;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_subfe(const SUBFE_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "{ uint32_t ca = (xer >> 29) & 1; r%u = r%u - r%u + ca - 1; "
                     "if ((r%u >= r%u) || (ca && r%u == r%u)) xer |= 0x20000000; "
                     "else xer &= ~0x20000000; }",
                     d->rD, d->rB, d->rA, d->rB, d->rA, d->rB, d->rA);
    if (d->Rc) w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                             "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rD, d->rD);
    return w;
}

static inline int comment_subfe(const SUBFE_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "subfe%s%s r%u, r%u, r%u", d->OE?"o":"", d->Rc?".":"", d->rD, d->rA, d->rB);
}

#endif

