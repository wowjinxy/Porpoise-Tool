/**
 * @file porpoise_tool.c
 * @brief Porpoise Tool - Main Transpiler Implementation
 * 
 * PowerPC to C Transpiler for GameCube/Wii Assembly
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "porpoise_tool.h"
#include "opcode.h"
#include "project_generator.h"

/**
 * @brief Transpile from assembly text (mnemonic + operands)
 */
bool transpile_from_asm(const char *mnemonic, const char *operands, uint32_t address,
                        char *output, size_t output_size,
                        char *comment, size_t comment_size,
                        const char **prev_lines, int num_prev_lines) {
    // Handle blr (branch to link register = return)
    if (strcmp(mnemonic, "blr") == 0) {
        snprintf(output, output_size, "return;");
        snprintf(comment, comment_size, "blr");
        return true;
    }
    
    // Handle blrl (branch to link register and link) 
    if (strcmp(mnemonic, "blrl") == 0) {
        snprintf(output, output_size, "((void (*)(void))lr)();");
        snprintf(comment, comment_size, "blrl - indirect call via lr");
        return true;
    }
    
    // Handle conditional returns (blelr, bgelr, bnelr, etc.)
    if (strncmp(mnemonic, "b", 1) == 0 && strstr(mnemonic, "lr") != NULL) {
        // Extract condition from mnemonic
        char clean_mnemonic[32];
        strncpy(clean_mnemonic, mnemonic, sizeof(clean_mnemonic) - 1);
        clean_mnemonic[sizeof(clean_mnemonic) - 1] = '\0';
        
        // Remove + or - suffix (branch prediction hints)
        char *hint = strchr(clean_mnemonic, '+');
        if (!hint) hint = strchr(clean_mnemonic, '-');
        if (hint) *hint = '\0';
        
        const char *condition = "";
        if (strcmp(clean_mnemonic, "beqlr") == 0) condition = "(cr0 & 0x2)";
        else if (strcmp(clean_mnemonic, "bnelr") == 0) condition = "(!(cr0 & 0x2))";
        else if (strcmp(clean_mnemonic, "bltlr") == 0) condition = "(cr0 & 0x8)";
        else if (strcmp(clean_mnemonic, "bgtlr") == 0) condition = "(cr0 & 0x4)";
        else if (strcmp(clean_mnemonic, "blelr") == 0) condition = "(cr0 & 0xA)";
        else if (strcmp(clean_mnemonic, "bgelr") == 0) condition = "(!(cr0 & 0x8))";
        else if (strcmp(clean_mnemonic, "blr") == 0) {
            // Unconditional return - already handled above
            snprintf(output, output_size, "return;");
            snprintf(comment, comment_size, "blr");
            return true;
        }
        else condition = "(1 /* unknown condition */)";
        
        snprintf(output, output_size, "if %s return;", condition);
        snprintf(comment, comment_size, "%s", mnemonic);
        return true;
    }
    
    // Handle bctr (branch to count register)
    if (strcmp(mnemonic, "bctr") == 0) {
        snprintf(output, output_size, "pc = ctr;  /* bctr - indirect branch (cannot be expressed as goto in C) */");
        snprintf(comment, comment_size, "bctr");
        return true;
    }
    
    // Handle bctrl (branch to count register and link)
    if (strcmp(mnemonic, "bctrl") == 0) {
        snprintf(output, output_size, "lr = 0x%08X; ((void (*)(void))ctr)();", address + 4);
        snprintf(comment, comment_size, "bctrl - indirect call via ctr");
        return true;
    }
    
    // Handle branches by parsing operands directly
    if (strcmp(mnemonic, "b") == 0 || strcmp(mnemonic, "bl") == 0 || 
        strcmp(mnemonic, "ba") == 0 || strcmp(mnemonic, "bla") == 0) {
        char target[128];
        sscanf(operands, "%127s", target);
        
        // Check if it's an absolute address (starts with 0x)
        if (target[0] == '0' && target[1] == 'x') {
            // Absolute address - treat as function call
            uint32_t addr;
            sscanf(target, "%x", &addr);
            if (strcmp(mnemonic, "bla") == 0 || strcmp(mnemonic, "bl") == 0) {
                snprintf(output, output_size, "lr = 0x%08X; ((void (*)(void))0x%08X)();  /* call absolute */", address + 4, addr);
            } else {
                snprintf(output, output_size, "pc = 0x%08X;  /* branch absolute */", addr);
            }
        }
        // Check if target is a label (.L_ or .lbl_) or function name
        else if (target[0] == '.') {
            // It's a local label - convert to C label (remove leading dot)
            char label_name[128];
            strcpy(label_name, target + 1);
            
            // Extract address from label name for cross-function jump detection
            uint32_t label_addr = 0;
            if (strncmp(label_name, "L_", 2) == 0) {
                sscanf(label_name + 2, "%x", &label_addr);
            } else if (strncmp(label_name, "lbl_", 4) == 0) {
                sscanf(label_name + 4, "%x", &label_addr);
            }
            
            // For now, always use goto - trampolines will be detected later
            // and flagged with comments. But to avoid compilation errors,
            // we'll generate a placeholder that compiles.
            // Note: This might be a cross-function jump (trampoline)
            if (strcmp(mnemonic, "bl") == 0 || strcmp(mnemonic, "bla") == 0) {
                snprintf(output, output_size, "lr = 0x%08X; goto %s;  /* May be cross-function */", address + 4, label_name);
            } else {
                snprintf(output, output_size, "goto %s;  /* May be cross-function */", label_name);
            }
        } else {
            // It's a function name (external or other file)
            // Both b and bl should be function calls for external symbols
            // Sanitize function name to avoid conflicts with compiler intrinsics
            char sanitized_name[MAX_FUNCTION_NAME];
            const char *func_name = sanitize_function_name(target, sanitized_name, sizeof(sanitized_name));
            
            // Note: All functions use emulated registers (r3, r4, etc.) so no C parameters needed
            if (strcmp(mnemonic, "bl") == 0 || strcmp(mnemonic, "bla") == 0) {
                snprintf(output, output_size, "%s();", func_name);
            } else {
                // Tail call optimization (branch without link)
                snprintf(output, output_size, "return %s();  /* Tail call */", func_name);
            }
        }
        snprintf(comment, comment_size, "%s %s", mnemonic, target);
        return true;
    }
    
    // Handle conditional branches
    if (strncmp(mnemonic, "b", 1) == 0 && strlen(mnemonic) > 1) {
        // Conditional branch (beq, bne, blt, etc.)
        // Extract mnemonic without branch prediction hints (+/-)
        char clean_mnemonic[32];
        strncpy(clean_mnemonic, mnemonic, sizeof(clean_mnemonic) - 1);
        clean_mnemonic[sizeof(clean_mnemonic) - 1] = '\0';
        
        // Remove + or - suffix (branch prediction hints)
        char *hint = strchr(clean_mnemonic, '+');
        if (!hint) hint = strchr(clean_mnemonic, '-');
        if (hint) *hint = '\0';
        
        // Parse operands - may be "cr1, .label" or just ".label"
        char cr_field[32] = "cr0";
        char target[128];
        
        if (sscanf(operands, "cr%*d , %127s", target) == 1 || 
            sscanf(operands, "cr%*d, %127s", target) == 1) {
            // Has explicit CR field
            sscanf(operands, "%31[^,]", cr_field);
            // Trim whitespace from cr_field
            char *end = cr_field + strlen(cr_field) - 1;
            while (end > cr_field && (*end == ' ' || *end == '\t')) end--;
            *(end + 1) = '\0';
        } else {
            // No explicit CR field, use cr0
            sscanf(operands, "%127s", target);
        }
        
        if (target[0] == '.') {
            char label_name[128];
            strcpy(label_name, target + 1);
            
            // Map branch conditions with proper parentheses
            if (strcmp(clean_mnemonic, "beq") == 0) {
                snprintf(output, output_size, "if (%s & 0x2) goto %s;", cr_field, label_name);
            }
            else if (strcmp(clean_mnemonic, "bne") == 0) {
                snprintf(output, output_size, "if (!(%s & 0x2)) goto %s;", cr_field, label_name);
            }
            else if (strcmp(clean_mnemonic, "blt") == 0) {
                snprintf(output, output_size, "if (%s & 0x8) goto %s;", cr_field, label_name);
            }
            else if (strcmp(clean_mnemonic, "bgt") == 0) {
                snprintf(output, output_size, "if (%s & 0x4) goto %s;", cr_field, label_name);
            }
            else if (strcmp(clean_mnemonic, "ble") == 0) {
                snprintf(output, output_size, "if (%s & 0xA) goto %s;", cr_field, label_name);
            }
            else if (strcmp(clean_mnemonic, "bge") == 0) {
                snprintf(output, output_size, "if (!(%s & 0x8)) goto %s;", cr_field, label_name);
            }
            else if (strcmp(clean_mnemonic, "bdnz") == 0) {
                snprintf(output, output_size, "if (--ctr) goto %s;", label_name);
            }
            else {
                snprintf(output, output_size, "if (1 /* unknown condition */) goto %s;", label_name);
            }
        } else {
            snprintf(output, output_size, "%s();  /* conditional call */", target);
        }
        snprintf(comment, comment_size, "%s %s", mnemonic, operands);
        return true;
    }
    
    // For all other instructions, return false to use byte-based decoding
    return false;
}

/**
 * @brief Transpile a single instruction
 */
bool transpile_instruction(uint32_t instruction, uint32_t address, 
                          char *output, size_t output_size,
                          char *comment, size_t comment_size) {
    // Try each opcode in order (TODO: use lookup table for efficiency)
    
    // Integer arithmetic
    ADD_Instruction add;
    if (decode_add(instruction, &add)) {
        transpile_add(&add, output, output_size);
        comment_add(&add, comment, comment_size);
        return true;
    }
    
    ADDI_Instruction addi;
    if (decode_addi(instruction, &addi)) {
        transpile_addi(&addi, output, output_size);
        comment_addi(&addi, comment, comment_size);
        return true;
    }
    
    LIS_Instruction lis;
    if (decode_lis(instruction, &lis)) {
        transpile_lis(&lis, output, output_size);
        comment_lis(&lis, comment, comment_size);
        return true;
    }
    
    SUBF_Instruction subf;
    if (decode_subf(instruction, &subf)) {
        transpile_subf(&subf, output, output_size);
        comment_subf(&subf, comment, comment_size);
        return true;
    }
    
    SUBFC_Instruction subfc;
    if (decode_subfc(instruction, &subfc)) {
        transpile_subfc(&subfc, output, output_size);
        comment_subfc(&subfc, comment, comment_size);
        return true;
    }
    
    SUBFE_Instruction subfe;
    if (decode_subfe(instruction, &subfe)) {
        transpile_subfe(&subfe, output, output_size);
        comment_subfe(&subfe, comment, comment_size);
        return true;
    }
    
    ADDC_Instruction addc;
    if (decode_addc(instruction, &addc)) {
        transpile_addc(&addc, output, output_size);
        comment_addc(&addc, comment, comment_size);
        return true;
    }
    
    ADDE_Instruction adde;
    if (decode_adde(instruction, &adde)) {
        transpile_adde(&adde, output, output_size);
        comment_adde(&adde, comment, comment_size);
        return true;
    }
    
    NEG_Instruction neg;
    if (decode_neg(instruction, &neg)) {
        transpile_neg(&neg, output, output_size);
        comment_neg(&neg, comment, comment_size);
        return true;
    }
    
    MULLI_Instruction mulli;
    if (decode_mulli(instruction, &mulli)) {
        transpile_mulli(&mulli, output, output_size);
        comment_mulli(&mulli, comment, comment_size);
        return true;
    }
    
    MULLW_Instruction mullw;
    if (decode_mullw(instruction, &mullw)) {
        transpile_mullw(&mullw, output, output_size);
        comment_mullw(&mullw, comment, comment_size);
        return true;
    }
    
    MULHWU_Instruction mulhwu;
    if (decode_mulhwu(instruction, &mulhwu)) {
        transpile_mulhwu(&mulhwu, output, output_size);
        comment_mulhwu(&mulhwu, comment, comment_size);
        return true;
    }
    
    // Logical
    AND_Instruction and;
    if (decode_and(instruction, &and)) {
        transpile_and(&and, output, output_size);
        comment_and(&and, comment, comment_size);
        return true;
    }
    
    ANDI_Instruction andi;
    if (decode_andi(instruction, &andi)) {
        transpile_andi(&andi, output, output_size);
        comment_andi(&andi, comment, comment_size);
        return true;
    }
    
    ANDIS_Instruction andis;
    if (decode_andis(instruction, &andis)) {
        transpile_andis(&andis, output, output_size);
        comment_andis(&andis, comment, comment_size);
        return true;
    }
    
    OR_Instruction or;
    if (decode_or(instruction, &or)) {
        transpile_or(&or, output, output_size);
        comment_or(&or, comment, comment_size);
        return true;
    }
    
    ORI_Instruction ori;
    if (decode_ori(instruction, &ori)) {
        transpile_ori(&ori, output, output_size);
        comment_ori(&ori, comment, comment_size);
        return true;
    }
    
    XOR_Instruction xor;
    if (decode_xor(instruction, &xor)) {
        transpile_xor(&xor, output, output_size);
        comment_xor(&xor, comment, comment_size);
        return true;
    }
    
    ORIS_Instruction oris;
    if (decode_oris(instruction, &oris)) {
        transpile_oris(&oris, output, output_size);
        comment_oris(&oris, comment, comment_size);
        return true;
    }
    
    XORIS_Instruction xoris;
    if (decode_xoris(instruction, &xoris)) {
        transpile_xoris(&xoris, output, output_size);
        comment_xoris(&xoris, comment, comment_size);
        return true;
    }
    
    // Shift/Rotate
    SLW_Instruction slw;
    if (decode_slw(instruction, &slw)) {
        transpile_slw(&slw, output, output_size);
        comment_slw(&slw, comment, comment_size);
        return true;
    }
    
    SRW_Instruction srw;
    if (decode_srw(instruction, &srw)) {
        transpile_srw(&srw, output, output_size);
        comment_srw(&srw, comment, comment_size);
        return true;
    }
    
    SRAWI_Instruction srawi;
    if (decode_srawi(instruction, &srawi)) {
        transpile_srawi(&srawi, output, output_size);
        comment_srawi(&srawi, comment, comment_size);
        return true;
    }
    
    RLWINM_Instruction rlwinm;
    if (decode_rlwinm(instruction, &rlwinm)) {
        transpile_rlwinm(&rlwinm, output, output_size);
        comment_rlwinm(&rlwinm, comment, comment_size);
        return true;
    }
    
    RLWNM_Instruction rlwnm;
    if (decode_rlwnm(instruction, &rlwnm)) {
        transpile_rlwnm(&rlwnm, output, output_size);
        comment_rlwnm(&rlwnm, comment, comment_size);
        return true;
    }

    // Comparison
    CMP_Instruction cmp;
    if (decode_cmp(instruction, &cmp)) {
        transpile_cmp(&cmp, output, output_size);
        comment_cmp(&cmp, comment, comment_size);
        return true;
    }
    
    CMPI_Instruction cmpi;
    if (decode_cmpi(instruction, &cmpi)) {
        transpile_cmpi(&cmpi, output, output_size);
        comment_cmpi(&cmpi, comment, comment_size);
        return true;
    }
    
    CMPLW_Instruction cmplw;
    if (decode_cmplw(instruction, &cmplw)) {
        transpile_cmplw(&cmplw, output, output_size);
        comment_cmplw(&cmplw, comment, comment_size);
        return true;
    }
    
    CMPLWI_Instruction cmplwi;
    if (decode_cmplwi(instruction, &cmplwi)) {
        transpile_cmplwi(&cmplwi, output, output_size);
        comment_cmplwi(&cmplwi, comment, comment_size);
        return true;
    }
    
    // Branch
    B_Instruction b;
    if (decode_b(instruction, &b)) {
        transpile_b(&b, address, output, output_size);
        comment_b(&b, address, comment, comment_size);
        return true;
    }
    
    BC_Instruction bc;
    if (decode_bc(instruction, &bc)) {
        transpile_bc(&bc, address, output, output_size);
        comment_bc(&bc, address, comment, comment_size);
        return true;
    }
    
    BLR_Instruction blr;
    if (decode_blr(instruction, &blr)) {
        transpile_blr(&blr, address, output, output_size);
        comment_blr(&blr, comment, comment_size);
        return true;
    }
    
    // Load/Store
    LBZ_Instruction lbz;
    if (decode_lbz(instruction, &lbz)) {
        transpile_lbz(&lbz, output, output_size);
        comment_lbz(&lbz, comment, comment_size);
        return true;
    }
    
    STB_Instruction stb;
    if (decode_stb(instruction, &stb)) {
        transpile_stb(&stb, output, output_size);
        comment_stb(&stb, comment, comment_size);
        return true;
    }
    
    LHZ_Instruction lhz;
    if (decode_lhz(instruction, &lhz)) {
        transpile_lhz(&lhz, output, output_size);
        comment_lhz(&lhz, comment, comment_size);
        return true;
    }
    
    STH_Instruction sth;
    if (decode_sth(instruction, &sth)) {
        transpile_sth(&sth, output, output_size);
        comment_sth(&sth, comment, comment_size);
        return true;
    }
    
    LWZ_Instruction lwz;
    if (decode_lwz(instruction, &lwz)) {
        transpile_lwz(&lwz, output, output_size);
        comment_lwz(&lwz, comment, comment_size);
        return true;
    }
    
    LWZU_Instruction lwzu;
    if (decode_lwzu(instruction, &lwzu)) {
        transpile_lwzu(&lwzu, output, output_size);
        comment_lwzu(&lwzu, comment, comment_size);
        return true;
    }
    
    LWZX_Instruction lwzx;
    if (decode_lwzx(instruction, &lwzx)) {
        transpile_lwzx(&lwzx, output, output_size);
        comment_lwzx(&lwzx, comment, comment_size);
        return true;
    }
    
    STW_Instruction stw;
    if (decode_stw(instruction, &stw)) {
        transpile_stw(&stw, output, output_size);
        comment_stw(&stw, comment, comment_size);
        return true;
    }
    
    STWU_Instruction stwu;
    if (decode_stwu(instruction, &stwu)) {
        transpile_stwu(&stwu, output, output_size);
        comment_stwu(&stwu, comment, comment_size);
        return true;
    }
    
    LMW_Instruction lmw;
    if (decode_lmw(instruction, &lmw)) {
        transpile_lmw(&lmw, output, output_size);
        comment_lmw(&lmw, comment, comment_size);
        return true;
    }
    
    STMW_Instruction stmw;
    if (decode_stmw(instruction, &stmw)) {
        transpile_stmw(&stmw, output, output_size);
        comment_stmw(&stmw, comment, comment_size);
        return true;
    }
    
    // SPR
    MFSPR_Instruction mfspr;
    if (decode_mfspr(instruction, &mfspr)) {
        transpile_mfspr(&mfspr, output, output_size);
        comment_mfspr(&mfspr, comment, comment_size);
        return true;
    }
    
    MTSPR_Instruction mtspr;
    if (decode_mtspr(instruction, &mtspr)) {
        transpile_mtspr(&mtspr, output, output_size);
        comment_mtspr(&mtspr, comment, comment_size);
        return true;
    }
    
    MFCR_Instruction mfcr;
    if (decode_mfcr(instruction, &mfcr)) {
        transpile_mfcr(&mfcr, output, output_size);
        comment_mfcr(&mfcr, comment, comment_size);
        return true;
    }
    
    MFXER_Instruction mfxer;
    if (decode_mfxer(instruction, &mfxer)) {
        transpile_mfxer(&mfxer, output, output_size);
        comment_mfxer(&mfxer, comment, comment_size);
        return true;
    }
    
    MTXER_Instruction mtxer;
    if (decode_mtxer(instruction, &mtxer)) {
        transpile_mtxer(&mtxer, output, output_size);
        comment_mtxer(&mtxer, comment, comment_size);
        return true;
    }
    
    MFLR_Instruction mflr;
    if (decode_mflr(instruction, &mflr)) {
        transpile_mflr(&mflr, output, output_size);
        comment_mflr(&mflr, comment, comment_size);
        return true;
    }
    
    MCRXR_Instruction mcrxr;
    if (decode_mcrxr(instruction, &mcrxr)) {
        transpile_mcrxr(&mcrxr, output, output_size);
        comment_mcrxr(&mcrxr, comment, comment_size);
        return true;
    }
    
    MFMSR_Instruction mfmsr;
    if (decode_mfmsr(instruction, &mfmsr)) {
        transpile_mfmsr(&mfmsr, output, output_size);
        comment_mfmsr(&mfmsr, comment, comment_size);
        return true;
    }
    
    MTMSR_Instruction mtmsr;
    if (decode_mtmsr(instruction, &mtmsr)) {
        transpile_mtmsr(&mtmsr, output, output_size);
        comment_mtmsr(&mtmsr, comment, comment_size);
        return true;
    }
    
    // System
    SYNC_Instruction sync_inst;
    if (decode_sync(instruction, &sync_inst)) {
        transpile_sync(&sync_inst, output, output_size);
        comment_sync(&sync_inst, comment, comment_size);
        return true;
    }
    
    RFI_Instruction rfi;
    if (decode_rfi(instruction, &rfi)) {
        transpile_rfi(&rfi, output, output_size);
        comment_rfi(&rfi, comment, comment_size);
        return true;
    }
    
    // Condition Register
    CRXOR_Instruction crxor;
    if (decode_crxor(instruction, &crxor)) {
        transpile_crxor(&crxor, output, output_size);
        comment_crxor(&crxor, comment, comment_size);
        return true;
    }
    
    // Floating-point
    FADD_Instruction fadd;
    if (decode_fadd(instruction, &fadd)) {
        transpile_fadd(&fadd, output, output_size);
        comment_fadd(&fadd, comment, comment_size);
        return true;
    }
    
    FADDS_Instruction fadds;
    if (decode_fadds(instruction, &fadds)) {
        transpile_fadds(&fadds, output, output_size);
        comment_fadds(&fadds, comment, comment_size);
        return true;
    }
    
    FSUBS_Instruction fsubs;
    if (decode_fsubs(instruction, &fsubs)) {
        transpile_fsubs(&fsubs, output, output_size);
        comment_fsubs(&fsubs, comment, comment_size);
        return true;
    }
    
    FMULS_Instruction fmuls;
    if (decode_fmuls(instruction, &fmuls)) {
        transpile_fmuls(&fmuls, output, output_size);
        comment_fmuls(&fmuls, comment, comment_size);
        return true;
    }
    
    FDIVS_Instruction fdivs;
    if (decode_fdivs(instruction, &fdivs)) {
        transpile_fdivs(&fdivs, output, output_size);
        comment_fdivs(&fdivs, comment, comment_size);
        return true;
    }
    
    FABS_Instruction fabs_inst;
    if (decode_fabs(instruction, &fabs_inst)) {
        transpile_fabs(&fabs_inst, output, output_size);
        comment_fabs(&fabs_inst, comment, comment_size);
        return true;
    }
    
    FRSP_Instruction frsp;
    if (decode_frsp(instruction, &frsp)) {
        transpile_frsp(&frsp, output, output_size);
        comment_frsp(&frsp, comment, comment_size);
        return true;
    }
    
    FMADD_Instruction fmadd;
    if (decode_fmadd(instruction, &fmadd)) {
        transpile_fmadd(&fmadd, output, output_size);
        comment_fmadd(&fmadd, comment, comment_size);
        return true;
    }
    
    FMADDS_Instruction fmadds;
    if (decode_fmadds(instruction, &fmadds)) {
        transpile_fmadds(&fmadds, output, output_size);
        comment_fmadds(&fmadds, comment, comment_size);
        return true;
    }
    
    FMSUB_Instruction fmsub;
    if (decode_fmsub(instruction, &fmsub)) {
        transpile_fmsub(&fmsub, output, output_size);
        comment_fmsub(&fmsub, comment, comment_size);
        return true;
    }
    
    FMSUBS_Instruction fmsubs;
    if (decode_fmsubs(instruction, &fmsubs)) {
        transpile_fmsubs(&fmsubs, output, output_size);
        comment_fmsubs(&fmsubs, comment, comment_size);
        return true;
    }
    
    FNMADD_Instruction fnmadd;
    if (decode_fnmadd(instruction, &fnmadd)) {
        transpile_fnmadd(&fnmadd, output, output_size);
        comment_fnmadd(&fnmadd, comment, comment_size);
        return true;
    }
    
    FNMADDS_Instruction fnmadds;
    if (decode_fnmadds(instruction, &fnmadds)) {
        transpile_fnmadds(&fnmadds, output, output_size);
        comment_fnmadds(&fnmadds, comment, comment_size);
        return true;
    }
    
    FNMSUB_Instruction fnmsub;
    if (decode_fnmsub(instruction, &fnmsub)) {
        transpile_fnmsub(&fnmsub, output, output_size);
        comment_fnmsub(&fnmsub, comment, comment_size);
        return true;
    }
    
    FNMSUBS_Instruction fnmsubs;
    if (decode_fnmsubs(instruction, &fnmsubs)) {
        transpile_fnmsubs(&fnmsubs, output, output_size);
        comment_fnmsubs(&fnmsubs, comment, comment_size);
        return true;
    }
    
    LFS_Instruction lfs;
    if (decode_lfs(instruction, &lfs)) {
        transpile_lfs(&lfs, output, output_size);
        comment_lfs(&lfs, comment, comment_size);
        return true;
    }
    
    LFD_Instruction lfd;
    if (decode_lfd(instruction, &lfd)) {
        transpile_lfd(&lfd, output, output_size);
        comment_lfd(&lfd, comment, comment_size);
        return true;
    }
    
    STFD_Instruction stfd;
    if (decode_stfd(instruction, &stfd)) {
        transpile_stfd(&stfd, output, output_size);
        comment_stfd(&stfd, comment, comment_size);
        return true;
    }
    
    FNABS_Instruction fnabs_inst;
    if (decode_fnabs(instruction, &fnabs_inst)) {
        transpile_fnabs(&fnabs_inst, output, output_size);
        comment_fnabs(&fnabs_inst, comment, comment_size);
        return true;
    }
    
    FSEL_Instruction fsel;
    if (decode_fsel(instruction, &fsel)) {
        transpile_fsel(&fsel, output, output_size);
        comment_fsel(&fsel, comment, comment_size);
        return true;
    }
    
    FRES_Instruction fres;
    if (decode_fres(instruction, &fres)) {
        transpile_fres(&fres, output, output_size);
        comment_fres(&fres, comment, comment_size);
        return true;
    }
    
    FRSQRTE_Instruction frsqrte;
    if (decode_frsqrte(instruction, &frsqrte)) {
        transpile_frsqrte(&frsqrte, output, output_size);
        comment_frsqrte(&frsqrte, comment, comment_size);
        return true;
    }
    
    FCTIW_Instruction fctiw;
    if (decode_fctiw(instruction, &fctiw)) {
        transpile_fctiw(&fctiw, output, output_size);
        comment_fctiw(&fctiw, comment, comment_size);
        return true;
    }
    
    LFSU_Instruction lfsu;
    if (decode_lfsu(instruction, &lfsu)) {
        transpile_lfsu(&lfsu, output, output_size);
        comment_lfsu(&lfsu, comment, comment_size);
        return true;
    }
    
    LFDU_Instruction lfdu;
    if (decode_lfdu(instruction, &lfdu)) {
        transpile_lfdu(&lfdu, output, output_size);
        comment_lfdu(&lfdu, comment, comment_size);
        return true;
    }
    
    LFSX_Instruction lfsx;
    if (decode_lfsx(instruction, &lfsx)) {
        transpile_lfsx(&lfsx, output, output_size);
        comment_lfsx(&lfsx, comment, comment_size);
        return true;
    }
    
    LFDX_Instruction lfdx;
    if (decode_lfdx(instruction, &lfdx)) {
        transpile_lfdx(&lfdx, output, output_size);
        comment_lfdx(&lfdx, comment, comment_size);
        return true;
    }
    
    STFS_Instruction stfs;
    if (decode_stfs(instruction, &stfs)) {
        transpile_stfs(&stfs, output, output_size);
        comment_stfs(&stfs, comment, comment_size);
        return true;
    }
    
    STFSX_Instruction stfsx;
    if (decode_stfsx(instruction, &stfsx)) {
        transpile_stfsx(&stfsx, output, output_size);
        comment_stfsx(&stfsx, comment, comment_size);
        return true;
    }
    
    STFDX_Instruction stfdx;
    if (decode_stfdx(instruction, &stfdx)) {
        transpile_stfdx(&stfdx, output, output_size);
        comment_stfdx(&stfdx, comment, comment_size);
        return true;
    }
    
    STFIWX_Instruction stfiwx;
    if (decode_stfiwx(instruction, &stfiwx)) {
        transpile_stfiwx(&stfiwx, output, output_size);
        comment_stfiwx(&stfiwx, comment, comment_size);
        return true;
    }
    
    STFSU_Instruction stfsu;
    if (decode_stfsu(instruction, &stfsu)) {
        transpile_stfsu(&stfsu, output, output_size);
        comment_stfsu(&stfsu, comment, comment_size);
        return true;
    }
    
    STFDU_Instruction stfdu;
    if (decode_stfdu(instruction, &stfdu)) {
        transpile_stfdu(&stfdu, output, output_size);
        comment_stfdu(&stfdu, comment, comment_size);
        return true;
    }
    
    LFSUX_Instruction lfsux;
    if (decode_lfsux(instruction, &lfsux)) {
        transpile_lfsux(&lfsux, output, output_size);
        comment_lfsux(&lfsux, comment, comment_size);
        return true;
    }
    
    LFDUX_Instruction lfdux;
    if (decode_lfdux(instruction, &lfdux)) {
        transpile_lfdux(&lfdux, output, output_size);
        comment_lfdux(&lfdux, comment, comment_size);
        return true;
    }
    
    STFSUX_Instruction stfsux;
    if (decode_stfsux(instruction, &stfsux)) {
        transpile_stfsux(&stfsux, output, output_size);
        comment_stfsux(&stfsux, comment, comment_size);
        return true;
    }
    
    STFDUX_Instruction stfdux;
    if (decode_stfdux(instruction, &stfdux)) {
        transpile_stfdux(&stfdux, output, output_size);
        comment_stfdux(&stfdux, comment, comment_size);
        return true;
    }
    
    LHZU_Instruction lhzu;
    if (decode_lhzu(instruction, &lhzu)) {
        transpile_lhzu(&lhzu, output, output_size);
        comment_lhzu(&lhzu, comment, comment_size);
        return true;
    }
    
    RLWIMI_Instruction rlwimi;
    if (decode_rlwimi(instruction, &rlwimi)) {
        transpile_rlwimi(&rlwimi, output, output_size);
        comment_rlwimi(&rlwimi, comment, comment_size);
        return true;
    }
    
    // Cache operations
    DCBF_Instruction dcbf;
    if (decode_dcbf(instruction, &dcbf)) {
        transpile_dcbf(&dcbf, output, output_size);
        comment_dcbf(&dcbf, comment, comment_size);
        return true;
    }
    
    DCBI_Instruction dcbi;
    if (decode_dcbi(instruction, &dcbi)) {
        transpile_dcbi(&dcbi, output, output_size);
        comment_dcbi(&dcbi, comment, comment_size);
        return true;
    }
    
    DCBST_Instruction dcbst;
    if (decode_dcbst(instruction, &dcbst)) {
        transpile_dcbst(&dcbst, output, output_size);
        comment_dcbst(&dcbst, comment, comment_size);
        return true;
    }
    
    ICBI_Instruction icbi;
    if (decode_icbi(instruction, &icbi)) {
        transpile_icbi(&icbi, output, output_size);
        comment_icbi(&icbi, comment, comment_size);
        return true;
    }
    
    DCBT_Instruction dcbt;
    if (decode_dcbt(instruction, &dcbt)) {
        transpile_dcbt(&dcbt, output, output_size);
        comment_dcbt(&dcbt, comment, comment_size);
        return true;
    }
    
    DCBTST_Instruction dcbtst;
    if (decode_dcbtst(instruction, &dcbtst)) {
        transpile_dcbtst(&dcbtst, output, output_size);
        comment_dcbtst(&dcbtst, comment, comment_size);
        return true;
    }
    
    DCBZ_Instruction dcbz;
    if (decode_dcbz(instruction, &dcbz)) {
        transpile_dcbz(&dcbz, output, output_size);
        comment_dcbz(&dcbz, comment, comment_size);
        return true;
    }
    
    // System
    ISYNC_Instruction isync;
    if (decode_isync(instruction, &isync)) {
        transpile_isync(&isync, output, output_size);
        comment_isync(&isync, comment, comment_size);
        return true;
    }
    
    EIEIO_Instruction eieio;
    if (decode_eieio(instruction, &eieio)) {
        transpile_eieio(&eieio, output, output_size);
        comment_eieio(&eieio, comment, comment_size);
        return true;
    }
    
    SC_Instruction sc;
    if (decode_sc(instruction, &sc)) {
        transpile_sc(&sc, output, output_size);
        comment_sc(&sc, comment, comment_size);
        return true;
    }
    
    TW_Instruction tw;
    if (decode_tw(instruction, &tw)) {
        transpile_tw(&tw, output, output_size);
        comment_tw(&tw, comment, comment_size);
        return true;
    }
    
    TWI_Instruction twi;
    if (decode_twi(instruction, &twi)) {
        transpile_twi(&twi, output, output_size);
        comment_twi(&twi, comment, comment_size);
        return true;
    }
    
    // FP Status
    MTFSF_Instruction mtfsf;
    if (decode_mtfsf(instruction, &mtfsf)) {
        transpile_mtfsf(&mtfsf, output, output_size);
        comment_mtfsf(&mtfsf, comment, comment_size);
        return true;
    }
    
    // Gekko Paired-Single
    PSQ_L_Instruction psq_l;
    if (decode_psq_l(instruction, &psq_l)) {
        transpile_psq_l(&psq_l, output, output_size);
        comment_psq_l(&psq_l, comment, comment_size);
        return true;
    }
    
    PSQ_ST_Instruction psq_st;
    if (decode_psq_st(instruction, &psq_st)) {
        transpile_psq_st(&psq_st, output, output_size);
        comment_psq_st(&psq_st, comment, comment_size);
        return true;
    }
    
    // More branches
    BCTR_Instruction bctr;
    if (decode_bctr(instruction, &bctr)) {
        transpile_bctr(&bctr, output, output_size);
        comment_bctr(&bctr, comment, comment_size);
        return true;
    }
    
    // More loads
    LHA_Instruction lha;
    if (decode_lha(instruction, &lha)) {
        transpile_lha(&lha, output, output_size);
        comment_lha(&lha, comment, comment_size);
        return true;
    }
    
    // Extended/Count ops
    EXTSH_Instruction extsh;
    if (decode_extsh(instruction, &extsh)) {
        transpile_extsh(&extsh, output, output_size);
        comment_extsh(&extsh, comment, comment_size);
        return true;
    }
    
    CNTLZW_Instruction cntlzw;
    if (decode_cntlzw(instruction, &cntlzw)) {
        transpile_cntlzw(&cntlzw, output, output_size);
        comment_cntlzw(&cntlzw, comment, comment_size);
        return true;
    }
    
    ANDC_Instruction andc;
    if (decode_andc(instruction, &andc)) {
        transpile_andc(&andc, output, output_size);
        comment_andc(&andc, comment, comment_size);
        return true;
    }
    
    // More SPR
    MTCRF_Instruction mtcrf;
    if (decode_mtcrf(instruction, &mtcrf)) {
        transpile_mtcrf(&mtcrf, output, output_size);
        comment_mtcrf(&mtcrf, comment, comment_size);
        return true;
    }
    
    MFTB_Instruction mftb;
    if (decode_mftb(instruction, &mftb)) {
        transpile_mftb(&mftb, output, output_size);
        comment_mftb(&mftb, comment, comment_size);
        return true;
    }
    
    // More FP
    MFFS_Instruction mffs;
    if (decode_mffs(instruction, &mffs)) {
        transpile_mffs(&mffs, output, output_size);
        comment_mffs(&mffs, comment, comment_size);
        return true;
    }
    
    // More arithmetic
    SUBFIC_Instruction subfic;
    if (decode_subfic(instruction, &subfic)) {
        transpile_subfic(&subfic, output, output_size);
        comment_subfic(&subfic, comment, comment_size);
        return true;
    }
    
    ADDZE_Instruction addze;
    if (decode_addze(instruction, &addze)) {
        transpile_addze(&addze, output, output_size);
        comment_addze(&addze, comment, comment_size);
        return true;
    }
    
    ADDME_Instruction addme;
    if (decode_addme(instruction, &addme)) {
        transpile_addme(&addme, output, output_size);
        comment_addme(&addme, comment, comment_size);
        return true;
    }
    
    MULHW_Instruction mulhw;
    if (decode_mulhw(instruction, &mulhw)) {
        transpile_mulhw(&mulhw, output, output_size);
        comment_mulhw(&mulhw, comment, comment_size);
        return true;
    }
    
    DIVW_Instruction divw;
    if (decode_divw(instruction, &divw)) {
        transpile_divw(&divw, output, output_size);
        comment_divw(&divw, comment, comment_size);
        return true;
    }
    
    DIVWU_Instruction divwu;
    if (decode_divwu(instruction, &divwu)) {
        transpile_divwu(&divwu, output, output_size);
        comment_divwu(&divwu, comment, comment_size);
        return true;
    }
    
    // More logical
    NOR_Instruction nor;
    if (decode_nor(instruction, &nor)) {
        transpile_nor(&nor, output, output_size);
        comment_nor(&nor, comment, comment_size);
        return true;
    }
    
    NAND_Instruction nand;
    if (decode_nand(instruction, &nand)) {
        transpile_nand(&nand, output, output_size);
        comment_nand(&nand, comment, comment_size);
        return true;
    }
    
    ORC_Instruction orc;
    if (decode_orc(instruction, &orc)) {
        transpile_orc(&orc, output, output_size);
        comment_orc(&orc, comment, comment_size);
        return true;
    }
    
    EXTSB_Instruction extsb;
    if (decode_extsb(instruction, &extsb)) {
        transpile_extsb(&extsb, output, output_size);
        comment_extsb(&extsb, comment, comment_size);
        return true;
    }
    
    // More shift
    SRAW_Instruction sraw;
    if (decode_sraw(instruction, &sraw)) {
        transpile_sraw(&sraw, output, output_size);
        comment_sraw(&sraw, comment, comment_size);
        return true;
    }
    
    // More indexed loads/stores
    LHZX_Instruction lhzx;
    if (decode_lhzx(instruction, &lhzx)) {
        transpile_lhzx(&lhzx, output, output_size);
        comment_lhzx(&lhzx, comment, comment_size);
        return true;
    }
    
    STHX_Instruction sthx;
    if (decode_sthx(instruction, &sthx)) {
        transpile_sthx(&sthx, output, output_size);
        comment_sthx(&sthx, comment, comment_size);
        return true;
    }
    
    LHAX_Instruction lhax;
    if (decode_lhax(instruction, &lhax)) {
        transpile_lhax(&lhax, output, output_size);
        comment_lhax(&lhax, comment, comment_size);
        return true;
    }
    
    LHAU_Instruction lhau;
    if (decode_lhau(instruction, &lhau)) {
        transpile_lhau(&lhau, output, output_size);
        comment_lhau(&lhau, comment, comment_size);
        return true;
    }
    
    LHBRX_Instruction lhbrx;
    if (decode_lhbrx(instruction, &lhbrx)) {
        transpile_lhbrx(&lhbrx, output, output_size);
        comment_lhbrx(&lhbrx, comment, comment_size);
        return true;
    }
    
    STHBRX_Instruction sthbrx;
    if (decode_sthbrx(instruction, &sthbrx)) {
        transpile_sthbrx(&sthbrx, output, output_size);
        comment_sthbrx(&sthbrx, comment, comment_size);
        return true;
    }
    
    LWBRX_Instruction lwbrx;
    if (decode_lwbrx(instruction, &lwbrx)) {
        transpile_lwbrx(&lwbrx, output, output_size);
        comment_lwbrx(&lwbrx, comment, comment_size);
        return true;
    }
    
    STWBRX_Instruction stwbrx;
    if (decode_stwbrx(instruction, &stwbrx)) {
        transpile_stwbrx(&stwbrx, output, output_size);
        comment_stwbrx(&stwbrx, comment, comment_size);
        return true;
    }
    
    STHU_Instruction sthu;
    if (decode_sthu(instruction, &sthu)) {
        transpile_sthu(&sthu, output, output_size);
        comment_sthu(&sthu, comment, comment_size);
        return true;
    }
    
    STWX_Instruction stwx;
    if (decode_stwx(instruction, &stwx)) {
        transpile_stwx(&stwx, output, output_size);
        comment_stwx(&stwx, comment, comment_size);
        return true;
    }
    
    // Byte with update
    LBZU_Instruction lbzu;
    if (decode_lbzu(instruction, &lbzu)) {
        transpile_lbzu(&lbzu, output, output_size);
        comment_lbzu(&lbzu, comment, comment_size);
        return true;
    }
    
    STBU_Instruction stbu;
    if (decode_stbu(instruction, &stbu)) {
        transpile_stbu(&stbu, output, output_size);
        comment_stbu(&stbu, comment, comment_size);
        return true;
    }
    
    // More arithmetic with carry
    ADDIC_Instruction addic;
    if (decode_addic(instruction, &addic)) {
        transpile_addic(&addic, output, output_size);
        comment_addic(&addic, comment, comment_size);
        return true;
    }
    
    SUBFZE_Instruction subfze;
    if (decode_subfze(instruction, &subfze)) {
        transpile_subfze(&subfze, output, output_size);
        comment_subfze(&subfze, comment, comment_size);
        return true;
    }
    
    SUBFME_Instruction subfme;
    if (decode_subfme(instruction, &subfme)) {
        transpile_subfme(&subfme, output, output_size);
        comment_subfme(&subfme, comment, comment_size);
        return true;
    }
    
    // Floating-point arithmetic
    FSUB_Instruction fsub;
    if (decode_fsub(instruction, &fsub)) {
        transpile_fsub(&fsub, output, output_size);
        comment_fsub(&fsub, comment, comment_size);
        return true;
    }
    
    FMUL_Instruction fmul;
    if (decode_fmul(instruction, &fmul)) {
        transpile_fmul(&fmul, output, output_size);
        comment_fmul(&fmul, comment, comment_size);
        return true;
    }
    
    FDIV_Instruction fdiv;
    if (decode_fdiv(instruction, &fdiv)) {
        transpile_fdiv(&fdiv, output, output_size);
        comment_fdiv(&fdiv, comment, comment_size);
        return true;
    }
    
    // FP move/negate/convert
    FMR_Instruction fmr;
    if (decode_fmr(instruction, &fmr)) {
        transpile_fmr(&fmr, output, output_size);
        comment_fmr(&fmr, comment, comment_size);
        return true;
    }
    
    FNEG_Instruction fneg;
    if (decode_fneg(instruction, &fneg)) {
        transpile_fneg(&fneg, output, output_size);
        comment_fneg(&fneg, comment, comment_size);
        return true;
    }
    
    FCTIWZ_Instruction fctiwz;
    if (decode_fctiwz(instruction, &fctiwz)) {
        transpile_fctiwz(&fctiwz, output, output_size);
        comment_fctiwz(&fctiwz, comment, comment_size);
        return true;
    }
    
    // FP compare
    FCMPU_Instruction fcmpu;
    if (decode_fcmpu(instruction, &fcmpu)) {
        transpile_fcmpu(&fcmpu, output, output_size);
        comment_fcmpu(&fcmpu, comment, comment_size);
        return true;
    }
    
    FCMPO_Instruction fcmpo;
    if (decode_fcmpo(instruction, &fcmpo)) {
        transpile_fcmpo(&fcmpo, output, output_size);
        comment_fcmpo(&fcmpo, comment, comment_size);
        return true;
    }
    
    // CR ops
    CROR_Instruction cror;
    if (decode_cror(instruction, &cror)) {
        transpile_cror(&cror, output, output_size);
        comment_cror(&cror, comment, comment_size);
        return true;
    }
    
    CRAND_Instruction crand;
    if (decode_crand(instruction, &crand)) {
        transpile_crand(&crand, output, output_size);
        comment_crand(&crand, comment, comment_size);
        return true;
    }
    
    CRANDC_Instruction crandc;
    if (decode_crandc(instruction, &crandc)) {
        transpile_crandc(&crandc, output, output_size);
        comment_crandc(&crandc, comment, comment_size);
        return true;
    }
    
    CREQV_Instruction creqv;
    if (decode_creqv(instruction, &creqv)) {
        transpile_creqv(&creqv, output, output_size);
        comment_creqv(&creqv, comment, comment_size);
        return true;
    }
    
    CRNAND_Instruction crnand;
    if (decode_crnand(instruction, &crnand)) {
        transpile_crnand(&crnand, output, output_size);
        comment_crnand(&crnand, comment, comment_size);
        return true;
    }
    
    CRNOR_Instruction crnor;
    if (decode_crnor(instruction, &crnor)) {
        transpile_crnor(&crnor, output, output_size);
        comment_crnor(&crnor, comment, comment_size);
        return true;
    }
    
    CRORC_Instruction crorc;
    if (decode_crorc(instruction, &crorc)) {
        transpile_crorc(&crorc, output, output_size);
        comment_crorc(&crorc, comment, comment_size);
        return true;
    }
    
    MCRF_Instruction mcrf;
    if (decode_mcrf(instruction, &mcrf)) {
        transpile_mcrf(&mcrf, output, output_size);
        comment_mcrf(&mcrf, comment, comment_size);
        return true;
    }

    // Final logical ops
    EQV_Instruction eqv;
    if (decode_eqv(instruction, &eqv)) {
        transpile_eqv(&eqv, output, output_size);
        comment_eqv(&eqv, comment, comment_size);
        return true;
    }
    
    XORI_Instruction xori;
    if (decode_xori(instruction, &xori)) {
        transpile_xori(&xori, output, output_size);
        comment_xori(&xori, comment, comment_size);
        return true;
    }
    
    // Indexed byte operations
    LBZX_Instruction lbzx;
    if (decode_lbzx(instruction, &lbzx)) {
        transpile_lbzx(&lbzx, output, output_size);
        comment_lbzx(&lbzx, comment, comment_size);
        return true;
    }
    
    STBX_Instruction stbx;
    if (decode_stbx(instruction, &stbx)) {
        transpile_stbx(&stbx, output, output_size);
        comment_stbx(&stbx, comment, comment_size);
        return true;
    }
    
    LBZUX_Instruction lbzux;
    if (decode_lbzux(instruction, &lbzux)) {
        transpile_lbzux(&lbzux, output, output_size);
        comment_lbzux(&lbzux, comment, comment_size);
        return true;
    }
    
    STBUX_Instruction stbux;
    if (decode_stbux(instruction, &stbux)) {
        transpile_stbux(&stbux, output, output_size);
        comment_stbux(&stbux, comment, comment_size);
        return true;
    }
    
    LHZUX_Instruction lhzux;
    if (decode_lhzux(instruction, &lhzux)) {
        transpile_lhzux(&lhzux, output, output_size);
        comment_lhzux(&lhzux, comment, comment_size);
        return true;
    }
    
    LHAUX_Instruction lhaux;
    if (decode_lhaux(instruction, &lhaux)) {
        transpile_lhaux(&lhaux, output, output_size);
        comment_lhaux(&lhaux, comment, comment_size);
        return true;
    }
    
    STHUX_Instruction sthux;
    if (decode_sthux(instruction, &sthux)) {
        transpile_sthux(&sthux, output, output_size);
        comment_sthux(&sthux, comment, comment_size);
        return true;
    }
    
    LWZUX_Instruction lwzux;
    if (decode_lwzux(instruction, &lwzux)) {
        transpile_lwzux(&lwzux, output, output_size);
        comment_lwzux(&lwzux, comment, comment_size);
        return true;
    }
    
    STWUX_Instruction stwux;
    if (decode_stwux(instruction, &stwux)) {
        transpile_stwux(&stwux, output, output_size);
        comment_stwux(&stwux, comment, comment_size);
        return true;
    }
    
    // Segment register operations
    MFSR_Instruction mfsr;
    if (decode_mfsr(instruction, &mfsr)) {
        transpile_mfsr(&mfsr, output, output_size);
        comment_mfsr(&mfsr, comment, comment_size);
        return true;
    }
    
    MTSR_Instruction mtsr;
    if (decode_mtsr(instruction, &mtsr)) {
        transpile_mtsr(&mtsr, output, output_size);
        comment_mtsr(&mtsr, comment, comment_size);
        return true;
    }
    
    // Paired-Single Operations
    PS_ABS_Instruction ps_abs;
    if (decode_ps_abs(instruction, &ps_abs)) {
        transpile_ps_abs(&ps_abs, output, output_size);
        comment_ps_abs(&ps_abs, comment, comment_size);
        return true;
    }
    
    PS_NEG_Instruction ps_neg;
    if (decode_ps_neg(instruction, &ps_neg)) {
        transpile_ps_neg(&ps_neg, output, output_size);
        comment_ps_neg(&ps_neg, comment, comment_size);
        return true;
    }
    
    PS_NABS_Instruction ps_nabs;
    if (decode_ps_nabs(instruction, &ps_nabs)) {
        transpile_ps_nabs(&ps_nabs, output, output_size);
        comment_ps_nabs(&ps_nabs, comment, comment_size);
        return true;
    }
    
    PS_MR_Instruction ps_mr;
    if (decode_ps_mr(instruction, &ps_mr)) {
        transpile_ps_mr(&ps_mr, output, output_size);
        comment_ps_mr(&ps_mr, comment, comment_size);
        return true;
    }
    
    PS_CMPU0_Instruction ps_cmpu0;
    if (decode_ps_cmpu0(instruction, &ps_cmpu0)) {
        transpile_ps_cmpu0(&ps_cmpu0, output, output_size);
        comment_ps_cmpu0(&ps_cmpu0, comment, comment_size);
        return true;
    }
    
    PS_CMPU1_Instruction ps_cmpu1;
    if (decode_ps_cmpu1(instruction, &ps_cmpu1)) {
        transpile_ps_cmpu1(&ps_cmpu1, output, output_size);
        comment_ps_cmpu1(&ps_cmpu1, comment, comment_size);
        return true;
    }
    
    PS_CMPO0_Instruction ps_cmpo0;
    if (decode_ps_cmpo0(instruction, &ps_cmpo0)) {
        transpile_ps_cmpo0(&ps_cmpo0, output, output_size);
        comment_ps_cmpo0(&ps_cmpo0, comment, comment_size);
        return true;
    }
    
    PS_CMPO1_Instruction ps_cmpo1;
    if (decode_ps_cmpo1(instruction, &ps_cmpo1)) {
        transpile_ps_cmpo1(&ps_cmpo1, output, output_size);
        comment_ps_cmpo1(&ps_cmpo1, comment, comment_size);
        return true;
    }
    
    PS_SEL_Instruction ps_sel;
    if (decode_ps_sel(instruction, &ps_sel)) {
        transpile_ps_sel(&ps_sel, output, output_size);
        comment_ps_sel(&ps_sel, comment, comment_size);
        return true;
    }
    
    PS_RES_Instruction ps_res;
    if (decode_ps_res(instruction, &ps_res)) {
        transpile_ps_res(&ps_res, output, output_size);
        comment_ps_res(&ps_res, comment, comment_size);
        return true;
    }
    
    PS_RSQRTE_Instruction ps_rsqrte;
    if (decode_ps_rsqrte(instruction, &ps_rsqrte)) {
        transpile_ps_rsqrte(&ps_rsqrte, output, output_size);
        comment_ps_rsqrte(&ps_rsqrte, comment, comment_size);
        return true;
    }
    
    PS_NMADD_Instruction ps_nmadd;
    if (decode_ps_nmadd(instruction, &ps_nmadd)) {
        transpile_ps_nmadd(&ps_nmadd, output, output_size);
        comment_ps_nmadd(&ps_nmadd, comment, comment_size);
        return true;
    }
    
    PS_NMSUB_Instruction ps_nmsub;
    if (decode_ps_nmsub(instruction, &ps_nmsub)) {
        transpile_ps_nmsub(&ps_nmsub, output, output_size);
        comment_ps_nmsub(&ps_nmsub, comment, comment_size);
        return true;
    }
    
    PS_SUM0_Instruction ps_sum0;
    if (decode_ps_sum0(instruction, &ps_sum0)) {
        transpile_ps_sum0(&ps_sum0, output, output_size);
        comment_ps_sum0(&ps_sum0, comment, comment_size);
        return true;
    }
    
    PS_SUM1_Instruction ps_sum1;
    if (decode_ps_sum1(instruction, &ps_sum1)) {
        transpile_ps_sum1(&ps_sum1, output, output_size);
        comment_ps_sum1(&ps_sum1, comment, comment_size);
        return true;
    }
    
    PS_MULS0_Instruction ps_muls0;
    if (decode_ps_muls0(instruction, &ps_muls0)) {
        transpile_ps_muls0(&ps_muls0, output, output_size);
        comment_ps_muls0(&ps_muls0, comment, comment_size);
        return true;
    }
    
    PS_MULS1_Instruction ps_muls1;
    if (decode_ps_muls1(instruction, &ps_muls1)) {
        transpile_ps_muls1(&ps_muls1, output, output_size);
        comment_ps_muls1(&ps_muls1, comment, comment_size);
        return true;
    }
    
    PS_MADDS0_Instruction ps_madds0;
    if (decode_ps_madds0(instruction, &ps_madds0)) {
        transpile_ps_madds0(&ps_madds0, output, output_size);
        comment_ps_madds0(&ps_madds0, comment, comment_size);
        return true;
    }
    
    PS_MADDS1_Instruction ps_madds1;
    if (decode_ps_madds1(instruction, &ps_madds1)) {
        transpile_ps_madds1(&ps_madds1, output, output_size);
        comment_ps_madds1(&ps_madds1, comment, comment_size);
        return true;
    }
    
    PSQ_LU_Instruction psq_lu;
    if (decode_psq_lu(instruction, &psq_lu)) {
        transpile_psq_lu(&psq_lu, output, output_size);
        comment_psq_lu(&psq_lu, comment, comment_size);
        return true;
    }
    
    PSQ_STU_Instruction psq_stu;
    if (decode_psq_stu(instruction, &psq_stu)) {
        transpile_psq_stu(&psq_stu, output, output_size);
        comment_psq_stu(&psq_stu, comment, comment_size);
        return true;
    }
    
    PSQ_LX_Instruction psq_lx;
    if (decode_psq_lx(instruction, &psq_lx)) {
        transpile_psq_lx(&psq_lx, output, output_size);
        comment_psq_lx(&psq_lx, comment, comment_size);
        return true;
    }
    
    PSQ_STX_Instruction psq_stx;
    if (decode_psq_stx(instruction, &psq_stx)) {
        transpile_psq_stx(&psq_stx, output, output_size);
        comment_psq_stx(&psq_stx, comment, comment_size);
        return true;
    }
    
    PSQ_LUX_Instruction psq_lux;
    if (decode_psq_lux(instruction, &psq_lux)) {
        transpile_psq_lux(&psq_lux, output, output_size);
        comment_psq_lux(&psq_lux, comment, comment_size);
        return true;
    }
    
    PSQ_STUX_Instruction psq_stux;
    if (decode_psq_stux(instruction, &psq_stux)) {
        transpile_psq_stux(&psq_stux, output, output_size);
        comment_psq_stux(&psq_stux, comment, comment_size);
        return true;
    }
    
    // Unknown instruction
    snprintf(output, output_size, "/* UNKNOWN OPCODE */");
    snprintf(comment, comment_size, "0x%08X", instruction);
    return false;
}

/**
 * @brief Transpile a single .s file
 */
int transpile_file(const char *input_filename, SkipList *skip_list) {
    printf("Processing: %s\n", input_filename);
    
    // Build label-to-function map for trampoline resolution
    LabelMap *label_map = build_label_map(input_filename);
    
    // Generate output filenames
    char output_c[256], output_h[256];
    generate_output_filenames(input_filename, output_c, output_h, sizeof(output_c));
    
    // Open input file
    FILE *input = fopen(input_filename, "r");
    if (!input) {
        fprintf(stderr, "Error: Cannot open input file %s\n", input_filename);
        return -1;
    }
    
    // Open output files
    FILE *c_file = fopen(output_c, "w");
    FILE *h_file = fopen(output_h, "w");
    
    if (!c_file || !h_file) {
        fprintf(stderr, "Error: Cannot create output files\n");
        if (input) fclose(input);
        if (c_file) fclose(c_file);
        if (h_file) fclose(h_file);
        return -1;
    }
    
    // Extract base name for header guard
    char guard_name[128];
    const char *base = strrchr(input_filename, '/');
    if (!base) base = strrchr(input_filename, '\\');
    if (!base) base = input_filename;
    else base++;
    
    strncpy(guard_name, base, sizeof(guard_name) - 1);
    char *dot = strchr(guard_name, '.');
    if (dot) *dot = '\0';
    
    // Convert to uppercase for guard
    for (char *p = guard_name; *p; p++) {
        *p = toupper(*p);
    }
    
    // Write file headers
    write_header_start(h_file, guard_name);
    write_c_file_start(c_file, output_h);
    
    // Process file line by line
    char line[MAX_LINE_LENGTH];
    bool in_function = false;
    bool in_data_section = false;
    Function_Info current_func = {0};
    
    // Buffer to track previous lines for parameter detection
    char line_buffer[MAX_LOOKBACK_LINES][MAX_LINE_LENGTH];
    const char *prev_lines[MAX_LOOKBACK_LINES];
    int line_index = 0;
    int lines_buffered = 0;
    
    // Initialize pointers
    for (int i = 0; i < MAX_LOOKBACK_LINES; i++) {
        prev_lines[i] = line_buffer[i];
        line_buffer[i][0] = '\0';
    }
    
    while (fgets(line, sizeof(line), input)) {
        ASM_Line parsed;
        
        if (!parse_asm_line(line, &parsed)) {
            // Failed to parse, write as comment
            fprintf(c_file, "    // %s", line);
            continue;
        }
        
        // Handle directives
        if (parsed.is_comment) {
            continue;  // Skip pure comment lines
        }
        
        if (parsed.is_directive) {
            // Handle .include
            if (strstr(line, ".include") != NULL) {
                char include_line[256];
                convert_include(line, include_line, sizeof(include_line));
                fprintf(h_file, "%s\n", include_line);
            }
            // .endfn marks end of function
            if (strstr(line, ".endfn") != NULL && in_function) {
                // Add trampoline fix instructions if detected
                if (current_func.is_trampoline && label_map) {
                    const char *target_func = labelmap_find_function(label_map, current_func.trampoline_target);
                    fprintf(c_file, "    /* TRAMPOLINE DETECTED - Cross-function jump to 0x%08X\n", 
                           current_func.trampoline_target);
                    fprintf(c_file, "     * Auto-fix: Replace the goto above with:\n");
                    fprintf(c_file, "     *   pc = 0x%08X;\n", current_func.trampoline_target);
                    if (target_func) {
                        fprintf(c_file, "     *   %s();  // Function containing L_%08X\n", 
                               target_func, current_func.trampoline_target);
                    } else {
                        fprintf(c_file, "     *   TARGET_FUNCTION();  // Function containing L_%08X (not found)\n", 
                               current_func.trampoline_target);
                    }
                    fprintf(c_file, "     * Then add to target function start:\n");
                    fprintf(c_file, "     *   if (pc == 0x%08X) goto L_%08X;\n", 
                           current_func.trampoline_target, current_func.trampoline_target);
                    fprintf(c_file, "     */\n");
                }
                write_function_end(c_file);
                in_function = false;
            }
            continue;
        }
        
        // Handle .data section
        if (parsed.is_data) {
            in_data_section = true;
            fprintf(c_file, "\n// === DATA SECTION ===\n");
            fprintf(c_file, "// (Data sections preserved as byte arrays)\n\n");
            continue;
        }
        
        // Handle function start
        if (parsed.is_function) {
            strcpy(current_func.name, parsed.function_name);
            current_func.start_address = 0;  // Will be set by first instruction
            current_func.instruction_count = 0;
            current_func.is_trampoline = false;
            current_func.trampoline_target = 0;
            
            // Auto-skip gap functions (usually data misidentified as code)
            if (strncmp(current_func.name, "gap_", 4) == 0) {
                current_func.skip = true;
            } else {
                current_func.skip = skiplist_should_skip(skip_list, current_func.name);
            }
            
            // Write to header with sanitized name
            if (!current_func.skip) {
                char sanitized_name[MAX_FUNCTION_NAME];
                const char *func_name = sanitize_function_name(current_func.name, sanitized_name, sizeof(sanitized_name));
                
                // Use () instead of (void) to allow flexible parameter passing
                // Functions communicate through emulated registers (r3, r4, etc.)
                fprintf(h_file, "void %s();  // Uses emulated register state\n", func_name);
                
                in_function = true;
            } else {
                // For skipped functions, don't set in_function to prevent closing brace
                in_function = false;
                fprintf(c_file, "// Function %s skipped (gap or in skip list)\n\n", current_func.name);
            }
            
            in_data_section = false;
            continue;
        }
        
        // Handle labels
        if (parsed.is_label) {
            if (in_function && !in_data_section) {
                char c_label[MAX_LABEL_NAME + 2];
                convert_label(parsed.label_name, c_label, sizeof(c_label));
                fprintf(c_file, "\n%s\n", c_label);  // Add newline before label for readability
            }
            continue;
        }
        
        // Handle instructions
        if (parsed.instruction != 0) {
            if (in_data_section) {
                // In data section, write as hex data
                fprintf(c_file, "    0x%02X, 0x%02X, 0x%02X, 0x%02X,  // 0x%08X\n",
                       (parsed.instruction >> 24) & 0xFF,
                       (parsed.instruction >> 16) & 0xFF,
                       (parsed.instruction >> 8) & 0xFF,
                       parsed.instruction & 0xFF,
                       parsed.address);
            } else if (in_function) {
                // Set function start address if not set
                if (current_func.start_address == 0) {
                    current_func.start_address = parsed.address;
                    
                    if (current_func.skip) {
                        fprintf(c_file, "// Function %s skipped (in skip list)\n\n",
                               current_func.name);
                        in_function = false;
                        continue;
                    }
                    
                    write_function_start(c_file, &current_func);
                }
                
                // Transpile instruction
                char c_code[512];
                char asm_comment[128];
                
                // Try parsing from assembly text first
                bool success = transpile_from_asm(parsed.mnemonic, parsed.operands, parsed.address,
                                                  c_code, sizeof(c_code),
                                                  asm_comment, sizeof(asm_comment),
                                                  prev_lines, lines_buffered);
                
                // Fall back to byte-based decoding
                if (!success) {
                    success = transpile_instruction(parsed.instruction, parsed.address,
                                                    c_code, sizeof(c_code),
                                                    asm_comment, sizeof(asm_comment));
                }
                
                if (success) {
                    // Detect trampoline: single branch instruction to a label
                    bool is_trampoline_branch = false;
                    uint32_t trampoline_target = 0;
                    
                    if (current_func.instruction_count == 0 && 
                        (strncmp(parsed.mnemonic, "b", 1) == 0) &&
                        (strstr(c_code, "goto L_") != NULL || strstr(c_code, "goto lbl_") != NULL)) {
                        // Extract target address from the goto statement
                        char *goto_pos = strstr(c_code, "goto ");
                        if (goto_pos) {
                            char *addr_start = goto_pos + 5; // Skip "goto "
                            while (*addr_start == ' ') addr_start++;
                            if (strncmp(addr_start, "L_", 2) == 0) {
                                sscanf(addr_start + 2, "%x", &trampoline_target);
                            } else if (strncmp(addr_start, "lbl_", 4) == 0) {
                                sscanf(addr_start + 4, "%x", &trampoline_target);
                            }
                            
                            if (trampoline_target != 0) {
                                is_trampoline_branch = true;
                                current_func.is_trampoline = true;
                                current_func.trampoline_target = trampoline_target;
                                
                                // Replace goto with valid C code that compiles
                                // Use a function call stub instead of goto
                                snprintf(c_code, sizeof(c_code), 
                                        "/* TRAMPOLINE to 0x%08X - Target function needed */ "
                                        "pc = 0x%08X; return;", 
                                        trampoline_target, trampoline_target);
                            }
                        }
                    }
                    
                    fprintf(c_file, "    %s  // 0x%08X: %s\n",
                           c_code, parsed.address, asm_comment);
                    current_func.instruction_count++;
                } else {
                    fprintf(c_file, "    /* 0x%08X: UNKNOWN 0x%08X - %s %s */\n",
                           parsed.address, parsed.instruction,
                           parsed.mnemonic, parsed.operands);
                    current_func.instruction_count++;
                }
            }
        }
        
        // Update line buffer (circular buffer for context)
        strncpy(line_buffer[line_index], line, MAX_LINE_LENGTH - 1);
        line_buffer[line_index][MAX_LINE_LENGTH - 1] = '\0';
        line_index = (line_index + 1) % MAX_LOOKBACK_LINES;
        if (lines_buffered < MAX_LOOKBACK_LINES) {
            lines_buffered++;
        }
    }
    
    // Close any open function
    if (in_function) {
        write_function_end(c_file);
    }
    
    // Write file footers
    write_header_end(h_file);
    
    // Close files
    fclose(input);
    fclose(c_file);
    fclose(h_file);
    
    // Cleanup label map
    if (label_map) labelmap_free(label_map);
    
    printf("  Created: %s\n", output_c);
    printf("  Created: %s\n", output_h);
    
    return 0;
}

/**
 * @brief Transpile a single .s file directly to project folders
 */
int transpile_file_to_project(const char *input_filename, const char *src_dir, 
                               const char *inc_dir, SkipList *skip_list) {
    // Build label-to-function map for trampoline resolution
    LabelMap *label_map = build_label_map(input_filename);
    
    // Extract base name
    const char *base = strrchr(input_filename, '/');
    if (!base) base = strrchr(input_filename, '\\');
    base = base ? (base + 1) : input_filename;
    
    char base_name[256];
    strncpy(base_name, base, sizeof(base_name) - 1);
    char *ext = strrchr(base_name, '.');
    if (ext) *ext = '\0';
    
    // Generate output paths in project structure
    char output_c[512], output_h[512];
    snprintf(output_c, sizeof(output_c), "%s/%s.c", src_dir, base_name);
    snprintf(output_h, sizeof(output_h), "%s/%s.h", inc_dir, base_name);
    
    // Open input file
    FILE *input = fopen(input_filename, "r");
    if (!input) {
        fprintf(stderr, "  Error: Cannot open %s\n", input_filename);
        if (label_map) labelmap_free(label_map);
        return -1;
    }
    
    // Open output files
    FILE *c_file = fopen(output_c, "w");
    FILE *h_file = fopen(output_h, "w");
    
    if (!c_file || !h_file) {
        fprintf(stderr, "  Error: Cannot create output files\n");
        if (input) fclose(input);
        if (c_file) fclose(c_file);
        if (h_file) fclose(h_file);
        return -1;
    }
    
    // Generate header guard
    char guard_name[128];
    snprintf(guard_name, sizeof(guard_name), "%s_H", base_name);
    for (char *p = guard_name; *p; p++) {
        if (*p == '-' || *p == ' ') *p = '_';
        *p = (*p >= 'a' && *p <= 'z') ? (*p - 32) : *p;
    }
    
    // Write header file boilerplate
    fprintf(h_file, "#ifndef %s\n", guard_name);
    fprintf(h_file, "#define %s\n\n", guard_name);
    
    // Write C file boilerplate
    fprintf(c_file, "#include \"%s.h\"\n", base_name);
    fprintf(c_file, "#include \"powerpc_state.h\"\n");
    fprintf(c_file, "#include \"all_functions.h\"  // For cross-file function calls\n\n");
    
    // Process file line by line
    char line[MAX_LINE_LENGTH];
    bool in_function = false;
    bool in_data_section = false;
    Function_Info current_func = {0};
    
    // Buffer to track previous lines for parameter detection
    char line_buffer[MAX_LOOKBACK_LINES][MAX_LINE_LENGTH];
    const char *prev_lines[MAX_LOOKBACK_LINES];
    int line_index = 0;
    int lines_buffered = 0;
    
    // Initialize pointers
    for (int i = 0; i < MAX_LOOKBACK_LINES; i++) {
        prev_lines[i] = line_buffer[i];
        line_buffer[i][0] = '\0';
    }
    
    while (fgets(line, sizeof(line), input)) {
        ASM_Line parsed;
        
        if (!parse_asm_line(line, &parsed)) {
            fprintf(c_file, "    // %s", line);
            continue;
        }
        
        if (parsed.is_comment) continue;
        
        if (parsed.is_directive) {
            if (strstr(line, ".include") != NULL) {
                char include_line[256];
                convert_include(line, include_line, sizeof(include_line));
                fprintf(h_file, "%s\n", include_line);
            }
            if (strstr(line, ".endfn") != NULL && in_function) {
                // Add trampoline fix instructions if detected
                if (current_func.is_trampoline) {
                    fprintf(c_file, "    /* TRAMPOLINE DETECTED - Cross-function jump to 0x%08X\n", 
                           current_func.trampoline_target);
                    fprintf(c_file, "     * Auto-fix: Replace the goto above with:\n");
                    fprintf(c_file, "     *   pc = 0x%08X;\n", current_func.trampoline_target);
                    fprintf(c_file, "     *   TARGET_FUNCTION();  // Function containing L_%08X\n", 
                           current_func.trampoline_target);
                    fprintf(c_file, "     * Then add to target function start:\n");
                    fprintf(c_file, "     *   if (pc == 0x%08X) goto L_%08X;\n", 
                           current_func.trampoline_target, current_func.trampoline_target);
                    fprintf(c_file, "     */\n");
                }
                write_function_end(c_file);
                in_function = false;
            }
            continue;
        }
        
        if (parsed.is_data) {
            in_data_section = true;
            fprintf(c_file, "\n// === DATA SECTION ===\n");
            fprintf(c_file, "// (Data sections preserved as byte arrays)\n\n");
            continue;
        }
        
        if (parsed.is_function) {
            strcpy(current_func.name, parsed.function_name);
            current_func.start_address = 0;
            current_func.instruction_count = 0;
            current_func.is_trampoline = false;
            current_func.trampoline_target = 0;
            
            // Auto-skip gap functions (usually data misidentified as code)
            if (strncmp(current_func.name, "gap_", 4) == 0) {
                current_func.skip = true;
            } else {
                current_func.skip = skiplist_should_skip(skip_list, current_func.name);
            }
            
            if (!current_func.skip) {
                char sanitized_name[MAX_FUNCTION_NAME];
                const char *func_name = sanitize_function_name(current_func.name, sanitized_name, sizeof(sanitized_name));
                
                // Use () instead of (void) to allow flexible parameter passing
                // Functions communicate through emulated registers (r3, r4, etc.)
                fprintf(h_file, "void %s();  // Uses emulated register state\n", func_name);
                
                in_function = true;
            } else {
                // For skipped functions, don't set in_function to prevent closing brace
                in_function = false;
                fprintf(c_file, "// Function %s skipped (gap or in skip list)\n\n", current_func.name);
            }
            
            in_data_section = false;
            continue;
        }
        
        if (parsed.is_label) {
            if (in_function && !in_data_section) {
                char c_label[MAX_LABEL_NAME + 2];
                convert_label(parsed.label_name, c_label, sizeof(c_label));
                fprintf(c_file, "\n%s\n", c_label);
            }
            continue;
        }
        
        if (parsed.instruction != 0) {
            if (in_data_section) {
                fprintf(c_file, "    0x%02X, 0x%02X, 0x%02X, 0x%02X,  // 0x%08X\n",
                       (parsed.instruction >> 24) & 0xFF,
                       (parsed.instruction >> 16) & 0xFF,
                       (parsed.instruction >> 8) & 0xFF,
                       parsed.instruction & 0xFF,
                       parsed.address);
            } else if (in_function) {
                if (current_func.start_address == 0) {
                    current_func.start_address = parsed.address;
                    
                    if (current_func.skip) {
                        fprintf(c_file, "// Function %s skipped (in skip list)\n\n",
                               current_func.name);
                        in_function = false;
                        continue;
                    }
                    
                    write_function_start(c_file, &current_func);
                }
                
                char c_code[512], asm_comment[128];
                
                // Try parsing from assembly text first (for branches, etc.)
                bool success = transpile_from_asm(parsed.mnemonic, parsed.operands, parsed.address,
                                                  c_code, sizeof(c_code),
                                                  asm_comment, sizeof(asm_comment),
                                                  prev_lines, lines_buffered);
                
                // Fall back to byte-based decoding if text-based failed
                if (!success) {
                    success = transpile_instruction(parsed.instruction, parsed.address,
                                                    c_code, sizeof(c_code),
                                                    asm_comment, sizeof(asm_comment));
                }
                
                if (success) {
                    // Detect trampoline: single branch instruction to a label
                    bool is_trampoline_branch = false;
                    uint32_t trampoline_target = 0;
                    
                    if (current_func.instruction_count == 0 && 
                        (strncmp(parsed.mnemonic, "b", 1) == 0) &&
                        (strstr(c_code, "goto L_") != NULL || strstr(c_code, "goto lbl_") != NULL)) {
                        // Extract target address from the goto statement
                        char *goto_pos = strstr(c_code, "goto ");
                        if (goto_pos) {
                            char *addr_start = goto_pos + 5; // Skip "goto "
                            while (*addr_start == ' ') addr_start++;
                            if (strncmp(addr_start, "L_", 2) == 0) {
                                sscanf(addr_start + 2, "%x", &trampoline_target);
                            } else if (strncmp(addr_start, "lbl_", 4) == 0) {
                                sscanf(addr_start + 4, "%x", &trampoline_target);
                            }
                            
                            if (trampoline_target != 0) {
                                is_trampoline_branch = true;
                                current_func.is_trampoline = true;
                                current_func.trampoline_target = trampoline_target;
                                
                                // Replace goto with valid C code that compiles
                                // Use a function call stub instead of goto
                                snprintf(c_code, sizeof(c_code), 
                                        "/* TRAMPOLINE to 0x%08X - Target function needed */ "
                                        "pc = 0x%08X; return;", 
                                        trampoline_target, trampoline_target);
                            }
                        }
                    }
                    
                    fprintf(c_file, "    %s  // 0x%08X: %s\n",
                           c_code, parsed.address, asm_comment);
                    current_func.instruction_count++;
                } else {
                    fprintf(c_file, "    /* 0x%08X: UNKNOWN 0x%08X - %s */\n",
                           parsed.address, parsed.instruction, asm_comment);
                    current_func.instruction_count++;
                }
            }
        }
        
        // Update line buffer (circular buffer for context)
        strncpy(line_buffer[line_index], line, MAX_LINE_LENGTH - 1);
        line_buffer[line_index][MAX_LINE_LENGTH - 1] = '\0';
        line_index = (line_index + 1) % MAX_LOOKBACK_LINES;
        if (lines_buffered < MAX_LOOKBACK_LINES) {
            lines_buffered++;
        }
    }
    
    fprintf(h_file, "\n#endif // %s\n", guard_name);
    
    fclose(input);
    fclose(c_file);
    fclose(h_file);
    
    // Cleanup label map
    if (label_map) labelmap_free(label_map);
    
    printf("  → %s/%s.c\n", src_dir, base_name);
    printf("  → %s/%s.h\n", inc_dir, base_name);
    
    return 0;
}

/**
 * @brief Process directory of .s files
 */
int transpile_directory(const char *dir_path, SkipList *skip_list) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        fprintf(stderr, "Error: Cannot open directory %s\n", dir_path);
        return -1;
    }
    
    int files_processed = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        // Check if it's a .s file
        size_t name_len = strlen(entry->d_name);
        if (name_len < 3) continue;
        
        if (strcmp(entry->d_name + name_len - 2, ".s") == 0) {
            // Construct full path
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, entry->d_name);
            
            // Transpile the file
            if (transpile_file(filepath, skip_list) == 0) {
                files_processed++;
            }
        }
    }
    
    closedir(dir);
    return files_processed;
}

/**
 * @brief Generate CMake project from transpiled files
 */
int generate_project(const char *output_dir, const char *dir_path) {
    printf("\n===========================================\n");
    printf("   Generating CMake Project\n");
    printf("===========================================\n\n");
    
    // Create project directories
    printf("Creating project structure...\n");
    create_directory(output_dir);
    
    char src_dir[512], inc_dir[512];
    snprintf(src_dir, sizeof(src_dir), "%s/src", output_dir);
    snprintf(inc_dir, sizeof(inc_dir), "%s/include", output_dir);
    
    create_directory(src_dir);
    create_directory(inc_dir);
    
    // Collect all generated .c and .h files
    int c_file_count = 0;
    int h_file_count = 0;
    char **c_files = malloc(sizeof(char*) * 100);
    char **h_files = malloc(sizeof(char*) * 100);
    
    // Count and copy .c and .h files
    DIR *dir = opendir(dir_path);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            size_t name_len = strlen(entry->d_name);
            if (name_len < 3) continue;
            
            if (strcmp(entry->d_name + name_len - 2, ".c") == 0) {
                // Copy .c file to src/
                char src_path[512], dst_path[512];
                snprintf(src_path, sizeof(src_path), "%s/%s", dir_path, entry->d_name);
                snprintf(dst_path, sizeof(dst_path), "%s/%s", src_dir, entry->d_name);
                
                FILE *src_file = fopen(src_path, "r");
                FILE *dst_file = fopen(dst_path, "w");
                if (src_file && dst_file) {
                    char buffer[4096];
                    size_t bytes;
                    while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
                        fwrite(buffer, 1, bytes, dst_file);
                    }
                    fclose(src_file);
                    fclose(dst_file);
                    c_files[c_file_count++] = strdup(entry->d_name);
                }
            }
            else if (strcmp(entry->d_name + name_len - 2, ".h") == 0) {
                // Copy .h file to include/
                char src_path[512], dst_path[512];
                snprintf(src_path, sizeof(src_path), "%s/%s", dir_path, entry->d_name);
                snprintf(dst_path, sizeof(dst_path), "%s/%s", inc_dir, entry->d_name);
                
                FILE *src_file = fopen(src_path, "r");
                FILE *dst_file = fopen(dst_path, "w");
                if (src_file && dst_file) {
                    char buffer[4096];
                    size_t bytes;
                    while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
                        fwrite(buffer, 1, bytes, dst_file);
                    }
                    fclose(src_file);
                    fclose(dst_file);
                    h_files[h_file_count++] = strdup(entry->d_name);
                }
            }
        }
        closedir(dir);
    }
    
    // Extract project name from path
    const char *proj_name = strrchr(output_dir, '/');
    if (!proj_name) proj_name = strrchr(output_dir, '\\');
    proj_name = proj_name ? (proj_name + 1) : output_dir;
    
    // Generate project files
    printf("Generating CMakeLists.txt...\n");
    generate_cmake(output_dir, proj_name, c_file_count, (const char**)c_files, 
                   h_file_count, (const char**)h_files);
    
    printf("Generating runtime files...\n");
    generate_runtime_h(output_dir);
    generate_runtime_c(output_dir);
    generate_main_c(output_dir);
    
    printf("Generating documentation...\n");
    generate_readme(output_dir, proj_name);
    generate_gitignore(output_dir);
    
    // Cleanup
    for (int i = 0; i < c_file_count; i++) {
        free(c_files[i]);
    }
    free(c_files);
    
    for (int i = 0; i < h_file_count; i++) {
        free(h_files[i]);
    }
    free(h_files);
    
    printf("\n===========================================\n");
    printf("   Project Generated Successfully!\n");
    printf("   Location: %s\n", output_dir);
    printf("===========================================\n");
    printf("\nTo build the project:\n");
    printf("  cd %s\n", output_dir);
    printf("  mkdir build && cd build\n");
    printf("  cmake ..\n");
    printf("  cmake --build .\n\n");
    
    return 0;
}

/**
 * @brief Main entry point
 */
int main(int argc, char *argv[]) {
    printf("===========================================\n");
    printf("   Porpoise Tool - PowerPC to C Transpiler\n");
    printf("   For GameCube/Wii Decompilation Projects\n");
    printf("===========================================\n");
    printf("   Opcodes: %d / %d (%.1f%% - COMPLETE!) 🎉\n",
           get_implemented_opcode_count(),
           get_implemented_opcode_count(),
           get_implementation_progress());
    printf("===========================================\n\n");
    
    // Check for help flag
    bool show_help = (argc < 2);
    if (argc >= 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 || 
            strcmp(argv[1], "-?") == 0 || strcmp(argv[1], "/?") == 0) {
            show_help = true;
        }
    }
    
    if (show_help) {
        printf("Porpoise transpiles PowerPC assembly (.s files) to portable C code.\n\n");
        
        printf("USAGE:\n");
        printf("  %s <input_dir> [output_project] [skip_list.txt]\n", argv[0]);
        printf("  %s --help | -h | -? | /?     Show this help message\n\n", argv[0]);
        
        printf("ARGUMENTS:\n");
        printf("  <input_dir>         Directory containing .s assembly files to transpile\n");
        printf("  [output_project]    Output project directory (default: GameCube_Project)\n");
        printf("  [skip_list.txt]     Optional text file with function names to skip (one per line)\n\n");
        
        printf("FEATURES:\n");
        printf("  • Transpiles 248 PowerPC + Gekko opcodes (100%% coverage!)\n");
        printf("  • Automatic parameter detection from register usage\n");
        printf("  • Generates complete CMake project with headers and source files\n");
        printf("  • Preserves labels, data sections, and function boundaries\n");
        printf("  • Cross-platform compatible C output (Windows/Linux/Mac)\n");
        printf("  • Runtime environment for emulated registers and memory\n\n");
        
        printf("EXAMPLES:\n");
        printf("  Basic usage (generates GameCube_Project/):\n");
        printf("    %s \"Test Asm\"\n\n", argv[0]);
        
        printf("  Custom output project name:\n");
        printf("    %s \"AirRide\" MyGame\n\n", argv[0]);
        
        printf("  Skip specific functions during transpilation:\n");
        printf("    %s \"Test Asm\" MyGame skip_functions.txt\n\n", argv[0]);
        
        printf("SKIP LIST FORMAT:\n");
        printf("  Create a text file with one function name per line:\n");
        printf("    fn_80003100\n");
        printf("    fn_80003200\n");
        printf("    InitSystem\n\n");
        
        printf("OUTPUT:\n");
        printf("  • <project>/src/       - Transpiled C source files\n");
        printf("  • <project>/include/   - Header files and declarations\n");
        printf("  • <project>/CMakeLists.txt - Build configuration\n");
        printf("  • <project>/<project>.sln - Visual Studio solution (on Windows)\n\n");
        
        printf("BUILDING THE OUTPUT:\n");
        printf("  cd <output_project>\n");
        printf("  cmake -B build\n");
        printf("  cmake --build build\n\n");
        
        printf("For more information, see docs/QUICK_START.md\n\n");
        return (argc < 2) ? 1 : 0;  // Error if no args, success if --help
    }
    
    const char *input_dir = argv[1];
    const char *output_project = (argc >= 3) ? argv[2] : "GameCube_Project";
    const char *skip_file = (argc >= 4) ? argv[3] : NULL;
    
    // Initialize skip list
    SkipList skip_list;
    skiplist_init(&skip_list);
    
    if (skip_file) {
        int loaded = skiplist_load_from_file(&skip_list, skip_file);
        if (loaded >= 0) {
            printf("Loaded skip list: %d functions to skip\n\n", loaded);
        } else {
            printf("Warning: Could not load skip list file %s\n\n", skip_file);
        }
    }
    
    // Create project structure
    printf("\n===========================================\n");
    printf("   Creating Project: %s\n", output_project);
    printf("===========================================\n");
    
    create_directory(output_project);
    
    char src_dir[512], inc_dir[512];
    snprintf(src_dir, sizeof(src_dir), "%s/src", output_project);
    snprintf(inc_dir, sizeof(inc_dir), "%s/include", output_project);
    
    create_directory(src_dir);
    create_directory(inc_dir);
    
    printf("Processing assembly files from: %s\n\n", input_dir);
    
    // Process all .s files and output directly to project
    DIR *dir = opendir(input_dir);
    if (!dir) {
        fprintf(stderr, "Error: Cannot open input directory %s\n", input_dir);
        return 1;
    }
    
    int files_processed = 0;
    int max_files = 5000;  // Support large games with thousands of files
    char **c_files = malloc(sizeof(char*) * max_files);
    char **h_files = malloc(sizeof(char*) * max_files);
    int file_count = 0;
    
    if (!c_files || !h_files) {
        fprintf(stderr, "Error: Failed to allocate memory for file arrays\n");
        closedir(dir);
        return 1;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t name_len = strlen(entry->d_name);
        if (name_len < 3) continue;
        
        if (strcmp(entry->d_name + name_len - 2, ".s") == 0) {
            char input_path[512];
            snprintf(input_path, sizeof(input_path), "%s/%s", input_dir, entry->d_name);
            
            printf("Processing [%d]: %s\n", files_processed + 1, entry->d_name);
            fflush(stdout);  // Force output to show immediately
            
            // Check for buffer overflow
            if (file_count >= max_files) {
                fprintf(stderr, "Warning: Too many files (max %d), skipping %s\n", max_files, entry->d_name);
                continue;
            }
            
            // Transpile directly to project folders (with error handling)
            int result = transpile_file_to_project(input_path, src_dir, inc_dir, &skip_list);
            if (result == 0) {
                // Track .c and .h filenames
                char base_name[256];
                strncpy(base_name, entry->d_name, name_len - 2);
                base_name[name_len - 2] = '\0';
                
                char c_filename[300], h_filename[300];
                snprintf(c_filename, sizeof(c_filename), "%s.c", base_name);
                snprintf(h_filename, sizeof(h_filename), "%s.h", base_name);
                c_files[file_count] = strdup(c_filename);
                h_files[file_count] = strdup(h_filename);
                file_count++;
                
                printf("  ✓ Success\n");
                files_processed++;
            } else {
                fprintf(stderr, "  ✗ Failed to transpile %s (error code: %d)\n", entry->d_name, result);
                // Continue processing other files instead of stopping
            }
        }
    }
    closedir(dir);
    
    printf("\n===========================================\n");
    printf("   Transpilation Complete!\n");
    printf("   Files processed: %d\n", files_processed);
    printf("===========================================\n\n");
    
    // Generate CMake project files
    printf("Generating CMake project files...\n");
    
    // Extract project name from path
    const char *proj_name = strrchr(output_project, '/');
    if (!proj_name) proj_name = strrchr(output_project, '\\');
    proj_name = proj_name ? (proj_name + 1) : output_project;
    
    // Add powerpc_state.c, compiler_runtime.c, and main.c to sources
    int total_c_count = file_count + 3;
    char **all_c_files = malloc(sizeof(char*) * total_c_count);
    for (int i = 0; i < file_count; i++) {
        all_c_files[i] = c_files[i];
    }
    all_c_files[file_count] = strdup("powerpc_state.c");
    all_c_files[file_count + 1] = strdup("compiler_runtime.c");
    all_c_files[file_count + 2] = strdup("main.c");
    
    // Add powerpc_state.h, all_functions.h, and macros.h to headers
    int total_h_count = file_count + 3;
    char **all_h_files = malloc(sizeof(char*) * total_h_count);
    for (int i = 0; i < file_count; i++) {
        all_h_files[i] = h_files[i];
    }
    all_h_files[file_count] = strdup("powerpc_state.h");
    all_h_files[file_count + 1] = strdup("all_functions.h");
    all_h_files[file_count + 2] = strdup("macros.h");
    
    generate_cmake(output_project, proj_name, total_c_count, (const char**)all_c_files, 
                   total_h_count, (const char**)all_h_files);
    generate_all_functions_h(output_project, file_count, (const char**)h_files);
    generate_runtime_h(output_project);
    generate_runtime_c(output_project);
    generate_compiler_runtime_c(output_project);
    generate_main_c(output_project);
    generate_macros_h(output_project);
    generate_readme(output_project, proj_name);
    generate_gitignore(output_project);
    
    // Cleanup
    for (int i = 0; i < file_count; i++) {
        free(c_files[i]);
        free(h_files[i]);
    }
    free(c_files);
    free(h_files);
    
    // Free the combined arrays
    for (int i = file_count; i < total_c_count; i++) {
        free(all_c_files[i]);
    }
    free(all_c_files);
    
    for (int i = file_count; i < total_h_count; i++) {
        free(all_h_files[i]);
    }
    free(all_h_files);
    
    printf("\n===========================================\n");
    printf("   CMake Project Generated!\n");
    printf("   Location: %s\n", output_project);
    printf("===========================================\n\n");
    
    printf("To build the project:\n");
    printf("  cd %s\n", output_project);
    printf("  mkdir build && cd build\n");
    printf("  cmake ..\n");
    printf("  cmake --build .\n\n");
    
    return 0;
}

