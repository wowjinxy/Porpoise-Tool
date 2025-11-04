/**
 * @file rlwnm.h
 * @brief RLWNM - Rotate Left Word then AND with Mask
 * 
 * Opcode: 23
 * Format: M-form
 * Syntax: rlwnm rA, rS, rB, MB, ME
 *         rlwnm. rA, rS, rB, MB, ME (with Rc=1)
 * 
 * Description: Rotate rS left by amount in rB[27-31], AND with mask (MB to ME), store in rA
 * Note: Only low-order 5 bits of rB are used for rotation amount (0-31)
 */

#ifndef OPCODE_RLWNM_H
#define OPCODE_RLWNM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_RLWNM            23
#define OP_RLWNM_EXTENDED   23

#define RLWNM_OPCD_MASK     0xFC000000
#define RLWNM_RS_MASK       0x03E00000
#define RLWNM_RA_MASK       0x001F0000
#define RLWNM_RB_MASK       0x0000F800
#define RLWNM_MB_MASK       0x000007C0
#define RLWNM_ME_MASK       0x0000003E
#define RLWNM_RC_MASK       0x00000001

#define RLWNM_RS_SHIFT      21
#define RLWNM_RA_SHIFT      16
#define RLWNM_RB_SHIFT      11
#define RLWNM_MB_SHIFT      6
#define RLWNM_ME_SHIFT      1

typedef struct {
    uint8_t rA;
    uint8_t rS;
    uint8_t rB;                 // Register containing shift amount
    uint8_t MB;                 // Mask begin (0-31)
    uint8_t ME;                 // Mask end (0-31)
    bool Rc;
} RLWNM_Instruction;

static inline bool decode_rlwnm(uint32_t instruction, RLWNM_Instruction *decoded) {
    uint32_t primary = (instruction & RLWNM_OPCD_MASK) >> 26;
    
    if (primary != OP_RLWNM) {
        return false;
    }
    
    decoded->rS = (instruction & RLWNM_RS_MASK) >> RLWNM_RS_SHIFT;
    decoded->rA = (instruction & RLWNM_RA_MASK) >> RLWNM_RA_SHIFT;
    decoded->rB = (instruction & RLWNM_RB_MASK) >> RLWNM_RB_SHIFT;
    decoded->MB = (instruction & RLWNM_MB_MASK) >> RLWNM_MB_SHIFT;
    decoded->ME = (instruction & RLWNM_ME_MASK) >> RLWNM_ME_SHIFT;
    decoded->Rc = (instruction & RLWNM_RC_MASK) != 0;
    
    return true;
}

/**
 * @brief Generate mask for rlwnm
 */
static inline uint32_t rlwnm_mask(uint8_t MB, uint8_t ME) {
    uint32_t mask;
    if (MB <= ME) {
        // Normal mask: bits MB through ME are 1
        mask = ((1U << (32 - MB)) - 1) & ~((1U << (31 - ME)) - 1);
    } else {
        // Wrapped mask: bits 0-ME and MB-31 are 1
        mask = ((1U << (32 - MB)) - 1) | ~((1U << (31 - ME)) - 1);
    }
    return mask;
}

static inline int transpile_rlwnm(const RLWNM_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    int written = 0;
    uint32_t mask = rlwnm_mask(decoded->MB, decoded->ME);
    
    // General case: rotate by register amount and mask
    // Only use low 5 bits of rB for shift amount
    written += snprintf(output + written, output_size - written,
                       "{ uint32_t sh = r%u & 0x1F; "
                       "r%u = ((r%u << sh) | (r%u >> (32 - sh))) & 0x%08X; }",
                       decoded->rB,
                       decoded->rA, decoded->rS, decoded->rS, mask);
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                           "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                           decoded->rA, decoded->rA);
    }
    
    return written;
}

static inline int comment_rlwnm(const RLWNM_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    // Check for rotlw pseudo-op (rotate left word)
    if (decoded->MB == 0 && decoded->ME == 31) {
        return snprintf(output, output_size,
                       "rotlw%s r%u, r%u, r%u",
                       decoded->Rc ? "." : "",
                       decoded->rA, decoded->rS, decoded->rB);
    }
    
    return snprintf(output, output_size,
                   "rlwnm%s r%u, r%u, r%u, %u, %u",
                   decoded->Rc ? "." : "",
                   decoded->rA, decoded->rS, decoded->rB,
                   decoded->MB, decoded->ME);
}

#endif // OPCODE_RLWNM_H

