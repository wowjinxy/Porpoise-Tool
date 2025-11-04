/**
 * @file lfdux.h
 * @brief LFDUX - Load Floating-Point Double with Update Indexed
 * 
 * Opcode: 31 / 631
 * Format: X-form
 * Syntax: lfdux frD, rA, rB
 * 
 * Description: EA = rA + rB; frD = double at EA; rA = EA
 */

#ifndef OPCODE_LFDUX_H
#define OPCODE_LFDUX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_LFDUX_PRIMARY    31
#define OP_LFDUX_EXTENDED   631

typedef struct {
    uint8_t frD;
    uint8_t rA;
    uint8_t rB;
} LFDUX_Instruction;

static inline bool decode_lfdux(uint32_t inst, LFDUX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LFDUX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_LFDUX_EXTENDED) return false;
    
    d->frD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lfdux(const LFDUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + r%u; "
                   "f%u = *(double*)(mem + ea); "
                   "r%u = ea; }",
                   d->rA, d->rB, d->frD, d->rA);
}

static inline int comment_lfdux(const LFDUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lfdux f%u, r%u, r%u", d->frD, d->rA, d->rB);
}

#endif // OPCODE_LFDUX_H

