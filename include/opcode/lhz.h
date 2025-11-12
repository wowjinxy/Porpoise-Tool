/**
 * @file lhz.h
 * @brief LHZ - Load Halfword and Zero
 * 
 * Opcode: 40
 * Format: D-form
 * Syntax: lhz rD, d(rA)
 * 
 * Description: Load halfword (16-bit) from memory address (rA|0) + d, zero-extend to 32 bits
 */

#ifndef OPCODE_LHZ_H
#define OPCODE_LHZ_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_LHZ              40

// Instruction format masks
#define LHZ_OPCD_MASK       0xFC000000  // Bits 0-5
#define LHZ_RT_MASK         0x03E00000  // Bits 6-10
#define LHZ_RA_MASK         0x001F0000  // Bits 11-15
#define LHZ_D_MASK          0x0000FFFF  // Bits 16-31 (signed)

// Instruction format shifts
#define LHZ_RT_SHIFT        21
#define LHZ_RA_SHIFT        16

/**
 * @brief Decoded LHZ instruction
 */
typedef struct {
    uint8_t rD;                 // Destination register (0-31)
    uint8_t rA;                 // Base address register (0-31, 0 means use 0)
    int16_t d;                  // Signed displacement
} LHZ_Instruction;

/**
 * @brief Decode LHZ instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_lhz(uint32_t instruction, LHZ_Instruction *decoded) {
    uint32_t primary = (instruction & LHZ_OPCD_MASK) >> 26;
    
    if (primary != OP_LHZ) {
        return false;
    }
    
    decoded->rD = (instruction & LHZ_RT_MASK) >> LHZ_RT_SHIFT;
    decoded->rA = (instruction & LHZ_RA_MASK) >> LHZ_RA_SHIFT;
    decoded->d = (int16_t)(instruction & LHZ_D_MASK);
    
    return true;
}

/**
 * @brief Transpile LHZ instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_lhz(const LHZ_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    if (decoded->rA == 0) {
        // Absolute address - should be resolved by transpiler to actual symbol/location
        uint32_t abs_addr = (uint32_t)(int16_t)decoded->d;
        return snprintf(output, output_size,
                       "r%u = *(uint16_t*)(uintptr_t)0x%08X;",
                       decoded->rD, abs_addr);
    } else {
        // Register-based address - registers contain real host pointers, use direct cast
        if (decoded->d == 0) {
            return snprintf(output, output_size,
                           "r%u = *(uint16_t*)(r%u);",
                           decoded->rD, decoded->rA);
        } else if (decoded->d > 0) {
            return snprintf(output, output_size,
                           "r%u = *(uint16_t*)(r%u + 0x%x);",
                           decoded->rD, decoded->rA, (uint16_t)decoded->d);
        } else {
            return snprintf(output, output_size,
                           "r%u = *(uint16_t*)(r%u - 0x%x);",
                           decoded->rD, decoded->rA, (uint16_t)(-decoded->d));
        }
    }
}

/**
 * @brief Generate assembly-like comment for LHZ instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_lhz(const LHZ_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    if (decoded->d == 0) {
        return snprintf(output, output_size,
                       "lhz r%u, 0(r%u)",
                       decoded->rD, decoded->rA);
    } else if (decoded->d > 0) {
        return snprintf(output, output_size,
                       "lhz r%u, 0x%x(r%u)",
                       decoded->rD, (uint16_t)decoded->d, decoded->rA);
    } else {
        return snprintf(output, output_size,
                       "lhz r%u, -0x%x(r%u)",
                       decoded->rD, (uint16_t)(-decoded->d), decoded->rA);
    }
}

#endif // OPCODE_LHZ_H

