/**
 * @file bc.h
 * @brief BC - Branch Conditional
 * 
 * Opcode: 16
 * Format: B-form
 * Syntax: bc BO, BI, target
 *         bc+ variants with prediction hints
 * 
 * Extended mnemonics: beq, bne, blt, ble, bgt, bge, etc.
 * 
 * Description: Branch conditionally based on CR bit and BO field
 */

#ifndef OPCODE_BC_H
#define OPCODE_BC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_BC               16

#define BC_OPCD_MASK        0xFC000000  // Bits 0-5
#define BC_BO_MASK          0x03E00000  // Bits 6-10
#define BC_BI_MASK          0x001F0000  // Bits 11-15
#define BC_BD_MASK          0x0000FFFC  // Bits 16-29 (14-bit signed)
#define BC_AA_MASK          0x00000002  // Bit 30
#define BC_LK_MASK          0x00000001  // Bit 31

#define BC_BO_SHIFT         21
#define BC_BI_SHIFT         16
#define BC_BD_SHIFT         2

// Common BO field values
#define BO_BRANCH_IF_FALSE  4   // Branch if condition false
#define BO_BRANCH_IF_TRUE   12  // Branch if condition true
#define BO_DECREMENT_NZ     16  // Decrement CTR, branch if CTR != 0
#define BO_DECREMENT_Z      18  // Decrement CTR, branch if CTR == 0
#define BO_ALWAYS           20  // Branch always (unconditional)

// CR bit meanings (BI field points to one of 32 CR bits)
#define CR_LT_BIT           0   // Less than (in CR0)
#define CR_GT_BIT           1   // Greater than
#define CR_EQ_BIT           2   // Equal
#define CR_SO_BIT           3   // Summary overflow

typedef struct {
    uint8_t BO;                 // Branch options (5 bits)
    uint8_t BI;                 // Condition register bit (0-31)
    int16_t BD;                 // Branch displacement (signed, 14-bit)
    bool AA;                    // Absolute address
    bool LK;                    // Link bit
} BC_Instruction;

static inline bool decode_bc(uint32_t instruction, BC_Instruction *decoded) {
    uint32_t primary = (instruction & BC_OPCD_MASK) >> 26;
    
    if (primary != OP_BC) {
        return false;
    }
    
    decoded->BO = (instruction & BC_BO_MASK) >> BC_BO_SHIFT;
    decoded->BI = (instruction & BC_BI_MASK) >> BC_BI_SHIFT;
    
    // Extract and sign-extend BD (14-bit signed)
    uint32_t bd_field = (instruction & BC_BD_MASK) >> BC_BD_SHIFT;
    if (bd_field & 0x2000) {
        decoded->BD = (int16_t)(bd_field | 0xC000);  // Sign extend
    } else {
        decoded->BD = (int16_t)bd_field;
    }
    
    decoded->AA = (instruction & BC_AA_MASK) != 0;
    decoded->LK = (instruction & BC_LK_MASK) != 0;
    
    return true;
}

/**
 * @brief Get CR field number from BI
 */
static inline uint8_t get_cr_field(uint8_t BI) {
    return BI / 4;
}

/**
 * @brief Get CR bit within field from BI
 */
static inline uint8_t get_cr_bit(uint8_t BI) {
    return BI % 4;
}

static inline int transpile_bc(const BC_Instruction *decoded,
                               uint32_t current_addr,
                               char *output,
                               size_t output_size) {
    uint32_t target_addr;
    
    if (decoded->AA) {
        target_addr = (uint32_t)decoded->BD;
    } else {
        target_addr = current_addr + (decoded->BD * 4);
    }
    
    uint8_t cr_field = get_cr_field(decoded->BI);
    uint8_t cr_bit = get_cr_bit(decoded->BI);
    
    // Determine condition
    const char *condition = NULL;
    bool invert = false;
    
    if (decoded->BO == BO_BRANCH_IF_TRUE) {
        // Branch if CR bit is 1
        if (cr_bit == CR_EQ_BIT) condition = "cr%u & 0x2";       // EQ
        else if (cr_bit == CR_LT_BIT) condition = "cr%u & 0x8";  // LT
        else if (cr_bit == CR_GT_BIT) condition = "cr%u & 0x4";  // GT
        else if (cr_bit == CR_SO_BIT) condition = "cr%u & 0x1";  // SO
        else condition = "cr%u & (1 << %d)";
    } else if (decoded->BO == BO_BRANCH_IF_FALSE) {
        // Branch if CR bit is 0
        if (cr_bit == CR_EQ_BIT) condition = "!(cr%u & 0x2)";
        else if (cr_bit == CR_LT_BIT) condition = "!(cr%u & 0x8)";
        else if (cr_bit == CR_GT_BIT) condition = "!(cr%u & 0x4)";
        else if (cr_bit == CR_SO_BIT) condition = "!(cr%u & 0x1)";
        else condition = "!(cr%u & (1 << %d))";
    } else if (decoded->BO == BO_DECREMENT_NZ) {
        condition = "--ctr != 0";
    } else if (decoded->BO == BO_DECREMENT_Z) {
        condition = "--ctr == 0";
    } else if (decoded->BO == BO_ALWAYS) {
        condition = NULL;  // Unconditional
    }
    
    int written = 0;
    
    if (decoded->LK) {
        written += snprintf(output + written, output_size - written,
                           "lr = 0x%08X; ", current_addr + 4);
    }
    
    if (condition) {
        if (strchr(condition, '%') && cr_bit != CR_EQ_BIT && cr_bit != CR_LT_BIT && 
            cr_bit != CR_GT_BIT && cr_bit != CR_SO_BIT) {
            // Generic bit condition
            written += snprintf(output + written, output_size - written,
                               "if (");
            written += snprintf(output + written, output_size - written,
                               condition, cr_field, cr_bit);
            written += snprintf(output + written, output_size - written,
                               ") goto L_%08X;", target_addr);
        } else {
            written += snprintf(output + written, output_size - written,
                               "if (");
            written += snprintf(output + written, output_size - written,
                               condition, cr_field);
            written += snprintf(output + written, output_size - written,
                               ") goto L_%08X;", target_addr);
        }
    } else {
        // Unconditional
        written += snprintf(output + written, output_size - written,
                           "goto L_%08X;", target_addr);
    }
    
    return written;
}

static inline int comment_bc(const BC_Instruction *decoded,
                             uint32_t current_addr,
                             char *output,
                             size_t output_size) {
    uint32_t target_addr;
    
    if (decoded->AA) {
        target_addr = (uint32_t)decoded->BD;
    } else {
        target_addr = current_addr + (decoded->BD * 4);
    }
    
    uint8_t cr_field = get_cr_field(decoded->BI);
    uint8_t cr_bit = get_cr_bit(decoded->BI);
    
    // Extended mnemonics for common cases
    if (decoded->BO == BO_BRANCH_IF_TRUE && cr_field == 0) {
        if (cr_bit == CR_EQ_BIT) {
            return snprintf(output, output_size, "beq 0x%08X", target_addr);
        } else if (cr_bit == CR_LT_BIT) {
            return snprintf(output, output_size, "blt 0x%08X", target_addr);
        } else if (cr_bit == CR_GT_BIT) {
            return snprintf(output, output_size, "bgt 0x%08X", target_addr);
        }
    } else if (decoded->BO == BO_BRANCH_IF_FALSE && cr_field == 0) {
        if (cr_bit == CR_EQ_BIT) {
            return snprintf(output, output_size, "bne 0x%08X", target_addr);
        } else if (cr_bit == CR_LT_BIT) {
            return snprintf(output, output_size, "bge 0x%08X", target_addr);
        } else if (cr_bit == CR_GT_BIT) {
            return snprintf(output, output_size, "ble 0x%08X", target_addr);
        }
    } else if (decoded->BO == BO_DECREMENT_NZ) {
        return snprintf(output, output_size, "bdnz 0x%08X", target_addr);
    } else if (decoded->BO == BO_DECREMENT_Z) {
        return snprintf(output, output_size, "bdz 0x%08X", target_addr);
    }
    
    // Generic form
    return snprintf(output, output_size,
                   "bc%s%s %u, %u, 0x%08X",
                   decoded->AA ? "a" : "",
                   decoded->LK ? "l" : "",
                   decoded->BO, decoded->BI, target_addr);
}

#endif // OPCODE_BC_H

