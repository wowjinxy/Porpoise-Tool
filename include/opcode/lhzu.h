/**
 * @file lhzu.h
 * @brief LHZU - Load Halfword and Zero with Update
 * 
 * Opcode: 41
 * Syntax: lhzu rD, d(rA)
 */

#ifndef OPCODE_LHZU_H
#define OPCODE_LHZU_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LHZU 41

typedef struct {
    uint8_t rD, rA;
    int16_t d;
} LHZU_Instruction;

static inline bool decode_lhzu(uint32_t inst, LHZU_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 41) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->d = inst & 0xFFFF;
    return true;
}

static inline int transpile_lhzu(const LHZU_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) {
        return snprintf(o, s, "r%u = r%u + 0x%x; r%u = *(uint16_t*)translate_address(r%u);",
                       d->rA, d->rA, (uint16_t)d->d, d->rD, d->rA);
    } else {
        return snprintf(o, s, "r%u = r%u - 0x%x; r%u = *(uint16_t*)translate_address(r%u);",
                       d->rA, d->rA, (uint16_t)(-d->d), d->rD, d->rA);
    }
}

static inline int comment_lhzu(const LHZU_Instruction *d, char *o, size_t s) {
    if (d->d >= 0) return snprintf(o, s, "lhzu r%u, 0x%x(r%u)", d->rD, (uint16_t)d->d, d->rA);
    return snprintf(o, s, "lhzu r%u, -0x%x(r%u)", d->rD, (uint16_t)(-d->d), d->rA);
}

#endif

