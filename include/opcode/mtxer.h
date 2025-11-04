/**
 * @file mtxer.h
 * @brief MTXER - Move To XER (uses mtspr)
 * 
 * Opcode: 31 / 467 (mtspr with SPR=1)
 * Format: XFX-form
 * Syntax: mtxer rS
 * 
 * Description: XER = rS (write XER register)
 * Note: This is a pseudo-op for mtspr 1, rS
 */

#ifndef OPCODE_MTXER_H
#define OPCODE_MTXER_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MTXER_PRIMARY    31
#define OP_MTXER_EXTENDED   467
#define SPR_XER             1

typedef struct {
    uint8_t rS;
} MTXER_Instruction;

static inline bool decode_mtxer(uint32_t inst, MTXER_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_MTXER_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_MTXER_EXTENDED) return false;
    
    // Check if SPR field is 1 (XER)
    uint32_t spr_encoded = ((inst >> 11) & 0x1F) | (((inst >> 16) & 0x1F) << 5);
    if (spr_encoded != SPR_XER) return false;
    
    d->rS = (inst >> 21) & 0x1F;
    return true;
}

static inline int transpile_mtxer(const MTXER_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "xer = r%u;", d->rS);
}

static inline int comment_mtxer(const MTXER_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mtxer r%u", d->rS);
}

#endif // OPCODE_MTXER_H

