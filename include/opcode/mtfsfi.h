/**
 * @file mtfsfi.h
 * @brief MTFSFI - Move To FPSCR Field Immediate
 * Opcode: 63 / 134
 */

#ifndef OPCODE_MTFSFI_H
#define OPCODE_MTFSFI_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MTFSFI_PRIMARY    63
#define OP_MTFSFI_EXTENDED   134

typedef struct {
    uint8_t crfD, IMM;
    bool Rc;
} MTFSFI_Instruction;

static inline bool decode_mtfsfi(uint32_t inst, MTFSFI_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 63 || ((inst >> 1) & 0x3FF) != 134) return false;
    d->crfD = (inst >> 23) & 0x7;
    d->IMM = (inst >> 12) & 0xF;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_mtfsfi(const MTFSFI_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "fpscr = (fpscr & ~(0xFU << (28-%u*4))) | ((uint32_t)0x%x << (28-%u*4));",
                     d->crfD, d->IMM, d->crfD);
    if (d->Rc) w += snprintf(o + w, s - w, "\ncr1 = (fpscr >> 28) & 0xF;");
    return w;
}

static inline int comment_mtfsfi(const MTFSFI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mtfsfi%s %u, %u", d->Rc?".":"", d->crfD, d->IMM);
}

#endif

