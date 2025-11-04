/**
 * @file lbzux.h
 * @brief LBZUX - Load Byte and Zero with Update Indexed
 * 
 * Opcode: 31 / 119
 * Format: X-form
 * Syntax: lbzux rD, rA, rB
 * 
 * Description: EA = rA + rB; rD = byte at EA (zero extended); rA = EA
 */

#ifndef OPCODE_LBZUX_H
#define OPCODE_LBZUX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LBZUX_PRIMARY    31
#define OP_LBZUX_EXTENDED   119

typedef struct {
    uint8_t rD;
    uint8_t rA;
    uint8_t rB;
} LBZUX_Instruction;

static inline bool decode_lbzux(uint32_t inst, LBZUX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LBZUX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_LBZUX_EXTENDED) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lbzux(const LBZUX_Instruction *d, char *o, size_t s) {
    // rA must not be 0 or rD for update indexed
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + r%u; "
                   "r%u = *(uint8_t*)(mem + ea); "
                   "r%u = ea; }",
                   d->rA, d->rB, d->rD, d->rA);
}

static inline int comment_lbzux(const LBZUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lbzux r%u, r%u, r%u", d->rD, d->rA, d->rB);
}

#endif // OPCODE_LBZUX_H

