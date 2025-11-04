/**
 * @file mcrxr.h
 * @brief MCRXR - Move to Condition Register from XER
 * 
 * Opcode: 31 / 512
 * Format: X-form
 * Syntax: mcrxr crD
 * 
 * Description: Copy XER[0-3] to CR field crD and clear XER[0-3]
 */

#ifndef OPCODE_MCRXR_H
#define OPCODE_MCRXR_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MCRXR_PRIMARY    31
#define OP_MCRXR_EXTENDED   512

typedef struct {
    uint8_t crD;
} MCRXR_Instruction;

static inline bool decode_mcrxr(uint32_t inst, MCRXR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_MCRXR_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_MCRXR_EXTENDED) return false;
    
    d->crD = (inst >> 23) & 0x7;
    return true;
}

static inline int transpile_mcrxr(const MCRXR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t val = (xer >> 28) & 0xF; "
                   "cr = (cr & ~(0xFU << (28 - %u*4))) | (val << (28 - %u*4)); "
                   "xer &= 0x0FFFFFFF; }",
                   d->crD, d->crD);
}

static inline int comment_mcrxr(const MCRXR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mcrxr cr%u", d->crD);
}

#endif // OPCODE_MCRXR_H

