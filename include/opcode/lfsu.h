/**
 * @file lfsu.h
 * @brief LFSU - Load Floating-Point Single with Update
 * 
 * Opcode: 49
 * Format: D-form
 * Syntax: lfsu frD, d(rA)
 * 
 * Description: EA = rA + d; frD = single at EA (as double); rA = EA
 */

#ifndef OPCODE_LFSU_H
#define OPCODE_LFSU_H

#include <stdint.h>
#include <stdbool.h>

#define OP_LFSU 49

typedef struct {
    uint8_t frD;
    uint8_t rA;
    int16_t d;
} LFSU_Instruction;

static inline bool decode_lfsu(uint32_t inst, LFSU_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LFSU) return false;
    
    d->frD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = inst & 0xFFFF;
    return true;
}

static inline int transpile_lfsu(const LFSU_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t ea = r%u + (int16_t)0x%x; "
                   "f%u = (double)*(float*)(mem + ea); "
                   "r%u = ea; }",
                   d->rA, (uint16_t)d->d, d->frD, d->rA);
}

static inline int comment_lfsu(const LFSU_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) return snprintf(o, s, "lfsu f%u, 0x%x(r%u)", d->frD, (uint16_t)d->d, d->rA);
    return snprintf(o, s, "lfsu f%u, -0x%x(r%u)", d->frD, (uint16_t)(-d->d), d->rA);
}

#endif // OPCODE_LFSU_H

