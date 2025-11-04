/**
 * @file lfdu.h
 * @brief LFDU - Load Floating-Point Double with Update
 * 
 * Opcode: 51
 * Format: D-form
 * Syntax: lfdu frD, d(rA)
 * 
 * Description: EA = rA + d; frD = double at EA; rA = EA
 */

#ifndef OPCODE_LFDU_H
#define OPCODE_LFDU_H

#include <stdint.h>
#include <stdbool.h>

#define OP_LFDU 51

typedef struct {
    uint8_t frD;
    uint8_t rA;
    int16_t d;
} LFDU_Instruction;

static inline bool decode_lfdu(uint32_t inst, LFDU_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LFDU) return false;
    
    d->frD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = inst & 0xFFFF;
    return true;
}

static inline int transpile_lfdu(const LFDU_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + (int16_t)0x%x; "
                   "f%u = *(double*)(mem + ea); "
                   "r%u = ea; }",
                   d->rA, (uint16_t)d->d, d->frD, d->rA);
}

static inline int comment_lfdu(const LFDU_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) return snprintf(o, s, "lfdu f%u, 0x%x(r%u)", d->frD, (uint16_t)d->d, d->rA);
    return snprintf(o, s, "lfdu f%u, -0x%x(r%u)", d->frD, (uint16_t)(-d->d), d->rA);
}

#endif // OPCODE_LFDU_H

