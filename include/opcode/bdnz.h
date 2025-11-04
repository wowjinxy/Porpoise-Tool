/**
 * @file bdnz.h
 * @brief BDNZ - Branch Decrement Not Zero (bc pseudo-op)
 * Opcode: 19 (bc with BO=16)
 */

#ifndef OPCODE_BDNZ_H
#define OPCODE_BDNZ_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t target;
    bool LK;
} BDNZ_Instruction;

// This is handled by bc opcode, this is just documentation
static inline bool decode_bdnz(uint32_t inst, BDNZ_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 16) return false;
    uint8_t BO = (inst >> 21) & 0x1F;
    if (BO != 16) return false;
    d->target = inst & 0xFFFC;
    d->LK = inst & 1;
    return true;
}

static inline int transpile_bdnz(const BDNZ_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, "if (--ctr) goto label;");
}

static inline int comment_bdnz(const BDNZ_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "bdnz%s 0x%x", d->LK ? "l" : "", d->target);
}

#endif

