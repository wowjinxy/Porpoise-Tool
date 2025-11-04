/**
 * @file stwu.h
 * @brief STWU - Store Word with Update
 * 
 * Opcode: 37
 * Format: D-form
 * Syntax: stwu rS, d(rA)
 * 
 * Description: Store word from rS to memory address rA + d, then update rA
 * Note: rA must not be 0
 */

#ifndef OPCODE_STWU_H
#define OPCODE_STWU_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_STWU             37

#define STWU_OPCD_MASK      0xFC000000
#define STWU_RS_MASK        0x03E00000
#define STWU_RA_MASK        0x001F0000
#define STWU_D_MASK         0x0000FFFF

#define STWU_RS_SHIFT       21
#define STWU_RA_SHIFT       16

typedef struct {
    uint8_t rS;
    uint8_t rA;
    int16_t d;
} STWU_Instruction;

static inline bool decode_stwu(uint32_t instruction, STWU_Instruction *decoded) {
    uint32_t primary = (instruction & STWU_OPCD_MASK) >> 26;
    
    if (primary != OP_STWU) {
        return false;
    }
    
    decoded->rS = (instruction & STWU_RS_MASK) >> STWU_RS_SHIFT;
    decoded->rA = (instruction & STWU_RA_MASK) >> STWU_RA_SHIFT;
    decoded->d = (int16_t)(instruction & STWU_D_MASK);
    
    return true;
}

static inline int transpile_stwu(const STWU_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    // Store and update rA
    if (decoded->d >= 0) {
        return snprintf(output, output_size,
                       "r%u = r%u + 0x%x; *(uint32_t*)(mem + r%u) = r%u;",
                       decoded->rA, decoded->rA, (uint16_t)decoded->d,
                       decoded->rA, decoded->rS);
    } else {
        return snprintf(output, output_size,
                       "r%u = r%u - 0x%x; *(uint32_t*)(mem + r%u) = r%u;",
                       decoded->rA, decoded->rA, (uint16_t)(-decoded->d),
                       decoded->rA, decoded->rS);
    }
}

static inline int comment_stwu(const STWU_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    if (decoded->d >= 0) {
        return snprintf(output, output_size,
                       "stwu r%u, 0x%x(r%u)",
                       decoded->rS, (uint16_t)decoded->d, decoded->rA);
    } else {
        return snprintf(output, output_size,
                       "stwu r%u, -0x%x(r%u)",
                       decoded->rS, (uint16_t)(-decoded->d), decoded->rA);
    }
}

#endif // OPCODE_STWU_H

