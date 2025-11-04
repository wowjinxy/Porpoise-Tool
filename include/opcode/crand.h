/**
 * @file crand.h
 * @brief CRAND - Condition Register AND
 * 
 * Opcode: 19 / 257
 * Format: XL-form
 * Syntax: crand crbD, crbA, crbB
 * 
 * Description: CR[crbD] = CR[crbA] & CR[crbB]
 */

#ifndef OPCODE_CRAND_H
#define OPCODE_CRAND_H

#include <stdint.h>
#include <stdbool.h>

#define OP_CRAND_PRIMARY    19
#define OP_CRAND_EXTENDED   257

typedef struct {
    uint8_t crbD;
    uint8_t crbA;
    uint8_t crbB;
} CRAND_Instruction;

static inline bool decode_crand(uint32_t inst, CRAND_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_CRAND_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_CRAND_EXTENDED) return false;
    
    d->crbD = (inst >> 21) & 0x1F;
    d->crbA = (inst >> 16) & 0x1F;
    d->crbB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_crand(const CRAND_Instruction *d, char *o, size_t s) {
    // CR bit manipulation: set bit crbD to (bit crbA AND bit crbB)
    return snprintf(o, s,
                   "{ uint32_t cr_val = (cr >> %u) & 1; "
                   "cr = (cr & ~(1U << (31-%u))) | ((((cr >> (31-%u)) & 1) & ((cr >> (31-%u)) & 1)) << (31-%u)); }",
                   31 - d->crbA, d->crbD, d->crbA, d->crbB, d->crbD);
}

static inline int comment_crand(const CRAND_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "crand %u, %u, %u", d->crbD, d->crbA, d->crbB);
}

#endif // OPCODE_CRAND_H

