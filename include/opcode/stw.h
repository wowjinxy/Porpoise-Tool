/**
 * @file stw.h
 * @brief STW - Store Word
 * 
 * Opcode: 36
 * Format: D-form
 * Syntax: stw rS, d(rA)
 * 
 * Description: Store word from rS to memory address (rA|0) + d
 */

#ifndef OPCODE_STW_H
#define OPCODE_STW_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_STW              36

// Instruction format masks
#define STW_OPCD_MASK       0xFC000000  // Bits 0-5
#define STW_RS_MASK         0x03E00000  // Bits 6-10
#define STW_RA_MASK         0x001F0000  // Bits 11-15
#define STW_D_MASK          0x0000FFFF  // Bits 16-31 (signed)

// Instruction format shifts
#define STW_RS_SHIFT        21
#define STW_RA_SHIFT        16

/**
 * @brief Decoded STW instruction
 */
typedef struct {
    uint8_t rS;                 // Source register (0-31)
    uint8_t rA;                 // Base address register (0-31, 0 means use 0)
    int16_t d;                  // Signed displacement
} STW_Instruction;

/**
 * @brief Decode STW instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_stw(uint32_t instruction, STW_Instruction *decoded) {
    uint32_t primary = (instruction & STW_OPCD_MASK) >> 26;
    
    if (primary != OP_STW) {
        return false;
    }
    
    decoded->rS = (instruction & STW_RS_MASK) >> STW_RS_SHIFT;
    decoded->rA = (instruction & STW_RA_MASK) >> STW_RA_SHIFT;
    decoded->d = (int16_t)(instruction & STW_D_MASK);  // Sign-extend
    
    return true;
}

/**
 * @brief Transpile STW instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_stw(const STW_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    if (decoded->rA == 0) {
        // Store to absolute address (displacement only)
        return snprintf(output, output_size,
                       "*(uint32_t*)(mem + 0x%x) = r%u;",
                       (uint32_t)decoded->d, decoded->rS);
    } else {
        // Store to address (rA + displacement)
        if (decoded->d == 0) {
            return snprintf(output, output_size,
                           "*(uint32_t*)(mem + r%u) = r%u;",
                           decoded->rA, decoded->rS);
        } else if (decoded->d > 0) {
            return snprintf(output, output_size,
                           "*(uint32_t*)(mem + r%u + 0x%x) = r%u;",
                           decoded->rA, (uint16_t)decoded->d, decoded->rS);
        } else {
            return snprintf(output, output_size,
                           "*(uint32_t*)(mem + r%u - 0x%x) = r%u;",
                           decoded->rA, (uint16_t)(-decoded->d), decoded->rS);
        }
    }
}

/**
 * @brief Generate assembly-like comment for STW instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_stw(const STW_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    if (decoded->d == 0) {
        return snprintf(output, output_size,
                       "stw r%u, 0(r%u)",
                       decoded->rS, decoded->rA);
    } else if (decoded->d > 0) {
        return snprintf(output, output_size,
                       "stw r%u, 0x%x(r%u)",
                       decoded->rS, (uint16_t)decoded->d, decoded->rA);
    } else {
        return snprintf(output, output_size,
                       "stw r%u, -0x%x(r%u)",
                       decoded->rS, (uint16_t)(-decoded->d), decoded->rA);
    }
}

#endif // OPCODE_STW_H

