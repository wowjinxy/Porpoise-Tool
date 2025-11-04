/**
 * @file and.h
 * @brief AND - Logical AND
 * 
 * Opcode: 31 (primary) / 28 (extended)
 * Format: X-form
 * Syntax: and rA, rS, rB
 *         and. rA, rS, rB (with Rc=1)
 * 
 * Description: AND rS with rB and store result in rA
 */

#ifndef OPCODE_AND_H
#define OPCODE_AND_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_AND_PRIMARY      31
#define OP_AND_EXTENDED     28

#define AND_OPCD_MASK       0xFC000000
#define AND_RS_MASK         0x03E00000
#define AND_RA_MASK         0x001F0000
#define AND_RB_MASK         0x0000F800
#define AND_XO_MASK         0x000007FE
#define AND_RC_MASK         0x00000001

#define AND_RS_SHIFT        21
#define AND_RA_SHIFT        16
#define AND_RB_SHIFT        11
#define AND_XO_SHIFT        1

typedef struct {
    uint8_t rA;
    uint8_t rS;
    uint8_t rB;
    bool Rc;
} AND_Instruction;

static inline bool decode_and(uint32_t instruction, AND_Instruction *decoded) {
    uint32_t primary = (instruction & AND_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & AND_XO_MASK) >> AND_XO_SHIFT;
    
    if (primary != OP_AND_PRIMARY || extended != OP_AND_EXTENDED) {
        return false;
    }
    
    decoded->rS = (instruction & AND_RS_MASK) >> AND_RS_SHIFT;
    decoded->rA = (instruction & AND_RA_MASK) >> AND_RA_SHIFT;
    decoded->rB = (instruction & AND_RB_MASK) >> AND_RB_SHIFT;
    decoded->Rc = (instruction & AND_RC_MASK) != 0;
    
    return true;
}

static inline int transpile_and(const AND_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    int written = 0;
    
    written += snprintf(output + written, output_size - written,
                       "r%u = r%u & r%u;",
                       decoded->rA, decoded->rS, decoded->rB);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                           "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                           decoded->rA, decoded->rA);
    }
    
    return written;
}

static inline int comment_and(const AND_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    return snprintf(output, output_size,
                   "and%s r%u, r%u, r%u",
                   decoded->Rc ? "." : "",
                   decoded->rA, decoded->rS, decoded->rB);
}

#endif // OPCODE_AND_H

