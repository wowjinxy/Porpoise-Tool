/**
 * @file xori.h
 * @brief XORI - XOR Immediate
 * Opcode: 26
 */

#ifndef OPCODE_XORI_H
#define OPCODE_XORI_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_XORI 26

typedef struct {
    uint8_t rA, rS;
    uint16_t UIMM;
} XORI_Instruction;

static inline bool decode_xori(uint32_t inst, XORI_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 26) return false;
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->UIMM = inst & 0xFFFF;
    return true;
}

static inline int transpile_xori(const XORI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = r%u ^ 0x%x;", d->rA, d->rS, d->UIMM);
}

static inline int comment_xori(const XORI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "xori r%u, r%u, 0x%x", d->rA, d->rS, d->UIMM);
}

#endif

