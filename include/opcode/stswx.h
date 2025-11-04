/**
 * @file stswx.h
 * @brief STSWX - Store String Word Indexed
 * 
 * Opcode: 31 / 661
 * Syntax: stswx rS, rA, rB
 * 
 * Description: Store string based on XER byte count
 */

#ifndef OPCODE_STSWX_H
#define OPCODE_STSWX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_STSWX_PRIMARY    31
#define OP_STSWX_EXTENDED   661

typedef struct {
    uint8_t rS;
    uint8_t rA;
    uint8_t rB;
} STSWX_Instruction;

static inline bool decode_stswx(uint32_t inst, STSWX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STSWX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_STSWX_EXTENDED) return false;
    
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_stswx(const STSWX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* stswx r%u, r%u, r%u - store string indexed (complex) */",
                   d->rS, d->rA, d->rB);
}

static inline int comment_stswx(const STSWX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "stswx r%u, r%u, r%u", d->rS, d->rA, d->rB);
}

#endif // OPCODE_STSWX_H

