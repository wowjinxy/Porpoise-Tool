/**
 * @file mulli.h
 * @brief MULLI - Multiply Low Immediate
 * 
 * Opcode: 7
 * Format: D-form
 * Syntax: mulli rD, rA, SIMM
 * 
 * Description: Multiply rA by signed immediate, store low 32 bits in rD
 */

#ifndef OPCODE_MULLI_H
#define OPCODE_MULLI_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_MULLI            7

// Instruction format masks
#define MULLI_OPCD_MASK     0xFC000000  // Bits 0-5
#define MULLI_RT_MASK       0x03E00000  // Bits 6-10
#define MULLI_RA_MASK       0x001F0000  // Bits 11-15
#define MULLI_SIMM_MASK     0x0000FFFF  // Bits 16-31 (signed)

// Instruction format shifts
#define MULLI_RT_SHIFT      21
#define MULLI_RA_SHIFT      16

/**
 * @brief Decoded MULLI instruction
 */
typedef struct {
    uint8_t rD;                 // Destination register (0-31)
    uint8_t rA;                 // Source register (0-31)
    int16_t SIMM;               // Signed immediate multiplier
} MULLI_Instruction;

/**
 * @brief Decode MULLI instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_mulli(uint32_t instruction, MULLI_Instruction *decoded) {
    uint32_t primary = (instruction & MULLI_OPCD_MASK) >> 26;
    
    if (primary != OP_MULLI) {
        return false;
    }
    
    decoded->rD = (instruction & MULLI_RT_MASK) >> MULLI_RT_SHIFT;
    decoded->rA = (instruction & MULLI_RA_MASK) >> MULLI_RA_SHIFT;
    decoded->SIMM = (int16_t)(instruction & MULLI_SIMM_MASK);
    
    return true;
}

/**
 * @brief Transpile MULLI instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_mulli(const MULLI_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    // Simple multiplication (low 32 bits)
    return snprintf(output, output_size,
                   "r%u = (int32_t)r%u * %d;",
                   decoded->rD, decoded->rA, decoded->SIMM);
}

/**
 * @brief Generate assembly-like comment for MULLI instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_mulli(const MULLI_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    return snprintf(output, output_size,
                   "mulli r%u, r%u, %d",
                   decoded->rD, decoded->rA, decoded->SIMM);
}

#endif // OPCODE_MULLI_H

