/**
 * @file li.h
 * @brief LI - Load Immediate (addi pseudo-op)
 * Opcode: 14 (addi with rA=0)
 */

#ifndef OPCODE_LI_H
#define OPCODE_LI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t rD;
    int16_t SIMM;
} LI_Instruction;

// Handled by addi, placeholder
static inline bool decode_li(uint32_t inst, LI_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 14) return false;
    uint8_t rA = (inst >> 16) & 0x1F;
    if (rA != 0) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->SIMM = inst & 0xFFFF;
    return true;
}

static inline int transpile_li(const LI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = (int16_t)0x%x;", d->rD, (uint16_t)d->SIMM);
}

static inline int comment_li(const LI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "li r%u, 0x%x", d->rD, (uint16_t)d->SIMM);
}

#endif

