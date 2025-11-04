/**
 * @file stswi.h
 * @brief STSWI - Store String Word Immediate
 * 
 * Opcode: 31 / 725
 * Syntax: stswi rS, rA, NB
 * 
 * Description: Store NB bytes to memory starting at EA
 */

#ifndef OPCODE_STSWI_H
#define OPCODE_STSWI_H

#include <stdint.h>
#include <stdbool.h>

#define OP_STSWI_PRIMARY    31
#define OP_STSWI_EXTENDED   725

typedef struct {
    uint8_t rS;
    uint8_t rA;
    uint8_t NB;
} STSWI_Instruction;

static inline bool decode_stswi(uint32_t inst, STSWI_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STSWI_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_STSWI_EXTENDED) return false;
    
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->NB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_stswi(const STSWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* stswi r%u, r%u, %u - store string (complex) */",
                   d->rS, d->rA, d->NB);
}

static inline int comment_stswi(const STSWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "stswi r%u, r%u, %u", d->rS, d->rA, d->NB);
}

#endif // OPCODE_STSWI_H

