/**
 * @file crorc.h
 * @brief CRORC - Condition Register OR with Complement
 * 
 * Opcode: 19 / 417
 * Format: XL-form
 * Syntax: crorc crbD, crbA, crbB
 * 
 * Description: CR[crbD] = CR[crbA] | ~CR[crbB]
 */

#ifndef OPCODE_CRORC_H
#define OPCODE_CRORC_H

#include <stdint.h>
#include <stdbool.h>

#define OP_CRORC_PRIMARY    19
#define OP_CRORC_EXTENDED   417

typedef struct {
    uint8_t crbD;
    uint8_t crbA;
    uint8_t crbB;
} CRORC_Instruction;

static inline bool decode_crorc(uint32_t inst, CRORC_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_CRORC_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_CRORC_EXTENDED) return false;
    
    d->crbD = (inst >> 21) & 0x1F;
    d->crbA = (inst >> 16) & 0x1F;
    d->crbB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_crorc(const CRORC_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ cr = (cr & ~(1U << (31-%u))) | "
                   "((((cr >> (31-%u)) & 1) | ~((cr >> (31-%u)) & 1)) << (31-%u)); }",
                   d->crbD, d->crbA, d->crbB, d->crbD);
}

static inline int comment_crorc(const CRORC_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "crorc %u, %u, %u", d->crbD, d->crbA, d->crbB);
}

#endif // OPCODE_CRORC_H

