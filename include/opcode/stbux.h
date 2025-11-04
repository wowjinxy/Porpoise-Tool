/**
 * @file stbux.h
 * @brief STBUX - Store Byte with Update Indexed
 * 
 * Opcode: 31 / 247
 * Format: X-form
 * Syntax: stbux rS, rA, rB
 * 
 * Description: EA = rA + rB; store byte from rS to EA; rA = EA
 */

#ifndef OPCODE_STBUX_H
#define OPCODE_STBUX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_STBUX_PRIMARY    31
#define OP_STBUX_EXTENDED   247

typedef struct {
    uint8_t rS;
    uint8_t rA;
    uint8_t rB;
} STBUX_Instruction;

static inline bool decode_stbux(uint32_t inst, STBUX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STBUX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_STBUX_EXTENDED) return false;
    
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_stbux(const STBUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + r%u; "
                   "*(uint8_t*)(mem + ea) = (uint8_t)r%u; "
                   "r%u = ea; }",
                   d->rA, d->rB, d->rS, d->rA);
}

static inline int comment_stbux(const STBUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "stbux r%u, r%u, r%u", d->rS, d->rA, d->rB);
}

#endif // OPCODE_STBUX_H

