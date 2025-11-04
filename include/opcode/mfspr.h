/**
 * @file mfspr.h
 * @brief MFSPR - Move From Special Purpose Register
 * 
 * Opcode: 31 (primary) / 339 (extended)
 * Format: XFX-form
 * Syntax: mfspr rD, SPR
 *         mflr rD (pseudo-op for mfspr rD, LR)
 *         mfctr rD (pseudo-op for mfspr rD, CTR)
 *         mfxer rD (pseudo-op for mfspr rD, XER)
 * 
 * Description: Move contents of SPR to rD
 * Note: SPR field is encoded as spr[5-9]||spr[0-4] (split field)
 */

#ifndef OPCODE_MFSPR_H
#define OPCODE_MFSPR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_MFSPR_PRIMARY    31
#define OP_MFSPR_EXTENDED   339

// Instruction format masks
#define MFSPR_OPCD_MASK     0xFC000000  // Bits 0-5
#define MFSPR_RT_MASK       0x03E00000  // Bits 6-10
#define MFSPR_SPR_MASK      0x001FF800  // Bits 11-20 (split encoding)
#define MFSPR_XO_MASK       0x000007FE  // Bits 21-30

// Instruction format shifts
#define MFSPR_RT_SHIFT      21
#define MFSPR_SPR_SHIFT     11
#define MFSPR_XO_SHIFT      1

// Common SPR numbers (decoded from split field)
#define SPR_XER             1
#define SPR_LR              8
#define SPR_CTR             9
#define SPR_DSISR           18
#define SPR_DAR             19
#define SPR_DEC             22
#define SPR_SRR0            26
#define SPR_SRR1            27
#define SPR_SPRG0           272
#define SPR_SPRG1           273
#define SPR_SPRG2           274
#define SPR_SPRG3           275
#define SPR_TBL             268
#define SPR_TBU             269
#define SPR_PVR             287
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
 * @brief Decoded MFSPR instruction
 */
typedef struct {
    uint8_t rD;                 // Destination register (0-31)
    uint16_t SPR;               // SPR number (decoded from split field)
} MFSPR_Instruction;

/**
 * @brief Get SPR name string
 */
static inline const char* get_spr_name(uint16_t spr) {
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
        case SPR_TBL: return "tbl";
        case SPR_TBU: return "tbu";
        case SPR_PVR: return "pvr";
        case SPR_HID0: return "hid0";
        case SPR_HID1: return "hid1";
        case SPR_HID2: return "hid2";
        case SPR_HID4: return "hid4";
        default: return "spr";
    }
}

/**
 * @brief Decode MFSPR instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_mfspr(uint32_t instruction, MFSPR_Instruction *decoded) {
    uint32_t primary = (instruction & MFSPR_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & MFSPR_XO_MASK) >> MFSPR_XO_SHIFT;
    
    if (primary != OP_MFSPR_PRIMARY || extended != OP_MFSPR_EXTENDED) {
        return false;
    }
    
    decoded->rD = (instruction & MFSPR_RT_MASK) >> MFSPR_RT_SHIFT;
    
    // SPR field is encoded as spr[5-9]||spr[0-4] (bits are swapped)
    uint32_t spr_field = (instruction & MFSPR_SPR_MASK) >> MFSPR_SPR_SHIFT;
    uint32_t spr_low = (spr_field >> 5) & 0x1F;   // Bits 11-15 -> SPR[0-4]
    uint32_t spr_high = spr_field & 0x1F;          // Bits 16-20 -> SPR[5-9]
    decoded->SPR = (spr_high << 5) | spr_low;
    
    return true;
}

/**
 * @brief Transpile MFSPR instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_mfspr(const MFSPR_Instruction *decoded,
                                  char *output,
                                  size_t output_size) {
    const char *spr_name = get_spr_name(decoded->SPR);
    
    return snprintf(output, output_size,
                   "r%u = %s;",
                   decoded->rD, spr_name);
}

/**
 * @brief Generate assembly-like comment for MFSPR instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_mfspr(const MFSPR_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    const char *spr_name = get_spr_name(decoded->SPR);
    
    // Use pseudo-ops for common SPRs
    if (decoded->SPR == SPR_LR) {
        return snprintf(output, output_size, "mflr r%u", decoded->rD);
    } else if (decoded->SPR == SPR_CTR) {
        return snprintf(output, output_size, "mfctr r%u", decoded->rD);
    } else if (decoded->SPR == SPR_XER) {
        return snprintf(output, output_size, "mfxer r%u", decoded->rD);
    } else {
        return snprintf(output, output_size,
                       "mfspr r%u, %s /* SPR %u */",
                       decoded->rD, spr_name, decoded->SPR);
    }
}

#endif // OPCODE_MFSPR_H

