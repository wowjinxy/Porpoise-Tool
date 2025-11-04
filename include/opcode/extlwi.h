/**
 * @file extlwi.h
 * @brief EXTLWI - Extract and Left Justify Immediate (rlwinm pseudo-op)
 */

#ifndef OPCODE_EXTLWI_H
#define OPCODE_EXTLWI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t rA, rS, n, b;
    bool Rc;
} EXTLWI_Instruction;

static inline bool decode_extlwi(uint32_t inst, EXTLWI_Instruction *d) {
    (void)inst; (void)d;
    return false;
}

static inline int transpile_extlwi(const EXTLWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = (r%u >> %u) & 0x%08X;", d->rA, d->rS, d->b, (1U << d->n) - 1);
}

static inline int comment_extlwi(const EXTLWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "extlwi r%u, r%u, %u, %u", d->rA, d->rS, d->n, d->b);
}

#endif

