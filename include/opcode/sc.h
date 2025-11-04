/**
 * @file sc.h
 * @brief SC - System Call
 * Opcode: 17
 */

#ifndef OPCODE_SC_H
#define OPCODE_SC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct { uint8_t dummy; } SC_Instruction;

static inline bool decode_sc(uint32_t inst, SC_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 17) return false;
    (void)d;
    return true;
}

static inline int transpile_sc(const SC_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, ";  /* sc - system call (no-op in C) */");
}

static inline int comment_sc(const SC_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, "sc");
}

#endif

