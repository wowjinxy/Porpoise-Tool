/**
 * @file cmplwi.h
 * @brief CMPLWI - Compare Logical Word Immediate (pseudo-op for CMPLI)
 * 
 * Opcode: 10
 * Format: D-form
 * Syntax: cmplwi crfD, rA, UIMM
 *         cmplwi rA, UIMM (pseudo-op for crfD=cr0)
 * 
 * Description: Compare rA with unsigned immediate and set crfD
 */

#ifndef OPCODE_CMPLWI_H
#define OPCODE_CMPLWI_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_CMPLWI           10  // CMPLI opcode

#define CMPLWI_OPCD_MASK    0xFC000000
#define CMPLWI_CRFD_MASK    0x03800000
#define CMPLWI_L_MASK       0x00200000
#define CMPLWI_RA_MASK      0x001F0000
#define CMPLWI_UIMM_MASK    0x0000FFFF

#define CMPLWI_CRFD_SHIFT   23
#define CMPLWI_L_SHIFT      21
#define CMPLWI_RA_SHIFT     16

typedef struct {
    uint8_t crfD;
    bool L;
    uint8_t rA;
    uint16_t UIMM;
} CMPLWI_Instruction;

static inline bool decode_cmplwi(uint32_t instruction, CMPLWI_Instruction *decoded) {
    uint32_t primary = (instruction & CMPLWI_OPCD_MASK) >> 26;
    
    if (primary != OP_CMPLWI) {
        return false;
    }
    
    decoded->crfD = (instruction & CMPLWI_CRFD_MASK) >> CMPLWI_CRFD_SHIFT;
    decoded->L = (instruction & CMPLWI_L_MASK) != 0;
    decoded->rA = (instruction & CMPLWI_RA_MASK) >> CMPLWI_RA_SHIFT;
    decoded->UIMM = instruction & CMPLWI_UIMM_MASK;
    
    return true;
}

static inline int transpile_cmplwi(const CMPLWI_Instruction *decoded,
                                   char *output,
                                   size_t output_size) {
    return snprintf(output, output_size,
                   "cr%u = (r%u < 0x%xU ? 0x8 : r%u > 0x%xU ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                   decoded->crfD, decoded->rA, decoded->UIMM,
                   decoded->rA, decoded->UIMM);
}

static inline int comment_cmplwi(const CMPLWI_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    if (decoded->crfD == 0) {
        return snprintf(output, output_size,
                       "cmplwi r%u, 0x%x",
                       decoded->rA, decoded->UIMM);
    }
    
    return snprintf(output, output_size,
                   "cmplwi cr%u, r%u, 0x%x",
                   decoded->crfD, decoded->rA, decoded->UIMM);
}

#endif // OPCODE_CMPLWI_H

