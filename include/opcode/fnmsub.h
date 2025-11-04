/**
 * @file fnmsub.h
 * @brief FNMSUB - Floating-Point Negative Multiply-Subtract (Double-Precision)
 * 
 * Opcode: 63 / 30
 * Format: A-form
 * Syntax: fnmsub frD, frA, frC, frB
 * 
 * Description: frD = -((frA * frC) - frB)
 */

#ifndef OPCODE_FNMSUB_H
#define OPCODE_FNMSUB_H

#include <stdint.h>
#include <stdbool.h>

#define OP_FNMSUB_PRIMARY    63
#define OP_FNMSUB_EXTENDED   30

typedef struct {
    uint8_t frD;
    uint8_t frA;
    uint8_t frB;
    uint8_t frC;
    bool Rc;
} FNMSUB_Instruction;

static inline bool decode_fnmsub(uint32_t instruction, FNMSUB_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    uint32_t extended = (instruction >> 1) & 0x1F;
    
    if (primary != OP_FNMSUB_PRIMARY || extended != OP_FNMSUB_EXTENDED) {
        return false;
    }
    
    decoded->frD = (instruction >> 21) & 0x1F;
    decoded->frA = (instruction >> 16) & 0x1F;
    decoded->frB = (instruction >> 11) & 0x1F;
    decoded->frC = (instruction >> 6) & 0x1F;
    decoded->Rc = instruction & 1;
    
    return true;
}

static inline int transpile_fnmsub(const FNMSUB_Instruction *decoded,
                                   char *output,
                                   size_t output_size) {
    int written = snprintf(output, output_size,
                          "f%u = -((f%u * f%u) - f%u);",
                          decoded->frD, decoded->frA, decoded->frC, decoded->frB);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    
    return written;
}

static inline int comment_fnmsub(const FNMSUB_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    return snprintf(output, output_size, "fnmsub%s f%u, f%u, f%u, f%u",
                   decoded->Rc ? "." : "",
                   decoded->frD, decoded->frA, decoded->frC, decoded->frB);
}

#endif // OPCODE_FNMSUB_H

