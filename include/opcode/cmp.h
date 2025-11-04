/**
 * @file cmp.h
 * @brief CMP - Compare
 * 
 * Opcode: 31 (primary) / 0 (extended)
 * Format: X-form
 * Syntax: cmp crfD, L, rA, rB
 *         cmpw crfD, rA, rB (pseudo-op for L=0, 32-bit compare)
 * 
 * Description: Compare rA with rB and set condition register field crfD
 */

#ifndef OPCODE_CMP_H
#define OPCODE_CMP_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_CMP_PRIMARY      31
#define OP_CMP_EXTENDED     0

// Instruction format masks
#define CMP_OPCD_MASK       0xFC000000  // Bits 0-5
#define CMP_CRFD_MASK       0x03800000  // Bits 6-8
#define CMP_L_MASK          0x00200000  // Bit 10
#define CMP_RA_MASK         0x001F0000  // Bits 11-15
#define CMP_RB_MASK         0x0000F800  // Bits 16-20
#define CMP_XO_MASK         0x000007FE  // Bits 21-30

// Instruction format shifts
#define CMP_CRFD_SHIFT      23
#define CMP_L_SHIFT         21
#define CMP_RA_SHIFT        16
#define CMP_RB_SHIFT        11
#define CMP_XO_SHIFT        1

/**
 * @brief Decoded CMP instruction
 */
typedef struct {
    uint8_t crfD;               // Destination CR field (0-7)
    bool L;                     // 0=32-bit compare, 1=64-bit
    uint8_t rA;                 // First register to compare (0-31)
    uint8_t rB;                 // Second register to compare (0-31)
} CMP_Instruction;

/**
 * @brief Decode CMP instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_cmp(uint32_t instruction, CMP_Instruction *decoded) {
    uint32_t primary = (instruction & CMP_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & CMP_XO_MASK) >> CMP_XO_SHIFT;
    
    if (primary != OP_CMP_PRIMARY || extended != OP_CMP_EXTENDED) {
        return false;
    }
    
    decoded->crfD = (instruction & CMP_CRFD_MASK) >> CMP_CRFD_SHIFT;
    decoded->L = (instruction & CMP_L_MASK) != 0;
    decoded->rA = (instruction & CMP_RA_MASK) >> CMP_RA_SHIFT;
    decoded->rB = (instruction & CMP_RB_MASK) >> CMP_RB_SHIFT;
    
    return true;
}

/**
 * @brief Transpile CMP instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_cmp(const CMP_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    // Generate signed comparison
    return snprintf(output, output_size,
                   "cr%u = ((int32_t)r%u < (int32_t)r%u ? 0x8 : "
                   "(int32_t)r%u > (int32_t)r%u ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                   decoded->crfD, decoded->rA, decoded->rB,
                   decoded->rA, decoded->rB);
}

/**
 * @brief Generate assembly-like comment for CMP instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_cmp(const CMP_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    // Use cmpw pseudo-op for cr0 and L=0
    if (decoded->crfD == 0 && !decoded->L) {
        return snprintf(output, output_size,
                       "cmpw r%u, r%u",
                       decoded->rA, decoded->rB);
    }
    
    return snprintf(output, output_size,
                   "cmp cr%u, %u, r%u, r%u",
                   decoded->crfD, decoded->L ? 1 : 0,
                   decoded->rA, decoded->rB);
}

#endif // OPCODE_CMP_H

