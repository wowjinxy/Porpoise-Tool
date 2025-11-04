/**
 * @file stfdux.h
 * @brief STFDUX - Store Floating-Point Double with Update Indexed
 * 
 * Opcode: 31 / 759
 * Format: X-form
 * Syntax: stfdux frS, rA, rB
 * 
 * Description: EA = rA + rB; store frS to EA as double; rA = EA
 */

#ifndef OPCODE_STFDUX_H
#define OPCODE_STFDUX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_STFDUX_PRIMARY    31
#define OP_STFDUX_EXTENDED   759

typedef struct {
    uint8_t frS;
    uint8_t rA;
    uint8_t rB;
} STFDUX_Instruction;

static inline bool decode_stfdux(uint32_t inst, STFDUX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STFDUX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_STFDUX_EXTENDED) return false;
    
    d->frS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_stfdux(const STFDUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + r%u; "
                   "*(double*)(mem + ea) = f%u; "
                   "r%u = ea; }",
                   d->rA, d->rB, d->frS, d->rA);
}

static inline int comment_stfdux(const STFDUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "stfdux f%u, r%u, r%u", d->frS, d->rA, d->rB);
}

#endif // OPCODE_STFDUX_H

