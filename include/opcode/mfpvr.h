/**
 * @file mfpvr.h
 * @brief MFPVR - Move From Processor Version Register
 * Opcode: 31 / 339 (mfspr with SPR=287)
 */

#ifndef OPCODE_MFPVR_H
#define OPCODE_MFPVR_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MFPVR_PRIMARY    31
#define OP_MFPVR_EXTENDED   339
#define SPR_PVR            287

typedef struct {
    uint8_t rD;
} MFPVR_Instruction;

static inline bool decode_mfpvr(uint32_t inst, MFPVR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 339) return false;
    uint32_t spr = ((inst >> 11) & 0x1F) | (((inst >> 16) & 0x1F) << 5);
    if (spr != SPR_PVR) return false;
    d->rD = (inst >> 21) & 0x1F;
    return true;
}

static inline int transpile_mfpvr(const MFPVR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = pvr;", d->rD);
}

static inline int comment_mfpvr(const MFPVR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mfpvr r%u", d->rD);
}

#endif

