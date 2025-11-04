/**
 * @file isync.h
 * @brief ISYNC - Instruction Synchronize
 * Opcode: 19 / 150
 */

#ifndef OPCODE_ISYNC_H
#define OPCODE_ISYNC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct { uint8_t dummy; } ISYNC_Instruction;

static inline bool decode_isync(uint32_t inst, ISYNC_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 19 || ((inst >> 1) & 0x3FF) != 150) return false;
    (void)d;
    return true;
}

static inline int transpile_isync(const ISYNC_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, ";  /* isync - instruction sync (no-op in C) */");
}

static inline int comment_isync(const ISYNC_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, "isync");
}

#endif

