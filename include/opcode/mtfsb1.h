/**
 * @file mtfsb1.h
 * @brief MTFSB1 - Move To FPSCR Bit 1
 * Opcode: 63 / 38
 */

#ifndef OPCODE_MTFSB1_H
#define OPCODE_MTFSB1_H

#include <stdint.h>
#include <stdbool.h>

#define OP_MTFSB1_PRIMARY    63
#define OP_MTFSB1_EXTENDED   38

typedef struct {
    uint8_t crbD;
    bool Rc;
} MTFSB1_Instruction;

static inline bool decode_mtfsb1(uint32_t inst, MTFSB1_Instruction *d) {
    if (((inst >> 26) & 0x3F) != 63 || ((inst >> 1) & 0x3FF) != 38) return false;
    d->crbD = (inst >> 21) & 0x1F;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_mtfsb1(const MTFSB1_Instruction *d, char *o, size_t s) {
    int w = snprintf(o, s, "fpscr |= (1U << (31-%u));", d->crbD);
    if (d->Rc) w += snprintf(o + w, s - w, "\ncr1 = (fpscr >> 28) & 0xF;");
    return w;
}

static inline int comment_mtfsb1(const MTFSB1_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "mtfsb1%s %u", d->Rc?".":"", d->crbD);
}

#endif

