/**
 * @file stmw.h
 * @brief STMW - Store Multiple Word
 * 
 * Opcode: 47
 * Format: D-form
 * Syntax: stmw rS, d(rA)
 * 
 * Description: Store consecutive words starting from rS through r31 to memory
 *              starting at address (rA|0) + d
 */

#ifndef OPCODE_STMW_H
#define OPCODE_STMW_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_STMW             47

// Instruction format masks
#define STMW_OPCD_MASK      0xFC000000  // Bits 0-5
#define STMW_RS_MASK        0x03E00000  // Bits 6-10
#define STMW_RA_MASK        0x001F0000  // Bits 11-15
#define STMW_D_MASK         0x0000FFFF  // Bits 16-31 (signed)

// Instruction format shifts
#define STMW_RS_SHIFT       21
#define STMW_RA_SHIFT       16

/**
 * @brief Decoded STMW instruction
 */
typedef struct {
    uint8_t rS;                 // Starting source register (0-31)
    uint8_t rA;                 // Base address register (0-31, 0 means use 0)
    int16_t d;                  // Signed displacement
} STMW_Instruction;

/**
 * @brief Decode STMW instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_stmw(uint32_t instruction, STMW_Instruction *decoded) {
    uint32_t primary = (instruction & STMW_OPCD_MASK) >> 26;
    
    if (primary != OP_STMW) {
        return false;
    }
    
    decoded->rS = (instruction & STMW_RS_MASK) >> STMW_RS_SHIFT;
    decoded->rA = (instruction & STMW_RA_MASK) >> STMW_RA_SHIFT;
    decoded->d = (int16_t)(instruction & STMW_D_MASK);
    
    return true;
}

/**
 * @brief Transpile STMW instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_stmw(const STMW_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    int written = 0;
    
    // Calculate base address expression
    char base_expr[64];
    bool is_absolute = (decoded->rA == 0);
    if (is_absolute) {
        uint32_t abs_addr = (uint32_t)(int16_t)decoded->d;
        snprintf(base_expr, sizeof(base_expr), "(uintptr_t)0x%08X", abs_addr);
    } else {
        if (decoded->d == 0) {
            snprintf(base_expr, sizeof(base_expr), "r%u", decoded->rA);
        } else if (decoded->d > 0) {
            snprintf(base_expr, sizeof(base_expr), "r%u + 0x%x", decoded->rA, (uint16_t)decoded->d);
        } else {
            snprintf(base_expr, sizeof(base_expr), "r%u - 0x%x", decoded->rA, (uint16_t)(-decoded->d));
        }
    }
    
    // Generate stores for each register from rS to r31
    int num_regs = 32 - decoded->rS;
    
    if (num_regs == 1) {
        // Only one register
        written += snprintf(output + written, output_size - written,
                           "*(uint32_t*)%s = r%u;",
                           base_expr, decoded->rS);
    } else {
        // Multiple registers - use a loop or expand inline
        written += snprintf(output + written, output_size - written,
                           "{ uint32_t *p = (uint32_t*)%s; ",
                           base_expr);
        
        for (int i = 0; i < num_regs; i++) {
            written += snprintf(output + written, output_size - written,
                               "p[%d] = r%u; ",
                               i, decoded->rS + i);
        }
        
        written += snprintf(output + written, output_size - written, "}");
    }
    
    return written;
}

/**
 * @brief Generate assembly-like comment for STMW instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_stmw(const STMW_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    if (decoded->d == 0) {
        return snprintf(output, output_size,
                       "stmw r%u, 0(r%u)",
                       decoded->rS, decoded->rA);
    } else if (decoded->d > 0) {
        return snprintf(output, output_size,
                       "stmw r%u, 0x%x(r%u)",
                       decoded->rS, (uint16_t)decoded->d, decoded->rA);
    } else {
        return snprintf(output, output_size,
                       "stmw r%u, -0x%x(r%u)",
                       decoded->rS, (uint16_t)(-decoded->d), decoded->rA);
    }
}

#endif // OPCODE_STMW_H

