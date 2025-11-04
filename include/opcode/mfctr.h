/**
 * @file mfctr.h
 * @brief MFCTR - Move From Count Register
 * 
 * Opcode: 31 / 339 (mfspr with SPR=9)
 * Syntax: mfctr rD
 * 
 * Description: rD = CTR
 */

#ifndef OPCODE_MFCTR_H
#define OPCODE_MFCTR_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MFCTR_PRIMARY    31
#define OP_MFCTR_EXTENDED   339
#define SPR_CTR            9

typedef struct {
    uint8_t rD;
} MFCTR_Instruction;

static inline bool decode_mfctr(uint32_t inst, MFCTR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_MFCTR_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_MFCTR_EXTENDED) return false;
    
    uint32_t spr_encoded = ((inst >> 11) & 0x1F) | (((inst >> 16) & 0x1F) << 5);
    if (spr_encoded != SPR_CTR) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    return true;
}

static inline int transpile_mfctr(const MFCTR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = ctr;", d->rD);
}

static inline int comment_mfctr(const MFCTR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mfctr r%u", d->rD);
}

#endif // OPCODE_MFCTR_H

