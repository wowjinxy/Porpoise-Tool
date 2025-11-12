/**
 * @file lbz.h
 * @brief LBZ - Load Byte and Zero
 * 
 * Opcode: 34
 * Format: D-form
 * Syntax: lbz rD, d(rA)
 * 
 * Description: Load byte (8-bit) from memory address (rA|0) + d, zero-extend to 32 bits
 */

#ifndef OPCODE_LBZ_H
#define OPCODE_LBZ_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_LBZ              34

// Instruction format masks
#define LBZ_OPCD_MASK       0xFC000000  // Bits 0-5
#define LBZ_RT_MASK         0x03E00000  // Bits 6-10
#define LBZ_RA_MASK         0x001F0000  // Bits 11-15
#define LBZ_D_MASK          0x0000FFFF  // Bits 16-31 (signed)

// Instruction format shifts
#define LBZ_RT_SHIFT        21
#define LBZ_RA_SHIFT        16

/**
 * @brief Decoded LBZ instruction
 */
typedef struct {
    uint8_t rD;                 // Destination register (0-31)
    uint8_t rA;                 // Base address register (0-31, 0 means use 0)
    int16_t d;                  // Signed displacement
} LBZ_Instruction;

/**
 * @brief Decode LBZ instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_lbz(uint32_t instruction, LBZ_Instruction *decoded) {
    uint32_t primary = (instruction & LBZ_OPCD_MASK) >> 26;
    
    if (primary != OP_LBZ) {
        return false;
    }
    
    decoded->rD = (instruction & LBZ_RT_MASK) >> LBZ_RT_SHIFT;
    decoded->rA = (instruction & LBZ_RA_MASK) >> LBZ_RA_SHIFT;
    decoded->d = (int16_t)(instruction & LBZ_D_MASK);
    
    return true;
}

/**
 * @brief Transpile LBZ instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_lbz(const LBZ_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    if (decoded->rA == 0) {
        // Absolute address - should be resolved by transpiler to actual symbol/location
        uint32_t abs_addr = (uint32_t)(int16_t)decoded->d;
        return snprintf(output, output_size,
                       "r%u = *(uint8_t*)(uintptr_t)0x%08X;",
                       decoded->rD, abs_addr);
    } else {
        // Register-based address - registers contain real host pointers, use direct cast
        if (decoded->d == 0) {
            return snprintf(output, output_size,
                           "r%u = *(uint8_t*)(r%u);",
                           decoded->rD, decoded->rA);
        } else if (decoded->d > 0) {
            return snprintf(output, output_size,
                           "r%u = *(uint8_t*)(r%u + 0x%x);",
                           decoded->rD, decoded->rA, (uint16_t)decoded->d);
        } else {
            return snprintf(output, output_size,
                           "r%u = *(uint8_t*)(r%u - 0x%x);",
                           decoded->rD, decoded->rA, (uint16_t)(-decoded->d));
        }
    }
}

/**
 * @brief Generate assembly-like comment for LBZ instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_lbz(const LBZ_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    if (decoded->d == 0) {
        return snprintf(output, output_size,
                       "lbz r%u, 0(r%u)",
                       decoded->rD, decoded->rA);
    } else if (decoded->d > 0) {
        return snprintf(output, output_size,
                       "lbz r%u, 0x%x(r%u)",
                       decoded->rD, (uint16_t)decoded->d, decoded->rA);
    } else {
        return snprintf(output, output_size,
                       "lbz r%u, -0x%x(r%u)",
                       decoded->rD, (uint16_t)(-decoded->d), decoded->rA);
    }
}

#endif // OPCODE_LBZ_H

