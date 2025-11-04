/**
 * @file lwzux.h
 * @brief LWZUX - Load Word and Zero with Update Indexed
 * 
 * Opcode: 31 / 55
 * Format: X-form
 * Syntax: lwzux rD, rA, rB
 * 
 * Description: EA = rA + rB; rD = word at EA; rA = EA
 */

#ifndef OPCODE_LWZUX_H
#define OPCODE_LWZUX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LWZUX_PRIMARY    31
#define OP_LWZUX_EXTENDED   55

typedef struct {
    uint8_t rD;
    uint8_t rA;
    uint8_t rB;
} LWZUX_Instruction;

static inline bool decode_lwzux(uint32_t inst, LWZUX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LWZUX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_LWZUX_EXTENDED) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lwzux(const LWZUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + r%u; "
                   "r%u = *(uint32_t*)(mem + ea); "
                   "r%u = ea; }",
                   d->rA, d->rB, d->rD, d->rA);
}

static inline int comment_lwzux(const LWZUX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lwzux r%u, r%u, r%u", d->rD, d->rA, d->rB);
}

#endif // OPCODE_LWZUX_H

