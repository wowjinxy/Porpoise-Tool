/**
 * @file sthbrx.h
 * @brief STHBRX - Store Halfword Byte-Reverse Indexed
 * 
 * Opcode: 31 / 918
 * Format: X-form
 * Syntax: sthbrx rS, rA, rB
 * 
 * Description: EA = (rA|0) + rB; store halfword to EA with bytes reversed
 * Used for endian conversion
 */

#ifndef OPCODE_STHBRX_H
#define OPCODE_STHBRX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_STHBRX_PRIMARY    31
#define OP_STHBRX_EXTENDED   918

typedef struct {
    uint8_t rS;
    uint8_t rA;
    uint8_t rB;
} STHBRX_Instruction;

static inline bool decode_sthbrx(uint32_t inst, STHBRX_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_STHBRX_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_STHBRX_EXTENDED) return false;
    
    d->rS = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->rB = (inst >> 11) & 0x1F;
    return true;
}

static inline int transpile_sthbrx(const STHBRX_Instruction *d, char *o, size_t s) {
    if (d->rA == 0) {
        return snprintf(o, s,
                       "{ uint16_t val = (uint16_t)r%u; "
                       "*(uint16_t*)translate_address(r%u) = ((val & 0xFF) << 8) | ((val >> 8) & 0xFF); }",
                       d->rS, d->rB);
    }
    return snprintf(o, s,
                   "{ uint16_t val = (uint16_t)r%u; "
                   "*(uint16_t*)(mem + r%u + r%u) = ((val & 0xFF) << 8) | ((val >> 8) & 0xFF); }",
                   d->rS, d->rA, d->rB);
}

static inline int comment_sthbrx(const STHBRX_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "sthbrx r%u, r%u, r%u", d->rS, d->rA, d->rB);
}

#endif // OPCODE_STHBRX_H

