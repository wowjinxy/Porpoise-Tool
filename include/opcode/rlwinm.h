/**
 * @file rlwinm.h
 * @brief RLWINM - Rotate Left Word Immediate then AND with Mask
 * 
 * Opcode: 21
 * Format: M-form
 * Syntax: rlwinm rA, rS, SH, MB, ME
 *         rlwinm. rA, rS, SH, MB, ME (with Rc=1)
 * 
 * Pseudo-ops:
 *   slwi rA, rS, n  (shift left, SH=n, MB=0, ME=31-n)
 *   srwi rA, rS, n  (shift right, SH=32-n, MB=n, ME=31)
 *   clrlwi rA, rS, n (clear left n bits, SH=0, MB=n, ME=31)
 *   extlwi rA, rS, n, b (extract n bits starting at bit b)
 * 
 * Description: Rotate rS left by SH, AND with mask (MB to ME), store in rA
 */

#ifndef OPCODE_RLWINM_H
#define OPCODE_RLWINM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_RLWINM           21

#define RLWINM_OPCD_MASK    0xFC000000
#define RLWINM_RS_MASK      0x03E00000
#define RLWINM_RA_MASK      0x001F0000
#define RLWINM_SH_MASK      0x0000F800
#define RLWINM_MB_MASK      0x000007C0
#define RLWINM_ME_MASK      0x0000003E
#define RLWINM_RC_MASK      0x00000001

#define RLWINM_RS_SHIFT     21
#define RLWINM_RA_SHIFT     16
#define RLWINM_SH_SHIFT     11
#define RLWINM_MB_SHIFT     6
#define RLWINM_ME_SHIFT     1

typedef struct {
    uint8_t rA;
    uint8_t rS;
    uint8_t SH;                 // Shift amount (0-31)
    uint8_t MB;                 // Mask begin (0-31)
    uint8_t ME;                 // Mask end (0-31)
    bool Rc;
} RLWINM_Instruction;

static inline bool decode_rlwinm(uint32_t instruction, RLWINM_Instruction *decoded) {
    uint32_t primary = (instruction & RLWINM_OPCD_MASK) >> 26;
    
    if (primary != OP_RLWINM) {
        return false;
    }
    
    decoded->rS = (instruction & RLWINM_RS_MASK) >> RLWINM_RS_SHIFT;
    decoded->rA = (instruction & RLWINM_RA_MASK) >> RLWINM_RA_SHIFT;
    decoded->SH = (instruction & RLWINM_SH_MASK) >> RLWINM_SH_SHIFT;
    decoded->MB = (instruction & RLWINM_MB_MASK) >> RLWINM_MB_SHIFT;
    decoded->ME = (instruction & RLWINM_ME_MASK) >> RLWINM_ME_SHIFT;
    decoded->Rc = (instruction & RLWINM_RC_MASK) != 0;
    
    return true;
}

/**
 * @brief Generate mask for rlwinm
 */
static inline uint32_t rlwinm_mask(uint8_t MB, uint8_t ME) {
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

static inline int transpile_rlwinm(const RLWINM_Instruction *decoded,
                                   char *output,
                                   size_t output_size) {
    int written = 0;
    uint32_t mask = rlwinm_mask(decoded->MB, decoded->ME);
    
    // Check for common pseudo-ops
    if (decoded->SH == 0 && decoded->MB == 0 && decoded->ME == 31) {
        // No-op (move without rotation/mask)
        written += snprintf(output + written, output_size - written,
                           "r%u = r%u;",
                           decoded->rA, decoded->rS);
    } else if (decoded->MB == 0 && decoded->ME == (31 - decoded->SH) && decoded->SH != 0) {
        // slwi - shift left immediate
        written += snprintf(output + written, output_size - written,
                           "r%u = r%u << %u;",
                           decoded->rA, decoded->rS, decoded->SH);
    } else if (decoded->SH == (32 - decoded->MB) && decoded->ME == 31) {
        // srwi - shift right immediate
        written += snprintf(output + written, output_size - written,
                           "r%u = r%u >> %u;",
                           decoded->rA, decoded->rS, decoded->MB);
    } else {
        // General case: rotate and mask
        if (decoded->SH == 0) {
            written += snprintf(output + written, output_size - written,
                               "r%u = r%u & 0x%08X;",
                               decoded->rA, decoded->rS, mask);
        } else {
            written += snprintf(output + written, output_size - written,
                               "r%u = ((r%u << %u) | (r%u >> %u)) & 0x%08X;",
                               decoded->rA, decoded->rS, decoded->SH,
                               decoded->rS, 32 - decoded->SH, mask);
        }
    }
    
    if (decoded->Rc) {
        written += snprintf(output + written, output_size - written,
                           "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                           "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                           decoded->rA, decoded->rA);
    }
    
    return written;
}

static inline int comment_rlwinm(const RLWINM_Instruction *decoded,
                                 char *output,
                                 size_t output_size) {
    // Check for pseudo-ops
    if (decoded->MB == 0 && decoded->ME == (31 - decoded->SH) && decoded->SH != 0) {
        return snprintf(output, output_size,
                       "slwi%s r%u, r%u, %u",
                       decoded->Rc ? "." : "",
                       decoded->rA, decoded->rS, decoded->SH);
    } else if (decoded->SH == (32 - decoded->MB) && decoded->ME == 31 && decoded->MB != 0) {
        return snprintf(output, output_size,
                       "srwi%s r%u, r%u, %u",
                       decoded->Rc ? "." : "",
                       decoded->rA, decoded->rS, decoded->MB);
    }
    
    return snprintf(output, output_size,
                   "rlwinm%s r%u, r%u, %u, %u, %u",
                   decoded->Rc ? "." : "",
                   decoded->rA, decoded->rS, decoded->SH,
                   decoded->MB, decoded->ME);
}

#endif // OPCODE_RLWINM_H

