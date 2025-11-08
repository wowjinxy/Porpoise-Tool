/**
 * @file sth.h
 * @brief STH - Store Halfword
 * 
 * Opcode: 44
 * Format: D-form
 * Syntax: sth rS, d(rA)
 * 
 * Description: Store halfword (16-bit) from rS to memory address (rA|0) + d
 */

#ifndef OPCODE_STH_H
#define OPCODE_STH_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_STH              44

// Instruction format masks
#define STH_OPCD_MASK       0xFC000000  // Bits 0-5
#define STH_RS_MASK         0x03E00000  // Bits 6-10
#define STH_RA_MASK         0x001F0000  // Bits 11-15
#define STH_D_MASK          0x0000FFFF  // Bits 16-31 (signed)

// Instruction format shifts
#define STH_RS_SHIFT        21
#define STH_RA_SHIFT        16

/**
 * @brief Decoded STH instruction
 */
typedef struct {
    uint8_t rS;                 // Source register (0-31)
    uint8_t rA;                 // Base address register (0-31, 0 means use 0)
    int16_t d;                  // Signed displacement
} STH_Instruction;

/**
 * @brief Decode STH instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_sth(uint32_t instruction, STH_Instruction *decoded) {
    uint32_t primary = (instruction & STH_OPCD_MASK) >> 26;
    
    if (primary != OP_STH) {
        return false;
    }
    
    decoded->rS = (instruction & STH_RS_MASK) >> STH_RS_SHIFT;
    decoded->rA = (instruction & STH_RA_MASK) >> STH_RA_SHIFT;
    decoded->d = (int16_t)(instruction & STH_D_MASK);
    
    return true;
}

/**
 * @brief Transpile STH instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_sth(const STH_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    if (decoded->rA == 0) {
        // Store to absolute address
        return snprintf(output, output_size,
                       "*(uint16_t*)translate_address(0x%x) = r%u;",
                       (uint32_t)decoded->d, decoded->rS);
    } else {
        // Store to address (rA + displacement)
        if (decoded->d == 0) {
            return snprintf(output, output_size,
                           "*(uint16_t*)translate_address(r%u) = r%u;",
                           decoded->rA, decoded->rS);
        } else if (decoded->d > 0) {
            return snprintf(output, output_size,
                           "*(uint16_t*)translate_address(r%u + 0x%x) = r%u;",
                           decoded->rA, (uint16_t)decoded->d, decoded->rS);
        } else {
            return snprintf(output, output_size,
                           "*(uint16_t*)translate_address(r%u - 0x%x) = r%u;",
                           decoded->rA, (uint16_t)(-decoded->d), decoded->rS);
        }
    }
}

/**
 * @brief Generate assembly-like comment for STH instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_sth(const STH_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    if (decoded->d == 0) {
        return snprintf(output, output_size,
                       "sth r%u, 0(r%u)",
                       decoded->rS, decoded->rA);
    } else if (decoded->d > 0) {
        return snprintf(output, output_size,
                       "sth r%u, 0x%x(r%u)",
                       decoded->rS, (uint16_t)decoded->d, decoded->rA);
    } else {
        return snprintf(output, output_size,
                       "sth r%u, -0x%x(r%u)",
                       decoded->rS, (uint16_t)(-decoded->d), decoded->rA);
    }
}

#endif // OPCODE_STH_H

