/**
 * @file tlbia.h
 * @brief TLBIA - TLB Invalidate All
 * Opcode: 31 / 370
 */

#ifndef OPCODE_TLBIA_H
#define OPCODE_TLBIA_H

#include <stdint.h>
#include <stdbool.h>

#define OP_TLBIA_PRIMARY    31
#define OP_TLBIA_EXTENDED   370

typedef struct {
    uint8_t dummy;
} TLBIA_Instruction;

static inline bool decode_tlbia(uint32_t inst, TLBIA_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 370) return false;
    (void)d;
    return true;
}

static inline int transpile_tlbia(const TLBIA_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, ";  /* tlbia - invalidate all TLB (no-op in C) */");
}

static inline int comment_tlbia(const TLBIA_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, "tlbia");
}

#endif

