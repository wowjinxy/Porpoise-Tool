/**
 * @file add.h
 * @brief ADD - Add (Integer)
 * 
 * Opcode: 31 (primary) / 266 (extended)
 * Format: XO-form
 * Syntax: add rD, rA, rB
 *         add. rD, rA, rB (with Rc=1)
 *         addo rD, rA, rB (with OE=1)
 *         addo. rD, rA, rB (with OE=1, Rc=1)
 */

#ifndef OPCODE_ADD_H
#define OPCODE_ADD_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_ADD_PRIMARY      31
#define OP_ADD_EXTENDED     266

// Instruction format masks
#define ADD_OPCD_MASK       0xFC000000  // Bits 0-5
#define ADD_RT_MASK         0x03E00000  // Bits 6-10
#define ADD_RA_MASK         0x001F0000  // Bits 11-15
#define ADD_RB_MASK         0x0000F800  // Bits 16-20
#define ADD_OE_MASK         0x00000400  // Bit 21
#define ADD_XO_MASK         0x000007FE  // Bits 22-30
#define ADD_RC_MASK         0x00000001  // Bit 31

// Instruction format shifts
#define ADD_RT_SHIFT        21
#define ADD_RA_SHIFT        16
#define ADD_RB_SHIFT        11
#define ADD_OE_SHIFT        10
#define ADD_XO_SHIFT        1
#define ADD_RC_SHIFT        0

/**
 * @brief Decoded ADD instruction
 */
typedef struct {
    uint8_t rD;                 // Destination register (0-31)
    uint8_t rA;                 // Source register A (0-31)
    uint8_t rB;                 // Source register B (0-31)
    bool OE;                    // Overflow enable
    bool Rc;                    // Record bit (update CR0)
} ADD_Instruction;

/**
 * @brief Decode ADD instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_add(uint32_t instruction, ADD_Instruction *decoded) {
    uint32_t primary = (instruction & ADD_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & ADD_XO_MASK) >> ADD_XO_SHIFT;
    
    if (primary != OP_ADD_PRIMARY || extended != OP_ADD_EXTENDED) {
        return false;
    }
    
    decoded->rD = (instruction & ADD_RT_MASK) >> ADD_RT_SHIFT;
    decoded->rA = (instruction & ADD_RA_MASK) >> ADD_RA_SHIFT;
    decoded->rB = (instruction & ADD_RB_MASK) >> ADD_RB_SHIFT;
    decoded->OE = (instruction & ADD_OE_MASK) != 0;
    decoded->Rc = (instruction & ADD_RC_MASK) != 0;
    
    return true;
}

/**
 * @brief Transpile ADD instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written (excluding null terminator)
 */
static inline int transpile_add(const ADD_Instruction *decoded, 
                                char *output, 
                                size_t output_size) {
    int written = 0;
    
    // Basic add operation: rD = rA + rB
    written += snprintf(output + written, output_size - written,
                       "r%u = r%u + r%u;",
                       decoded->rD, decoded->rA, decoded->rB);
    
    // Handle overflow checking if OE=1
    if (decoded->OE) {
        written += snprintf(output + written, output_size - written,
                           "\nif (((int32_t)r%u ^ (int32_t)r%u) >= 0 && "
                           "((int32_t)r%u ^ (int32_t)r%u) < 0) { "
                           "xer |= 0xC0000000; } else { xer &= ~0x80000000; }",
                           decoded->rA, decoded->rB,
                           decoded->rA, decoded->rD);
    }
    
    // Handle CR0 update if Rc=1
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                           "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                           decoded->rD, decoded->rD);
    }
    
    return written;
}

/**
 * @brief Generate assembly-like comment for ADD instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_add(const ADD_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    return snprintf(output, output_size,
                   "add%s%s r%u, r%u, r%u",
                   decoded->OE ? "o" : "",
                   decoded->Rc ? "." : "",
                   decoded->rD, decoded->rA, decoded->rB);
}

#endif // OPCODE_ADD_H

