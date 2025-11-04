/**
 * @file bcctr.h
 * @brief BCCTR - Branch Conditional to Count Register
 * 
 * Opcode: 19 / 528
 * Syntax: bcctr BO, BI
 *         bcctrl BO, BI (with LK=1)
 * 
 * Description: Branch to address in CTR if condition met
 */

#ifndef OPCODE_BCCTR_H
#define OPCODE_BCCTR_H

#include <stdint.h>
#include <stdbool.h>

#define OP_BCCTR_PRIMARY    19
#define OP_BCCTR_EXTENDED   528

typedef struct {
    uint8_t BO;
    uint8_t BI;
    bool LK;
} BCCTR_Instruction;

static inline bool decode_bcctr(uint32_t inst, BCCTR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_BCCTR_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_BCCTR_EXTENDED) return false;
    
    d->BO = (inst >> 21) & 0x1F;
    d->BI = (inst >> 16) & 0x1F;
    d->LK = inst & 1;
    return true;
}

static inline int transpile_bcctr(const BCCTR_Instruction *d, char *o, size_t s) {
    // For unconditional (BO=20), it's just bctr/bctrl
    if (d->BO == 20) {
        if (d->LK) {
            return snprintf(o, s, "((void (*)(void))ctr)();  /* bctrl */");
        } else {
            return snprintf(o, s, "pc = ctr;  /* bctr */");
        }
    }
    
    // Conditional - simplified version
    return snprintf(o, s, ";  /* bcctr - conditional branch to ctr (complex) */");
}

static inline int comment_bcctr(const BCCTR_Instruction *d, char *o, size_t s) {
    if (d->BO == 20 && !d->LK) return snprintf(o, s, "bctr");
    if (d->BO == 20 && d->LK) return snprintf(o, s, "bctrl");
    return snprintf(o, s, "bcctr%s %u, %u", d->LK ? "l" : "", d->BO, d->BI);
}

#endif // OPCODE_BCCTR_H

