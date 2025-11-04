/**
 * @file sync.h
 * @brief SYNC - Synchronize
 * 
 * Opcode: 31 (primary) / 598 (extended)
 * Format: X-form
 * Syntax: sync
 * 
 * Description: Synchronize - ensures all previous instructions complete before continuing
 */

#ifndef OPCODE_SYNC_H
#define OPCODE_SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_SYNC_PRIMARY     31
#define OP_SYNC_EXTENDED    598

#define SYNC_OPCD_MASK      0xFC000000
#define SYNC_XO_MASK        0x000007FE

#define SYNC_XO_SHIFT       1

typedef struct {
    // No operands
    uint8_t dummy;  // Placeholder
} SYNC_Instruction;

static inline bool decode_sync(uint32_t instruction, SYNC_Instruction *decoded) {
    uint32_t primary = (instruction & SYNC_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & SYNC_XO_MASK) >> SYNC_XO_SHIFT;
    
    if (primary != OP_SYNC_PRIMARY || extended != OP_SYNC_EXTENDED) {
        return false;
    }
    
    (void)decoded;  // No fields to decode
    return true;
}

static inline int transpile_sync(const SYNC_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    (void)decoded;
    return snprintf(output, output_size,
                   ";  /* sync - memory barrier (no-op in C) */");
}

static inline int comment_sync(const SYNC_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    (void)decoded;
    return snprintf(output, output_size, "sync");
}

#endif // OPCODE_SYNC_H

