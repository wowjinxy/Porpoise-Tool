/**
 * @file blr.h
 * @brief BLR - Branch to Link Register (Return)
 * 
 * Opcode: 19 (primary) / 16 (extended) - Special case of BCLR
 * Format: XL-form
 * Syntax: blr (pseudo-op for bclr 20, 0)
 *         blrl (with LK=1, branch and link to LR)
 * 
 * Description: Branch to address in Link Register (function return)
 * This is a special case of BCLR with BO=20 (unconditional)
 */

#ifndef OPCODE_BLR_H
#define OPCODE_BLR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding (same as BCLR)
#define OP_BLR_PRIMARY      19
#define OP_BLR_EXTENDED     16

// Instruction format masks
#define BLR_OPCD_MASK       0xFC000000  // Bits 0-5
#define BLR_BO_MASK         0x03E00000  // Bits 6-10
#define BLR_BI_MASK         0x001F0000  // Bits 11-15
#define BLR_XO_MASK         0x000007FE  // Bits 21-30
#define BLR_LK_MASK         0x00000001  // Bit 31

// Instruction format shifts
#define BLR_BO_SHIFT        21
#define BLR_BI_SHIFT        16
#define BLR_XO_SHIFT        1

/**
 * @brief Decoded BLR instruction
 */
typedef struct {
    uint8_t BO;                 // Branch options (usually 20 for unconditional)
    uint8_t BI;                 // Condition bit (ignored for unconditional)
    bool LK;                    // Link bit
} BLR_Instruction;

/**
 * @brief Decode BLR/BCLR instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_blr(uint32_t instruction, BLR_Instruction *decoded) {
    uint32_t primary = (instruction & BLR_OPCD_MASK) >> 26;
    uint32_t extended = (instruction & BLR_XO_MASK) >> BLR_XO_SHIFT;
    
    if (primary != OP_BLR_PRIMARY || extended != OP_BLR_EXTENDED) {
        return false;
    }
    
    decoded->BO = (instruction & BLR_BO_MASK) >> BLR_BO_SHIFT;
    decoded->BI = (instruction & BLR_BI_MASK) >> BLR_BI_SHIFT;
    decoded->LK = (instruction & BLR_LK_MASK) != 0;
    
    return true;
}

/**
 * @brief Check if this is an unconditional blr (BO=20)
 */
static inline bool is_unconditional_blr(const BLR_Instruction *decoded) {
    return decoded->BO == 20;
}

/**
 * @brief Transpile BLR instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param current_addr Current instruction address
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_blr(const BLR_Instruction *decoded,
                                uint32_t current_addr,
                                char *output,
                                size_t output_size) {
    if (is_unconditional_blr(decoded)) {
        if (decoded->LK) {
            // blrl - branch and link to LR (unusual)
            return snprintf(output, output_size,
                           "{ uint32_t target = lr; lr = 0x%08X; goto *target; }",
                           current_addr + 4);
        } else {
            // blr - simple return
            return snprintf(output, output_size, "return;");
        }
    } else {
        // Conditional blr (bclr with conditions)
        // TODO: Handle conditional cases based on BO and BI fields
        return snprintf(output, output_size,
                       "/* conditional bclr %u, %u */",
                       decoded->BO, decoded->BI);
    }
}

/**
 * @brief Generate assembly-like comment for BLR instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_blr(const BLR_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    if (is_unconditional_blr(decoded)) {
        return snprintf(output, output_size,
                       "blr%s",
                       decoded->LK ? "l" : "");
    } else {
        return snprintf(output, output_size,
                       "bclr%s %u, %u",
                       decoded->LK ? "l" : "",
                       decoded->BO, decoded->BI);
    }
}

#endif // OPCODE_BLR_H

