/**
 * @file cmplw.h
 * @brief CMPLW - Compare Logical Word
 * 
 * Opcode: 31 (primary) / 32 (extended)
 * Format: X-form
 * Syntax: cmplw crfD, rA, rB
 *         cmplw rA, rB (pseudo-op for crfD=cr0)
 * 
 * Description: Compare rA with rB as unsigned integers and set crfD
 */

#ifndef OPCODE_CMPLW_H
#define OPCODE_CMPLW_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_CMPLW_PRIMARY    31
#define OP_CMPLW_EXTENDED   32

#define CMPLW_OPCD_MASK     0xFC000000
#define CMPLW_CRFD_MASK     0x03800000
#define CMPLW_L_MASK        0x00200000
#define CMPLW_RA_MASK       0x001F0000
#define CMPLW_RB_MASK       0x0000F800
#define CMPLW_XO_MASK       0x000007FE

#define CMPLW_CRFD_SHIFT    23
#define CMPLW_L_SHIFT       21
#define CMPLW_RA_SHIFT      16
#define CMPLW_RB_SHIFT      11
#define CMPLW_XO_SHIFT      1

typedef struct {
    uint8_t crfD;
    bool L;
    uint8_t rA;
    uint8_t rB;
} CMPLW_Instruction;

static inline bool decode_cmplw(uint32_t instruction, CMPLW_Instruction *decoded) {
    uint32_t primary = (instruction & CMPLW_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & CMPLW_XO_MASK) >> CMPLW_XO_SHIFT;
    
    if (primary != OP_CMPLW_PRIMARY || extended != OP_CMPLW_EXTENDED) {
        return false;
    }
    
    decoded->crfD = (instruction & CMPLW_CRFD_MASK) >> CMPLW_CRFD_SHIFT;
    decoded->L = (instruction & CMPLW_L_MASK) != 0;
    decoded->rA = (instruction & CMPLW_RA_MASK) >> CMPLW_RA_SHIFT;
    decoded->rB = (instruction & CMPLW_RB_MASK) >> CMPLW_RB_SHIFT;
    
    return true;
}

static inline int transpile_cmplw(const CMPLW_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    return snprintf(output, output_size,
                   "cr%u = (r%u < r%u ? 0x8 : r%u > r%u ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                   decoded->crfD, decoded->rA, decoded->rB,
                   decoded->rA, decoded->rB);
}

static inline int comment_cmplw(const CMPLW_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    if (decoded->crfD == 0) {
        return snprintf(output, output_size,
                       "cmplw r%u, r%u",
                       decoded->rA, decoded->rB);
    }
    
    return snprintf(output, output_size,
                   "cmplw cr%u, r%u, r%u",
                   decoded->crfD, decoded->rA, decoded->rB);
}

#endif // OPCODE_CMPLW_H

