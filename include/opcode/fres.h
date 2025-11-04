/**
 * @file fres.h
 * @brief FRES - Floating-Point Reciprocal Estimate Single
 * 
 * Opcode: 59 / 24
 * Format: A-form
 * Syntax: fres frD, frB
 * 
 * Description: frD = 1.0 / frB (approximate)
 */

#ifndef OPCODE_FRES_H
#define OPCODE_FRES_H

#include <stdint.h>
#include <stdbool.h>

#define OP_FRES_PRIMARY    59
#define OP_FRES_EXTENDED   24

typedef struct {
    uint8_t frD;
    uint8_t frB;
    bool Rc;
} FRES_Instruction;

static inline bool decode_fres(uint32_t instruction, FRES_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    uint32_t extended = (instruction >> 1) & 0x1F;
    
    if (primary != OP_FRES_PRIMARY || extended != OP_FRES_EXTENDED) {
        return false;
    }
    
    decoded->frD = (instruction >> 21) & 0x1F;
    decoded->frB = (instruction >> 11) & 0x1F;
    decoded->Rc = instruction & 1;
    
    return true;
}

static inline int transpile_fres(const FRES_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    int written = snprintf(output, output_size,
                          "f%u = (float)(1.0 / f%u);",
                          decoded->frD, decoded->frB);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    
    return written;
}

static inline int comment_fres(const FRES_Instruction *decoded,
                               char *output,
                               size_t output_size) {
    return snprintf(output, output_size, "fres%s f%u, f%u",
                   decoded->Rc ? "." : "",
                   decoded->frD, decoded->frB);
}

#endif // OPCODE_FRES_H

