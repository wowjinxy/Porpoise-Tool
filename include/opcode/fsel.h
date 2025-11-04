/**
 * @file fsel.h
 * @brief FSEL - Floating-Point Select
 * 
 * Opcode: 63 / 23
 * Format: A-form
 * Syntax: fsel frD, frA, frC, frB
 * 
 * Description: if frA >= 0.0 then frD = frC else frD = frB
 */

#ifndef OPCODE_FSEL_H
#define OPCODE_FSEL_H

#include <stdint.h>
#include <stdbool.h>

#define OP_FSEL_PRIMARY    63
#define OP_FSEL_EXTENDED   23

typedef struct {
    uint8_t frD;
    uint8_t frA;
    uint8_t frB;
    uint8_t frC;
    bool Rc;
} FSEL_Instruction;

static inline bool decode_fsel(uint32_t instruction, FSEL_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    uint32_t extended = (instruction >> 1) & 0x1F;
    
    if (primary != OP_FSEL_PRIMARY || extended != OP_FSEL_EXTENDED) {
        return false;
    }
    
    decoded->frD = (instruction >> 21) & 0x1F;
    decoded->frA = (instruction >> 16) & 0x1F;
    decoded->frB = (instruction >> 11) & 0x1F;
    decoded->frC = (instruction >> 6) & 0x1F;
    decoded->Rc = instruction & 1;
    
    return true;
}

static inline int transpile_fsel(const FSEL_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    int written = snprintf(output, output_size,
                          "f%u = (f%u >= 0.0) ? f%u : f%u;",
                          decoded->frD, decoded->frA, decoded->frC, decoded->frB);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    
    return written;
}

static inline int comment_fsel(const FSEL_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    return snprintf(output, output_size, "fsel%s f%u, f%u, f%u, f%u",
                   decoded->Rc ? "." : "",
                   decoded->frD, decoded->frA, decoded->frC, decoded->frB);
}

#endif // OPCODE_FSEL_H

