/**
 * @file fmuls.h
 * @brief FMULS - Floating-Point Multiply Single-Precision
 * 
 * Opcode: 59 / 25
 * Format: A-form
 * Syntax: fmuls frD, frA, frC
 */

#ifndef OPCODE_FMULS_H
#define OPCODE_FMULS_H

#include <stdint.h>
#include <stdbool.h>

#define OP_FMULS_PRIMARY    59
#define OP_FMULS_EXTENDED   25

typedef struct {
    uint8_t frD;
    uint8_t frA;
    uint8_t frC;
    bool Rc;
} FMULS_Instruction;

static inline bool decode_fmuls(uint32_t instruction, FMULS_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    uint32_t extended = (instruction >> 1) & 0x1F;
    
    if (primary != OP_FMULS_PRIMARY || extended != OP_FMULS_EXTENDED) {
        return false;
    }
    
    decoded->frD = (instruction >> 21) & 0x1F;
    decoded->frA = (instruction >> 16) & 0x1F;
    decoded->frC = (instruction >> 6) & 0x1F;
    decoded->Rc = instruction & 1;
    
    return true;
}

static inline int transpile_fmuls(const FMULS_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    int written = snprintf(output, output_size,
                          "f%u = (float)(f%u * f%u);",
                          decoded->frD, decoded->frA, decoded->frC);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    
    return written;
}

static inline int comment_fmuls(const FMULS_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    return snprintf(output, output_size, "fmuls%s f%u, f%u, f%u",
                   decoded->Rc ? "." : "",
                   decoded->frD, decoded->frA, decoded->frC);
}

#endif // OPCODE_FMULS_H

