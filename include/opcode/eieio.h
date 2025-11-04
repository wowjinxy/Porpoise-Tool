/**
 * @file eieio.h
 * @brief EIEIO - Enforce In-order Execution of I/O
 * 
 * Opcode: 31 / 854
 * Format: X-form
 * Syntax: eieio
 * 
 * Description: Ensure all previous I/O operations complete before continuing
 */

#ifndef OPCODE_EIEIO_H
#define OPCODE_EIEIO_H

#include <stdint.h>
#include <stdbool.h>

#define OP_EIEIO_PRIMARY    31
#define OP_EIEIO_EXTENDED   854

typedef struct {
    uint8_t dummy;
} EIEIO_Instruction;

static inline bool decode_eieio(uint32_t inst, EIEIO_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_EIEIO_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_EIEIO_EXTENDED) return false;
    (void)d;
    return true;
}

static inline int transpile_eieio(const EIEIO_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, ";  /* eieio - enforce I/O order (no-op in C) */");
}

static inline int comment_eieio(const EIEIO_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, "eieio");
}

#endif // OPCODE_EIEIO_H

