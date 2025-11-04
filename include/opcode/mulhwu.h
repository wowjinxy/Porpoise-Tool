/**
 * @file mulhwu.h
 * @brief MULHWU - Multiply High Word Unsigned
 * 
 * Opcode: 31 (primary) / 11 (extended)
 * Format: XO-form
 * Syntax: mulhwu rD, rA, rB
 *         mulhwu. rD, rA, rB (with Rc=1)
 * 
 * Description: Multiply rA by rB (unsigned), store high 32 bits in rD
 */

#ifndef OPCODE_MULHWU_H
#define OPCODE_MULHWU_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_MULHWU_PRIMARY   31
#define OP_MULHWU_EXTENDED  11

#define MULHWU_OPCD_MASK    0xFC000000
#define MULHWU_RT_MASK      0x03E00000
#define MULHWU_RA_MASK      0x001F0000
#define MULHWU_RB_MASK      0x0000F800
#define MULHWU_XO_MASK      0x000007FE
#define MULHWU_RC_MASK      0x00000001

#define MULHWU_RT_SHIFT     21
#define MULHWU_RA_SHIFT     16
#define MULHWU_RB_SHIFT     11
#define MULHWU_XO_SHIFT     1

typedef struct {
    uint8_t rD;
    uint8_t rA;
    uint8_t rB;
    bool Rc;
} MULHWU_Instruction;

static inline bool decode_mulhwu(uint32_t instruction, MULHWU_Instruction *decoded) {
    uint32_t primary = (instruction & MULHWU_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & MULHWU_XO_MASK) >> MULHWU_XO_SHIFT;
    
    if (primary != OP_MULHWU_PRIMARY || extended != OP_MULHWU_EXTENDED) {
        return false;
    }
    
    decoded->rD = (instruction & MULHWU_RT_MASK) >> MULHWU_RT_SHIFT;
    decoded->rA = (instruction & MULHWU_RA_MASK) >> MULHWU_RA_SHIFT;
    decoded->rB = (instruction & MULHWU_RB_MASK) >> MULHWU_RB_SHIFT;
    decoded->Rc = (instruction & MULHWU_RC_MASK) != 0;
    
    return true;
}

static inline int transpile_mulhwu(const MULHWU_Instruction *decoded,
                                   char *output,
                                   size_t output_size) {
    int written = 0;
    
    // Multiply and extract high 32 bits (unsigned)
    written += snprintf(output + written, output_size - written,
                       "r%u = (uint32_t)(((uint64_t)r%u * (uint64_t)r%u) >> 32);",
                       decoded->rD, decoded->rA, decoded->rB);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                           "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                           decoded->rD, decoded->rD);
    }
    
    return written;
}

static inline int comment_mulhwu(const MULHWU_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    return snprintf(output, output_size,
                   "mulhwu%s r%u, r%u, r%u",
                   decoded->Rc ? "." : "",
                   decoded->rD, decoded->rA, decoded->rB);
}

#endif // OPCODE_MULHWU_H

