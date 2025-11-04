/**
 * @file addi.h
 * @brief ADDI - Add Immediate
 * 
 * Opcode: 14
 * Format: D-form
 * Syntax: addi rD, rA, SIMM
 *         li rD, SIMM (when rA=0, pseudo-op for load immediate)
 *         la rD, d(rA) (pseudo-op for load address)
 * 
 * Description: Add signed immediate to rA (or 0) and store in rD
 */

#ifndef OPCODE_ADDI_H
#define OPCODE_ADDI_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_ADDI             14

// Instruction format masks
#define ADDI_OPCD_MASK      0xFC000000  // Bits 0-5
#define ADDI_RT_MASK        0x03E00000  // Bits 6-10
#define ADDI_RA_MASK        0x001F0000  // Bits 11-15
#define ADDI_SIMM_MASK      0x0000FFFF  // Bits 16-31 (signed)

// Instruction format shifts
#define ADDI_RT_SHIFT       21
#define ADDI_RA_SHIFT       16

/**
 * @brief Decoded ADDI instruction
 */
typedef struct {
    uint8_t rD;                 // Destination register (0-31)
    uint8_t rA;                 // Source register (0-31, 0 means use 0)
    int16_t SIMM;               // Signed immediate value
} ADDI_Instruction;

/**
 * @brief Decode ADDI instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_addi(uint32_t instruction, ADDI_Instruction *decoded) {
    uint32_t primary = (instruction & ADDI_OPCD_MASK) >> 26;
    
    if (primary != OP_ADDI) {
        return false;
    }
    
    decoded->rD = (instruction & ADDI_RT_MASK) >> ADDI_RT_SHIFT;
    decoded->rA = (instruction & ADDI_RA_MASK) >> ADDI_RA_SHIFT;
    decoded->SIMM = (int16_t)(instruction & ADDI_SIMM_MASK);  // Sign-extend
    
    return true;
}

/**
 * @brief Transpile ADDI instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_addi(const ADDI_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    if (decoded->rA == 0) {
        // li rD, SIMM (load immediate - pseudo-op)
        return snprintf(output, output_size,
                       "r%u = %d;",
                       decoded->rD, decoded->SIMM);
    } else {
        // addi rD, rA, SIMM
        return snprintf(output, output_size,
                       "r%u = r%u + %d;",
                       decoded->rD, decoded->rA, decoded->SIMM);
    }
}

/**
 * @brief Generate assembly-like comment for ADDI instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_addi(const ADDI_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    if (decoded->rA == 0) {
        // Use 'li' pseudo-op when rA=0
        return snprintf(output, output_size,
                       "li r%u, %d",
                       decoded->rD, decoded->SIMM);
    } else if (decoded->SIMM >= 0) {
        return snprintf(output, output_size,
                       "addi r%u, r%u, 0x%x",
                       decoded->rD, decoded->rA, (uint16_t)decoded->SIMM);
    } else {
        return snprintf(output, output_size,
                       "addi r%u, r%u, %d",
                       decoded->rD, decoded->rA, decoded->SIMM);
    }
}

#endif // OPCODE_ADDI_H

