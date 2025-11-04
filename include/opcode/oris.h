/**
 * @file oris.h
 * @brief ORIS - OR Immediate Shifted
 * 
 * Opcode: 25
 * Format: D-form
 * Syntax: oris rA, rS, UIMM
 * 
 * Description: OR rS with (UIMM << 16) and store in rA
 */

#ifndef OPCODE_ORIS_H
#define OPCODE_ORIS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_ORIS             25

typedef struct {
    uint8_t rA;
    uint8_t rS;
    uint16_t UIMM;
} ORIS_Instruction;

static inline bool decode_oris(uint32_t instruction, ORIS_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    
    if (primary != OP_ORIS) {
        return false;
    }
    
    decoded->rS = (instruction >> 21) & 0x1F;
    decoded->rA = (instruction >> 16) & 0x1F;
    decoded->UIMM = instruction & 0xFFFF;
    
    return true;
}

static inline int transpile_oris(const ORIS_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    return snprintf(output, output_size,
                   "r%u = r%u | (0x%x << 16);",
                   decoded->rA, decoded->rS, decoded->UIMM);
}

static inline int comment_oris(const ORIS_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    return snprintf(output, output_size,
                   "oris r%u, r%u, 0x%x",
                   decoded->rA, decoded->rS, decoded->UIMM);
}

#endif // OPCODE_ORIS_H

