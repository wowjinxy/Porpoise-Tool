/**
 * @file eciwx.h
 * @brief ECIWX - External Control In Word Indexed
 * Opcode: 31 / 310
 */

#ifndef OPCODE_ECIWX_H
#define OPCODE_ECIWX_H

#include <stdint.h>
#include <stdbool.h>

#define OP_ECIWX_PRIMARY    31
#define OP_ECIWX_EXTENDED   310

typedef struct {
    uint8_t rD, rA, rB;
} ECIWX_Instruction;

static inline bool decode_eciwx(uint32_t inst, ECIWX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 310) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_eciwx(const ECIWX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) return snprintf(o, s, "r%u = *(uint32_t*)translate_address(r%u);", d->rD, d->rB);
    return snprintf(o, s, "r%u = *(uint32_t*)(mem + r%u + r%u);", d->rD, d->rA, d->rB);
}

static inline int comment_eciwx(const ECIWX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "eciwx r%u, r%u, r%u", d->rD, d->rA, d->rB);
}

#endif

