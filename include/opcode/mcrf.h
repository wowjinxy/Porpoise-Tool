/**
 * @file mcrf.h
 * @brief MCRF - Move Condition Register Field
 * 
 * Opcode: 19 / 0
 * Format: XL-form
 * Syntax: mcrf crD, crS
 * 
 * Description: Copy CR field crS to CR field crD
 */

#ifndef OPCODE_MCRF_H
#define OPCODE_MCRF_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MCRF_PRIMARY    19
#define OP_MCRF_EXTENDED   0

typedef struct {
    uint8_t crD;
    uint8_t crS;
} MCRF_Instruction;

static inline bool decode_mcrf(uint32_t inst, MCRF_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_MCRF_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_MCRF_EXTENDED) return false;
    
    d->crD = (inst >> 23) & 0x7;  // CR field (3 bits)
    d->crS = (inst >> 18) & 0x7;
    return true;
}

static inline int transpile_mcrf(const MCRF_Instruction *d, char *o, size_t s) {
    // Copy 4-bit CR field
    return snprintf(o, s,
                   "{ uint32_t val = (cr >> (28 - %u*4)) & 0xF; "
                   "cr = (cr & ~(0xFU << (28 - %u*4))) | (val << (28 - %u*4)); }",
                   d->crS, d->crD, d->crD);
}

static inline int comment_mcrf(const MCRF_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mcrf cr%u, cr%u", d->crD, d->crS);
}

#endif // OPCODE_MCRF_H

