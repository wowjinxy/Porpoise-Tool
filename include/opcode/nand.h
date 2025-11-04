/**
 * @file nand.h
 * @brief NAND - Logical NAND
 * 
 * Opcode: 31 / 476
 * Format: X-form
 * Syntax: nand rA, rS, rB
 *         nand. rA, rS, rB (with Rc=1)
 * 
 * Description: rA = ~(rS & rB)
 */

#ifndef OPCODE_NAND_H
#define OPCODE_NAND_H

#include <stdint.h>
#include <stdbool.h>

#define OP_NAND_PRIMARY    31
#define OP_NAND_EXTENDED   476

typedef struct {
    uint8_t rA;
    uint8_t rS;
    uint8_t rB;
    bool Rc;
} NAND_Instruction;

static inline bool decode_nand(uint32_t inst, NAND_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_NAND_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_NAND_EXTENDED) return false;
    
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_nand(const NAND_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "r%u = ~(r%u & r%u);", d->rA, d->rS, d->rB);
    
    if (d->Rc) {
        w += snprintf(o + w, s - w,
                     "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                     "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                     d->rA, d->rA);
    }
    
    return w;
}

static inline int comment_nand(const NAND_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "nand%s r%u, r%u, r%u",
                   d->Rc ? "." : "", d->rA, d->rS, d->rB);
}

#endif // OPCODE_NAND_H

