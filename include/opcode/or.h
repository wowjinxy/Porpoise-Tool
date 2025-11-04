/**
 * @file or.h
 * @brief OR - Logical OR
 * 
 * Opcode: 31 (primary) / 444 (extended)
 * Format: X-form
 * Syntax: or rA, rS, rB
 *         or. rA, rS, rB (with Rc=1)
 *         mr rA, rS (pseudo-op when rB=rS, move register)
 * 
 * Description: OR rS with rB and store result in rA
 */

#ifndef OPCODE_OR_H
#define OPCODE_OR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_OR_PRIMARY       31
#define OP_OR_EXTENDED      444

// Instruction format masks
#define OR_OPCD_MASK        0xFC000000  // Bits 0-5
#define OR_RS_MASK          0x03E00000  // Bits 6-10
#define OR_RA_MASK          0x001F0000  // Bits 11-15
#define OR_RB_MASK          0x0000F800  // Bits 16-20
#define OR_XO_MASK          0x000007FE  // Bits 21-30
#define OR_RC_MASK          0x00000001  // Bit 31

// Instruction format shifts
#define OR_RS_SHIFT         21
#define OR_RA_SHIFT         16
#define OR_RB_SHIFT         11
#define OR_XO_SHIFT         1

/**
 * @brief Decoded OR instruction
 */
typedef struct {
    uint8_t rA;                 // Destination register (0-31)
    uint8_t rS;                 // Source register (0-31)
    uint8_t rB;                 // Source register B (0-31)
    bool Rc;                    // Record bit (update CR0)
} OR_Instruction;

/**
 * @brief Decode OR instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_or(uint32_t instruction, OR_Instruction *decoded) {
    uint32_t primary = (instruction & OR_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & OR_XO_MASK) >> OR_XO_SHIFT;
    
    if (primary != OP_OR_PRIMARY || extended != OP_OR_EXTENDED) {
        return false;
    }
    
    decoded->rS = (instruction & OR_RS_MASK) >> OR_RS_SHIFT;
    decoded->rA = (instruction & OR_RA_MASK) >> OR_RA_SHIFT;
    decoded->rB = (instruction & OR_RB_MASK) >> OR_RB_SHIFT;
    decoded->Rc = (instruction & OR_RC_MASK) != 0;
    
    return true;
}

/**
 * @brief Transpile OR instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_or(const OR_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    int written = 0;
    
    // Check for 'mr' pseudo-op (or rA, rS, rS)
    if (decoded->rS == decoded->rB) {
        // Move register
        written += snprintf(output + written, output_size - written,
                           "r%u = r%u;",
                           decoded->rA, decoded->rS);
    } else {
        // Regular OR
        written += snprintf(output + written, output_size - written,
                           "r%u = r%u | r%u;",
                           decoded->rA, decoded->rS, decoded->rB);
    }
    
    // Update CR0 if Rc=1
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                           "(int32_t)r%u > 0 ? 0x4 : 0x2);",
                           decoded->rA, decoded->rA);
    }
    
    return written;
}

/**
 * @brief Generate assembly-like comment for OR instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_or(const OR_Instruction *decoded,
                             char *output,
                             size_t output_size) {
    // Use 'mr' pseudo-op for move register
    if (decoded->rS == decoded->rB) {
        return snprintf(output, output_size,
                       "mr%s r%u, r%u",
                       decoded->Rc ? "." : "",
                       decoded->rA, decoded->rS);
    }
    
    return snprintf(output, output_size,
                   "or%s r%u, r%u, r%u",
                   decoded->Rc ? "." : "",
                   decoded->rA, decoded->rS, decoded->rB);
}

#endif // OPCODE_OR_H

