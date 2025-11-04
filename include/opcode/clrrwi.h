/**
 * @file clrrwi.h
 * @brief CLRRWI - Clear Right Word Immediate (rlwinm pseudo-op)
 * Opcode: 21 (rlwinm with SH=0, MB=0, ME=31-n)
 */

#ifndef OPCODE_CLRRWI_H
#define OPCODE_CLRRWI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t rA, rS, n;
    bool Rc;
} CLRRWI_Instruction;

// Handled by rlwinm, placeholder
static inline bool decode_clrrwi(uint32_t inst, CLRRWI_Instruction *d) {
    (void)inst; (void)d;
    return false;
}

static inline int transpile_clrrwi(const CLRRWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = r%u & 0x%08X;", d->rA, d->rS, (0xFFFFFFFF << d->n));
}

static inline int comment_clrrwi(const CLRRWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "clrrwi%s r%u, r%u, %u", d->Rc?".":"", d->rA, d->rS, d->n);
}

#endif

