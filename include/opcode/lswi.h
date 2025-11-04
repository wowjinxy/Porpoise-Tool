/**
 * @file lswi.h
 * @brief LSWI - Load String Word Immediate
 * 
 * Opcode: 31 / 597
 * Syntax: lswi rD, rA, NB
 * 
 * Description: Load NB bytes from memory starting at EA
 */

#ifndef OPCODE_LSWI_H
#define OPCODE_LSWI_H

#include <stdint.h>
#include <stdbool.h>

#define OP_LSWI_PRIMARY    31
#define OP_LSWI_EXTENDED   597

typedef struct {
    uint8_t rD;
    uint8_t rA;
    uint8_t NB;
} LSWI_Instruction;

static inline bool decode_lswi(uint32_t inst, LSWI_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_LSWI_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_LSWI_EXTENDED) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->NB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lswi(const LSWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* lswi r%u, r%u, %u - load string (complex) */",
                   d->rD, d->rA, d->NB);
}

static inline int comment_lswi(const LSWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lswi r%u, r%u, %u", d->rD, d->rA, d->NB);
}

#endif // OPCODE_LSWI_H

