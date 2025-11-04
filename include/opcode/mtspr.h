/**
 * @file mtspr.h
 * @brief MTSPR - Move To Special Purpose Register
 * 
 * Opcode: 31 (primary) / 467 (extended)
 * Format: XFX-form
 * Syntax: mtspr SPR, rS
 *         mtlr rS (pseudo-op for mtspr LR, rS)
 *         mtctr rS (pseudo-op for mtspr CTR, rS)
 *         mtxer rS (pseudo-op for mtspr XER, rS)
 * 
 * Description: Move contents of rS to SPR
 * Note: SPR field is encoded as spr[5-9]||spr[0-4] (split field)
 */

#ifndef OPCODE_MTSPR_H
#define OPCODE_MTSPR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_MTSPR_PRIMARY    31
#define OP_MTSPR_EXTENDED   467

// Instruction format masks
#define MTSPR_OPCD_MASK     0xFC000000  // Bits 0-5
#define MTSPR_RS_MASK       0x03E00000  // Bits 6-10
#define MTSPR_SPR_MASK      0x001FF800  // Bits 11-20 (split encoding)
#define MTSPR_XO_MASK       0x000007FE  // Bits 21-30
#define MTSPR_OPCD_MASK     0xFC000000  // Bits 0-5

// Instruction format shifts
#define MTSPR_RS_SHIFT      21
#define MTSPR_SPR_SHIFT     11
#define MTSPR_XO_SHIFT      1

// Common SPR numbers (same as mfspr.h)
#define SPR_XER             1
#define SPR_LR              8
#define SPR_CTR             9
#define SPR_SRR0            26
#define SPR_SRR1            27
#define SPR_SPRG0           272
#define SPR_SPRG1           273
#define SPR_SPRG2           274
#define SPR_SPRG3           275
#define SPR_GQR0            912
#define SPR_GQR1            913
#define SPR_GQR2            914
#define SPR_GQR3            915
#define SPR_GQR4            916
#define SPR_GQR5            917
#define SPR_GQR6            918
#define SPR_GQR7            919
#define SPR_HID0            1008
#define SPR_HID1            1009
#define SPR_HID2            920
#define SPR_HID4            1011

/**
 * @brief Decoded MTSPR instruction
 */
typedef struct {
    uint8_t rS;                 // Source register (0-31)
    uint16_t SPR;               // SPR number (decoded from split field)
} MTSPR_Instruction;

/**
 * @brief Get SPR name string
 */
static inline const char* get_spr_name_mt(uint16_t spr) {
    switch (spr) {
        case SPR_XER: return "xer";
        case SPR_LR: return "lr";
        case SPR_CTR: return "ctr";
        case SPR_SRR0: return "srr0";
        case SPR_SRR1: return "srr1";
        case SPR_SPRG0: return "sprg0";
        case SPR_SPRG1: return "sprg1";
        case SPR_SPRG2: return "sprg2";
        case SPR_SPRG3: return "sprg3";
        case SPR_GQR0: return "gqr0";
        case SPR_GQR1: return "gqr1";
        case SPR_GQR2: return "gqr2";
        case SPR_GQR3: return "gqr3";
        case SPR_GQR4: return "gqr4";
        case SPR_GQR5: return "gqr5";
        case SPR_GQR6: return "gqr6";
        case SPR_GQR7: return "gqr7";
        case SPR_HID0: return "hid0";
        case SPR_HID1: return "hid1";
        case SPR_HID2: return "hid2";
        case SPR_HID4: return "hid4";
        default: return "spr";
    }
}

/**
 * @brief Decode MTSPR instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_mtspr(uint32_t instruction, MTSPR_Instruction *decoded) {
    uint32_t primary = (instruction & MTSPR_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & MTSPR_XO_MASK) >> MTSPR_XO_SHIFT;
    
    if (primary != OP_MTSPR_PRIMARY || extended != OP_MTSPR_EXTENDED) {
        return false;
    }
    
    decoded->rS = (instruction & MTSPR_RS_MASK) >> MTSPR_RS_SHIFT;
    
    // SPR field is encoded as spr[5-9]||spr[0-4] (bits are swapped)
    uint32_t spr_field = (instruction & MTSPR_SPR_MASK) >> MTSPR_SPR_SHIFT;
    uint32_t spr_low = (spr_field >> 5) & 0x1F;
    uint32_t spr_high = spr_field & 0x1F;
    decoded->SPR = (spr_high << 5) | spr_low;
    
    return true;
}

/**
 * @brief Transpile MTSPR instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_mtspr(const MTSPR_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    const char *spr_name = get_spr_name_mt(decoded->SPR);
    
    return snprintf(output, output_size,
                   "%s = r%u;",
                   spr_name, decoded->rS);
}

/**
 * @brief Generate assembly-like comment for MTSPR instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_mtspr(const MTSPR_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    const char *spr_name = get_spr_name_mt(decoded->SPR);
    
    // Use pseudo-ops for common SPRs
    if (decoded->SPR == SPR_LR) {
        return snprintf(output, output_size, "mtlr r%u", decoded->rS);
    } else if (decoded->SPR == SPR_CTR) {
        return snprintf(output, output_size, "mtctr r%u", decoded->rS);
    } else if (decoded->SPR == SPR_XER) {
        return snprintf(output, output_size, "mtxer r%u", decoded->rS);
    } else {
        return snprintf(output, output_size,
                       "mtspr %s, r%u /* SPR %u */",
                       spr_name, decoded->rS, decoded->SPR);
    }
}

#endif // OPCODE_MTSPR_H

