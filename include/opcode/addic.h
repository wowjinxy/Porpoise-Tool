/**
 * @file addic.h
 * @brief ADDIC - Add Immediate Carrying (no record)
 * Opcode: 12
 */

#ifndef OPCODE_ADDIC_H
#define OPCODE_ADDIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_ADDIC 12

typedef struct {
    uint8_t rD, rA;
    int16_t SIMM;
    bool Rc;  // For addic. (opcode 13)
} ADDIC_Instruction;

static inline bool decode_addic(uint32_t inst, ADDIC_Instruction *d) {
    uint8_t op = (inst >> 26) & 0x3F;
    if (op != 12 && op != 13) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->SIMM = inst & 0xFFFF;
    d->Rc = (op == 13);
    return true;
}

static inline int transpile_addic(const ADDIC_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "{ uint32_t old = r%u; r%u = r%u + (int16_t)0x%x; "
                   "xer = (xer & ~0x20000000) | ((r%u < old) ? 0x20000000 : 0); }",
                   d->rA, d->rD, d->rA, (uint16_t)d->SIMM, d->rD);
    if (d->Rc) {
        w += snprintf(o + w, s - w, "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                     "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);", d->rD, d->rD);
    }
    return w;
}

static inline int comment_addic(const ADDIC_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "addic%s r%u, r%u, 0x%x", d->Rc?".":"", d->rD, d->rA, (uint16_t)d->SIMM);
}

#endif

