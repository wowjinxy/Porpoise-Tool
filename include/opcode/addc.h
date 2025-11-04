/**
 * @file addc.h
 * @brief ADDC - Add Carrying
 * 
 * Opcode: 31 (primary) / 10 (extended)
 * Syntax: addc rD, rA, rB
 * 
 * Description: rD = rA + rB, set CA in XER if carry
 */

#ifndef OPCODE_ADDC_H
#define OPCODE_ADDC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_ADDC_PRIMARY     31
#define OP_ADDC_EXTENDED    10

typedef struct {
    uint8_t rD, rA, rB;
    bool OE, Rc;
} ADDC_Instruction;

static inline bool decode_addc(uint32_t inst, ADDC_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 10) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->OE = (inst >> 10) & 1;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_addc(const ADDC_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "{ uint64_t sum = (uint64_t)r%u + r%u; r%u = sum; "
                     "if (sum > 0xFFFFFFFF) xer |= 0x20000000; else xer &= ~0x20000000; }",
                     d->rA, d->rB, d->rD);
    if (d->Rc) w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                             "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rD, d->rD);
    return w;
}

static inline int comment_addc(const ADDC_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "addc%s%s r%u, r%u, r%u", d->OE?"o":"", d->Rc?".":"", d->rD, d->rA, d->rB);
}

#endif

