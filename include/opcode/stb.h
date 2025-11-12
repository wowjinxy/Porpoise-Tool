/**
 * @file stb.h
 * @brief STB - Store Byte
 * 
 * Opcode: 38
 * Format: D-form
 * Syntax: stb rS, d(rA)
 * 
 * Description: Store byte (8-bit) from rS to memory address (rA|0) + d
 */

#ifndef OPCODE_STB_H
#define OPCODE_STB_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_STB              38

// Instruction format masks
#define STB_OPCD_MASK       0xFC000000  // Bits 0-5
#define STB_RS_MASK         0x03E00000  // Bits 6-10
#define STB_RA_MASK         0x001F0000  // Bits 11-15
#define STB_D_MASK          0x0000FFFF  // Bits 16-31 (signed)

// Instruction format shifts
#define STB_RS_SHIFT        21
#define STB_RA_SHIFT        16

/**
 * @brief Decoded STB instruction
 */
typedef struct {
    uint8_t rS;                 // Source register (0-31)
    uint8_t rA;                 // Base address register (0-31, 0 means use 0)
    int16_t d;                  // Signed displacement
} STB_Instruction;

/**
 * @brief Decode STB instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_stb(uint32_t instruction, STB_Instruction *decoded) {
    uint32_t primary = (instruction & STB_OPCD_MASK) >> 26;
    
    if (primary != OP_STB) {
        return false;
    }
    
    decoded->rS = (instruction & STB_RS_MASK) >> STB_RS_SHIFT;
    decoded->rA = (instruction & STB_RA_MASK) >> STB_RA_SHIFT;
    decoded->d = (int16_t)(instruction & STB_D_MASK);
    
    return true;
}

/**
 * @brief Transpile STB instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_stb(const STB_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    if (decoded->rA == 0) {
        // Absolute address - should be resolved by transpiler to actual symbol/location
        uint32_t abs_addr = (uint32_t)(int16_t)decoded->d;
        return snprintf(output, output_size,
                       "*(uint8_t*)(uintptr_t)0x%08X = r%u;",
                       abs_addr, decoded->rS);
    } else {
        // Register-based address - registers contain real host pointers, use direct cast
        if (decoded->d == 0) {
            return snprintf(output, output_size,
                           "*(uint8_t*)(r%u) = r%u;",
                           decoded->rA, decoded->rS);
        } else if (decoded->d > 0) {
            return snprintf(output, output_size,
                           "*(uint8_t*)(r%u + 0x%x) = r%u;",
                           decoded->rA, (uint16_t)decoded->d, decoded->rS);
        } else {
            return snprintf(output, output_size,
                           "*(uint8_t*)(r%u - 0x%x) = r%u;",
                           decoded->rA, (uint16_t)(-decoded->d), decoded->rS);
        }
    }
}

/**
 * @brief Generate assembly-like comment for STB instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_stb(const STB_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    if (decoded->d == 0) {
        return snprintf(output, output_size,
                       "stb r%u, 0(r%u)",
                       decoded->rS, decoded->rA);
    } else if (decoded->d > 0) {
        return snprintf(output, output_size,
                       "stb r%u, 0x%x(r%u)",
                       decoded->rS, (uint16_t)decoded->d, decoded->rA);
    } else {
        return snprintf(output, output_size,
                       "stb r%u, -0x%x(r%u)",
                       decoded->rS, (uint16_t)(-decoded->d), decoded->rA);
    }
}

#endif // OPCODE_STB_H

