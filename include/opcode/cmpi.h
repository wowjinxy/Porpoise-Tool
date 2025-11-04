/**
 * @file cmpi.h
 * @brief CMPI - Compare Immediate
 * 
 * Opcode: 11
 * Format: D-form
 * Syntax: cmpi crfD, L, rA, SIMM
 *         cmpwi crfD, rA, SIMM (pseudo-op for L=0, 32-bit compare)
 *         cmpdi crfD, rA, SIMM (pseudo-op for L=1, 64-bit compare - not applicable to 32-bit PPC)
 * 
 * Description: Compare rA with signed immediate and set condition register field crfD
 */

#ifndef OPCODE_CMPI_H
#define OPCODE_CMPI_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_CMPI             11

// Instruction format masks
#define CMPI_OPCD_MASK      0xFC000000  // Bits 0-5
#define CMPI_CRFD_MASK      0x03800000  // Bits 6-8
#define CMPI_L_MASK         0x00200000  // Bit 10
#define CMPI_RA_MASK        0x001F0000  // Bits 11-15
#define CMPI_SIMM_MASK      0x0000FFFF  // Bits 16-31 (signed)

// Instruction format shifts
#define CMPI_CRFD_SHIFT     23
#define CMPI_L_SHIFT        21
#define CMPI_RA_SHIFT       16

/**
 * @brief Decoded CMPI instruction
 */
typedef struct {
    uint8_t crfD;               // Destination CR field (0-7)
    bool L;                     // 0=32-bit compare, 1=64-bit (always 0 on 32-bit PPC)
    uint8_t rA;                 // Source register to compare (0-31)
    int16_t SIMM;               // Signed immediate to compare against
} CMPI_Instruction;

/**
 * @brief Decode CMPI instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_cmpi(uint32_t instruction, CMPI_Instruction *decoded) {
    uint32_t primary = (instruction & CMPI_OPCD_MASK) >> 26;
    
    if (primary != OP_CMPI) {
        return false;
    }
    
    decoded->crfD = (instruction & CMPI_CRFD_MASK) >> CMPI_CRFD_SHIFT;
    decoded->L = (instruction & CMPI_L_MASK) != 0;
    decoded->rA = (instruction & CMPI_RA_MASK) >> CMPI_RA_SHIFT;
    decoded->SIMM = (int16_t)(instruction & CMPI_SIMM_MASK);
    
    return true;
}

/**
 * @brief Transpile CMPI instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_cmpi(const CMPI_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    // Generate condition register update
    return snprintf(output, output_size,
                   "cr%u = ((int32_t)r%u < %d ? 0x8 : "
                   "(int32_t)r%u > %d ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                   decoded->crfD, decoded->rA, decoded->SIMM,
                   decoded->rA, decoded->SIMM);
}

/**
 * @brief Generate assembly-like comment for CMPI instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_cmpi(const CMPI_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    // Use cmpwi pseudo-op for cr0 and L=0
    if (decoded->crfD == 0 && !decoded->L) {
        return snprintf(output, output_size,
                       "cmpwi r%u, %d",
                       decoded->rA, decoded->SIMM);
    }
    
    return snprintf(output, output_size,
                   "cmpi cr%u, %u, r%u, %d",
                   decoded->crfD, decoded->L ? 1 : 0, decoded->rA, decoded->SIMM);
}

#endif // OPCODE_CMPI_H

