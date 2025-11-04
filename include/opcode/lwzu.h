/**
 * @file lwzu.h
 * @brief LWZU - Load Word and Zero with Update
 * 
 * Opcode: 33
 * Format: D-form
 * Syntax: lwzu rD, d(rA)
 * 
 * Description: Load word from memory address rA + d, store in rD, then update rA
 * Note: rA must not be 0 and rA must not equal rD
 */

#ifndef OPCODE_LWZU_H
#define OPCODE_LWZU_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LWZU             33

#define LWZU_OPCD_MASK      0xFC000000
#define LWZU_RT_MASK        0x03E00000
#define LWZU_RA_MASK        0x001F0000
#define LWZU_D_MASK         0x0000FFFF

#define LWZU_RT_SHIFT       21
#define LWZU_RA_SHIFT       16

typedef struct {
    uint8_t rD;
    uint8_t rA;
    int16_t d;
} LWZU_Instruction;

static inline bool decode_lwzu(uint32_t instruction, LWZU_Instruction *decoded) {
    uint32_t primary = (instruction & LWZU_OPCD_MASK) >> 26;
    
    if (primary != OP_LWZU) {
        return false;
    }
    
    decoded->rD = (instruction & LWZU_RT_MASK) >> LWZU_RT_SHIFT;
    decoded->rA = (instruction & LWZU_RA_MASK) >> LWZU_RA_SHIFT;
    decoded->d = (int16_t)(instruction & LWZU_D_MASK);
    
    return true;
}

static inline int transpile_lwzu(const LWZU_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    // Load and update rA
    if (decoded->d >= 0) {
        return snprintf(output, output_size,
                       "r%u = r%u + 0x%x; r%u = *(uint32_t*)(mem + r%u);",
                       decoded->rA, decoded->rA, (uint16_t)decoded->d,
                       decoded->rD, decoded->rA);
    } else {
        return snprintf(output, output_size,
                       "r%u = r%u - 0x%x; r%u = *(uint32_t*)(mem + r%u);",
                       decoded->rA, decoded->rA, (uint16_t)(-decoded->d),
                       decoded->rD, decoded->rA);
    }
}

static inline int comment_lwzu(const LWZU_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    if (decoded->d >= 0) {
        return snprintf(output, output_size,
                       "lwzu r%u, 0x%x(r%u)",
                       decoded->rD, (uint16_t)decoded->d, decoded->rA);
    } else {
        return snprintf(output, output_size,
                       "lwzu r%u, -0x%x(r%u)",
                       decoded->rD, (uint16_t)(-decoded->d), decoded->rA);
    }
}

#endif // OPCODE_LWZU_H

