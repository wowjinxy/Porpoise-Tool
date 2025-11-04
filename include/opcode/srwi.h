/**
 * @file srwi.h
 * @brief SRWI - Shift Right Word Immediate (rlwinm pseudo-op)
 * Opcode: 21 (rlwinm with SH=32-n, MB=n, ME=31)
 */

#ifndef OPCODE_SRWI_H
#define OPCODE_SRWI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t rA, rS, n;
    bool Rc;
} SRWI_Instruction;

// Handled by rlwinm, placeholder
static inline bool decode_srwi(uint32_t inst, SRWI_Instruction *d) {
    (void)inst; (void)d;
    return false;
}

static inline int transpile_srwi(const SRWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = r%u >> %u;", d->rA, d->rS, d->n);
}

static inline int comment_srwi(const SRWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "srwi%s r%u, r%u, %u", d->Rc?".":"", d->rA, d->rS, d->n);
}

#endif

