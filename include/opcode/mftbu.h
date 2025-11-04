/**
 * @file mftbu.h
 * @brief MFTBU - Move From Time Base Upper
 * Opcode: 31 / 371 (mfspr with SPR=269)
 */

#ifndef OPCODE_MFTBU_H
#define OPCODE_MFTBU_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MFTBU_PRIMARY    31
#define OP_MFTBU_EXTENDED   371
#define SPR_TBU            269

typedef struct {
    uint8_t rD;
} MFTBU_Instruction;

static inline bool decode_mftbu(uint32_t inst, MFTBU_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 371) return false;
    uint32_t spr = ((inst >> 11) & 0x1F) | (((inst >> 16) & 0x1F) << 5);
    if (spr != SPR_TBU) return false;
    d->rD = (inst >> 21) & 0x1F;
    return true;
}

static inline int transpile_mftbu(const MFTBU_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = tbu;", d->rD);
}

static inline int comment_mftbu(const MFTBU_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mftbu r%u", d->rD);
}

#endif

