#ifndef PORPOISE_PPC_FP_H
#define PORPOISE_PPC_FP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Raw binary32 arithmetic used by Gekko paired-single instructions.
 *
 * These routines are deliberately independent of the host floating-point
 * environment.  They consume the original IEEE-754 encodings, form the exact
 * mathematical intermediate with integer arithmetic, and round once at the
 * paired-single destination boundary.
 */
typedef enum PorpoisePpcFpInvalidCause {
    PORPOISE_PPC_FP_INVALID_NONE = 0,
    PORPOISE_PPC_FP_INVALID_SNAN = 1U << 0,
    PORPOISE_PPC_FP_INVALID_INFINITY_MINUS_INFINITY = 1U << 1,
    PORPOISE_PPC_FP_INVALID_INFINITY_TIMES_ZERO = 1U << 2
} PorpoisePpcFpInvalidCause;

typedef struct PorpoisePpcFp32Result {
    uint32_t bits;
    /* Binary64 encoding of the single-significand result after the Gekko
     * enabled-exception exponent adjustment (-192 for overflow, +192 for
     * underflow). Valid only when adjusted_result_valid is nonzero. */
    uint64_t adjusted_bits;
    uint32_t invalid_causes;
    int inexact;
    int incremented;
    int overflow;
    int underflow;
    int tiny_before_rounding;
    int adjusted_result_valid;
    int adjusted_inexact;
    int adjusted_incremented;
} PorpoisePpcFp32Result;

/* FPSCR RN encoding: 0 nearest/even, 1 toward zero, 2 +infinity, 3 -infinity. */
int porpoise_ppc_fp32_mul(
    uint32_t multiplicand_bits,
    uint32_t multiplier_bits,
    unsigned int rounding_mode,
    int non_ieee_mode,
    PorpoisePpcFp32Result *result_out);

/* Computes multiplicand * multiplier + addend with no intermediate rounding. */
int porpoise_ppc_fp32_madd(
    uint32_t multiplicand_bits,
    uint32_t multiplier_bits,
    uint32_t addend_bits,
    unsigned int rounding_mode,
    int non_ieee_mode,
    PorpoisePpcFp32Result *result_out);

#ifdef __cplusplus
}
#endif

#endif
