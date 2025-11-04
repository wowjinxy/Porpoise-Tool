/**
 * @file andis.h
 * @brief ANDIS. - AND Immediate Shifted (Always sets Rc)
 * 
 * Opcode: 29
 * Format: D-form
 * Syntax: andis. rA, rS, UIMM
 * 
 * Description: AND rS with (UIMM << 16), store in rA, and update CR0
 * Note: This instruction ALWAYS sets the record bit (updates CR0)
 */

#ifndef OPCODE_ANDIS_H
#define OPCODE_ANDIS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_ANDIS            29

// Instruction format masks
#define ANDIS_OPCD_MASK     0xFC000000  // Bits 0-5
#define ANDIS_RS_MASK       0x03E00000  // Bits 6-10
#define ANDIS_RA_MASK       0x001F0000  // Bits 11-15
#define ANDIS_UIMM_MASK     0x0000FFFF  // Bits 16-31 (unsigned)

// Instruction format shifts
#define ANDIS_RS_SHIFT      21
#define ANDIS_RA_SHIFT      16

/**
 * @brief Decoded ANDIS instruction
 */
typedef struct {
    uint8_t rA;                 // Destination register (0-31)
    uint8_t rS;                 // Source register (0-31)
    uint16_t UIMM;              // Unsigned immediate value
} ANDIS_Instruction;

/**
 * @brief Decode ANDIS instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_andis(uint32_t instruction, ANDIS_Instruction *decoded) {
    uint32_t primary = (instruction & ANDIS_OPCD_MASK) >> 26;
    
    if (primary != OP_ANDIS) {
        return false;
    }
    
    decoded->rS = (instruction & ANDIS_RS_MASK) >> ANDIS_RS_SHIFT;
    decoded->rA = (instruction & ANDIS_RA_MASK) >> ANDIS_RA_SHIFT;
    decoded->UIMM = instruction & ANDIS_UIMM_MASK;
    
    return true;
}

/**
 * @brief Transpile ANDIS instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_andis(const ANDIS_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    int written = 0;
    
    // Perform AND operation with shifted immediate
    written += snprintf(output + written, output_size - written,
                       "r%u = r%u & 0x%x;",
                       decoded->rA, decoded->rS, decoded->UIMM << 16);
    
    // ANDIS always updates CR0
    written += snprintf(output + written, output_size - written,
                       "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                       "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                       decoded->rA, decoded->rA);
    
    return written;
}

/**
 * @brief Generate assembly-like comment for ANDIS instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_andis(const ANDIS_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    return snprintf(output, output_size,
                   "andis. r%u, r%u, 0x%x",
                   decoded->rA, decoded->rS, decoded->UIMM);
}

#endif // OPCODE_ANDIS_H

