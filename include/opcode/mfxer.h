/**
 * @file mfxer.h
 * @brief MFXER - Move From XER (uses mfspr)
 * 
 * Opcode: 31 / 339 (mfspr with SPR=1)
 * Format: XFX-form
 * Syntax: mfxer rD
 * 
 * Description: rD = XER (read XER register)
 * Note: This is a pseudo-op for mfspr rD, 1
 */

#ifndef OPCODE_MFXER_H
#define OPCODE_MFXER_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MFXER_PRIMARY    31
#define OP_MFXER_EXTENDED   339
#define SPR_XER             1

typedef struct {
    uint8_t rD;
} MFXER_Instruction;

static inline bool decode_mfxer(uint32_t inst, MFXER_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_MFXER_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_MFXER_EXTENDED) return false;
    
    // Check if SPR field is 1 (XER)
    uint32_t spr_encoded = ((inst >> 11) & 0x1F) | (((inst >> 16) & 0x1F) << 5);
    if (spr_encoded != SPR_XER) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    return true;
}

static inline int transpile_mfxer(const MFXER_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = xer;", d->rD);
}

static inline int comment_mfxer(const MFXER_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mfxer r%u", d->rD);
}

#endif // OPCODE_MFXER_H

