/**
 * @file fabs.h
 * @brief FABS - Floating-Point Absolute Value
 * 
 * Opcode: 63 / 264
 * Format: X-form
 * Syntax: fabs frD, frB
 *         fabs. frD, frB (with Rc=1)
 * 
 * Description: Set frD to absolute value of frB
 */

#ifndef OPCODE_FABS_H
#define OPCODE_FABS_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define OP_FABS_PRIMARY    63
#define OP_FABS_EXTENDED   264

typedef struct {
    uint8_t frD;
    uint8_t frB;
    bool Rc;
} FABS_Instruction;

static inline bool decode_fabs(uint32_t instruction, FABS_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    uint32_t extended = (instruction >> 1) & 0x3FF;
    
    if (primary != OP_FABS_PRIMARY || extended != OP_FABS_EXTENDED) {
        return false;
    }
    
    decoded->frD = (instruction >> 21) & 0x1F;
    decoded->frB = (instruction >> 11) & 0x1F;
    decoded->Rc = instruction & 1;
    
    return true;
}

static inline int transpile_fabs(const FABS_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    int written = snprintf(output, output_size,
                          "f%u = fabs(f%u);",
                          decoded->frD, decoded->frB);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    
    return written;
}

static inline int comment_fabs(const FABS_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    return snprintf(output, output_size, "fabs%s f%u, f%u",
                   decoded->Rc ? "." : "",
                   decoded->frD, decoded->frB);
}

#endif // OPCODE_FABS_H

