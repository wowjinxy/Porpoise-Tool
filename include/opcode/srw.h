/**
 * @file srw.h
 * @brief SRW - Shift Right Word
 * 
 * Opcode: 31 (primary) / 536 (extended)
 * Format: X-form
 * Syntax: srw rA, rS, rB
 *         srw. rA, rS, rB (with Rc=1)
 * 
 * Description: Shift rS right by rB[27-31] bits (logical), store in rA
 */

#ifndef OPCODE_SRW_H
#define OPCODE_SRW_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_SRW_PRIMARY      31
#define OP_SRW_EXTENDED     536

// Instruction format masks
#define SRW_OPCD_MASK       0xFC000000
#define SRW_RS_MASK         0x03E00000
#define SRW_RA_MASK         0x001F0000
#define SRW_RB_MASK         0x0000F800
#define SRW_XO_MASK         0x000007FE
#define SRW_RC_MASK         0x00000001

// Instruction format shifts
#define SRW_RS_SHIFT        21
#define SRW_RA_SHIFT        16
#define SRW_RB_SHIFT        11
#define SRW_XO_SHIFT        1

/**
 * @brief Decoded SRW instruction
 */
typedef struct {
    uint8_t rA;
    uint8_t rS;
    uint8_t rB;
    bool Rc;
} SRW_Instruction;

/**
 * @brief Decode SRW instruction
 */
static inline bool decode_srw(uint32_t instruction, SRW_Instruction *decoded) {
    uint32_t primary = (instruction & SRW_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & SRW_XO_MASK) >> SRW_XO_SHIFT;
    
    if (primary != OP_SRW_PRIMARY || extended != OP_SRW_EXTENDED) {
        return false;
    }
    
    decoded->rS = (instruction & SRW_RS_MASK) >> SRW_RS_SHIFT;
    decoded->rA = (instruction & SRW_RA_MASK) >> SRW_RA_SHIFT;
    decoded->rB = (instruction & SRW_RB_MASK) >> SRW_RB_SHIFT;
    decoded->Rc = (instruction & SRW_RC_MASK) != 0;
    
    return true;
}

/**
 * @brief Transpile SRW instruction to C code
 */
static inline int transpile_srw(const SRW_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    int written = 0;
    
    written += snprintf(output + written, output_size - written,
                       "r%u = r%u >> (r%u & 0x1F);",
                       decoded->rA, decoded->rS, decoded->rB);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                           "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                           decoded->rA, decoded->rA);
    }
    
    return written;
}

/**
 * @brief Generate assembly-like comment for SRW instruction
 */
static inline int comment_srw(const SRW_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    return snprintf(output, output_size,
                   "srw%s r%u, r%u, r%u",
                   decoded->Rc ? "." : "",
                   decoded->rA, decoded->rS, decoded->rB);
}

#endif // OPCODE_SRW_H

