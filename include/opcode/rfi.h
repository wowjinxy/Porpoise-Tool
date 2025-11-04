/**
 * @file rfi.h
 * @brief RFI - Return From Interrupt
 * 
 * Opcode: 19 / 50
 * Syntax: rfi
 * Description: Return from interrupt (supervisor only)
 */

#ifndef OPCODE_RFI_H
#define OPCODE_RFI_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_RFI_PRIMARY      19
#define OP_RFI_EXTENDED     50

typedef struct {
    uint8_t dummy;
} RFI_Instruction;

static inline bool decode_rfi(uint32_t inst, RFI_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 19 || ((inst >> 1) & 0x3FF) != 50) return false;
    (void)d;
    return true;
}

static inline int transpile_rfi(const RFI_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, "/* RFI - restore MSR from SRR1, branch to SRR0 */ msr = srr1; pc = srr0;");
}

static inline int comment_rfi(const RFI_Instruction *d, char *o, size_t s) {
    (void)d;
    return snprintf(o, s, "rfi");
}

#endif

