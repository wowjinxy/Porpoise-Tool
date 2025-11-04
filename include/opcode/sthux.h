/**
 * @file sthux.h
 * @brief STHUX - Store Halfword with Update Indexed
 * 
 * Opcode: 31 / 439
 * Format: X-form
 * Syntax: sthux rS, rA, rB
 * 
 * Description: EA = rA + rB; store halfword from rS to EA; rA = EA
 */

#ifndef OPCODE_STHUX_H
#define OPCODE_STHUX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_STHUX_PRIMARY    31
#define OP_STHUX_EXTENDED   439

typedef struct {
    uint8_t rS;
    uint8_t rA;
    uint8_t rB;
} STHUX_Instruction;

static inline bool decode_sthux(uint32_t inst, STHUX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STHUX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_STHUX_EXTENDED) return false;
    
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_sthux(const STHUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + r%u; "
                   "*(uint16_t*)(mem + ea) = (uint16_t)r%u; "
                   "r%u = ea; }",
                   d->rA, d->rB, d->rS, d->rA);
}

static inline int comment_sthux(const STHUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "sthux r%u, r%u, r%u", d->rS, d->rA, d->rB);
}

#endif // OPCODE_STHUX_H

