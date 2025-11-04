/**
 * @file lmw.h
 * @brief LMW - Load Multiple Word
 * 
 * Opcode: 46
 * Format: D-form
 * Syntax: lmw rD, d(rA)
 * 
 * Description: Load consecutive words from memory into rD through r31
 *              starting at address (rA|0) + d
 */

#ifndef OPCODE_LMW_H
#define OPCODE_LMW_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_LMW              46

#define LMW_OPCD_MASK       0xFC000000
#define LMW_RT_MASK         0x03E00000
#define LMW_RA_MASK         0x001F0000
#define LMW_D_MASK          0x0000FFFF

#define LMW_RT_SHIFT        21
#define LMW_RA_SHIFT        16

typedef struct {
    uint8_t rD;
    uint8_t rA;
    int16_t d;
} LMW_Instruction;

static inline bool decode_lmw(uint32_t instruction, LMW_Instruction *decoded) {
    uint32_t primary = (instruction & LMW_OPCD_MASK) >> 26;
    
    if (primary != OP_LMW) {
        return false;
    }
    
    decoded->rD = (instruction & LMW_RT_MASK) >> LMW_RT_SHIFT;
    decoded->rA = (instruction & LMW_RA_MASK) >> LMW_RA_SHIFT;
    decoded->d = (int16_t)(instruction & LMW_D_MASK);
    
    return true;
}

static inline int transpile_lmw(const LMW_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    int written = 0;
    
    char base_expr[64];
    if (decoded->rA == 0) {
        snprintf(base_expr, sizeof(base_expr), "0x%x", 
                (uint32_t)(decoded->d >= 0 ? decoded->d : -decoded->d));
    } else {
        if (decoded->d == 0) {
            snprintf(base_expr, sizeof(base_expr), "r%u", decoded->rA);
        } else if (decoded->d > 0) {
            snprintf(base_expr, sizeof(base_expr), "r%u + 0x%x", 
                    decoded->rA, (uint16_t)decoded->d);
        } else {
            snprintf(base_expr, sizeof(base_expr), "r%u - 0x%x", 
                    decoded->rA, (uint16_t)(-decoded->d));
        }
    }
    
    int num_regs = 32 - decoded->rD;
    
    if (num_regs == 1) {
        written += snprintf(output + written, output_size - written,
                           "r%u = *(uint32_t*)(mem + %s);",
                           decoded->rD, base_expr);
    } else {
        written += snprintf(output + written, output_size - written,
                           "{ uint32_t *p = (uint32_t*)(mem + %s); ",
                           base_expr);
        
        for (int i = 0; i < num_regs; i++) {
            written += snprintf(output + written, output_size - written,
                               "r%u = p[%d]; ",
                               decoded->rD + i, i);
        }
        
        written += snprintf(output + written, output_size - written, "}");
    }
    
    return written;
}

static inline int comment_lmw(const LMW_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    if (decoded->d >= 0) {
        return snprintf(output, output_size,
                       "lmw r%u, 0x%x(r%u)",
                       decoded->rD, (uint16_t)decoded->d, decoded->rA);
    } else {
        return snprintf(output, output_size,
                       "lmw r%u, -0x%x(r%u)",
                       decoded->rD, (uint16_t)(-decoded->d), decoded->rA);
    }
}

#endif // OPCODE_LMW_H

