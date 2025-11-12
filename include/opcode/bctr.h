/**
 * @file bctr.h
 * @brief BCTR - Branch to Count Register
 * Opcode: 19 / 528 (bcctr with BO=20, BI=0)
 */

#ifndef OPCODE_BCTR_H
#define OPCODE_BCTR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    bool LK;  // Link bit
} BCTR_Instruction;

static inline bool decode_bctr(uint32_t inst, BCTR_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 19 || ((inst >> 1) & 0x3FF) != 528) return false;
    uint8_t BO = (inst >> 21) & 0x1F;
    if (BO != 20) return false;  // bctr is bcctr with BO=20 (always branch)
    d->LK = inst & 1;
    return true;
}

static inline int transpile_bctr(const BCTR_Instruction *d, uint32_t current_addr, char *o, size_t s, const char* (*lookup_func)(uint32_t)) {
    if (d->LK) {
        // bctrl - indirect call via CTR
        // Generate code that uses function address map to resolve indirect calls
        (void)lookup_func; // May be used for compile-time optimization in future
        uint32_t return_addr = current_addr + 4;
        return snprintf(o, s,
                       "{ uintptr_t saved_ctr = ctr; lr = 0x%08X; "
                       "call_function_by_address((uint32_t)saved_ctr, r3, r4, r5, r6, r7, r8, r9, r10, f1, f2); }",
                       return_addr);
    }
    // bctr without link - typically used for computed jumps (switch statements)
    // This is trickier as it's a jump, not a call
    return snprintf(o, s, "/* bctr - computed jump not yet supported */");
}

static inline int comment_bctr(const BCTR_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "bctr%s", d->LK?"l":"");
}

#endif

