/**
 * @file clrlwi.h
 * @brief CLRLWI - Clear Left Word Immediate (rlwinm pseudo-op)
 * Opcode: 21 (rlwinm with SH=0, MB=n, ME=31)
 */

#ifndef OPCODE_CLRLWI_H
#define OPCODE_CLRLWI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t rA, rS, n;
    bool Rc;
} CLRLWI_Instruction;

// Handled by rlwinm, placeholder
static inline bool decode_clrlwi(uint32_t inst, CLRLWI_Instruction *d) {
    (void)inst; (void)d;
    return false;
}

static inline int transpile_clrlwi(const CLRLWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = r%u & 0x%08X;", d->rA, d->rS, (0xFFFFFFFF >> d->n));
}

static inline int comment_clrlwi(const CLRLWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "clrlwi%s r%u, r%u, %u", d->Rc?".":"", d->rA, d->rS, d->n);
}

#endif

