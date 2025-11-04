/**
 * @file ori.h
 * @brief ORI - OR Immediate
 * 
 * Opcode: 24
 * Format: D-form
 * Syntax: ori rA, rS, UIMM
 *         nop (when rA=rS=0, UIMM=0 - pseudo-op)
 * 
 * Description: OR rS with immediate value and store in rA
 */

#ifndef OPCODE_ORI_H
#define OPCODE_ORI_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding
#define OP_ORI              24

// Instruction format masks
#define ORI_OPCD_MASK       0xFC000000  // Bits 0-5
#define ORI_RS_MASK         0x03E00000  // Bits 6-10
#define ORI_RA_MASK         0x001F0000  // Bits 11-15
#define ORI_UIMM_MASK       0x0000FFFF  // Bits 16-31 (unsigned)

// Instruction format shifts
#define ORI_RS_SHIFT        21
#define ORI_RA_SHIFT        16

/**
 * @brief Decoded ORI instruction
 */
typedef struct {
    uint8_t rA;                 // Destination register (0-31)
    uint8_t rS;                 // Source register (0-31)
    uint16_t UIMM;              // Unsigned immediate value
} ORI_Instruction;

/**
 * @brief Decode ORI instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_ori(uint32_t instruction, ORI_Instruction *decoded) {
    uint32_t primary = (instruction & ORI_OPCD_MASK) >> 26;
    
    if (primary != OP_ORI) {
        return false;
    }
    
    decoded->rS = (instruction & ORI_RS_MASK) >> ORI_RS_SHIFT;
    decoded->rA = (instruction & ORI_RA_MASK) >> ORI_RA_SHIFT;
    decoded->UIMM = instruction & ORI_UIMM_MASK;
    
    return true;
}

/**
 * @brief Transpile ORI instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_ori(const ORI_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    // Check for nop (ori r0, r0, 0)
    if (decoded->rA == 0 && decoded->rS == 0 && decoded->UIMM == 0) {
        return snprintf(output, output_size, ";  /* nop */");
    }
    
    if (decoded->UIMM == 0) {
        // ori rA, rS, 0 is effectively a move (though mr uses or)
        return snprintf(output, output_size,
                       "r%u = r%u;",
                       decoded->rA, decoded->rS);
    } else {
        return snprintf(output, output_size,
                       "r%u = r%u | 0x%x;",
                       decoded->rA, decoded->rS, decoded->UIMM);
    }
}

/**
 * @brief Generate assembly-like comment for ORI instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_ori(const ORI_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    // Check for nop pseudo-op
    if (decoded->rA == 0 && decoded->rS == 0 && decoded->UIMM == 0) {
        return snprintf(output, output_size, "nop");
    }
    
    return snprintf(output, output_size,
                   "ori r%u, r%u, 0x%x",
                   decoded->rA, decoded->rS, decoded->UIMM);
}

#endif // OPCODE_ORI_H

