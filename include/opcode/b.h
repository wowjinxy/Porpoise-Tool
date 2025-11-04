/**
 * @file b.h
 * @brief B - Branch
 * 
 * Opcode: 18
 * Format: I-form
 * Syntax: b target
 *         ba target (absolute addressing, AA=1)
 *         bl target (branch and link, LK=1)
 *         bla target (absolute + link, AA=1, LK=1)
 * 
 * Description: Unconditional branch to target address
 */

#ifndef OPCODE_B_H
#define OPCODE_B_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_B                18

// Instruction format masks
#define B_OPCD_MASK         0xFC000000  // Bits 0-5
#define B_LI_MASK           0x03FFFFFC  // Bits 6-29 (24-bit signed, word-aligned)
#define B_AA_MASK           0x00000002  // Bit 30 (Absolute Address)
#define B_LK_MASK           0x00000001  // Bit 31 (Link)

// Instruction format shifts
#define B_LI_SHIFT          2
#define B_AA_SHIFT          1
#define B_LK_SHIFT          0

/**
 * @brief Decoded B instruction
 */
typedef struct {
    int32_t LI;                 // Branch displacement (sign-extended, 24-bit)
    bool AA;                    // Absolute address bit
    bool LK;                    // Link bit (save return address)
} B_Instruction;

/**
 * @brief Decode B instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_b(uint32_t instruction, B_Instruction *decoded) {
    uint32_t primary = (instruction & B_OPCD_MASK) >> 26;
    
    if (primary != OP_B) {
        return false;
    }
    
    // Extract LI field (24-bit signed, word-aligned)
    uint32_t li_field = (instruction & B_LI_MASK) >> B_LI_SHIFT;
    
    // Sign-extend from 24-bit to 32-bit
    if (li_field & 0x800000) {
        decoded->LI = (int32_t)(li_field | 0xFF000000);
    } else {
        decoded->LI = (int32_t)li_field;
    }
    
    decoded->AA = (instruction & B_AA_MASK) != 0;
    decoded->LK = (instruction & B_LK_MASK) != 0;
    
    return true;
}

/**
 * @brief Transpile B instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param current_addr Current instruction address (for relative branches)
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_b(const B_Instruction *decoded,
                              uint32_t current_addr,
                              char *output,
                              size_t output_size) {
    uint32_t target_addr;
    
    if (decoded->AA) {
        // Absolute address (LI is already in bytes)
        target_addr = (uint32_t)decoded->LI;
    } else {
        // Relative to current instruction (multiply by 4 since LI is in words)
        target_addr = current_addr + (decoded->LI * 4);
    }
    
    if (decoded->LK) {
        // Branch and link (function call)
        return snprintf(output, output_size,
                       "lr = 0x%08X; goto L_%08X;",
                       current_addr + 4, target_addr);
    } else {
        // Simple branch (goto)
        return snprintf(output, output_size,
                       "goto L_%08X;",
                       target_addr);
    }
}

/**
 * @brief Generate assembly-like comment for B instruction
 * @param decoded Pointer to decoded instruction
 * @param current_addr Current instruction address
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_b(const B_Instruction *decoded,
                            uint32_t current_addr,
                            char *output,
                            size_t output_size) {
    uint32_t target_addr;
    
    if (decoded->AA) {
        target_addr = (uint32_t)decoded->LI;
    } else {
        target_addr = current_addr + (decoded->LI * 4);
    }
    
    const char *mnemonic = "b";
    if (decoded->AA && decoded->LK) {
        mnemonic = "bla";
    } else if (decoded->AA) {
        mnemonic = "ba";
    } else if (decoded->LK) {
        mnemonic = "bl";
    }
    
    return snprintf(output, output_size,
                   "%s 0x%08X",
                   mnemonic, target_addr);
}

#endif // OPCODE_B_H

