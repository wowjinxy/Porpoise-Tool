/**
 * @file mflr.h
 * @brief MFLR - Move From Link Register (uses mfspr)
 * 
 * Opcode: 31 / 339 (mfspr with SPR=8)
 * Format: XFX-form
 * Syntax: mflr rD
 * 
 * Description: rD = LR (read link register)
 * Note: This is a pseudo-op for mfspr rD, 8
 */

#ifndef OPCODE_MFLR_H
#define OPCODE_MFLR_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MFLR_PRIMARY    31
#define OP_MFLR_EXTENDED   339
#define SPR_LR             8

typedef struct {
    uint8_t rD;
} MFLR_Instruction;

static inline bool decode_mflr(uint32_t inst, MFLR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_MFLR_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_MFLR_EXTENDED) return false;
    
    // Check if SPR field is 8 (LR) - SPR is encoded as 5-bit fields swapped
    uint32_t spr_encoded = ((inst >> 11) & 0x1F) | (((inst >> 16) & 0x1F) << 5);
    if (spr_encoded != SPR_LR) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    return true;
}

static inline int transpile_mflr(const MFLR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = lr;", d->rD);
}

static inline int comment_mflr(const MFLR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mflr r%u", d->rD);
}

#endif // OPCODE_MFLR_H

