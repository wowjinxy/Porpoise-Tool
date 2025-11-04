/**
 * @file stwcx.h
 * @brief STWCX. - Store Word Conditional Indexed
 * Opcode: 31 / 150
 * Used for atomic operations (always has Rc=1)
 */

#ifndef OPCODE_STWCX_H
#define OPCODE_STWCX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_STWCX_PRIMARY    31
#define OP_STWCX_EXTENDED   150

typedef struct {
    uint8_t rS, rA, rB;
} STWCX_Instruction;

static inline bool decode_stwcx(uint32_t inst, STWCX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 150) return false;
    if (!(inst & 1)) return false;  // Must have Rc=1
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_stwcx(const STWCX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        return snprintf(o, s,
                       "{ *(uint32_t*)(mem + r%u) = r%u; "
                       "cr0 = 0x2 | (xer >> 28 & 0x1); }  /* conditional store success */",
                       d->rB, d->rS);
    }
    return snprintf(o, s,
                   "{ *(uint32_t*)(mem + r%u + r%u) = r%u; "
                   "cr0 = 0x2 | (xer >> 28 & 0x1); }  /* conditional store success */",
                   d->rA, d->rB, d->rS);
}

static inline int comment_stwcx(const STWCX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "stwcx. r%u, r%u, r%u", d->rS, d->rA, d->rB);
}

#endif

