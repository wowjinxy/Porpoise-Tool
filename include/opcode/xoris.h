/**
 * @file xoris.h
 * @brief XORIS - XOR Immediate Shifted
 * 
 * Opcode: 27
 * Format: D-form
 * Syntax: xoris rA, rS, UIMM
 * 
 * Description: XOR rS with (UIMM << 16) and store in rA
 */

#ifndef OPCODE_XORIS_H
#define OPCODE_XORIS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_XORIS            27

typedef struct {
    uint8_t rA;
    uint8_t rS;
    uint16_t UIMM;
} XORIS_Instruction;

static inline bool decode_xoris(uint32_t instruction, XORIS_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    
    if (primary != OP_XORIS) {
        return false;
    }
    
    decoded->rS = (instruction >> 21) & 0x1F;
    decoded->rA = (instruction >> 16) & 0x1F;
    decoded->UIMM = instruction & 0xFFFF;
    
    return true;
}

static inline int transpile_xoris(const XORIS_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    return snprintf(output, output_size,
                   "r%u = r%u ^ (0x%x << 16);",
                   decoded->rA, decoded->rS, decoded->UIMM);
}

static inline int comment_xoris(const XORIS_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    return snprintf(output, output_size,
                   "xoris r%u, r%u, 0x%x",
                   decoded->rA, decoded->rS, decoded->UIMM);
}

#endif // OPCODE_XORIS_H

