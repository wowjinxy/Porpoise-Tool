/**
 * @file lhau.h
 * @brief LHAU - Load Halfword Algebraic with Update
 * 
 * Opcode: 43
 * Format: D-form
 * Syntax: lhau rD, d(rA)
 * 
 * Description: EA = rA + d; rD = halfword at EA (sign extended); rA = EA
 */

#ifndef OPCODE_LHAU_H
#define OPCODE_LHAU_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LHAU 43

typedef struct {
    uint8_t rD;
    uint8_t rA;
    int16_t d;
} LHAU_Instruction;

static inline bool decode_lhau(uint32_t inst, LHAU_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LHAU) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = inst & 0xFFFF;
    return true;
}

static inline int transpile_lhau(const LHAU_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + (int16_t)0x%x; "
                   "r%u = (int32_t)(int16_t)*(uint16_t*)(mem + ea); "
                   "r%u = ea; }",
                   d->rA, (uint16_t)d->d, d->rD, d->rA);
}

static inline int comment_lhau(const LHAU_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) return snprintf(o, s, "lhau r%u, 0x%x(r%u)", d->rD, (uint16_t)d->d, d->rA);
    return snprintf(o, s, "lhau r%u, -0x%x(r%u)", d->rD, (uint16_t)(-d->d), d->rA);
}

#endif // OPCODE_LHAU_H

