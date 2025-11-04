/**
 * @file crxor.h
 * @brief CRXOR - Condition Register XOR
 * 
 * Opcode: 19 (primary) / 193 (extended)
 * Format: XL-form
 * Syntax: crxor crbD, crbA, crbB
 *         crclr crbD (pseudo-op when crbA=crbB=crbD, clears bit)
 * 
 * Description: XOR two CR bits and store result in destination CR bit
 */

#ifndef OPCODE_CRXOR_H
#define OPCODE_CRXOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_CRXOR_PRIMARY    19
#define OP_CRXOR_EXTENDED   193

#define CRXOR_OPCD_MASK     0xFC000000
#define CRXOR_CRBD_MASK     0x03E00000
#define CRXOR_CRBA_MASK     0x001F0000
#define CRXOR_CRBB_MASK     0x0000F800
#define CRXOR_XO_MASK       0x000007FE

#define CRXOR_CRBD_SHIFT    21
#define CRXOR_CRBA_SHIFT    16
#define CRXOR_CRBB_SHIFT    11
#define CRXOR_XO_SHIFT      1

typedef struct {
    uint8_t crbD;               // Destination CR bit (0-31)
    uint8_t crbA;               // Source CR bit A (0-31)
    uint8_t crbB;               // Source CR bit B (0-31)
} CRXOR_Instruction;

static inline bool decode_crxor(uint32_t instruction, CRXOR_Instruction *decoded) {
    uint32_t primary = (instruction & CRXOR_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & CRXOR_XO_MASK) >> CRXOR_XO_SHIFT;
    
    if (primary != OP_CRXOR_PRIMARY || extended != OP_CRXOR_EXTENDED) {
        return false;
    }
    
    decoded->crbD = (instruction & CRXOR_CRBD_MASK) >> CRXOR_CRBD_SHIFT;
    decoded->crbA = (instruction & CRXOR_CRBA_MASK) >> CRXOR_CRBA_SHIFT;
    decoded->crbB = (instruction & CRXOR_CRBB_MASK) >> CRXOR_CRBB_SHIFT;
    
    return true;
}

static inline int transpile_crxor(const CRXOR_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    uint8_t cr_field_d = decoded->crbD / 4;
    uint8_t cr_bit_d = decoded->crbD % 4;
    uint8_t cr_field_a = decoded->crbA / 4;
    uint8_t cr_bit_a = decoded->crbA % 4;
    uint8_t cr_field_b = decoded->crbB / 4;
    uint8_t cr_bit_b = decoded->crbB % 4;
    
    // Check for crclr pseudo-op (crxor crbD, crbD, crbD)
    if (decoded->crbD == decoded->crbA && decoded->crbA == decoded->crbB) {
        // Clear CR bit
        return snprintf(output, output_size,
                       "cr%u &= ~(1 << %u);  /* crclr */",
                       cr_field_d, (3 - cr_bit_d));
    }
    
    // General crxor
    return snprintf(output, output_size,
                   "{ uint8_t a = (cr%u >> %u) & 1; "
                   "uint8_t b = (cr%u >> %u) & 1; "
                   "cr%u = (cr%u & ~(1 << %u)) | ((a ^ b) << %u); }",
                   cr_field_a, (3 - cr_bit_a),
                   cr_field_b, (3 - cr_bit_b),
                   cr_field_d, cr_field_d, (3 - cr_bit_d), (3 - cr_bit_d));
}

static inline int comment_crxor(const CRXOR_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    // Check for crclr pseudo-op
    if (decoded->crbD == decoded->crbA && decoded->crbA == decoded->crbB) {
        // Decode which CR bit this is
        uint8_t cr_field = decoded->crbD / 4;
        uint8_t cr_bit = decoded->crbD % 4;
        
        const char *bit_name = "";
        if (cr_bit == 0) bit_name = "lt";
        else if (cr_bit == 1) bit_name = "gt";
        else if (cr_bit == 2) bit_name = "eq";
        else if (cr_bit == 3) bit_name = "so";
        
        return snprintf(output, output_size,
                       "crclr cr%u%s",
                       cr_field, bit_name);
    }
    
    return snprintf(output, output_size,
                   "crxor %u, %u, %u",
                   decoded->crbD, decoded->crbA, decoded->crbB);
}

#endif // OPCODE_CRXOR_H

