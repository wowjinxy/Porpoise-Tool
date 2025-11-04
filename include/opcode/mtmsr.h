/**
 * @file mtmsr.h
 * @brief MTMSR - Move To Machine State Register
 * 
 * Opcode: 31 (primary) / 146 (extended)
 * Format: X-form
 * Syntax: mtmsr rS
 * 
 * Description: Move contents of rS to MSR (supervisor only)
 */

#ifndef OPCODE_MTMSR_H
#define OPCODE_MTMSR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_MTMSR_PRIMARY    31
#define OP_MTMSR_EXTENDED   146

#define MTMSR_OPCD_MASK     0xFC000000
#define MTMSR_RS_MASK       0x03E00000
#define MTMSR_XO_MASK       0x000007FE

#define MTMSR_RS_SHIFT      21
#define MTMSR_XO_SHIFT      1

typedef struct {
    uint8_t rS;
} MTMSR_Instruction;

static inline bool decode_mtmsr(uint32_t instruction, MTMSR_Instruction *decoded) {
    uint32_t primary = (instruction & MTMSR_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & MTMSR_XO_MASK) >> MTMSR_XO_SHIFT;
    
    if (primary != OP_MTMSR_PRIMARY || extended != OP_MTMSR_EXTENDED) {
        return false;
    }
    
    decoded->rS = (instruction & MTMSR_RS_MASK) >> MTMSR_RS_SHIFT;
    
    return true;
}

static inline int transpile_mtmsr(const MTMSR_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    return snprintf(output, output_size,
                   "msr = r%u;",
                   decoded->rS);
}

static inline int comment_mtmsr(const MTMSR_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    return snprintf(output, output_size, "mtmsr r%u", decoded->rS);
}

#endif // OPCODE_MTMSR_H

