#include "porpoise_ppc_fp.h"

#include <stdint.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) abort(); } while (0)

static PorpoisePpcFp32Result multiply(
    uint32_t a,
    uint32_t c,
    unsigned int rounding_mode,
    int non_ieee_mode)
{
    PorpoisePpcFp32Result result;
    CHECK(porpoise_ppc_fp32_mul(
        a,
        c,
        rounding_mode,
        non_ieee_mode,
        &result));
    return result;
}

static PorpoisePpcFp32Result multiply_add(
    uint32_t a,
    uint32_t c,
    uint32_t b,
    unsigned int rounding_mode,
    int non_ieee_mode)
{
    PorpoisePpcFp32Result result;
    CHECK(porpoise_ppc_fp32_madd(
        a,
        c,
        b,
        rounding_mode,
        non_ieee_mode,
        &result));
    return result;
}

static void test_multiply_rounding_modes(void)
{
    static const uint32_t positive_results[4] = {
        UINT32_C(0x3F800002),
        UINT32_C(0x3F800002),
        UINT32_C(0x3F800003),
        UINT32_C(0x3F800002),
    };
    static const uint32_t negative_results[4] = {
        UINT32_C(0xBF800002),
        UINT32_C(0xBF800002),
        UINT32_C(0xBF800002),
        UINT32_C(0xBF800003),
    };
    unsigned int rounding_mode;

    for (rounding_mode = 0U; rounding_mode < 4U; rounding_mode++) {
        PorpoisePpcFp32Result positive = multiply(
            UINT32_C(0x3F800001),
            UINT32_C(0x3F800001),
            rounding_mode,
            0);
        PorpoisePpcFp32Result negative = multiply(
            UINT32_C(0xBF800001),
            UINT32_C(0x3F800001),
            rounding_mode,
            0);

        CHECK(positive.bits == positive_results[rounding_mode]);
        CHECK(negative.bits == negative_results[rounding_mode]);
        CHECK(positive.inexact && negative.inexact);
        CHECK(positive.incremented == (rounding_mode == 2U));
        CHECK(negative.incremented == (rounding_mode == 3U));
    }

    {
        PorpoisePpcFp32Result exact = multiply(
            UINT32_C(0x40000000),
            UINT32_C(0x40400000),
            0U,
            0);
        CHECK(exact.bits == UINT32_C(0x40C00000));
        CHECK(!exact.inexact && !exact.incremented);
    }
}

static void test_fused_rounding(void)
{
    PorpoisePpcFp32Result fused = multiply_add(
        UINT32_C(0x3F800001),
        UINT32_C(0x3F800001),
        UINT32_C(0xBF800002),
        0U,
        0);
    PorpoisePpcFp32Result double_rounding = multiply_add(
        UINT32_C(0x42480000),
        UINT32_C(0xBC88CC38),
        UINT32_C(0x1B1C72A0),
        0U,
        0);
    unsigned int rounding_mode;

    CHECK(fused.bits == UINT32_C(0x28800000));
    CHECK(double_rounding.bits == UINT32_C(0xBF55BF17));

    for (rounding_mode = 0U; rounding_mode < 4U; rounding_mode++) {
        PorpoisePpcFp32Result cancellation = multiply_add(
            UINT32_C(0x3F800000),
            UINT32_C(0x3F800000),
            UINT32_C(0xBF800000),
            rounding_mode,
            0);
        CHECK(cancellation.bits ==
              (rounding_mode == 3U ? UINT32_C(0x80000000) : 0U));
        CHECK(!cancellation.inexact);
    }
}

static void test_tiny_overflow_and_non_ieee(void)
{
    PorpoisePpcFp32Result exact_subnormal = multiply(
        UINT32_C(0x00000001),
        UINT32_C(0x3F800000),
        0U,
        0);
    PorpoisePpcFp32Result flushed_operand = multiply(
        UINT32_C(0x00000001),
        UINT32_C(0x3F800000),
        0U,
        1);
    PorpoisePpcFp32Result flushed_result = multiply(
        UINT32_C(0x00800000),
        UINT32_C(0x3F000000),
        0U,
        1);
    PorpoisePpcFp32Result underflow = multiply(
        UINT32_C(0x00800000),
        UINT32_C(0x3F7FFFFF),
        1U,
        0);
    PorpoisePpcFp32Result overflow = multiply(
        UINT32_C(0x7F7FFFFF),
        UINT32_C(0x40000000),
        0U,
        0);

    CHECK(exact_subnormal.bits == UINT32_C(0x00000001));
    CHECK(exact_subnormal.tiny_before_rounding);
    CHECK(!exact_subnormal.underflow && !exact_subnormal.inexact);
    CHECK(exact_subnormal.adjusted_result_valid);
    CHECK(exact_subnormal.adjusted_bits == UINT64_C(0x42A0000000000000));

    CHECK(flushed_operand.bits == 0U);
    CHECK(!flushed_operand.inexact && !flushed_operand.underflow);
    CHECK(flushed_result.bits == 0U);
    CHECK(flushed_result.tiny_before_rounding);
    CHECK(flushed_result.inexact && flushed_result.underflow);

    CHECK(underflow.bits == UINT32_C(0x007FFFFF));
    CHECK(underflow.tiny_before_rounding && underflow.underflow);
    CHECK(underflow.inexact && !underflow.incremented);

    CHECK(overflow.bits == UINT32_C(0x7F800000));
    CHECK(overflow.overflow && overflow.inexact);
    CHECK(overflow.adjusted_result_valid);
    CHECK(overflow.adjusted_bits == UINT64_C(0x3BFFFFFFE0000000));
}

static void test_special_values_and_nan_priority(void)
{
    PorpoisePpcFp32Result invalid_multiply = multiply(
        UINT32_C(0x7F800000),
        0U,
        0U,
        0);
    PorpoisePpcFp32Result invalid_add = multiply_add(
        UINT32_C(0x7F800000),
        UINT32_C(0x3F800000),
        UINT32_C(0xFF800000),
        0U,
        0);
    PorpoisePpcFp32Result priority = multiply_add(
        UINT32_C(0x7FC11111),
        UINT32_C(0x7FC33333),
        UINT32_C(0xFFC22222),
        0U,
        0);
    PorpoisePpcFp32Result combined = multiply_add(
        UINT32_C(0x7F800000),
        0U,
        UINT32_C(0x7F812345),
        0U,
        0);

    CHECK(invalid_multiply.bits == UINT32_C(0x7FC00000));
    CHECK(invalid_multiply.invalid_causes ==
          PORPOISE_PPC_FP_INVALID_INFINITY_TIMES_ZERO);
    CHECK(invalid_add.bits == UINT32_C(0x7FC00000));
    CHECK(invalid_add.invalid_causes ==
          PORPOISE_PPC_FP_INVALID_INFINITY_MINUS_INFINITY);
    CHECK(priority.bits == UINT32_C(0x7FC11111));
    CHECK(priority.invalid_causes == PORPOISE_PPC_FP_INVALID_NONE);
    CHECK(combined.bits == UINT32_C(0x7FC12345));
    CHECK(combined.invalid_causes ==
          (PORPOISE_PPC_FP_INVALID_SNAN |
           PORPOISE_PPC_FP_INVALID_INFINITY_TIMES_ZERO));
}

int main(void)
{
    PorpoisePpcFp32Result unused;

    CHECK(!porpoise_ppc_fp32_mul(0U, 0U, 4U, 0, &unused));
    CHECK(!porpoise_ppc_fp32_madd(0U, 0U, 0U, 0U, 0, NULL));
    test_multiply_rounding_modes();
    test_fused_rounding();
    test_tiny_overflow_and_non_ieee();
    test_special_values_and_nan_priority();
    return 0;
}
