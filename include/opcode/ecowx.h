/**
 * @file ecowx.h
 * @brief ECOWX - External Control Out Word Indexed
 * Opcode: 31 / 438
 */

#ifndef OPCODE_ECOWX_H
#define OPCODE_ECOWX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_ECOWX_PRIMARY    31
#define OP_ECOWX_EXTENDED   438

typedef struct {
    uint8_t rS, rA, rB;
} ECOWX_Instruction;

static inline bool decode_ecowx(uint32_t inst, ECOWX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 438) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_ecowx(const ECOWX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) return snprintf(o, s, "*(uint32_t*)translate_address(r%u) = r%u;", d->rB, d->rS);
    return snprintf(o, s, "*(uint32_t*)translate_address(r%u + r%u) = r%u;", d->rA, d->rB, d->rS);
}

static inline int comment_ecowx(const ECOWX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "ecowx r%u, r%u, r%u", d->rS, d->rA, d->rB);
}

#endif

