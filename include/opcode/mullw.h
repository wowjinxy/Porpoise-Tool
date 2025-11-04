/**
 * @file mullw.h
 * @brief MULLW - Multiply Low Word
 * 
 * Opcode: 31 (primary) / 235 (extended)
 * Format: XO-form
 * Syntax: mullw rD, rA, rB
 *         mullw. rD, rA, rB (with Rc=1)
 *         mullwo rD, rA, rB (with OE=1)
 *         mullwo. rD, rA, rB (with OE=1, Rc=1)
 * 
 * Description: Multiply rA by rB, store low 32 bits in rD
 */

#ifndef OPCODE_MULLW_H
#define OPCODE_MULLW_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_MULLW_PRIMARY    31
#define OP_MULLW_EXTENDED   235

// Instruction format masks
#define MULLW_OPCD_MASK     0xFC000000  // Bits 0-5
#define MULLW_RT_MASK       0x03E00000  // Bits 6-10
#define MULLW_RA_MASK       0x001F0000  // Bits 11-15
#define MULLW_RB_MASK       0x0000F800  // Bits 16-20
#define MULLW_OE_MASK       0x00000400  // Bit 21
#define MULLW_XO_MASK       0x000007FE  // Bits 22-30
#define MULLW_RC_MASK       0x00000001  // Bit 31

// Instruction format shifts
#define MULLW_RT_SHIFT      21
#define MULLW_RA_SHIFT      16
#define MULLW_RB_SHIFT      11
#define MULLW_OE_SHIFT      10
#define MULLW_XO_SHIFT      1

/**
 * @brief Decoded MULLW instruction
 */
typedef struct {
    uint8_t rD;                 // Destination register (0-31)
    uint8_t rA;                 // Source register A (0-31)
    uint8_t rB;                 // Source register B (0-31)
    bool OE;                    // Overflow enable
    bool Rc;                    // Record bit (update CR0)
} MULLW_Instruction;

/**
 * @brief Decode MULLW instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_mullw(uint32_t instruction, MULLW_Instruction *decoded) {
    uint32_t primary = (instruction & MULLW_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & MULLW_XO_MASK) >> MULLW_XO_SHIFT;
    
    if (primary != OP_MULLW_PRIMARY || extended != OP_MULLW_EXTENDED) {
        return false;
    }
    
    decoded->rD = (instruction & MULLW_RT_MASK) >> MULLW_RT_SHIFT;
    decoded->rA = (instruction & MULLW_RA_MASK) >> MULLW_RA_SHIFT;
    decoded->rB = (instruction & MULLW_RB_MASK) >> MULLW_RB_SHIFT;
    decoded->OE = (instruction & MULLW_OE_MASK) != 0;
    decoded->Rc = (instruction & MULLW_RC_MASK) != 0;
    
    return true;
}

/**
 * @brief Transpile MULLW instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_mullw(const MULLW_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    int written = 0;
    
    // Perform multiplication (low 32 bits)
    written += snprintf(output + written, output_size - written,
                       "r%u = r%u * r%u;",
                       decoded->rD, decoded->rA, decoded->rB);
    
    // Handle overflow if OE=1
    if (decoded->OE) {
        // Check for signed overflow in multiplication
        written += snprintf(output + written, output_size - written,
                           "\n{ int64_t prod = (int64_t)(int32_t)r%u * (int64_t)(int32_t)r%u; "
                           "if (prod != (int32_t)prod) { xer |= 0xC0000000; } "
                           "else { xer &= ~0x80000000; } }",
                           decoded->rA, decoded->rB);
    }
    
    // Update CR0 if Rc=1
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                           "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                           decoded->rD, decoded->rD);
    }
    
    return written;
}

/**
 * @brief Generate assembly-like comment for MULLW instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_mullw(const MULLW_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    return snprintf(output, output_size,
                   "mullw%s%s r%u, r%u, r%u",
                   decoded->OE ? "o" : "",
                   decoded->Rc ? "." : "",
                   decoded->rD, decoded->rA, decoded->rB);
}

#endif // OPCODE_MULLW_H

