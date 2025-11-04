/**
 * @file stwux.h
 * @brief STWUX - Store Word with Update Indexed
 * 
 * Opcode: 31 / 183
 * Format: X-form
 * Syntax: stwux rS, rA, rB
 * 
 * Description: EA = rA + rB; store word from rS to EA; rA = EA
 */

#ifndef OPCODE_STWUX_H
#define OPCODE_STWUX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_STWUX_PRIMARY    31
#define OP_STWUX_EXTENDED   183

typedef struct {
    uint8_t rS;
    uint8_t rA;
    uint8_t rB;
} STWUX_Instruction;

static inline bool decode_stwux(uint32_t inst, STWUX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STWUX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_STWUX_EXTENDED) return false;
    
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_stwux(const STWUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + r%u; "
                   "*(uint32_t*)(mem + ea) = r%u; "
                   "r%u = ea; }",
                   d->rA, d->rB, d->rS, d->rA);
}

static inline int comment_stwux(const STWUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "stwux r%u, r%u, r%u", d->rS, d->rA, d->rB);
}

#endif // OPCODE_STWUX_H

