/**
 * @file bctr.h
 * @brief BCTR - Branch to Count Register
 * Opcode: 19 / 528 (bcctr with BO=20, BI=0)
 */

#ifndef OPCODE_BCTR_H
#define OPCODE_BCTR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    bool LK;  // Link bit
} BCTR_Instruction;

static inline bool decode_bctr(uint32_t inst, BCTR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 19 || ((inst >> 1) & 0x3FF) != 528) return false;
    uint8_t BO = (inst >> 21) & 0x1F;
    if (BO != 20) return false;  // bctr is bcctr with BO=20 (always branch)
    d->LK = inst & 1;
    return true;
}

static inline int transpile_bctr(const BCTR_Instruction *d, char *o, size_t s) {
    if (d->LK) {
        return snprintf(o, s, "lr = pc + 4; pc = ctr; goto *branch_table[ctr];");
    }
    return snprintf(o, s, "pc = ctr; goto *branch_table[ctr];");
}

static inline int comment_bctr(const BCTR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "bctr%s", d->LK?"l":"");
}

#endif

