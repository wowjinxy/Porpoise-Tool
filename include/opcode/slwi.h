/**
 * @file slwi.h
 * @brief SLWI - Shift Left Word Immediate (rlwinm pseudo-op)
 * Opcode: 21 (rlwinm with MB=0, ME=31-n)
 */

#ifndef OPCODE_SLWI_H
#define OPCODE_SLWI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t rA, rS, n;
    bool Rc;
} SLWI_Instruction;

// Handled by rlwinm, placeholder
static inline bool decode_slwi(uint32_t inst, SLWI_Instruction *d) {
    (void)inst; (void)d;
    return false;
}

static inline int transpile_slwi(const SLWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = r%u << %u;", d->rA, d->rS, d->n);
}

static inline int comment_slwi(const SLWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "slwi%s r%u, r%u, %u", d->Rc?".":"", d->rA, d->rS, d->n);
}

#endif

