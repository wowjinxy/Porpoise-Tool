/**
 * @file tlbie.h
 * @brief TLBIE - TLB Invalidate Entry
 * Opcode: 31 / 306
 */

#ifndef OPCODE_TLBIE_H
#define OPCODE_TLBIE_H

#include <stdint.h>
#include <stdbool.h>

#define OP_TLBIE_PRIMARY    31
#define OP_TLBIE_EXTENDED   306

typedef struct {
    uint8_t rB;
} TLBIE_Instruction;

static inline bool decode_tlbie(uint32_t inst, TLBIE_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 306) return false;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_tlbie(const TLBIE_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, ";  /* tlbie - TLB invalidate (no-op in C) */");
}

static inline int comment_tlbie(const TLBIE_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "tlbie r%u", d->rB);
}

#endif

