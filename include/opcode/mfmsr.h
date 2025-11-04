/**
 * @file mfmsr.h
 * @brief MFMSR - Move From Machine State Register
 * 
 * Opcode: 31 (primary) / 83 (extended)
 * Format: X-form
 * Syntax: mfmsr rD
 * 
 * Description: Move contents of MSR to rD (supervisor only)
 */

#ifndef OPCODE_MFMSR_H
#define OPCODE_MFMSR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_MFMSR_PRIMARY    31
#define OP_MFMSR_EXTENDED   83

#define MFMSR_OPCD_MASK     0xFC000000
#define MFMSR_RT_MASK       0x03E00000
#define MFMSR_XO_MASK       0x000007FE

#define MFMSR_RT_SHIFT      21
#define MFMSR_XO_SHIFT      1

typedef struct {
    uint8_t rD;
} MFMSR_Instruction;

static inline bool decode_mfmsr(uint32_t instruction, MFMSR_Instruction *decoded) {
    uint32_t primary = (instruction & MFMSR_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & MFMSR_XO_MASK) >> MFMSR_XO_SHIFT;
    
    if (primary != OP_MFMSR_PRIMARY || extended != OP_MFMSR_EXTENDED) {
        return false;
    }
    
    decoded->rD = (instruction & MFMSR_RT_MASK) >> MFMSR_RT_SHIFT;
    
    return true;
}

static inline int transpile_mfmsr(const MFMSR_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    return snprintf(output, output_size,
                   "r%u = msr;",
                   decoded->rD);
}

static inline int comment_mfmsr(const MFMSR_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    return snprintf(output, output_size, "mfmsr r%u", decoded->rD);
}

#endif // OPCODE_MFMSR_H

