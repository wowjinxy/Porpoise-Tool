/**
 * @file lwz.h
 * @brief LWZ - Load Word and Zero
 * 
 * Opcode: 32
 * Format: D-form
 * Syntax: lwz rD, d(rA)
 * 
 * Description: Load word from memory address (rA|0) + d, zero-extend to 32 bits
 */

#ifndef OPCODE_LWZ_H
#define OPCODE_LWZ_H

#include <stdint.h>
#include <stdbool.h>

// Opcode encoding
#define OP_LWZ              32

// Instruction format masks
#define LWZ_OPCD_MASK       0xFC000000  // Bits 0-5
#define LWZ_RT_MASK         0x03E00000  // Bits 6-10
#define LWZ_RA_MASK         0x001F0000  // Bits 11-15
#define LWZ_D_MASK          0x0000FFFF  // Bits 16-31 (signed)

// Instruction format shifts
#define LWZ_RT_SHIFT        21
#define LWZ_RA_SHIFT        16

/**
 * @brief Decoded LWZ instruction
 */
typedef struct {
    uint8_t rD;                 // Destination register (0-31)
    uint8_t rA;                 // Base address register (0-31, 0 means use 0)
    int16_t d;                  // Signed displacement
} LWZ_Instruction;

/**
 * @brief Decode LWZ instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_lwz(uint32_t instruction, LWZ_Instruction *decoded) {
    uint32_t primary = (instruction & LWZ_OPCD_MASK) >> 26;
    
    if (primary != OP_LWZ) {
        return false;
    }
    
    decoded->rD = (instruction & LWZ_RT_MASK) >> LWZ_RT_SHIFT;
    decoded->rA = (instruction & LWZ_RA_MASK) >> LWZ_RA_SHIFT;
    decoded->d = (int16_t)(instruction & LWZ_D_MASK);  // Sign-extend
    
    return true;
}

/**
 * @brief Transpile LWZ instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_lwz(const LWZ_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    if (decoded->rA == 0) {
        return snprintf(output, output_size,
                       "r%u = *(uint32_t*)translate_address(%d);",
                       decoded->rD, (int16_t)decoded->d);
    } else {
        // Load from address (rA + displacement) - use safe address translation
        if (decoded->d == 0) {
            return snprintf(output, output_size,
                           "r%u = *(uint32_t*)translate_address(r%u);",
                           decoded->rD, decoded->rA);
        } else if (decoded->d > 0) {
            return snprintf(output, output_size,
                           "r%u = *(uint32_t*)translate_address(r%u + 0x%x);",
                           decoded->rD, decoded->rA, (uint16_t)decoded->d);
        } else {
            return snprintf(output, output_size,
                           "r%u = *(uint32_t*)translate_address(r%u - 0x%x);",
                           decoded->rD, decoded->rA, (uint16_t)(-decoded->d));
        }
    }
}

/**
 * @brief Generate assembly-like comment for LWZ instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_lwz(const LWZ_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    if (decoded->d == 0) {
        return snprintf(output, output_size,
                       "lwz r%u, 0(r%u)",
                       decoded->rD, decoded->rA);
    } else if (decoded->d > 0) {
        return snprintf(output, output_size,
                       "lwz r%u, 0x%x(r%u)",
                       decoded->rD, (uint16_t)decoded->d, decoded->rA);
    } else {
        return snprintf(output, output_size,
                       "lwz r%u, -0x%x(r%u)",
                       decoded->rD, (uint16_t)(-decoded->d), decoded->rA);
    }
}

#endif // OPCODE_LWZ_H

