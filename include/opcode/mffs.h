/**
 * @file mffs.h
 * @brief MFFS - Move From FPSCR
 * Opcode: 63 / 583
 */

#ifndef OPCODE_MFFS_H
#define OPCODE_MFFS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t frD;
    bool Rc;
} MFFS_Instruction;

static inline bool decode_mffs(uint32_t inst, MFFS_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 63 || ((inst >> 1) & 0x3FF) != 583) return false;
    d->frD = (inst >> 21) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_mffs(const MFFS_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "f%u = (double)fpscr;", d->frD);
    if (d->Rc) w += snprintf(o + w, s - w, "\ncr1 = (fpscr >> 28) & 0xF;");
    return w;
}

static inline int comment_mffs(const MFFS_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mffs%s f%u", d->Rc?".":"", d->frD);
}

#endif

