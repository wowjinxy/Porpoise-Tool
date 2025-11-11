/**
 * @file lis.h
 * @brief LIS - Load Immediate Shifted (pseudo-op for ADDIS with rA=0)
 * 
 * Opcode: 15
 * Format: D-form
 * Syntax: lis rD, SIMM
 *         (equivalent to: addis rD, r0, SIMM)
 * 
 * Description: Load immediate value shifted left 16 bits into rD
 * This is actually ADDIS with rA=0, but commonly used as "lis"
 */

#ifndef OPCODE_LIS_H
#define OPCODE_LIS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode encoding (same as ADDIS)
#define OP_LIS              15

// Instruction format masks
#define LIS_OPCD_MASK       0xFC000000  // Bits 0-5
#define LIS_RT_MASK         0x03E00000  // Bits 6-10
#define LIS_RA_MASK         0x001F0000  // Bits 11-15
#define LIS_SIMM_MASK       0x0000FFFF  // Bits 16-31 (signed)

// Instruction format shifts
#define LIS_RT_SHIFT        21
#define LIS_RA_SHIFT        16

/**
 * @brief Decoded LIS/ADDIS instruction
 */
typedef struct {
    uint8_t rD;                 // Destination register (0-31)
    uint8_t rA;                 // Source register (0-31, 0 for lis)
    int16_t SIMM;               // Signed immediate value
} LIS_Instruction;

/**
 * @brief Decode LIS/ADDIS instruction
 * @param instruction 32-bit instruction word
 * @param decoded Pointer to decoded instruction structure
 * @return true if successfully decoded, false otherwise
 */
static inline bool decode_lis(uint32_t instruction, LIS_Instruction *decoded) {
    uint32_t primary = (instruction & LIS_OPCD_MASK) >> 26;
    
    if (primary != OP_LIS) {
        return false;
    }
    
    decoded->rD = (instruction & LIS_RT_MASK) >> LIS_RT_SHIFT;
    decoded->rA = (instruction & LIS_RA_MASK) >> LIS_RA_SHIFT;
    decoded->SIMM = (int16_t)(instruction & LIS_SIMM_MASK);
    
    return true;
}

/**
 * @brief Transpile LIS/ADDIS instruction to C code
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write C code to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int transpile_lis(const LIS_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    if (decoded->rA == 0) {
        // lis rD, SIMM (load immediate shifted)
        uint32_t addr = (uint16_t)decoded->SIMM << 16;
        
        // Check if this is a GameCube address (0x80000000-0x84000000 range)
        // If so, translate it to a host pointer immediately
        if (addr >= 0x80000000 && addr < 0x84000000) {
            return snprintf(output, output_size,
                           "r%u = (uintptr_t)translate_address(0x%x << 16);",
                           decoded->rD, (uint16_t)decoded->SIMM);
        } else {
            return snprintf(output, output_size,
                           "r%u = 0x%x << 16;",
                           decoded->rD, (uint16_t)decoded->SIMM);
        }
    } else {
        // addis rD, rA, SIMM
        // Note: We can't translate here because rA might already be a host pointer
        // or might need offset added first. Leave as-is.
        return snprintf(output, output_size,
                       "r%u = r%u + (0x%x << 16);",
                       decoded->rD, decoded->rA, (uint16_t)decoded->SIMM);
    }
}

/**
 * @brief Generate assembly-like comment for LIS/ADDIS instruction
 * @param decoded Pointer to decoded instruction
 * @param output Buffer to write comment to
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int comment_lis(const LIS_Instruction *decoded,
                              char *output,
                              size_t output_size) {
    if (decoded->rA == 0) {
        // Use 'lis' pseudo-op when rA=0
        return snprintf(output, output_size,
                       "lis r%u, 0x%x",
                       decoded->rD, (uint16_t)decoded->SIMM);
    } else {
        // Use 'addis' when rA != 0
        return snprintf(output, output_size,
                       "addis r%u, r%u, 0x%x",
                       decoded->rD, decoded->rA, (uint16_t)decoded->SIMM);
    }
}

#endif // OPCODE_LIS_H

