/**
 * @file cror.h
 * @brief CROR - Condition Register OR
 * Opcode: 19 / 449
 */

#ifndef OPCODE_CROR_H
#define OPCODE_CROR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t crbD, crbA, crbB;
} CROR_Instruction;

static inline bool decode_cror(uint32_t inst, CROR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 19 || ((inst >> 1) & 0x3FF) != 449) return false;
    d->crbD = (inst >> 21) & 0x1F;
    d->crbA = (inst >> 16) & 0x1F;
    d->crbB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_cror(const CROR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "/* cror: CR bit %u = CR bit %u | CR bit %u */", 
                   d->crbD, d->crbA, d->crbB);
}

static inline int comment_cror(const CROR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "cror %u, %u, %u", d->crbD, d->crbA, d->crbB);
}

#endif

