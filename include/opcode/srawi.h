/**
 * @file srawi.h
 * @brief SRAWI - Shift Right Algebraic Word Immediate
 * 
 * Opcode: 31 (primary) / 824 (extended)
 * Format: X-form
 * Syntax: srawi rA, rS, SH
 *         srawi. rA, rS, SH (with Rc=1)
 * 
 * Description: Arithmetic right shift rS by SH bits, store in rA, set CA in XER
 */

#ifndef OPCODE_SRAWI_H
#define OPCODE_SRAWI_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_SRAWI_PRIMARY    31
#define OP_SRAWI_EXTENDED   824

#define SRAWI_OPCD_MASK     0xFC000000
#define SRAWI_RS_MASK       0x03E00000
#define SRAWI_RA_MASK       0x001F0000
#define SRAWI_SH_MASK       0x0000F800
#define SRAWI_XO_MASK       0x000007FE
#define SRAWI_RC_MASK       0x00000001

#define SRAWI_RS_SHIFT      21
#define SRAWI_RA_SHIFT      16
#define SRAWI_SH_SHIFT      11
#define SRAWI_XO_SHIFT      1

typedef struct {
    uint8_t rA;
    uint8_t rS;
    uint8_t SH;                 // Shift amount (0-31)
    bool Rc;
} SRAWI_Instruction;

static inline bool decode_srawi(uint32_t instruction, SRAWI_Instruction *decoded) {
    uint32_t primary = (instruction & SRAWI_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & SRAWI_XO_MASK) >> SRAWI_XO_SHIFT;
    
    if (primary != OP_SRAWI_PRIMARY || extended != OP_SRAWI_EXTENDED) {
        return false;
    }
    
    decoded->rS = (instruction & SRAWI_RS_MASK) >> SRAWI_RS_SHIFT;
    decoded->rA = (instruction & SRAWI_RA_MASK) >> SRAWI_RA_SHIFT;
    decoded->SH = (instruction & SRAWI_SH_MASK) >> SRAWI_SH_SHIFT;
    decoded->Rc = (instruction & SRAWI_RC_MASK) != 0;
    
    return true;
}

static inline int transpile_srawi(const SRAWI_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    int written = 0;
    
    // Arithmetic right shift
    written += snprintf(output + written, output_size - written,
                       "r%u = (int32_t)r%u >> %u;",
                       decoded->rA, decoded->rS, decoded->SH);
    
    // Set carry if negative and any 1 bits shifted out
    if (decoded->SH > 0) {
        written += snprintf(output + written, output_size - written,
                           "\nif ((int32_t)r%u < 0 && (r%u & 0x%X)) { "
                           "xer |= 0x20000000; } else { xer &= ~0x20000000; }",
                           decoded->rS, decoded->rS, (1U << decoded->SH) - 1);
    }
    
    // Update CR0 if Rc=1
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                           "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                           decoded->rA, decoded->rA);
    }
    
    return written;
}

static inline int comment_srawi(const SRAWI_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    return snprintf(output, output_size,
                   "srawi%s r%u, r%u, %u",
                   decoded->Rc ? "." : "",
                   decoded->rA, decoded->rS, decoded->SH);
}

#endif // OPCODE_SRAWI_H

