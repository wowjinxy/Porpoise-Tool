/**
 * @file tw.h
 * @brief TW - Trap Word
 * 
 * Opcode: 31 / 4
 * Format: X-form
 * Syntax: tw TO, rA, rB
 * 
 * Description: Trap if condition is met (used for runtime checks/debugging)
 * Common pseudo-op: trap (unconditional trap, TO=31)
 */

#ifndef OPCODE_TW_H
#define OPCODE_TW_H

#include <stdint.h>
#include <stdbool.h>

#define OP_TW_PRIMARY    31
#define OP_TW_EXTENDED   4

typedef struct {
    uint8_t TO;
    uint8_t rA;
    uint8_t rB;
} TW_Instruction;

static inline bool decode_tw(uint32_t inst, TW_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_TW_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_TW_EXTENDED) return false;
    
    d->TO = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_tw(const TW_Instruction *d, char *o, size_t s) {
    // Unconditional trap (trap pseudo-op)
    if (d->TO == 31) {
        return snprintf(o, s, ";  /* trap - unconditional (no-op in C) */");
    }
    
    // For other trap conditions, generate a comment (traps aren't used in normal code flow)
    return snprintf(o, s, ";  /* tw %u, r%u, r%u - conditional trap (no-op in C) */",
                   d->TO, d->rA, d->rB);
}

static inline int comment_tw(const TW_Instruction *d, char *o, size_t s) {
    if (d->TO == 31) {
        return snprintf(o, s, "trap");
    }
    return snprintf(o, s, "tw %u, r%u, r%u", d->TO, d->rA, d->rB);
}

#endif // OPCODE_TW_H

