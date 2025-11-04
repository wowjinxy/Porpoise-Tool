/**
 * @file lswx.h
 * @brief LSWX - Load String Word Indexed
 * 
 * Opcode: 31 / 533
 * Syntax: lswx rD, rA, rB
 * 
 * Description: Load string based on XER byte count
 */

#ifndef OPCODE_LSWX_H
#define OPCODE_LSWX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_LSWX_PRIMARY    31
#define OP_LSWX_EXTENDED   533

typedef struct {
    uint8_t rD;
    uint8_t rA;
    uint8_t rB;
} LSWX_Instruction;

static inline bool decode_lswx(uint32_t inst, LSWX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LSWX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_LSWX_EXTENDED) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lswx(const LSWX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* lswx r%u, r%u, r%u - load string indexed (complex) */",
                   d->rD, d->rA, d->rB);
}

static inline int comment_lswx(const LSWX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lswx r%u, r%u, r%u", d->rD, d->rA, d->rB);
}

#endif // OPCODE_LSWX_H

