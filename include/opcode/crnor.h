/**
 * @file crnor.h
 * @brief CRNOR - Condition Register NOR
 * 
 * Opcode: 19 / 33
 * Format: XL-form
 * Syntax: crnor crbD, crbA, crbB
 *         crnot crbD, crbA (pseudo-op when crbA == crbB)
 * 
 * Description: CR[crbD] = ~(CR[crbA] | CR[crbB])
 */

#ifndef OPCODE_CRNOR_H
#define OPCODE_CRNOR_H

#include <stdint.h>
#include <stdbool.h>

#define OP_CRNOR_PRIMARY    19
#define OP_CRNOR_EXTENDED   33

typedef struct {
    uint8_t crbD;
    uint8_t crbA;
    uint8_t crbB;
} CRNOR_Instruction;

static inline bool decode_crnor(uint32_t inst, CRNOR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_CRNOR_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_CRNOR_EXTENDED) return false;
    
    d->crbD = (inst >> 21) & 0x1F;
    d->crbA = (inst >> 16) & 0x1F;
    d->crbB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_crnor(const CRNOR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ cr = (cr & ~(1U << (31-%u))) | "
                   "((~(((cr >> (31-%u)) & 1) | ((cr >> (31-%u)) & 1))) << (31-%u)); }",
                   d->crbD, d->crbA, d->crbB, d->crbD);
}

static inline int comment_crnor(const CRNOR_Instruction *d, char *o, size_t s) {
    if (d->crbA == d->crbB) {
        return snprintf(o, s, "crnot %u, %u", d->crbD, d->crbA);
    }
    return snprintf(o, s, "crnor %u, %u, %u", d->crbD, d->crbA, d->crbB);
}

#endif // OPCODE_CRNOR_H

