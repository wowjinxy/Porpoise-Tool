/**
 * @file lfsux.h
 * @brief LFSUX - Load Floating-Point Single with Update Indexed
 * 
 * Opcode: 31 / 567
 * Format: X-form
 * Syntax: lfsux frD, rA, rB
 * 
 * Description: EA = rA + rB; frD = single at EA (as double); rA = EA
 */

#ifndef OPCODE_LFSUX_H
#define OPCODE_LFSUX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_LFSUX_PRIMARY    31
#define OP_LFSUX_EXTENDED   567

typedef struct {
    uint8_t frD;
    uint8_t rA;
    uint8_t rB;
} LFSUX_Instruction;

static inline bool decode_lfsux(uint32_t inst, LFSUX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LFSUX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_LFSUX_EXTENDED) return false;
    
    d->frD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lfsux(const LFSUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + r%u; "
                   "f%u = (double)*(float*)(mem + ea); "
                   "r%u = ea; }",
                   d->rA, d->rB, d->frD, d->rA);
}

static inline int comment_lfsux(const LFSUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lfsux f%u, r%u, r%u", d->frD, d->rA, d->rB);
}

#endif // OPCODE_LFSUX_H

