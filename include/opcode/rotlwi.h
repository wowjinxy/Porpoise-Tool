/**
 * @file rotlwi.h
 * @brief ROTLWI - Rotate Left Word Immediate (rlwinm pseudo-op)
 * Opcode: 21 (rlwinm with MB=0, ME=31)
 */

#ifndef OPCODE_ROTLWI_H
#define OPCODE_ROTLWI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t rA, rS, n;
    bool Rc;
} ROTLWI_Instruction;

// Handled by rlwinm, placeholder
static inline bool decode_rotlwi(uint32_t inst, ROTLWI_Instruction *d) {
    (void)inst; (void)d;
    return false;
}

static inline int transpile_rotlwi(const ROTLWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = (r%u << %u) | (r%u >> %u);", d->rA, d->rS, d->n, d->rS, 32-d->n);
}

static inline int comment_rotlwi(const ROTLWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "rotlwi%s r%u, r%u, %u", d->Rc?".":"", d->rA, d->rS, d->n);
}

#endif

