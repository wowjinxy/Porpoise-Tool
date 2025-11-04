/**
 * @file la.h
 * @brief LA - Load Address (addi pseudo-op)
 * Opcode: 14 (addi with symbolic address)
 */

#ifndef OPCODE_LA_H
#define OPCODE_LA_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t rD, rA;
    int16_t d;
} LA_Instruction;

// Handled by addi, placeholder
static inline bool decode_la(uint32_t inst, LA_Instruction *d) {
    (void)inst; (void)d;
    return false;
}

static inline int transpile_la(const LA_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "r%u = r%u + (int16_t)0x%x;", d->rD, d->rA, (uint16_t)d->d);
}

static inline int comment_la(const LA_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "la r%u, 0x%x(r%u)", d->rD, (uint16_t)d->d, d->rA);
}

#endif

