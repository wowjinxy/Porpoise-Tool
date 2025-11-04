/**
 * @file mtsrin.h
 * @brief MTSRIN - Move To Segment Register Indirect
 * Opcode: 31 / 242
 */

#ifndef OPCODE_MTSRIN_H
#define OPCODE_MTSRIN_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MTSRIN_PRIMARY    31
#define OP_MTSRIN_EXTENDED   242

typedef struct {
    uint8_t rS, rB;
} MTSRIN_Instruction;

static inline bool decode_mtsrin(uint32_t inst, MTSRIN_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 242) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_mtsrin(const MTSRIN_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "sr[(r%u >> 28) & 0xF] = r%u;", d->rB, d->rS);
}

static inline int comment_mtsrin(const MTSRIN_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mtsrin r%u, r%u", d->rS, d->rB);
}

#endif

