/**
 * @file stfdu.h
 * @brief STFDU - Store Floating-Point Double with Update
 * 
 * Opcode: 55
 * Format: D-form
 * Syntax: stfdu frS, d(rA)
 * 
 * Description: EA = rA + d; store frS to EA as double; rA = EA
 */

#ifndef OPCODE_STFDU_H
#define OPCODE_STFDU_H

#include <stdint.h>
#include <stdbool.h>

#define OP_STFDU 55

typedef struct {
    uint8_t frS;
    uint8_t rA;
    int16_t d;
} STFDU_Instruction;

static inline bool decode_stfdu(uint32_t inst, STFDU_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STFDU) return false;
    
    d->frS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = inst & 0xFFFF;
    return true;
}

static inline int transpile_stfdu(const STFDU_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + (int16_t)0x%x; "
                   "*(double*)(mem + ea) = f%u; "
                   "r%u = ea; }",
                   d->rA, (uint16_t)d->d, d->frS, d->rA);
}

static inline int comment_stfdu(const STFDU_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) return snprintf(o, s, "stfdu f%u, 0x%x(r%u)", d->frS, (uint16_t)d->d, d->rA);
    return snprintf(o, s, "stfdu f%u, -0x%x(r%u)", d->frS, (uint16_t)(-d->d), d->rA);
}

#endif // OPCODE_STFDU_H

