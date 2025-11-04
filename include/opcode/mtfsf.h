/**
 * @file mtfsf.h
 * @brief MTFSF - Move To FPSCR Fields
 * Opcode: 63 / 711
 */

#ifndef OPCODE_MTFSF_H
#define OPCODE_MTFSF_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t FM;     // Field mask
    uint8_t frB;    // Source FP register
    bool Rc;
} MTFSF_Instruction;

static inline bool decode_mtfsf(uint32_t inst, MTFSF_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 63 || ((inst >> 1) & 0x3FF) != 711) return false;
    d->FM = (inst >> 17) & 0xFF;
    d->frB = (inst >> 11) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_mtfsf(const MTFSF_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "/* mtfsf with mask 0x%02X */ fpscr = (uint32_t)f%u;", d->FM, d->frB);
    if (d->Rc) w += snprintf(o + w, s - w, "\ncr1 = (fpscr >> 28) & 0xF;");
    return w;
}

static inline int comment_mtfsf(const MTFSF_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mtfsf%s %u, f%u", d->Rc?".":"", d->FM, d->frB);
}

#endif

