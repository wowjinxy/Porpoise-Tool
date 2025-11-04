/**
 * @file transpiler_example.c
 * @brief Example of using opcode headers for PowerPC to C transpilation
 * 
 * This example demonstrates how to decode PowerPC instructions and
 * generate equivalent C code using the opcode headers.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Include master opcode header (includes all individual opcode headers)
#include "opcode.h"

/**
 * @brief Example transpiler function
 * 
 * Takes PowerPC machine code and generates equivalent C code
 */
void transpile_instruction(uint32_t instruction, uint32_t address) {
    char c_code[512];
    char comment[128];
    
    // Try to decode as different instruction types
    
    // Try ADD
    ADD_Instruction add_decoded;
    if (decode_add(instruction, &add_decoded)) {
        transpile_add(&add_decoded, c_code, sizeof(c_code));
        comment_add(&add_decoded, comment, sizeof(comment));
        printf("0x%08X  %-40s // %s\n", address, c_code, comment);
        return;
    }
    
    // Try ADDI
    ADDI_Instruction addi_decoded;
    if (decode_addi(instruction, &addi_decoded)) {
        transpile_addi(&addi_decoded, c_code, sizeof(c_code));
        comment_addi(&addi_decoded, comment, sizeof(comment));
        printf("0x%08X  %-40s // %s\n", address, c_code, comment);
        return;
    }
    
    // Try STW
    STW_Instruction stw_decoded;
    if (decode_stw(instruction, &stw_decoded)) {
        transpile_stw(&stw_decoded, c_code, sizeof(c_code));
        comment_stw(&stw_decoded, comment, sizeof(comment));
        printf("0x%08X  %-40s // %s\n", address, c_code, comment);
        return;
    }
    
    // Try MFSPR
    MFSPR_Instruction mfspr_decoded;
    if (decode_mfspr(instruction, &mfspr_decoded)) {
        transpile_mfspr(&mfspr_decoded, c_code, sizeof(c_code));
        comment_mfspr(&mfspr_decoded, comment, sizeof(comment));
        printf("0x%08X  %-40s // %s\n", address, c_code, comment);
        return;
    }
    
    // Unknown instruction
    printf("0x%08X  /* UNKNOWN: 0x%08X */\n", address, instruction);
}

/**
 * @brief Transpile a function from the user's example
 */
void transpile_function_example() {
    printf("/**\n");
    printf(" * Function: fn_80428398\n");
    printf(" * Address: 0x80428398\n");
    printf(" * Size: 0x98\n");
    printf(" */\n");
    printf("void fn_80428398() {\n");
    printf("    // Context save routine\n");
    printf("    \n");
    
    // Example instructions from user's assembly
    // Each line: address | instruction code
    struct {
        uint32_t addr;
        uint32_t instruction;
        const char *original_asm;
    } instructions[] = {
        // Address    Instruction   Original Assembly
        {0x804283A0, 0x3863BEC8, "addi r3, r3, lbl_8058BEC8@l"},
        {0x804283AC, 0x9083000C, "stw r4, 0xc(r3)"},
        {0x804283B0, 0x7C90E2A6, "mfspr r4, GQR0"},
        {0x804283B4, 0x908301A4, "stw r4, 0x1a4(r3)"},
        {0x804283F8, 0x7C8802A6, "mflr r4"},
        {0x804283FC, 0x90830084, "stw r4, 0x84(r3)"},
        // Add more as needed...
    };
    
    int num_instructions = sizeof(instructions) / sizeof(instructions[0]);
    
    printf("    // Transpiled code:\n");
    for (int i = 0; i < num_instructions; i++) {
        printf("    ");
        transpile_instruction(instructions[i].instruction, instructions[i].addr);
    }
    
    printf("}\n");
}

/**
 * @brief Demonstrate handling of common patterns
 */
void demonstrate_patterns() {
    printf("\n\n=== Common Pattern Examples ===\n\n");
    
    printf("1. Load Immediate Address (lis + addi):\n");
    printf("   Assembly:\n");
    printf("      lis r3, 0x8059      # r3 = 0x8059 << 16\n");
    printf("      addi r3, r3, 0xBEC8 # r3 = r3 + 0xBEC8\n");
    printf("   C Code:\n");
    printf("      r3 = 0x8059 << 16;\n");
    printf("      r3 = r3 + 0xBEC8;\n");
    printf("   Optimized:\n");
    printf("      r3 = 0x8058BEC8;\n");
    printf("\n");
    
    printf("2. Store to offset:\n");
    printf("   Assembly:\n");
    printf("      stw r4, 0x1a4(r3)\n");
    printf("   C Code:\n");
    printf("      *(uint32_t*)(mem + r3 + 0x1a4) = r4;\n");
    printf("\n");
    
    printf("3. Move from SPR:\n");
    printf("   Assembly:\n");
    printf("      mfspr r4, GQR0\n");
    printf("   C Code:\n");
    printf("      r4 = gqr0;\n");
    printf("\n");
}

int main(void) {
    printf("PowerPC to C Transpiler Example\n");
    printf("================================\n");
    printf("Implemented opcodes: %d / 246 (%.1f%%)\n\n",
           get_implemented_opcode_count(),
           get_implementation_progress());
    
    // Demonstrate transpiling a function
    transpile_function_example();
    
    // Show common patterns
    demonstrate_patterns();
    
    printf("\n=== Individual Instruction Tests ===\n\n");
    
    // Test ADD instruction
    printf("Test 1: add r3, r4, r5\n");
    transpile_instruction(0x7C842A14, 0x80000000);
    
    // Test ADDI instruction
    printf("\nTest 2: addi r3, r3, 0x10\n");
    transpile_instruction(0x38630010, 0x80000004);
    
    // Test LI pseudo-op (addi with rA=0)
    printf("\nTest 3: li r5, 100\n");
    transpile_instruction(0x38A00064, 0x80000008);
    
    // Test STW instruction
    printf("\nTest 4: stw r4, 0x20(r3)\n");
    transpile_instruction(0x90830020, 0x8000000C);
    
    // Test MFSPR (LR)
    printf("\nTest 5: mflr r0\n");
    transpile_instruction(0x7C0802A6, 0x80000010);
    
    // Test MFSPR (GQR0)
    printf("\nTest 6: mfspr r4, GQR0\n");
    transpile_instruction(0x7C90E2A6, 0x80000014);
    
    printf("\n");
    return 0;
}

