/**
 * @file adde.h
 * @brief ADDE - Add Extended
 * 
 * Opcode: 31 / 138
 * Syntax: adde rD, rA, rB
 * Description: rD = rA + rB + CA
 */

#ifndef OPCODE_ADDE_H
#define OPCODE_ADDE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_ADDE_PRIMARY     31
#define OP_ADDE_EXTENDED    138

typedef struct {
    uint8_t rD, rA, rB;
    bool OE, Rc;
} ADDE_Instruction;

static inline bool decode_adde(uint32_t inst, ADDE_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 138) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->OE = (inst >> 10) & 1;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_adde(const ADDE_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "{ uint64_t sum = (uint64_t)r%u + r%u + (xer >> 29 & 1); r%u = sum; "
                     "if (sum > 0xFFFFFFFF) xer |= 0x20000000; else xer &= ~0x20000000; }",
                     d->rA, d->rB, d->rD);
    if (d->Rc) w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                             "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rD, d->rD);
    return w;
}

static inline int comment_adde(const ADDE_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "adde%s%s r%u, r%u, r%u", d->OE?"o":"", d->Rc?".":"", d->rD, d->rA, d->rB);
}

#endif

