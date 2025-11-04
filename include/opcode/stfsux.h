/**
 * @file stfsux.h
 * @brief STFSUX - Store Floating-Point Single with Update Indexed
 * 
 * Opcode: 31 / 695
 * Format: X-form
 * Syntax: stfsux frS, rA, rB
 * 
 * Description: EA = rA + rB; store frS to EA as single; rA = EA
 */

#ifndef OPCODE_STFSUX_H
#define OPCODE_STFSUX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_STFSUX_PRIMARY    31
#define OP_STFSUX_EXTENDED   695

typedef struct {
    uint8_t frS;
    uint8_t rA;
    uint8_t rB;
} STFSUX_Instruction;

static inline bool decode_stfsux(uint32_t inst, STFSUX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STFSUX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_STFSUX_EXTENDED) return false;
    
    d->frS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_stfsux(const STFSUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + r%u; "
                   "*(float*)(mem + ea) = (float)f%u; "
                   "r%u = ea; }",
                   d->rA, d->rB, d->frS, d->rA);
}

static inline int comment_stfsux(const STFSUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "stfsux f%u, r%u, r%u", d->frS, d->rA, d->rB);
}

#endif // OPCODE_STFSUX_H

