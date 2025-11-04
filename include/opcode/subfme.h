/**
 * @file subfme.h
 * @brief SUBFME - Subtract From Minus One Extended
 * 
 * Opcode: 31 / 232
 * Format: XO-form
 * Syntax: subfme rD, rA
 *         subfme. rD, rA (with Rc=1)
 *         subfmeo rD, rA (with OE=1)
 *         subfmeo. rD, rA (with OE=1, Rc=1)
 * 
 * Description: rD = ~rA + CA = -1 - rA + CA
 * CA is the carry bit from XER[29]
 */

#ifndef OPCODE_SUBFME_H
#define OPCODE_SUBFME_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_SUBFME_PRIMARY    31
#define OP_SUBFME_EXTENDED   232

typedef struct {
    uint8_t rD;
    uint8_t rA;
    bool OE;                    // Overflow enable
    bool Rc;                    // Record bit
} SUBFME_Instruction;

static inline bool decode_subfme(uint32_t inst, SUBFME_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_SUBFME_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_SUBFME_EXTENDED) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->OE = (inst >> 10) & 1;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_subfme(const SUBFME_Instruction *d, char *o, size_t s) {
    int w = 0;
    
    // rD = ~rA + CA = -1 - rA + CA
    w += snprintf(o + w, s - w,
                 "{ uint32_t ca = (xer >> 29) & 1; "
                 "r%u = ~r%u + ca; ",
                 d->rD, d->rA);
    
    // Update carry flag
    w += snprintf(o + w, s - w,
                 "if (r%u >= ca) { xer |= 0x20000000; } "
                 "else { xer &= ~0x20000000; } }",
                 d->rD);
    
    // Handle overflow if OE bit set
    if (d->OE) {
        w += snprintf(o + w, s - w,
                     "\nif (r%u == 0x80000000) { "
                     "xer |= 0xC0000000; } else { xer &= ~0x80000000; }",
                     d->rA);
    }
    
    // Update CR0 if Rc bit set
    if (d->Rc) {
        w += snprintf(o + w, s - w,
                     "\ncr0 = ((int32_t)r%u < 0 ? 0x8 : "
                     "(int32_t)r%u > 0 ? 0x4 : 0x2) | (xer >> 28 & 0x1);",
                     d->rD, d->rD);
    }
    
    return w;
}

static inline int comment_subfme(const SUBFME_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "subfme%s%s r%u, r%u",
                   d->OE ? "o" : "",
                   d->Rc ? "." : "",
                   d->rD, d->rA);
}

#endif // OPCODE_SUBFME_H

