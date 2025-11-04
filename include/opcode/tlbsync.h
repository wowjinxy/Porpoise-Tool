/**
 * @file tlbsync.h
 * @brief TLBSYNC - TLB Synchronize
 * Opcode: 31 / 566
 */

#ifndef OPCODE_TLBSYNC_H
#define OPCODE_TLBSYNC_H

#include <stdint.h>
#include <stdbool.h>

#define OP_TLBSYNC_PRIMARY    31
#define OP_TLBSYNC_EXTENDED   566

typedef struct {
    uint8_t dummy;
} TLBSYNC_Instruction;

static inline bool decode_tlbsync(uint32_t inst, TLBSYNC_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 31 || ((inst >> 1) & 0x3FF) != 566) return false;
    (void)d;
    return true;
}

static inline int transpile_tlbsync(const TLBSYNC_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, ";  /* tlbsync - TLB sync (no-op in C) */");
}

static inline int comment_tlbsync(const TLBSYNC_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, "tlbsync");
}

#endif

