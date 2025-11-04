/**
 * @file frsqrte.h
 * @brief FRSQRTE - Floating-Point Reciprocal Square Root Estimate
 * 
 * Opcode: 63 / 26
 * Format: A-form
 * Syntax: frsqrte frD, frB
 * 
 * Description: frD = 1.0 / sqrt(frB) (approximate)
 */

#ifndef OPCODE_FRSQRTE_H
#define OPCODE_FRSQRTE_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define OP_FRSQRTE_PRIMARY    63
#define OP_FRSQRTE_EXTENDED   26

typedef struct {
    uint8_t frD;
    uint8_t frB;
    bool Rc;
} FRSQRTE_Instruction;

static inline bool decode_frsqrte(uint32_t instruction, FRSQRTE_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    uint32_t extended = (instruction >> 1) & 0x1F;
    
    if (primary != OP_FRSQRTE_PRIMARY || extended != OP_FRSQRTE_EXTENDED) {
        return false;
    }
    
    decoded->frD = (instruction >> 21) & 0x1F;
    decoded->frB = (instruction >> 11) & 0x1F;
    decoded->Rc = instruction & 1;
    
    return true;
}

static inline int transpile_frsqrte(const FRSQRTE_Instruction *decoded,
                                    char *output,
                                    size_t output_size) {
    int written = snprintf(output, output_size,
                          "f%u = 1.0 / sqrt(f%u);",
                          decoded->frD, decoded->frB);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    
    return written;
}

static inline int comment_frsqrte(const FRSQRTE_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    return snprintf(output, output_size, "frsqrte%s f%u, f%u",
                   decoded->Rc ? "." : "",
                   decoded->frD, decoded->frB);
}

#endif // OPCODE_FRSQRTE_H

