/**
 * @file bdz.h
 * @brief BDZ - Branch Decrement Zero (bc pseudo-op)
 * Opcode: 19 (bc with BO=18)
 */

#ifndef OPCODE_BDZ_H
#define OPCODE_BDZ_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t target;
    bool LK;
} BDZ_Instruction;

static inline bool decode_bdz(uint32_t inst, BDZ_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 16) return false;
    uint8_t BO = (inst >> 21) & 0x1F;
    if (BO != 18) return false;
    d->target = inst & 0xFFFC;
    d->LK = inst & 1;
    return true;
}

static inline int transpile_bdz(const BDZ_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, "if (!(--ctr)) goto label;");
}

static inline int comment_bdz(const BDZ_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "bdz%s 0x%x", d->LK ? "l" : "", d->target);
}

#endif

