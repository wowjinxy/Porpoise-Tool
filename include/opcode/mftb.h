/**
 * @file mftb.h
 * @brief MFTB - Move From Time Base
 * Opcode: 31 / 371
 */

#ifndef OPCODE_MFTB_H
#define OPCODE_MFTB_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t rD;
    uint16_t TBR;  // Time base register number (268=TBL, 269=TBU)
} MFTB_Instruction;

static inline bool decode_mftb(uint32_t inst, MFTB_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 371) return false;
    d->rD = (inst >> 21) & 0x1F;
    // The TBR field is split and bit-reversed in the encoding
    uint8_t spr_lo = (inst >> 11) & 0x1F;
    uint8_t spr_hi = (inst >> 16) & 0x1F;
    // Swap the halves to get the actual TBR number
    d->TBR = (spr_lo << 5) | spr_hi;
    return true;
}

static inline int transpile_mftb(const MFTB_Instruction *d, char *o, size_t s) {
    if (d->TBR == 268) {
        return snprintf(o, s, "r%u = (uint32_t)tbl;  /* Read TBL */", d->rD);
    } else if (d->TBR == 269) {
        return snprintf(o, s, "r%u = (uint32_t)tbu;  /* Read TBU */", d->rD);
    } else {
        return snprintf(o, s, "r%u = (uint32_t)tb[%u];  /* Read TB[%u] */", d->rD, d->TBR, d->TBR);
    }
}

static inline int comment_mftb(const MFTB_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mftb r%u, %u", d->rD, d->TBR);
}

#endif

