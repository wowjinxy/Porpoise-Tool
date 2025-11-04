/**
 * @file slw.h
 * @brief SLW - Shift Left Word
 * 
 * Opcode: 31 (primary) / 24 (extended)
 * Format: X-form
 * Syntax: slw rA, rS, rB
 *         slw. rA, rS, rB (with Rc=1)
 * 
 * Description: Shift rS left by rB[27-31] bits, store in rA
 */

#ifndef OPCODE_SLW_H
#define OPCODE_SLW_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_SLW_PRIMARY      31
#define OP_SLW_EXTENDED     24

// Instruction format masks
#define SLW_OPCD_MASK       0xFC000000  // Bits 0-5
#define SLW_RS_MASK         0x03E00000  // Bits 6-10
#define SLW_RA_MASK         0x001F0000  // Bits 11-15
#define SLW_RB_MASK         0x0000F800  // Bits 16-20
#define SLW_XO_MASK         0x000007FE  // Bits 21-30
#define SLW_RC_MASK         0x00000001  // Bit 31

// Instruction format shifts
#define SLW_RS_SHIFT        21
#define SLW_RA_SHIFT        16
#define SLW_RB_SHIFT        11
#define SLW_XO_SHIFT        1

/**
 * @brief Decoded SLW instruction
 */
typedef struct {
    uint8_t rA;                 // Destination register (0-31)
    uint8_t rS;                 // Source register (0-31)
    uint8_t rB;                 // Shift amount register (0-31)
    bool Rc;                    // Record bit (update CR0)
} SLW_Instruction;

/**
 * @brief Decode SLW instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_slw(uint32_t instruction, SLW_Instruction *decoded) {
    uint32_t primary = (instruction & SLW_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & SLW_XO_MASK) >> SLW_XO_SHIFT;
    
    if (primary != OP_SLW_PRIMARY || extended != OP_SLW_EXTENDED) {
        return false;
    }
    
    decoded->rS = (instruction & SLW_RS_MASK) >> SLW_RS_SHIFT;
    decoded->rA = (instruction & SLW_RA_MASK) >> SLW_RA_SHIFT;
    decoded->rB = (instruction & SLW_RB_MASK) >> SLW_RB_SHIFT;
    decoded->Rc = (instruction & SLW_RC_MASK) != 0;
    
    return true;
}

/**
 * @brief Transpile SLW instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_slw(const SLW_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    int written = 0;
    
    // Shift left (use bottom 5 bits of rB for shift amount)
    written += snprintf(output + written, output_size - written,
                       "r%u = r%u << (r%u & 0x1F);",
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
 * @brief Generate assembly-like comment for SLW instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_slw(const SLW_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    return snprintf(output, output_size,
                   "slw%s r%u, r%u, r%u",
                   decoded->Rc ? "." : "",
                   decoded->rA, decoded->rS, decoded->rB);
}

#endif // OPCODE_SLW_H

