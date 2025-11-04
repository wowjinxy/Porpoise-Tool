/**
 * @file fadds.h
 * @brief FADDS - Floating-Point Add Single-Precision
 * 
 * Opcode: 59 (primary) / 21 (extended)
 * Format: A-form
 * Syntax: fadds frD, frA, frB
 *         fadds. frD, frA, frB (with Rc=1)
 * 
 * Description: Add two single-precision floating-point values
 */

#ifndef OPCODE_FADDS_H
#define OPCODE_FADDS_H

#include <stdint.h>
#include <stdbool.h>

#define OP_FADDS_PRIMARY    59
#define OP_FADDS_EXTENDED   21

typedef struct {
    uint8_t frD;
    uint8_t frA;
    uint8_t frB;
    bool Rc;
} FADDS_Instruction;

static inline bool decode_fadds(uint32_t instruction, FADDS_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    uint32_t extended = (instruction >> 1) & 0x1F;
    
    if (primary != OP_FADDS_PRIMARY || extended != OP_FADDS_EXTENDED) {
        return false;
    }
    
    decoded->frD = (instruction >> 21) & 0x1F;
    decoded->frA = (instruction >> 16) & 0x1F;
    decoded->frB = (instruction >> 11) & 0x1F;
    decoded->Rc = instruction & 1;
    
    return true;
}

static inline int transpile_fadds(const FADDS_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    int written = snprintf(output, output_size,
                          "f%u = (float)(f%u + f%u);",
                          decoded->frD, decoded->frA, decoded->frB);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    
    return written;
}

static inline int comment_fadds(const FADDS_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    return snprintf(output, output_size, "fadds%s f%u, f%u, f%u",
                   decoded->Rc ? "." : "",
                   decoded->frD, decoded->frA, decoded->frB);
}

#endif // OPCODE_FADDS_H

