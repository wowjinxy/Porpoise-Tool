/**
 * @file neg.h
 * @brief NEG - Negate
 * 
 * Opcode: 31 (primary) / 104 (extended)
 * Format: XO-form
 * Syntax: neg rD, rA
 *         neg. rD, rA (with Rc=1)
 *         nego rD, rA (with OE=1)
 *         nego. rD, rA (with OE=1, Rc=1)
 * 
 * Description: rD = -rA (two's complement negation)
 */

#ifndef OPCODE_NEG_H
#define OPCODE_NEG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_NEG_PRIMARY      31
#define OP_NEG_EXTENDED     104

typedef struct {
    uint8_t rD;
    uint8_t rA;
    bool OE;
    bool Rc;
} NEG_Instruction;

static inline bool decode_neg(uint32_t instruction, NEG_Instruction *decoded) {
    uint32_t primary = (instruction >> 26) & 0x3F;
    uint32_t extended = (instruction >> 1) & 0x3FF;
    
    if (primary != OP_NEG_PRIMARY || extended != OP_NEG_EXTENDED) {
        return false;
    }
    
    decoded->rD = (instruction >> 21) & 0x1F;
    decoded->rA = (instruction >> 16) & 0x1F;
    decoded->OE = (instruction >> 10) & 1;
    decoded->Rc = instruction & 1;
    
    return true;
}

static inline int transpile_neg(const NEG_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    int written = 0;
    
    written += snprintf(output + written, output_size - written,
                       "r%u = -r%u;",
                       decoded->rD, decoded->rA);
    
    if (decoded->OE) {
        written += snprintf(output + written, output_size - written,
                           "\nif (r%u == 0x80000000) { xer |= 0xC0000000; } "
                           "else { xer &= ~0x80000000; }",
                           decoded->rA);
    }
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                           "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                           decoded->rD, decoded->rD);
    }
    
    return written;
}

static inline int comment_neg(const NEG_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    return snprintf(output, output_size,
                   "neg%s%s r%u, r%u",
                   decoded->OE ? "o" : "",
                   decoded->Rc ? "." : "",
                   decoded->rD, decoded->rA);
}

#endif // OPCODE_NEG_H

