/**
 * @file lwzx.h
 * @brief LWZX - Load Word and Zero Indexed
 * 
 * Opcode: 31 / 23
 * Syntax: lwzx rD, rA, rB
 * Description: Load word from address (rA|0) + rB
 */

#ifndef OPCODE_LWZX_H
#define OPCODE_LWZX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LWZX_PRIMARY     31
#define OP_LWZX_EXTENDED    23

typedef struct {
    uint8_t rD, rA, rB;
} LWZX_Instruction;

static inline bool decode_lwzx(uint32_t inst, LWZX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 23) return false;
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_lwzx(const LWZX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        return snprintf(o, s, "r%u = *(uint32_t*)(mem + r%u);", d->rD, d->rB);
    }
    return snprintf(o, s, "r%u = *(uint32_t*)(mem + r%u + r%u);", d->rD, d->rA, d->rB);
}

static inline int comment_lwzx(const LWZX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "lwzx r%u, r%u, r%u", d->rD, d->rA, d->rB);
}

#endif

