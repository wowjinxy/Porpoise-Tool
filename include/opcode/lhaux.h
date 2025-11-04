/**
 * @file lhaux.h
 * @brief LHAUX - Load Halfword Algebraic with Update Indexed
 * 
 * Opcode: 31 / 375
 * Format: X-form
 * Syntax: lhaux rD, rA, rB
 * 
 * Description: EA = rA + rB; rD = halfword at EA (sign extended); rA = EA
 */

#ifndef OPCODE_LHAUX_H
#define OPCODE_LHAUX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LHAUX_PRIMARY    31
#define OP_LHAUX_EXTENDED   375

typedef struct {
    uint8_t rD;
    uint8_t rA;
    uint8_t rB;
} LHAUX_Instruction;

static inline bool decode_lhaux(uint32_t inst, LHAUX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LHAUX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_LHAUX_EXTENDED) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lhaux(const LHAUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + r%u; "
                   "r%u = (int32_t)(int16_t)*(uint16_t*)(mem + ea); "
                   "r%u = ea; }",
                   d->rA, d->rB, d->rD, d->rA);
}

static inline int comment_lhaux(const LHAUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lhaux r%u, r%u, r%u", d->rD, d->rA, d->rB);
}

#endif // OPCODE_LHAUX_H

