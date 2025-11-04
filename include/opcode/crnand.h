/**
 * @file crnand.h
 * @brief CRNAND - Condition Register NAND
 * 
 * Opcode: 19 / 225
 * Format: XL-form
 * Syntax: crnand crbD, crbA, crbB
 * 
 * Description: CR[crbD] = ~(CR[crbA] & CR[crbB])
 */

#ifndef OPCODE_CRNAND_H
#define OPCODE_CRNAND_H

#include <stdint.h>
#include <stdbool.h>

#define OP_CRNAND_PRIMARY    19
#define OP_CRNAND_EXTENDED   225

typedef struct {
    uint8_t crbD;
    uint8_t crbA;
    uint8_t crbB;
} CRNAND_Instruction;

static inline bool decode_crnand(uint32_t inst, CRNAND_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_CRNAND_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_CRNAND_EXTENDED) return false;
    
    d->crbD = (inst >> 21) & 0x1F;
    d->crbA = (inst >> 16) & 0x1F;
    d->crbB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_crnand(const CRNAND_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ cr = (cr & ~(1U << (31-%u))) | "
                   "((~(((cr >> (31-%u)) & 1) & ((cr >> (31-%u)) & 1))) << (31-%u)); }",
                   d->crbD, d->crbA, d->crbB, d->crbD);
}

static inline int comment_crnand(const CRNAND_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "crnand %u, %u, %u", d->crbD, d->crbA, d->crbB);
}

#endif // OPCODE_CRNAND_H

