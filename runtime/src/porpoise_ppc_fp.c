#include "porpoise_ppc_fp.h"

#include <stddef.h>
#include <string.h>

#define PORPOISE_F32_SIGN_MASK UINT32_C(0x80000000)
#define PORPOISE_F32_EXPONENT_MASK UINT32_C(0x7F800000)
#define PORPOISE_F32_FRACTION_MASK UINT32_C(0x007FFFFF)
#define PORPOISE_F32_QUIET_MASK UINT32_C(0x00400000)
#define PORPOISE_F32_DEFAULT_QNAN UINT32_C(0x7FC00000)

/* The complete exact range of a binary32 FMA spans fewer than 426 bits. */
#define PORPOISE_BIG_LIMBS 8U

typedef struct PorpoiseBigUnsigned {
    uint64_t limb[PORPOISE_BIG_LIMBS];
} PorpoiseBigUnsigned;

typedef enum PorpoiseFp32Class {
    PORPOISE_FP32_ZERO = 0,
    PORPOISE_FP32_FINITE,
    PORPOISE_FP32_INFINITY,
    PORPOISE_FP32_NAN
} PorpoiseFp32Class;

typedef struct PorpoiseDecodedFp32 {
    PorpoiseFp32Class value_class;
    uint32_t bits;
    uint32_t significand;
    int exponent;
    int negative;
    int signaling;
} PorpoiseDecodedFp32;

static PorpoiseDecodedFp32 porpoise_decode_fp32(
    uint32_t bits,
    int non_ieee_mode)
{
    PorpoiseDecodedFp32 decoded;
    unsigned int exponent_field = (bits >> 23U) & 0xFFU;
    uint32_t fraction = bits & PORPOISE_F32_FRACTION_MASK;

    memset(&decoded, 0, sizeof(decoded));
    decoded.bits = bits;
    decoded.negative = (bits & PORPOISE_F32_SIGN_MASK) != 0U;
    if (exponent_field == 0xFFU) {
        decoded.value_class = fraction == 0U
                                  ? PORPOISE_FP32_INFINITY
                                  : PORPOISE_FP32_NAN;
        decoded.signaling = fraction != 0U &&
                            (fraction & PORPOISE_F32_QUIET_MASK) == 0U;
        return decoded;
    }
    if (exponent_field == 0U) {
        if (fraction == 0U || non_ieee_mode) {
            decoded.value_class = PORPOISE_FP32_ZERO;
            decoded.bits &= PORPOISE_F32_SIGN_MASK;
            return decoded;
        }
        decoded.value_class = PORPOISE_FP32_FINITE;
        decoded.significand = fraction;
        decoded.exponent = -149;
        return decoded;
    }

    decoded.value_class = PORPOISE_FP32_FINITE;
    decoded.significand = UINT32_C(0x00800000) | fraction;
    decoded.exponent = (int)exponent_field - 127 - 23;
    return decoded;
}

static uint32_t porpoise_quiet_nan(uint32_t bits)
{
    return bits | PORPOISE_F32_QUIET_MASK;
}

static int porpoise_big_is_zero(const PorpoiseBigUnsigned *value)
{
    size_t index;
    for (index = 0U; index < PORPOISE_BIG_LIMBS; index++) {
        if (value->limb[index] != 0U) {
            return 0;
        }
    }
    return 1;
}

static void porpoise_big_from_shifted_u64(
    PorpoiseBigUnsigned *result,
    uint64_t value,
    unsigned int shift)
{
    unsigned int word = shift / 64U;
    unsigned int offset = shift % 64U;

    memset(result, 0, sizeof(*result));
    if (value == 0U || word >= PORPOISE_BIG_LIMBS) {
        return;
    }
    result->limb[word] = value << offset;
    if (offset != 0U && word + 1U < PORPOISE_BIG_LIMBS) {
        result->limb[word + 1U] = value >> (64U - offset);
    }
}

static int porpoise_big_compare(
    const PorpoiseBigUnsigned *left,
    const PorpoiseBigUnsigned *right)
{
    size_t index = PORPOISE_BIG_LIMBS;
    while (index-- != 0U) {
        if (left->limb[index] < right->limb[index]) {
            return -1;
        }
        if (left->limb[index] > right->limb[index]) {
            return 1;
        }
    }
    return 0;
}

static void porpoise_big_add(
    PorpoiseBigUnsigned *result,
    const PorpoiseBigUnsigned *left,
    const PorpoiseBigUnsigned *right)
{
    uint64_t carry = 0U;
    size_t index;

    for (index = 0U; index < PORPOISE_BIG_LIMBS; index++) {
        uint64_t partial = left->limb[index] + right->limb[index];
        uint64_t partial_carry = partial < left->limb[index];
        uint64_t sum = partial + carry;
        uint64_t carry_carry = sum < partial;
        result->limb[index] = sum;
        carry = partial_carry | carry_carry;
    }
}

/* left must be greater than or equal to right. */
static void porpoise_big_subtract(
    PorpoiseBigUnsigned *result,
    const PorpoiseBigUnsigned *left,
    const PorpoiseBigUnsigned *right)
{
    uint64_t borrow = 0U;
    size_t index;

    for (index = 0U; index < PORPOISE_BIG_LIMBS; index++) {
        uint64_t subtrahend = right->limb[index] + borrow;
        uint64_t add_overflow = subtrahend < right->limb[index];
        uint64_t next_borrow = add_overflow |
                               (left->limb[index] < subtrahend);
        result->limb[index] = left->limb[index] - subtrahend;
        borrow = next_borrow;
    }
}

static int porpoise_big_highest_bit(const PorpoiseBigUnsigned *value)
{
    size_t index = PORPOISE_BIG_LIMBS;
    while (index-- != 0U) {
        uint64_t word = value->limb[index];
        if (word != 0U) {
            unsigned int bit = 0U;
            while ((word >>= 1U) != 0U) {
                bit++;
            }
            return (int)(index * 64U + bit);
        }
    }
    return -1;
}

static int porpoise_big_test_bit(
    const PorpoiseBigUnsigned *value,
    unsigned int bit)
{
    unsigned int word = bit / 64U;
    if (word >= PORPOISE_BIG_LIMBS) {
        return 0;
    }
    return (value->limb[word] &
            (UINT64_C(1) << (bit % 64U))) != 0U;
}

static int porpoise_big_any_low_bits(
    const PorpoiseBigUnsigned *value,
    unsigned int bit_count)
{
    unsigned int full_words = bit_count / 64U;
    unsigned int partial_bits = bit_count % 64U;
    unsigned int index;

    if (full_words > PORPOISE_BIG_LIMBS) {
        full_words = PORPOISE_BIG_LIMBS;
    }
    for (index = 0U; index < full_words; index++) {
        if (value->limb[index] != 0U) {
            return 1;
        }
    }
    if (partial_bits != 0U && full_words < PORPOISE_BIG_LIMBS) {
        uint64_t mask = (UINT64_C(1) << partial_bits) - UINT64_C(1);
        if ((value->limb[full_words] & mask) != 0U) {
            return 1;
        }
    }
    return 0;
}

static uint64_t porpoise_big_shift_right_low64(
    const PorpoiseBigUnsigned *value,
    unsigned int shift)
{
    unsigned int word = shift / 64U;
    unsigned int offset = shift % 64U;
    uint64_t result;

    if (word >= PORPOISE_BIG_LIMBS) {
        return 0U;
    }
    result = value->limb[word] >> offset;
    if (offset != 0U && word + 1U < PORPOISE_BIG_LIMBS) {
        result |= value->limb[word + 1U] << (64U - offset);
    }
    return result;
}

static uint64_t porpoise_round_big_right(
    const PorpoiseBigUnsigned *value,
    unsigned int shift,
    int negative,
    unsigned int rounding_mode,
    int *inexact_out,
    int *incremented_out)
{
    uint64_t retained;
    int inexact;
    int increment = 0;

    if (shift == 0U) {
        *inexact_out = 0;
        *incremented_out = 0;
        return porpoise_big_shift_right_low64(value, 0U);
    }

    retained = porpoise_big_shift_right_low64(value, shift);
    inexact = porpoise_big_any_low_bits(value, shift);
    if (inexact) {
        if (rounding_mode == 0U) {
            int half = porpoise_big_test_bit(value, shift - 1U);
            int below_half = porpoise_big_any_low_bits(value, shift - 1U);
            increment = half && (below_half || (retained & 1U) != 0U);
        } else if (rounding_mode == 2U && !negative) {
            increment = 1;
        } else if (rounding_mode == 3U && negative) {
            increment = 1;
        }
    }
    *inexact_out = inexact;
    *incremented_out = increment;
    return retained + (uint64_t)increment;
}

static uint32_t porpoise_overflow_result(
    int negative,
    unsigned int rounding_mode)
{
    uint32_t sign = negative ? PORPOISE_F32_SIGN_MASK : 0U;
    int to_infinity = rounding_mode == 0U ||
                      (rounding_mode == 2U && !negative) ||
                      (rounding_mode == 3U && negative);
    return sign | (to_infinity
                       ? PORPOISE_F32_EXPONENT_MASK
                       : UINT32_C(0x7F7FFFFF));
}

static uint64_t porpoise_adjusted_binary64_result(
    const PorpoiseBigUnsigned *magnitude,
    int binary_exponent,
    int exponent_adjustment,
    int negative,
    unsigned int rounding_mode,
    int *inexact_out,
    int *incremented_out)
{
    int highest = porpoise_big_highest_bit(magnitude);
    int result_exponent = highest + binary_exponent + exponent_adjustment;
    int shift = highest - 23;
    uint64_t rounded;
    uint64_t sign = negative ? UINT64_C(0x8000000000000000) : 0U;

    if (shift > 0) {
        rounded = porpoise_round_big_right(
            magnitude,
            (unsigned int)shift,
            negative,
            rounding_mode,
            inexact_out,
            incremented_out);
    } else {
        rounded = porpoise_big_shift_right_low64(magnitude, 0U)
                  << (unsigned int)(-shift);
        *inexact_out = 0;
        *incremented_out = 0;
    }
    if (rounded >= UINT64_C(0x01000000)) {
        rounded >>= 1U;
        result_exponent++;
    }

    /* Binary32 overflow/underflow adjusted by 192 is always a normal f64. */
    return sign |
           ((uint64_t)(result_exponent + 1023) << 52U) |
           ((rounded & UINT64_C(0x007FFFFF)) << 29U);
}

static void porpoise_round_exact_to_fp32(
    const PorpoiseBigUnsigned *magnitude,
    int binary_exponent,
    int negative,
    unsigned int rounding_mode,
    int non_ieee_mode,
    PorpoisePpcFp32Result *result)
{
    int highest = porpoise_big_highest_bit(magnitude);
    int result_exponent = highest + binary_exponent;
    uint64_t rounded;

    result->bits = negative ? PORPOISE_F32_SIGN_MASK : 0U;
    if (highest < 0) {
        return;
    }

    /* Gekko NI mode flushes a tiny pre-round single result to signed zero. */
    if (non_ieee_mode && result_exponent < -126) {
        result->tiny_before_rounding = 1;
        result->inexact = 1;
        result->underflow = 1;
        result->adjusted_bits = porpoise_adjusted_binary64_result(
            magnitude,
            binary_exponent,
            192,
            negative,
            rounding_mode,
            &result->adjusted_inexact,
            &result->adjusted_incremented);
        result->adjusted_result_valid = 1;
        return;
    }

    if (result_exponent >= -126) {
        int shift = highest - 23;
        if (shift > 0) {
            rounded = porpoise_round_big_right(
                magnitude,
                (unsigned int)shift,
                negative,
                rounding_mode,
                &result->inexact,
                &result->incremented);
        } else {
            rounded = porpoise_big_shift_right_low64(magnitude, 0U)
                      << (unsigned int)(-shift);
        }
        if (rounded >= UINT64_C(0x01000000)) {
            rounded >>= 1U;
            result_exponent++;
        }
        if (result_exponent > 127) {
            result->adjusted_bits = porpoise_adjusted_binary64_result(
                magnitude,
                binary_exponent,
                -192,
                negative,
                rounding_mode,
                &result->adjusted_inexact,
                &result->adjusted_incremented);
            result->bits = porpoise_overflow_result(
                negative,
                rounding_mode);
            result->inexact = 1;
            result->incremented = 0;
            result->overflow = 1;
            result->adjusted_result_valid = 1;
            return;
        }
        result->bits |= ((uint32_t)(result_exponent + 127) << 23U) |
                        ((uint32_t)rounded & PORPOISE_F32_FRACTION_MASK);
        return;
    }

    {
        int shift = -149 - binary_exponent;
        result->tiny_before_rounding = 1;
        if (shift > 0) {
            rounded = porpoise_round_big_right(
                magnitude,
                (unsigned int)shift,
                negative,
                rounding_mode,
                &result->inexact,
                &result->incremented);
        } else {
            rounded = porpoise_big_shift_right_low64(magnitude, 0U)
                      << (unsigned int)(-shift);
        }
        result->underflow = result->inexact;
        result->adjusted_bits = porpoise_adjusted_binary64_result(
            magnitude,
            binary_exponent,
            192,
            negative,
            rounding_mode,
            &result->adjusted_inexact,
            &result->adjusted_incremented);
        result->adjusted_result_valid = 1;
        if (rounded >= UINT64_C(0x00800000)) {
            result->bits |= UINT32_C(0x00800000);
        } else {
            result->bits |= (uint32_t)rounded;
        }
    }
}

static void porpoise_set_special_result(
    PorpoisePpcFp32Result *result,
    uint32_t bits,
    uint32_t invalid_causes)
{
    memset(result, 0, sizeof(*result));
    result->bits = bits;
    result->invalid_causes = invalid_causes;
}

static uint32_t porpoise_snan_causes2(
    const PorpoiseDecodedFp32 *a,
    const PorpoiseDecodedFp32 *b)
{
    return a->signaling || b->signaling
               ? PORPOISE_PPC_FP_INVALID_SNAN
               : PORPOISE_PPC_FP_INVALID_NONE;
}

static uint32_t porpoise_snan_causes3(
    const PorpoiseDecodedFp32 *a,
    const PorpoiseDecodedFp32 *b,
    const PorpoiseDecodedFp32 *c)
{
    return a->signaling || b->signaling || c->signaling
               ? PORPOISE_PPC_FP_INVALID_SNAN
               : PORPOISE_PPC_FP_INVALID_NONE;
}

int porpoise_ppc_fp32_mul(
    uint32_t multiplicand_bits,
    uint32_t multiplier_bits,
    unsigned int rounding_mode,
    int non_ieee_mode,
    PorpoisePpcFp32Result *result_out)
{
    PorpoiseDecodedFp32 a;
    PorpoiseDecodedFp32 c;
    PorpoiseBigUnsigned product;
    uint32_t causes;
    int negative;

    if (result_out == NULL || rounding_mode > 3U) {
        return 0;
    }
    a = porpoise_decode_fp32(multiplicand_bits, non_ieee_mode != 0);
    c = porpoise_decode_fp32(multiplier_bits, non_ieee_mode != 0);
    causes = porpoise_snan_causes2(&a, &c);

    if (a.value_class == PORPOISE_FP32_NAN ||
        c.value_class == PORPOISE_FP32_NAN) {
        uint32_t selected = a.value_class == PORPOISE_FP32_NAN
                                ? a.bits
                                : c.bits;
        porpoise_set_special_result(
            result_out,
            porpoise_quiet_nan(selected),
            causes);
        return 1;
    }
    if ((a.value_class == PORPOISE_FP32_ZERO &&
         c.value_class == PORPOISE_FP32_INFINITY) ||
        (a.value_class == PORPOISE_FP32_INFINITY &&
         c.value_class == PORPOISE_FP32_ZERO)) {
        porpoise_set_special_result(
            result_out,
            PORPOISE_F32_DEFAULT_QNAN,
            PORPOISE_PPC_FP_INVALID_INFINITY_TIMES_ZERO);
        return 1;
    }

    negative = a.negative ^ c.negative;
    if (a.value_class == PORPOISE_FP32_INFINITY ||
        c.value_class == PORPOISE_FP32_INFINITY) {
        porpoise_set_special_result(
            result_out,
            (negative ? PORPOISE_F32_SIGN_MASK : 0U) |
                PORPOISE_F32_EXPONENT_MASK,
            PORPOISE_PPC_FP_INVALID_NONE);
        return 1;
    }
    if (a.value_class == PORPOISE_FP32_ZERO ||
        c.value_class == PORPOISE_FP32_ZERO) {
        porpoise_set_special_result(
            result_out,
            negative ? PORPOISE_F32_SIGN_MASK : 0U,
            PORPOISE_PPC_FP_INVALID_NONE);
        return 1;
    }

    memset(result_out, 0, sizeof(*result_out));
    porpoise_big_from_shifted_u64(
        &product,
        (uint64_t)a.significand * (uint64_t)c.significand,
        0U);
    porpoise_round_exact_to_fp32(
        &product,
        a.exponent + c.exponent,
        negative,
        rounding_mode,
        non_ieee_mode != 0,
        result_out);
    return 1;
}

int porpoise_ppc_fp32_madd(
    uint32_t multiplicand_bits,
    uint32_t multiplier_bits,
    uint32_t addend_bits,
    unsigned int rounding_mode,
    int non_ieee_mode,
    PorpoisePpcFp32Result *result_out)
{
    PorpoiseDecodedFp32 a;
    PorpoiseDecodedFp32 b;
    PorpoiseDecodedFp32 c;
    PorpoiseBigUnsigned product;
    PorpoiseBigUnsigned addend;
    PorpoiseBigUnsigned magnitude;
    uint64_t product_significand;
    uint32_t causes;
    int product_exponent;
    int common_exponent;
    int product_negative;
    int result_negative;
    int comparison;

    if (result_out == NULL || rounding_mode > 3U) {
        return 0;
    }
    a = porpoise_decode_fp32(multiplicand_bits, non_ieee_mode != 0);
    b = porpoise_decode_fp32(addend_bits, non_ieee_mode != 0);
    c = porpoise_decode_fp32(multiplier_bits, non_ieee_mode != 0);
    causes = porpoise_snan_causes3(&a, &b, &c);

    /* Invalid causes are sticky and may be raised together.  In particular,
     * an sNaN addend does not hide an independent infinity-times-zero
     * product exception. */
    if ((a.value_class == PORPOISE_FP32_ZERO &&
         c.value_class == PORPOISE_FP32_INFINITY) ||
        (a.value_class == PORPOISE_FP32_INFINITY &&
         c.value_class == PORPOISE_FP32_ZERO)) {
        causes |= PORPOISE_PPC_FP_INVALID_INFINITY_TIMES_ZERO;
    }

    /* Gekko FMA NaN selection is architectural operand order a, b, c. */
    if (a.value_class == PORPOISE_FP32_NAN ||
        b.value_class == PORPOISE_FP32_NAN ||
        c.value_class == PORPOISE_FP32_NAN) {
        uint32_t selected = a.value_class == PORPOISE_FP32_NAN
                                ? a.bits
                                : b.value_class == PORPOISE_FP32_NAN
                                      ? b.bits
                                      : c.bits;
        porpoise_set_special_result(
            result_out,
            porpoise_quiet_nan(selected),
            causes);
        return 1;
    }

    product_negative = a.negative ^ c.negative;
    if ((causes & PORPOISE_PPC_FP_INVALID_INFINITY_TIMES_ZERO) != 0U) {
        porpoise_set_special_result(
            result_out,
            PORPOISE_F32_DEFAULT_QNAN,
            causes);
        return 1;
    }
    if (a.value_class == PORPOISE_FP32_INFINITY ||
        c.value_class == PORPOISE_FP32_INFINITY) {
        if (b.value_class == PORPOISE_FP32_INFINITY &&
            product_negative != b.negative) {
            porpoise_set_special_result(
                result_out,
                PORPOISE_F32_DEFAULT_QNAN,
                PORPOISE_PPC_FP_INVALID_INFINITY_MINUS_INFINITY);
        } else {
            porpoise_set_special_result(
                result_out,
                (product_negative ? PORPOISE_F32_SIGN_MASK : 0U) |
                    PORPOISE_F32_EXPONENT_MASK,
                PORPOISE_PPC_FP_INVALID_NONE);
        }
        return 1;
    }
    if (b.value_class == PORPOISE_FP32_INFINITY) {
        porpoise_set_special_result(
            result_out,
            b.bits,
            PORPOISE_PPC_FP_INVALID_NONE);
        return 1;
    }

    memset(result_out, 0, sizeof(*result_out));
    if (a.value_class == PORPOISE_FP32_ZERO ||
        c.value_class == PORPOISE_FP32_ZERO) {
        if (b.value_class == PORPOISE_FP32_ZERO) {
            result_negative = product_negative == b.negative
                                  ? product_negative
                                  : rounding_mode == 3U;
            result_out->bits = result_negative
                                   ? PORPOISE_F32_SIGN_MASK
                                   : 0U;
            return 1;
        }
        result_out->bits = b.bits;
        return 1;
    }
    if (b.value_class == PORPOISE_FP32_ZERO) {
        product_significand =
            (uint64_t)a.significand * (uint64_t)c.significand;
        product_exponent = a.exponent + c.exponent;
        porpoise_big_from_shifted_u64(&product, product_significand, 0U);
        porpoise_round_exact_to_fp32(
            &product,
            product_exponent,
            product_negative,
            rounding_mode,
            non_ieee_mode != 0,
            result_out);
        return 1;
    }

    product_significand =
        (uint64_t)a.significand * (uint64_t)c.significand;
    product_exponent = a.exponent + c.exponent;
    common_exponent = product_exponent < b.exponent
                          ? product_exponent
                          : b.exponent;
    porpoise_big_from_shifted_u64(
        &product,
        product_significand,
        (unsigned int)(product_exponent - common_exponent));
    porpoise_big_from_shifted_u64(
        &addend,
        b.significand,
        (unsigned int)(b.exponent - common_exponent));

    if (product_negative == b.negative) {
        porpoise_big_add(&magnitude, &product, &addend);
        result_negative = product_negative;
    } else {
        comparison = porpoise_big_compare(&product, &addend);
        if (comparison == 0) {
            memset(&magnitude, 0, sizeof(magnitude));
            result_negative = rounding_mode == 3U;
        } else if (comparison > 0) {
            porpoise_big_subtract(&magnitude, &product, &addend);
            result_negative = product_negative;
        } else {
            porpoise_big_subtract(&magnitude, &addend, &product);
            result_negative = b.negative;
        }
    }

    if (porpoise_big_is_zero(&magnitude)) {
        result_out->bits = result_negative ? PORPOISE_F32_SIGN_MASK : 0U;
        return 1;
    }
    porpoise_round_exact_to_fp32(
        &magnitude,
        common_exponent,
        result_negative,
        rounding_mode,
        non_ieee_mode != 0,
        result_out);
    return 1;
}
