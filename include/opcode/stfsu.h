/**
 * @file stfsu.h
 * @brief STFSU - Store Floating-Point Single with Update
 * 
 * Opcode: 53
 * Format: D-form
 * Syntax: stfsu frS, d(rA)
 * 
 * Description: EA = rA + d; store frS to EA as single; rA = EA
 */

#ifndef OPCODE_STFSU_H
#define OPCODE_STFSU_H

#include <stdint.h>
#include <stdbool.h>

#define OP_STFSU 53

typedef struct {
    uint8_t frS;
    uint8_t rA;
    int16_t d;
} STFSU_Instruction;

static inline bool decode_stfsu(uint32_t inst, STFSU_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STFSU) return false;
    
    d->frS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = inst & 0xFFFF;
    return true;
}

static inline int transpile_stfsu(const STFSU_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + (int16_t)0x%x; "
                   "*(float*)(mem + ea) = (float)f%u; "
                   "r%u = ea; }",
                   d->rA, (uint16_t)d->d, d->frS, d->rA);
}

static inline int comment_stfsu(const STFSU_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) return snprintf(o, s, "stfsu f%u, 0x%x(r%u)", d->frS, (uint16_t)d->d, d->rA);
    return snprintf(o, s, "stfsu f%u, -0x%x(r%u)", d->frS, (uint16_t)(-d->d), d->rA);
}

#endif // OPCODE_STFSU_H

