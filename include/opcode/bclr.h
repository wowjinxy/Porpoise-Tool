/**
 * @file bclr.h
 * @brief BCLR - Branch Conditional to Link Register
 * 
 * Opcode: 19 / 16
 * Syntax: bclr BO, BI
 *         bclrl BO, BI (with LK=1)
 *         blr (pseudo-op when BO=20)
 * 
 * Description: Branch to address in LR if condition met
 */

#ifndef OPCODE_BCLR_H
#define OPCODE_BCLR_H

#include <stdint.h>
#include <stdbool.h>

#define OP_BCLR_PRIMARY    19
#define OP_BCLR_EXTENDED   16

typedef struct {
    uint8_t BO;
    uint8_t BI;
    bool LK;
} BCLR_Instruction;

static inline bool decode_bclr(uint32_t inst, BCLR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_BCLR_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_BCLR_EXTENDED) return false;
    
    d->BO = (inst >> 21) & 0x1F;
    d->BI = (inst >> 16) & 0x1F;
    d->LK = inst & 1;
    return true;
}

static inline int transpile_bclr(const BCLR_Instruction *d, char *o, size_t s) {
    // Unconditional return (blr)
    if (d->BO == 20 && !d->LK) {
        return snprintf(o, s, "return;");
    }
    
    // Unconditional call via LR (blrl)
    if (d->BO == 20 && d->LK) {
        return snprintf(o, s, "((void (*)(void))lr)();");
    }
    
    // Conditional returns - already handled in transpile_from_asm
    // but we can add a fallback
    return snprintf(o, s, ";  /* bclr - conditional return */");
}

static inline int comment_bclr(const BCLR_Instruction *d, char *o, size_t s) {
    if (d->BO == 20 && !d->LK) return snprintf(o, s, "blr");
    if (d->BO == 20 && d->LK) return snprintf(o, s, "blrl");
    return snprintf(o, s, "bclr%s %u, %u", d->LK ? "l" : "", d->BO, d->BI);
}

#endif // OPCODE_BCLR_H

