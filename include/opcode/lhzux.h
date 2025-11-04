/**
 * @file lhzux.h
 * @brief LHZUX - Load Halfword and Zero with Update Indexed
 * 
 * Opcode: 31 / 311
 * Format: X-form
 * Syntax: lhzux rD, rA, rB
 * 
 * Description: EA = rA + rB; rD = halfword at EA (zero extended); rA = EA
 */

#ifndef OPCODE_LHZUX_H
#define OPCODE_LHZUX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LHZUX_PRIMARY    31
#define OP_LHZUX_EXTENDED   311

typedef struct {
    uint8_t rD;
    uint8_t rA;
    uint8_t rB;
} LHZUX_Instruction;

static inline bool decode_lhzux(uint32_t inst, LHZUX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LHZUX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_LHZUX_EXTENDED) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lhzux(const LHZUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + r%u; "
                   "r%u = *(uint16_t*)(mem + ea); "
                   "r%u = ea; }",
                   d->rA, d->rB, d->rD, d->rA);
}

static inline int comment_lhzux(const LHZUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lhzux r%u, r%u, r%u", d->rD, d->rA, d->rB);
}

#endif // OPCODE_LHZUX_H

