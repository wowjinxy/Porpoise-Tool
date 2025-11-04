/**
 * @file crandc.h
 * @brief CRANDC - Condition Register AND with Complement
 * 
 * Opcode: 19 / 129
 * Format: XL-form
 * Syntax: crandc crbD, crbA, crbB
 * 
 * Description: CR[crbD] = CR[crbA] & ~CR[crbB]
 */

#ifndef OPCODE_CRANDC_H
#define OPCODE_CRANDC_H

#include <stdint.h>
#include <stdbool.h>

#define OP_CRANDC_PRIMARY    19
#define OP_CRANDC_EXTENDED   129

typedef struct {
    uint8_t crbD;
    uint8_t crbA;
    uint8_t crbB;
} CRANDC_Instruction;

static inline bool decode_crandc(uint32_t inst, CRANDC_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_CRANDC_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_CRANDC_EXTENDED) return false;
    
    d->crbD = (inst >> 21) & 0x1F;
    d->crbA = (inst >> 16) & 0x1F;
    d->crbB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_crandc(const CRANDC_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ cr = (cr & ~(1U << (31-%u))) | "
                   "((((cr >> (31-%u)) & 1) & ~((cr >> (31-%u)) & 1)) << (31-%u)); }",
                   d->crbD, d->crbA, d->crbB, d->crbD);
}

static inline int comment_crandc(const CRANDC_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "crandc %u, %u, %u", d->crbD, d->crbA, d->crbB);
}

#endif // OPCODE_CRANDC_H

