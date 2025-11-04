/**
 * @file clrlslwi.h
 * @brief CLRLSLWI - Clear Left and Shift Left (rlwinm pseudo-op)
 * Special optimized rlwinm patterns
 */

#ifndef OPCODE_CLRLSLWI_H
#define OPCODE_CLRLSLWI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t rA, rS, n, b;
    bool Rc;
} CLRLSLWI_Instruction;

// This would be detected by rlwinm, just a placeholder for extended pseudo-ops
static inline bool decode_clrlslwi(uint32_t inst, CLRLSLWI_Instruction *d) {
    (void)inst; (void)d;
    return false;  // Handled by rlwinm
}

static inline int transpile_clrlslwi(const CLRLSLWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = (r%u << %u) & 0x%08X;", d->rA, d->rS, d->n, (0xFFFFFFFF << d->n) & (0xFFFFFFFF >> d->b));
}

static inline int comment_clrlslwi(const CLRLSLWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "clrlslwi r%u, r%u, %u, %u", d->rA, d->rS, d->b, d->n);
}

#endif

