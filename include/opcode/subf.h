/**
 * @file subf.h
 * @brief SUBF - Subtract From
 * 
 * Opcode: 31 (primary) / 40 (extended)
 * Format: XO-form
 * Syntax: subf rD, rA, rB
 *         subf. rD, rA, rB (with Rc=1)
 *         subfo rD, rA, rB (with OE=1)
 *         subfo. rD, rA, rB (with OE=1, Rc=1)
 *         sub rD, rA, rB (pseudo-op, same as subf)
 * 
 * Description: rD = rB - rA (note: operands appear reversed in assembly)
 */

#ifndef OPCODE_SUBF_H
#define OPCODE_SUBF_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_SUBF_PRIMARY     31
#define OP_SUBF_EXTENDED    40

// Instruction format masks
#define SUBF_OPCD_MASK      0xFC000000  // Bits 0-5
#define SUBF_RT_MASK        0x03E00000  // Bits 6-10
#define SUBF_RA_MASK        0x001F0000  // Bits 11-15
#define SUBF_RB_MASK        0x0000F800  // Bits 16-20
#define SUBF_OE_MASK        0x00000400  // Bit 21
#define SUBF_XO_MASK        0x000007FE  // Bits 22-30
#define SUBF_RC_MASK        0x00000001  // Bit 31

// Instruction format shifts
#define SUBF_RT_SHIFT       21
#define SUBF_RA_SHIFT       16
#define SUBF_RB_SHIFT       11
#define SUBF_OE_SHIFT       10
#define SUBF_XO_SHIFT       1

/**
 * @brief Decoded SUBF instruction
 */
typedef struct {
    uint8_t rD;                 // Destination register (0-31)
    uint8_t rA;                 // Source register A (subtracted from rB)
    uint8_t rB;                 // Source register B (minuend)
    bool OE;                    // Overflow enable
    bool Rc;                    // Record bit (update CR0)
} SUBF_Instruction;

/**
 * @brief Decode SUBF instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_subf(uint32_t instruction, SUBF_Instruction *decoded) {
    uint32_t primary = (instruction & SUBF_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & SUBF_XO_MASK) >> SUBF_XO_SHIFT;
    
    if (primary != OP_SUBF_PRIMARY || extended != OP_SUBF_EXTENDED) {
        return false;
    }
    
    decoded->rD = (instruction & SUBF_RT_MASK) >> SUBF_RT_SHIFT;
    decoded->rA = (instruction & SUBF_RA_MASK) >> SUBF_RA_SHIFT;
    decoded->rB = (instruction & SUBF_RB_MASK) >> SUBF_RB_SHIFT;
    decoded->OE = (instruction & SUBF_OE_MASK) != 0;
    decoded->Rc = (instruction & SUBF_RC_MASK) != 0;
    
    return true;
}

/**
 * @brief Transpile SUBF instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_subf(const SUBF_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    int written = 0;
    
    // Perform subtraction: rD = rB - rA
    written += snprintf(output + written, output_size - written,
                       "r%u = r%u - r%u;",
                       decoded->rD, decoded->rB, decoded->rA);
    
    // Handle overflow if OE=1
    if (decoded->OE) {
        written += snprintf(output + written, output_size - written,
                           "\nif (((int32_t)r%u ^ (int32_t)r%u) < 0 && "
                           "((int32_t)r%u ^ (int32_t)r%u) < 0) { "
                           "xer |= 0xC0000000; } else { xer &= ~0x80000000; }",
                           decoded->rB, decoded->rA,
                           decoded->rB, decoded->rD);
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
 * @brief Generate assembly-like comment for SUBF instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_subf(const SUBF_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    return snprintf(output, output_size,
                   "subf%s%s r%u, r%u, r%u",
                   decoded->OE ? "o" : "",
                   decoded->Rc ? "." : "",
                   decoded->rD, decoded->rA, decoded->rB);
}

#endif // OPCODE_SUBF_H

