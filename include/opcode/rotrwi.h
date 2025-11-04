/**
 * @file rotrwi.h
 * @brief ROTRWI - Rotate Right Word Immediate (rlwinm pseudo-op)
 * Opcode: 21 (rlwinm with SH=32-n, MB=0, ME=31)
 */

#ifndef OPCODE_ROTRWI_H
#define OPCODE_ROTRWI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t rA, rS, n;
    bool Rc;
} ROTRWI_Instruction;

// Handled by rlwinm, placeholder
static inline bool decode_rotrwi(uint32_t inst, ROTRWI_Instruction *d) {
    (void)inst; (void)d;
    return false;
}

static inline int transpile_rotrwi(const ROTRWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = (r%u >> %u) | (r%u << %u);", d->rA, d->rS, d->n, d->rS, 32-d->n);
}

static inline int comment_rotrwi(const ROTRWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "rotrwi%s r%u, r%u, %u", d->Rc?".":"", d->rA, d->rS, d->n);
}

#endif

