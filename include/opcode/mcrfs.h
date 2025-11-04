/**
 * @file mcrfs.h
 * @brief MCRFS - Move to CR from FPSCR
 * Opcode: 63 / 64
 */

#ifndef OPCODE_MCRFS_H
#define OPCODE_MCRFS_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MCRFS_PRIMARY    63
#define OP_MCRFS_EXTENDED   64

typedef struct {
    uint8_t crfD, crfS;
} MCRFS_Instruction;

static inline bool decode_mcrfs(uint32_t inst, MCRFS_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 63 || ((inst >> 1) & 0x3FF) != 64) return false;
    d->crfD = (inst >> 23) & 0x7;
    d->crfS = (inst >> 18) & 0x7;
    return true;
}

static inline int transpile_mcrfs(const MCRFS_Instruction *d, char *o, size_t s) {
    return snprintf(o, s,
                   "{ uint32_t val = (fpscr >> (28-%u*4)) & 0xF; "
                   "cr = (cr & ~(0xFU << (28-%u*4))) | (val << (28-%u*4)); }",
                   d->crfS, d->crfD, d->crfD);
}

static inline int comment_mcrfs(const MCRFS_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mcrfs cr%u, cr%u", d->crfD, d->crfS);
}

#endif

