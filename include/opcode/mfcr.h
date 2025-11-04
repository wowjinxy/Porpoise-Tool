/**
 * @file mfcr.h
 * @brief MFCR - Move From Condition Register
 * 
 * Opcode: 31 (primary) / 19 (extended)
 * Format: XFX-form
 * Syntax: mfcr rD
 * 
 * Description: Move entire 32-bit Condition Register to rD
 */

#ifndef OPCODE_MFCR_H
#define OPCODE_MFCR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_MFCR_PRIMARY     31
#define OP_MFCR_EXTENDED    19

// Instruction format masks
#define MFCR_OPCD_MASK      0xFC000000  // Bits 0-5
#define MFCR_RT_MASK        0x03E00000  // Bits 6-10
#define MFCR_XO_MASK        0x000007FE  // Bits 21-30

// Instruction format shifts
#define MFCR_RT_SHIFT       21
#define MFCR_XO_SHIFT       1

/**
 * @brief Decoded MFCR instruction
 */
typedef struct {
    uint8_t rD;                 // Destination register (0-31)
} MFCR_Instruction;

/**
 * @brief Decode MFCR instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_mfcr(uint32_t instruction, MFCR_Instruction *decoded) {
    uint32_t primary = (instruction & MFCR_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & MFCR_XO_MASK) >> MFCR_XO_SHIFT;
    
    if (primary != OP_MFCR_PRIMARY || extended != OP_MFCR_EXTENDED) {
        return false;
    }
    
    decoded->rD = (instruction & MFCR_RT_MASK) >> MFCR_RT_SHIFT;
    
    return true;
}

/**
 * @brief Transpile MFCR instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_mfcr(const MFCR_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    // Pack all CR fields into a single 32-bit value
    return snprintf(output, output_size,
                   "r%u = (cr0 << 28) | (cr1 << 24) | (cr2 << 20) | (cr3 << 16) | "
                   "(cr4 << 12) | (cr5 << 8) | (cr6 << 4) | cr7;",
                   decoded->rD);
}

/**
 * @brief Generate assembly-like comment for MFCR instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_mfcr(const MFCR_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    return snprintf(output, output_size, "mfcr r%u", decoded->rD);
}

#endif // OPCODE_MFCR_H

