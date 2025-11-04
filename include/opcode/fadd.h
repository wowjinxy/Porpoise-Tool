/**
 * @file fadd.h
 * @brief FADD - Floating-Point Add (Double-Precision)
 * 
 * Opcode: 63 (primary) / 21 (extended)
 * Format: A-form
 * Syntax: fadd frD, frA, frB
 *         fadd. frD, frA, frB (with Rc=1)
 * 
 * Description: Add two double-precision floating-point values
 */

#ifndef OPCODE_FADD_H
#define OPCODE_FADD_H

#include <stdint.h>
#include <stdbool.h>

// Opcode encoding
#define OP_FADD_PRIMARY     63
#define OP_FADD_EXTENDED    21

// Instruction format masks
#define FADD_OPCD_MASK      0xFC000000  // Bits 0-5
#define FADD_FRT_MASK       0x03E00000  // Bits 6-10
#define FADD_FRA_MASK       0x001F0000  // Bits 11-15
#define FADD_FRB_MASK       0x0000F800  // Bits 16-20
#define FADD_XO_MASK        0x000007C0  // Bits 21-25 (extended opcode)
#define FADD_RC_MASK        0x00000001  // Bit 31

// Instruction format shifts
#define FADD_FRT_SHIFT      21
#define FADD_FRA_SHIFT      16
#define FADD_FRB_SHIFT      11
#define FADD_XO_SHIFT       6
#define FADD_RC_SHIFT       0

/**
 * @brief Decoded FADD instruction
 */
typedef struct {
    uint8_t frD;                // Destination FP register (0-31)
    uint8_t frA;                // Source FP register A (0-31)
    uint8_t frB;                // Source FP register B (0-31)
    bool Rc;                    // Record bit (update CR1)
} FADD_Instruction;

/**
 * @brief Decode FADD instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_fadd(uint32_t instruction, FADD_Instruction *decoded) {
    uint32_t primary = (instruction & FADD_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & FADD_XO_MASK) >> FADD_XO_SHIFT;
    
    if (primary != OP_FADD_PRIMARY || extended != OP_FADD_EXTENDED) {
        return false;
    }
    
    decoded->frD = (instruction & FADD_FRT_MASK) >> FADD_FRT_SHIFT;
    decoded->frA = (instruction & FADD_FRA_MASK) >> FADD_FRA_SHIFT;
    decoded->frB = (instruction & FADD_FRB_MASK) >> FADD_FRB_SHIFT;
    decoded->Rc = (instruction & FADD_RC_MASK) != 0;
    
    return true;
}

/**
 * @brief Transpile FADD instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_fadd(const FADD_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    int written = 0;
    
    // Perform double-precision addition
    written += snprintf(output + written, output_size - written,
                       "f%u = f%u + f%u;",
                       decoded->frD, decoded->frA, decoded->frB);
    
    // Update CR1 if Rc=1
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    
    return written;
}

/**
 * @brief Generate assembly-like comment for FADD instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_fadd(const FADD_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    return snprintf(output, output_size,
                   "fadd%s f%u, f%u, f%u",
                   decoded->Rc ? "." : "",
                   decoded->frD, decoded->frA, decoded->frB);
}

#endif // OPCODE_FADD_H

