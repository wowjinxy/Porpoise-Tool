/**
 * @file inslwi.h
 * @brief INSLWI - Insert from Left Immediate (rlwimi pseudo-op)
 */

#ifndef OPCODE_INSLWI_H
#define OPCODE_INSLWI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t rA, rS, n, b;
    bool Rc;
} INSLWI_Instruction;

static inline bool decode_inslwi(uint32_t inst, INSLWI_Instruction *d) {
    (void)inst; (void)d;
    return false;
}

static inline int transpile_inslwi(const INSLWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, ";  /* inslwi - insert (rlwimi pseudo-op) */");
}

static inline int comment_inslwi(const INSLWI_Instruction *d, char *o, size_t s) {
    return snprintf(o, s, "inslwi r%u, r%u, %u, %u", d->rA, d->rS, d->n, d->b);
}

#endif

