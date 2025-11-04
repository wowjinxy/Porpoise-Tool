/**
 * @file andi.h
 * @brief ANDI. - AND Immediate (Always sets Rc)
 * 
 * Opcode: 28
 * Format: D-form
 * Syntax: andi. rA, rS, UIMM
 * 
 * Description: AND rS with immediate value, store in rA, and update CR0
 * Note: This instruction ALWAYS sets the record bit (updates CR0)
 */

#ifndef OPCODE_ANDI_H
#define OPCODE_ANDI_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_ANDI             28

// Instruction format masks
#define ANDI_OPCD_MASK      0xFC000000  // Bits 0-5
#define ANDI_RS_MASK        0x03E00000  // Bits 6-10
#define ANDI_RA_MASK        0x001F0000  // Bits 11-15
#define ANDI_UIMM_MASK      0x0000FFFF  // Bits 16-31 (unsigned)

// Instruction format shifts
#define ANDI_RS_SHIFT       21
#define ANDI_RA_SHIFT       16

/**
 * @brief Decoded ANDI instruction
 */
typedef struct {
    uint8_t rA;                 // Destination register (0-31)
    uint8_t rS;                 // Source register (0-31)
    uint16_t UIMM;              // Unsigned immediate value
} ANDI_Instruction;

/**
 * @brief Decode ANDI instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_andi(uint32_t instruction, ANDI_Instruction *decoded) {
    uint32_t primary = (instruction & ANDI_OPCD_MASK) >> 26;
    
    if (primary != OP_ANDI) {
        return false;
    }
    
    decoded->rS = (instruction & ANDI_RS_MASK) >> ANDI_RS_SHIFT;
    decoded->rA = (instruction & ANDI_RA_MASK) >> ANDI_RA_SHIFT;
    decoded->UIMM = instruction & ANDI_UIMM_MASK;
    
    return true;
}

/**
 * @brief Transpile ANDI instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_andi(const ANDI_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    int written = 0;
    
    // Perform AND operation
    written += snprintf(output + written, output_size - written,
                       "r%u = r%u & 0x%x;",
                       decoded->rA, decoded->rS, decoded->UIMM);
    
    // ANDI always updates CR0
    written += snprintf(output + written, output_size - written,
                       "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                       "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                       decoded->rA, decoded->rA);
    
    return written;
}

/**
 * @brief Generate assembly-like comment for ANDI instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_andi(const ANDI_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    return snprintf(output, output_size,
                   "andi. r%u, r%u, 0x%x",
                   decoded->rA, decoded->rS, decoded->UIMM);
}

#endif // OPCODE_ANDI_H

