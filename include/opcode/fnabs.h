/**
 * @file fnabs.h
 * @brief FNABS - Floating-Point Negative Absolute Value
 * 
 * Opcode: 63 / 136
 * Format: X-form
 * Syntax: fnabs frD, frB
 *         fnabs. frD, frB (with Rc=1)
 * 
 * Description: frD = -|frB|
 */

#ifndef OPCODE_FNABS_H
#define OPCODE_FNABS_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define OP_FNABS_PRIMARY    63
#define OP_FNABS_EXTENDED   136

typedef struct {
    uint8_t frD;
    uint8_t frB;
    bool Rc;
} FNABS_Instruction;

static inline bool decode_fnabs(uint32_t instruction, FNABS_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    uint32_t extended = (instruction >> 1) & 0x3FF;
    
    if (primary != OP_FNABS_PRIMARY || extended != OP_FNABS_EXTENDED) {
        return false;
    }
    
    decoded->frD = (instruction >> 21) & 0x1F;
    decoded->frB = (instruction >> 11) & 0x1F;
    decoded->Rc = instruction & 1;
    
    return true;
}

static inline int transpile_fnabs(const FNABS_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    int written = snprintf(output, output_size,
                          "f%u = -fabs(f%u);",
                          decoded->frD, decoded->frB);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    
    return written;
}

static inline int comment_fnabs(const FNABS_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    return snprintf(output, output_size, "fnabs%s f%u, f%u",
                   decoded->Rc ? "." : "",
                   decoded->frD, decoded->frB);
}

#endif // OPCODE_FNABS_H

