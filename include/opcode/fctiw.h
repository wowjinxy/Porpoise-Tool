/**
 * @file fctiw.h
 * @brief FCTIW - Floating-Point Convert to Integer Word
 * 
 * Opcode: 63 / 14
 * Format: X-form
 * Syntax: fctiw frD, frB
 *         fctiw. frD, frB (with Rc=1)
 * 
 * Description: Convert floating-point to signed 32-bit integer
 */

#ifndef OPCODE_FCTIW_H
#define OPCODE_FCTIW_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define OP_FCTIW_PRIMARY    63
#define OP_FCTIW_EXTENDED   14

typedef struct {
    uint8_t frD;
    uint8_t frB;
    bool Rc;
} FCTIW_Instruction;

static inline bool decode_fctiw(uint32_t instruction, FCTIW_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    uint32_t extended = (instruction >> 1) & 0x3FF;
    
    if (primary != OP_FCTIW_PRIMARY || extended != OP_FCTIW_EXTENDED) {
        return false;
    }
    
    decoded->frD = (instruction >> 21) & 0x1F;
    decoded->frB = (instruction >> 11) & 0x1F;
    decoded->Rc = instruction & 1;
    
    return true;
}

static inline int transpile_fctiw(const FCTIW_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    int written = snprintf(output, output_size,
                          "{ union { double d; uint64_t i; } u; "
                          "u.i = (int32_t)round(f%u); f%u = u.d; }",
                          decoded->frB, decoded->frD);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr1 = (fpscr >> 28) & 0xF;");
    }
    
    return written;
}

static inline int comment_fctiw(const FCTIW_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    return snprintf(output, output_size, "fctiw%s f%u, f%u",
                   decoded->Rc ? "." : "",
                   decoded->frD, decoded->frB);
}

#endif // OPCODE_FCTIW_H

