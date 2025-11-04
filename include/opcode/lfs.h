/**
 * @file lfs.h
 * @brief LFS - Load Floating-Point Single
 * 
 * Opcode: 48
 * Format: D-form
 * Syntax: lfs frD, d(rA)
 * 
 * Description: Load single-precision float from memory, convert to double, store in frD
 */

#ifndef OPCODE_LFS_H
#define OPCODE_LFS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LFS              48

#define LFS_OPCD_MASK       0xFC000000
#define LFS_FRT_MASK        0x03E00000
#define LFS_RA_MASK         0x001F0000
#define LFS_D_MASK          0x0000FFFF

#define LFS_FRT_SHIFT       21
#define LFS_RA_SHIFT        16

typedef struct {
    uint8_t frD;
    uint8_t rA;
    int16_t d;
} LFS_Instruction;

static inline bool decode_lfs(uint32_t instruction, LFS_Instruction *decoded) {
    uint32_t primary = (instruction & LFS_OPCD_MASK) >> 26;
    
    if (primary != OP_LFS) {
        return false;
    }
    
    decoded->frD = (instruction & LFS_FRT_MASK) >> LFS_FRT_SHIFT;
    decoded->rA = (instruction & LFS_RA_MASK) >> LFS_RA_SHIFT;
    decoded->d = (int16_t)(instruction & LFS_D_MASK);
    
    return true;
}

static inline int transpile_lfs(const LFS_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    if (decoded->rA == 0) {
        return snprintf(output, output_size,
                       "f%u = (double)*(float*)(mem + 0x%x);",
                       decoded->frD, (uint32_t)decoded->d);
    } else {
        if (decoded->d == 0) {
            return snprintf(output, output_size,
                           "f%u = (double)*(float*)(mem + r%u);",
                           decoded->frD, decoded->rA);
        } else if (decoded->d > 0) {
            return snprintf(output, output_size,
                           "f%u = (double)*(float*)(mem + r%u + 0x%x);",
                           decoded->frD, decoded->rA, (uint16_t)decoded->d);
        } else {
            return snprintf(output, output_size,
                           "f%u = (double)*(float*)(mem + r%u - 0x%x);",
                           decoded->frD, decoded->rA, (uint16_t)(-decoded->d));
        }
    }
}

static inline int comment_lfs(const LFS_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    if (decoded->d >= 0) {
        return snprintf(output, output_size,
                       "lfs f%u, 0x%x(r%u)",
                       decoded->frD, (uint16_t)decoded->d, decoded->rA);
    } else {
        return snprintf(output, output_size,
                       "lfs f%u, -0x%x(r%u)",
                       decoded->frD, (uint16_t)(-decoded->d), decoded->rA);
    }
}

#endif // OPCODE_LFS_H

