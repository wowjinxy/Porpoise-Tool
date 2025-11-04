/**
 * @file mtfsb0.h
 * @brief MTFSB0 - Move To FPSCR Bit 0
 * Opcode: 63 / 70
 */

#ifndef OPCODE_MTFSB0_H
#define OPCODE_MTFSB0_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MTFSB0_PRIMARY    63
#define OP_MTFSB0_EXTENDED   70

typedef struct {
    uint8_t crbD;
    bool Rc;
} MTFSB0_Instruction;

static inline bool decode_mtfsb0(uint32_t inst, MTFSB0_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 63 || ((inst >> 1) & 0x3FF) != 70) return false;
    d->crbD = (inst >> 21) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_mtfsb0(const MTFSB0_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "fpscr &= ~(1U << (31-%u));", d->crbD);
    if (d->Rc) w += snprintf(o + w, s - w, "\ncr1 = (fpscr >> 28) & 0xF;");
    return w;
}

static inline int comment_mtfsb0(const MTFSB0_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mtfsb0%s %u", d->Rc?".":"", d->crbD);
}

#endif

