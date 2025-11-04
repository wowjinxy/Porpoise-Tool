/**
 * @file mtlr.h
 * @brief MTLR - Move To Link Register (uses mtspr)
 * 
 * Opcode: 31 / 467 (mtspr with SPR=8)
 * Syntax: mtlr rS
 * 
 * Description: LR = rS
 */

#ifndef OPCODE_MTLR_H
#define OPCODE_MTLR_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MTLR_PRIMARY    31
#define OP_MTLR_EXTENDED   467
#define SPR_LR             8

typedef struct {
    uint8_t rS;
} MTLR_Instruction;

static inline bool decode_mtlr(uint32_t inst, MTLR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_MTLR_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_MTLR_EXTENDED) return false;
    
    uint32_t spr_encoded = ((inst >> 11) & 0x1F) | (((inst >> 16) & 0x1F) << 5);
    if (spr_encoded != SPR_LR) return false;
    
    d->rS = (inst >> 21) & 0x1F;
    return true;
}

static inline int transpile_mtlr(const MTLR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lr = r%u;", d->rS);
}

static inline int comment_mtlr(const MTLR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mtlr r%u", d->rS);
}

#endif // OPCODE_MTLR_H

