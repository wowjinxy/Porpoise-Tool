/**
 * @file fmadds.h
 * @brief FMADDS - Floating-Point Multiply-Add Single-Precision
 * 
 * Opcode: 59 / 29
 * Format: A-form
 * Syntax: fmadds frD, frA, frC, frB
 * 
 * Description: frD = (frA * frC) + frB (single precision)
 */

#ifndef OPCODE_FMADDS_H
#define OPCODE_FMADDS_H

#include <stdint.h>
#include <stdbool.h>

#define OP_FMADDS_PRIMARY    59
#define OP_FMADDS_EXTENDED   29

typedef struct {
    uint8_t frD;
    uint8_t frA;
    uint8_t frB;
    uint8_t frC;
    bool Rc;
} FMADDS_Instruction;

static inline bool decode_fmadds(uint32_t instruction, FMADDS_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    uint32_t extended = (instruction >> 1) & 0x1F;
    
    if (primary != OP_FMADDS_PRIMARY || extended != OP_FMADDS_EXTENDED) {
        return false;
    }
    
    decoded->frD = (instruction >> 21) & 0x1F;
    decoded->frA = (instruction >> 16) & 0x1F;
    decoded->frB = (instruction >> 11) & 0x1F;
    decoded->frC = (instruction >> 6) & 0x1F;
    decoded->Rc = instruction & 1;
    
    return true;
}

static inline int transpile_fmadds(const FMADDS_Instruction *decoded,
                                   char *output,
                                   size_t output_size) {
    int written = snprintf(output, output_size,
                          "f%u = (float)((f%u * f%u) + f%u);",
                          decoded->frD, decoded->frA, decoded->frC, decoded->frB);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    
    return written;
}

static inline int comment_fmadds(const FMADDS_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    return snprintf(output, output_size, "fmadds%s f%u, f%u, f%u, f%u",
                   decoded->Rc ? "." : "",
                   decoded->frD, decoded->frA, decoded->frC, decoded->frB);
}

#endif // OPCODE_FMADDS_H

