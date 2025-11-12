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
        uint32_t shifted_value = (uint16_t)decoded->SIMM << 16;
        
        // Check if this is a GameCube address that should be converted
        // MEM1 cached: 0x80000000-0x84000000
        // MEM1 uncached: 0xC0000000-0xC2000000
        // Hardware I/O: 0xCC000000-0xCC010000
        // MEM2 cached: 0x90000000-0x94000000
        // MEM2 uncached: 0xD0000000-0xD4000000
        // Locked cache: 0xE0000000-0xE0010000
        uint32_t offset = 0xFFFFFFFF;
        if (shifted_value >= 0x80000000 && shifted_value < 0x84000000) {
            offset = shifted_value - 0x80000000;
        } else if (shifted_value >= 0xC0000000 && shifted_value < 0xC2000000) {
            offset = shifted_value - 0xC0000000;
        } else if (shifted_value >= 0xCC000000 && shifted_value < 0xCC010000) {
            offset = shifted_value - 0xCC000000 + 0x1800000;  // After MEM1 (24MB), within 256MB buffer
        } else if (shifted_value >= 0x90000000 && shifted_value < 0x94000000) {
            offset = shifted_value - 0x90000000 + 0x1800000;  // After MEM1
        } else if (shifted_value >= 0xD0000000 && shifted_value < 0xD4000000) {
            offset = shifted_value - 0xD0000000 + 0x1800000;  // After MEM1
        } else if (shifted_value >= 0xE0000000 && shifted_value < 0xE0010000) {
            offset = shifted_value - 0xE0000000 + 0x5800000;  // After MEM2
        }
        
        if (offset != 0xFFFFFFFF) {
            // Generate code that directly creates host pointer: mem + offset
            return snprintf(output, output_size,
                           "r%u = (uintptr_t)(mem + 0x%08X);",
                           decoded->rD, offset);
        } else {
            // Not a GameCube address - generate the address literal (transpiler will resolve this)
            return snprintf(output, output_size,
                           "r%u = (uintptr_t)(0x%x << 16);",
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

