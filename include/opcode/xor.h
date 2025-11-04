/**
 * @file xor.h
 * @brief XOR - Logical XOR
 * 
 * Opcode: 31 (primary) / 316 (extended)
 * Format: X-form
 * Syntax: xor rA, rS, rB
 *         xor. rA, rS, rB (with Rc=1)
 * 
 * Description: XOR rS with rB and store result in rA
 */

#ifndef OPCODE_XOR_H
#define OPCODE_XOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_XOR_PRIMARY      31
#define OP_XOR_EXTENDED     316

// Instruction format masks
#define XOR_OPCD_MASK       0xFC000000  // Bits 0-5
#define XOR_RS_MASK         0x03E00000  // Bits 6-10
#define XOR_RA_MASK         0x001F0000  // Bits 11-15
#define XOR_RB_MASK         0x0000F800  // Bits 16-20
#define XOR_XO_MASK         0x000007FE  // Bits 21-30
#define XOR_RC_MASK         0x00000001  // Bit 31

// Instruction format shifts
#define XOR_RS_SHIFT        21
#define XOR_RA_SHIFT        16
#define XOR_RB_SHIFT        11
#define XOR_XO_SHIFT        1

/**
 * @brief Decoded XOR instruction
 */
typedef struct {
    uint8_t rA;                 // Destination register (0-31)
    uint8_t rS;                 // Source register (0-31)
    uint8_t rB;                 // Source register B (0-31)
    bool Rc;                    // Record bit (update CR0)
} XOR_Instruction;

/**
 * @brief Decode XOR instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_xor(uint32_t instruction, XOR_Instruction *decoded) {
    uint32_t primary = (instruction & XOR_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & XOR_XO_MASK) >> XOR_XO_SHIFT;
    
    if (primary != OP_XOR_PRIMARY || extended != OP_XOR_EXTENDED) {
        return false;
    }
    
    decoded->rS = (instruction & XOR_RS_MASK) >> XOR_RS_SHIFT;
    decoded->rA = (instruction & XOR_RA_MASK) >> XOR_RA_SHIFT;
    decoded->rB = (instruction & XOR_RB_MASK) >> XOR_RB_SHIFT;
    decoded->Rc = (instruction & XOR_RC_MASK) != 0;
    
    return true;
}

/**
 * @brief Transpile XOR instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_xor(const XOR_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    int written = 0;
    
    // Perform XOR operation
    written += snprintf(output + written, output_size - written,
                       "r%u = r%u ^ r%u;",
                       decoded->rA, decoded->rS, decoded->rB);
    
    // Update CR0 if Rc=1
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                           "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                           decoded->rA, decoded->rA);
    }
    
    return written;
}

/**
 * @brief Generate assembly-like comment for XOR instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_xor(const XOR_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    return snprintf(output, output_size,
                   "xor%s r%u, r%u, r%u",
                   decoded->Rc ? "." : "",
                   decoded->rA, decoded->rS, decoded->rB);
}

#endif // OPCODE_XOR_H

