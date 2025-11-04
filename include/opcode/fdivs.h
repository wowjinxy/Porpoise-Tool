/**
 * @file fdivs.h
 * @brief FDIVS - Floating-Point Divide Single-Precision
 * 
 * Opcode: 59 / 18
 * Format: A-form
 * Syntax: fdivs frD, frA, frB
 */

#ifndef OPCODE_FDIVS_H
#define OPCODE_FDIVS_H

#include <stdint.h>
#include <stdbool.h>

#define OP_FDIVS_PRIMARY    59
#define OP_FDIVS_EXTENDED   18

typedef struct {
    uint8_t frD;
    uint8_t frA;
    uint8_t frB;
    bool Rc;
} FDIVS_Instruction;

static inline bool decode_fdivs(uint32_t instruction, FDIVS_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    uint32_t extended = (instruction >> 1) & 0x1F;
    
    if (primary != OP_FDIVS_PRIMARY || extended != OP_FDIVS_EXTENDED) {
        return false;
    }
    
    decoded->frD = (instruction >> 21) & 0x1F;
    decoded->frA = (instruction >> 16) & 0x1F;
    decoded->frB = (instruction >> 11) & 0x1F;
    decoded->Rc = instruction & 1;
    
    return true;
}

static inline int transpile_fdivs(const FDIVS_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    int written = snprintf(output, output_size,
                          "f%u = (float)(f%u / f%u);",
                          decoded->frD, decoded->frA, decoded->frB);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    
    return written;
}

static inline int comment_fdivs(const FDIVS_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    return snprintf(output, output_size, "fdivs%s f%u, f%u, f%u",
                   decoded->Rc ? "." : "",
                   decoded->frD, decoded->frA, decoded->frB);
}

#endif // OPCODE_FDIVS_H

