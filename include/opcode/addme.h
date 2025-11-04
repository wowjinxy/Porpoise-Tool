/**
 * @file addme.h
 * @brief ADDME - Add to Minus One Extended
 * 
 * Opcode: 31 / 234
 * Format: XO-form
 * Syntax: addme rD, rA
 *         addme. rD, rA (with Rc=1)
 *         addmeo rD, rA (with OE=1)
 *         addmeo. rD, rA (with OE=1, Rc=1)
 * 
 * Description: rD = rA + CA - 1
 * CA is the carry bit from XER[29]
 */

#ifndef OPCODE_ADDME_H
#define OPCODE_ADDME_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define OP_ADDME_PRIMARY    31
#define OP_ADDME_EXTENDED   234

typedef struct {
    uint8_t rD;
    uint8_t rA;
    bool OE;                    // Overflow enable
    bool Rc;                    // Record bit
} ADDME_Instruction;

static inline bool decode_addme(uint32_t inst, ADDME_Instruction *d) {
    if (((inst >> 26) & 0x3F) != OP_ADDME_PRIMARY) return false;
    if (((inst >> 1) & 0x3FF) != OP_ADDME_EXTENDED) return false;
    
    d->rD = (inst >> 21) & 0x1F;
    d->rA = (inst >> 16) & 0x1F;
    d->OE = (inst >> 10) & 1;
    d->Rc = inst & 1;
    return true;
}

static inline int transpile_addme(const ADDME_Instruction *d, char *o, size_t s) {
    int w = 0;
    
    // rD = rA + CA - 1
    w += snprintf(o + w, s - w,
                 "{ uint32_t ca = (xer >> 29) & 1; "
                 "r%u = r%u + ca - 1; ",
                 d->rD, d->rA);
    
    // Update carry flag: carry if result wrapped around
    w += snprintf(o + w, s - w,
                 "if (ca) { xer |= 0x20000000; } else { xer &= ~0x20000000; } }");
    
    // Handle overflow if OE bit set
    if (d->OE) {
        w += snprintf(o + w, s - w,
                     "\nif (((r%u ^ r%u) & 0x80000000) && "
                     "((r%u ^ r%u) & 0x80000000)) { "
                     "xer |= 0xC0000000; } else { xer &= ~0x80000000; }",
                     d->rA, d->rD, d->rA, d->rD);
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

static inline int comment_addme(const ADDME_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "addme%s%s r%u, r%u",
                   d->OE ? "o" : "",
                   d->Rc ? "." : "",
                   d->rD, d->rA);
}

#endif // OPCODE_ADDME_H

