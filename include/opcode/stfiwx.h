/**
 * @file stfiwx.h
 * @brief STFIWX - Store Floating-Point as Integer Word Indexed
 * 
 * Opcode: 31 / 983
 * Format: X-form
 * Syntax: stfiwx frS, rA, rB
 * 
 * Description: Store low 32 bits of FPR as integer
 */

#ifndef OPCODE_STFIWX_H
#define OPCODE_STFIWX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_STFIWX_PRIMARY    31
#define OP_STFIWX_EXTENDED   983

typedef struct {
    uint8_t frS;
    uint8_t rA;
    uint8_t rB;
} STFIWX_Instruction;

static inline bool decode_stfiwx(uint32_t inst, STFIWX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STFIWX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_STFIWX_EXTENDED) return false;
    
    d->frS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_stfiwx(const STFIWX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        return snprintf(o, s,
                       "{ union { double d; uint64_t i; } u; u.d = f%u; "
                       "*(uint32_t*)(mem + r%u) = (uint32_t)u.i; }",
                       d->frS, d->rB);
    }
    return snprintf(o, s,
                   "{ union { double d; uint64_t i; } u; u.d = f%u; "
                   "*(uint32_t*)(mem + r%u + r%u) = (uint32_t)u.i; }",
                   d->frS, d->rA, d->rB);
}

static inline int comment_stfiwx(const STFIWX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "stfiwx f%u, r%u, r%u", d->frS, d->rA, d->rB);
}

#endif // OPCODE_STFIWX_H

