/**
 * @file mtctr.h
 * @brief MTCTR - Move To Count Register
 * 
 * Opcode: 31 / 467 (mtspr with SPR=9)
 * Syntax: mtctr rS
 * 
 * Description: CTR = rS
 */

#ifndef OPCODE_MTCTR_H
#define OPCODE_MTCTR_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MTCTR_PRIMARY    31
#define OP_MTCTR_EXTENDED   467
#define SPR_CTR            9

typedef struct {
    uint8_t rS;
} MTCTR_Instruction;

static inline bool decode_mtctr(uint32_t inst, MTCTR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_MTCTR_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_MTCTR_EXTENDED) return false;
    
    uint32_t spr_encoded = ((inst >> 11) & 0x1F) | (((inst >> 16) & 0x1F) << 5);
    if (spr_encoded != SPR_CTR) return false;
    
    d->rS = (inst >> 21) & 0x1F;
    return true;
}

static inline int transpile_mtctr(const MTCTR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ctr = r%u;", d->rS);
}

static inline int comment_mtctr(const MTCTR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mtctr r%u", d->rS);
}

#endif // OPCODE_MTCTR_H

