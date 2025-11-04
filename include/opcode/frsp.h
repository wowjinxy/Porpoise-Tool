/**
 * @file frsp.h
 * @brief FRSP - Floating-Point Round to Single-Precision
 * 
 * Opcode: 63 / 12
 * Format: X-form
 * Syntax: frsp frD, frB
 *         frsp. frD, frB (with Rc=1)
 * 
 * Description: Round double to single precision and store as double
 */

#ifndef OPCODE_FRSP_H
#define OPCODE_FRSP_H

#include <stdint.h>
#include <stdbool.h>

#define OP_FRSP_PRIMARY    63
#define OP_FRSP_EXTENDED   12

typedef struct {
    uint8_t frD;
    uint8_t frB;
    bool Rc;
} FRSP_Instruction;

static inline bool decode_frsp(uint32_t instruction, FRSP_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    uint32_t extended = (instruction >> 1) & 0x3FF;
    
    if (primary != OP_FRSP_PRIMARY || extended != OP_FRSP_EXTENDED) {
        return false;
    }
    
    decoded->frD = (instruction >> 21) & 0x1F;
    decoded->frB = (instruction >> 11) & 0x1F;
    decoded->Rc = instruction & 1;
    
    return true;
}

static inline int transpile_frsp(const FRSP_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    int written = snprintf(output, output_size,
                          "f%u = (double)(float)f%u;",
                          decoded->frD, decoded->frB);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    
    return written;
}

static inline int comment_frsp(const FRSP_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    return snprintf(output, output_size, "frsp%s f%u, f%u",
                   decoded->Rc ? "." : "",
                   decoded->frD, decoded->frB);
}

#endif // OPCODE_FRSP_H

