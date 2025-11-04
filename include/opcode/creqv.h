/**
 * @file creqv.h
 * @brief CREQV - Condition Register Equivalent
 * 
 * Opcode: 19 / 289
 * Format: XL-form
 * Syntax: creqv crbD, crbA, crbB
 *         crset crbD (pseudo-op when crbA == crbB == crbD)
 * 
 * Description: CR[crbD] = CR[crbA] == CR[crbB] (equivalence, i.e., XNOR)
 */

#ifndef OPCODE_CREQV_H
#define OPCODE_CREQV_H

#include <stdint.h>
#include <stdbool.h>

#define OP_CREQV_PRIMARY    19
#define OP_CREQV_EXTENDED   289

typedef struct {
    uint8_t crbD;
    uint8_t crbA;
    uint8_t crbB;
} CREQV_Instruction;

static inline bool decode_creqv(uint32_t inst, CREQV_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_CREQV_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_CREQV_EXTENDED) return false;
    
    d->crbD = (inst >> 21) & 0x1F;
    d->crbA = (inst >> 16) & 0x1F;
    d->crbB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_creqv(const CREQV_Instruction *d, char *o, size_t s) {
    // crset pseudo-op: set CR bit to 1
    if (d->crbA == d->crbB && d->crbB == d->crbD) {
        return snprintf(o, s, "cr |= (1U << (31-%u));  /* crset */", d->crbD);
    }
    
    // XNOR: equivalent if both same
    return snprintf(o, s,
                   "{ cr = (cr & ~(1U << (31-%u))) | "
                   "((~(((cr >> (31-%u)) & 1) ^ ((cr >> (31-%u)) & 1))) << (31-%u)); }",
                   d->crbD, d->crbA, d->crbB, d->crbD);
}

static inline int comment_creqv(const CREQV_Instruction *d, char *o, size_t s) {
    if (d->crbA == d->crbB && d->crbB == d->crbD) {
        return snprintf(o, s, "crset %u", d->crbD);
    }
    return snprintf(o, s, "creqv %u, %u, %u", d->crbD, d->crbA, d->crbB);
}

#endif // OPCODE_CREQV_H

