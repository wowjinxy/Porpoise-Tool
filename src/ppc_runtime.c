/**
 * @file ppc_runtime.c
 * @brief PowerPC Runtime Library - Compiler Intrinsics Implementation
 * 
 * These implement the 64-bit arithmetic helpers that CodeWarrior provided
 * for PowerPC. Since PowerPC doesn't have native 64-bit division/modulo,
 * the compiler generates calls to these runtime functions.
 */

#include "ppc_runtime.h"
#include <stdint.h>

//==============================================================================
// 64-BIT UNSIGNED ARITHMETIC
//==============================================================================

/**
 * @brief 64-bit unsigned division
 */
uint64_result_t __div2u(uint32_t dividend_hi, uint32_t dividend_lo,
                        uint32_t divisor_hi, uint32_t divisor_lo) {
    uint64_t dividend = MAKE_U64(dividend_hi, dividend_lo);
    uint64_t divisor = MAKE_U64(divisor_hi, divisor_lo);
    
    // Handle divide by zero
    if (divisor == 0) {
        return (uint64_result_t){.hi = 0xFFFFFFFF, .lo = 0xFFFFFFFF};
    }
    
    uint64_t quotient = dividend / divisor;
    return MAKE_RESULT(quotient);
}

/**
 * @brief 64-bit unsigned modulo
 */
uint64_result_t __mod2u(uint32_t dividend_hi, uint32_t dividend_lo,
                        uint32_t divisor_hi, uint32_t divisor_lo) {
    uint64_t dividend = MAKE_U64(dividend_hi, dividend_lo);
    uint64_t divisor = MAKE_U64(divisor_hi, divisor_lo);
    
    // Handle divide by zero
    if (divisor == 0) {
        return (uint64_result_t){.hi = 0, .lo = 0};
    }
    
    uint64_t remainder = dividend % divisor;
    return MAKE_RESULT(remainder);
}

/**
 * @brief 64-bit unsigned multiplication
 */
uint64_result_t __mul2u(uint32_t multiplicand_hi, uint32_t multiplicand_lo,
                        uint32_t multiplier_hi, uint32_t multiplier_lo) {
    uint64_t multiplicand = MAKE_U64(multiplicand_hi, multiplicand_lo);
    uint64_t multiplier = MAKE_U64(multiplier_hi, multiplier_lo);
    
    uint64_t product = multiplicand * multiplier;
    return MAKE_RESULT(product);
}

//==============================================================================
// 64-BIT SIGNED ARITHMETIC
//==============================================================================

/**
 * @brief 64-bit signed division
 */
uint64_result_t __div2i(uint32_t dividend_hi, uint32_t dividend_lo,
                        uint32_t divisor_hi, uint32_t divisor_lo) {
    int64_t dividend = (int64_t)MAKE_U64(dividend_hi, dividend_lo);
    int64_t divisor = (int64_t)MAKE_U64(divisor_hi, divisor_lo);
    
    // Handle divide by zero
    if (divisor == 0) {
        return (uint64_result_t){.hi = 0xFFFFFFFF, .lo = 0xFFFFFFFF};
    }
    
    int64_t quotient = dividend / divisor;
    return MAKE_RESULT((uint64_t)quotient);
}

/**
 * @brief 64-bit signed modulo
 */
uint64_result_t __mod2i(uint32_t dividend_hi, uint32_t dividend_lo,
                        uint32_t divisor_hi, uint32_t divisor_lo) {
    int64_t dividend = (int64_t)MAKE_U64(dividend_hi, dividend_lo);
    int64_t divisor = (int64_t)MAKE_U64(divisor_hi, divisor_lo);
    
    // Handle divide by zero
    if (divisor == 0) {
        return (uint64_result_t){.hi = 0, .lo = 0};
    }
    
    int64_t remainder = dividend % divisor;
    return MAKE_RESULT((uint64_t)remainder);
}

/**
 * @brief 64-bit signed multiplication
 */
uint64_result_t __mul2i(uint32_t multiplicand_hi, uint32_t multiplicand_lo,
                        uint32_t multiplier_hi, uint32_t multiplier_lo) {
    int64_t multiplicand = (int64_t)MAKE_U64(multiplicand_hi, multiplicand_lo);
    int64_t multiplier = (int64_t)MAKE_U64(multiplier_hi, multiplier_lo);
    
    int64_t product = multiplicand * multiplier;
    return MAKE_RESULT((uint64_t)product);
}

//==============================================================================
// 64-BIT SHIFT OPERATIONS
//==============================================================================

/**
 * @brief 64-bit logical shift right
 */
uint64_result_t __lshr2(uint32_t value_hi, uint32_t value_lo, uint32_t shift_count) {
    uint64_t value = MAKE_U64(value_hi, value_lo);
    
    // Clamp shift count to 0-63
    shift_count &= 0x3F;
    
    uint64_t result = value >> shift_count;
    return MAKE_RESULT(result);
}

/**
 * @brief 64-bit arithmetic shift right (sign-extending)
 */
uint64_result_t __ashr2(uint32_t value_hi, uint32_t value_lo, uint32_t shift_count) {
    int64_t value = (int64_t)MAKE_U64(value_hi, value_lo);
    
    // Clamp shift count to 0-63
    shift_count &= 0x3F;
    
    int64_t result = value >> shift_count;
    return MAKE_RESULT((uint64_t)result);
}

/**
 * @brief 64-bit logical shift left
 */
uint64_result_t __lshl2(uint32_t value_hi, uint32_t value_lo, uint32_t shift_count) {
    uint64_t value = MAKE_U64(value_hi, value_lo);
    
    // Clamp shift count to 0-63
    shift_count &= 0x3F;
    
    uint64_t result = value << shift_count;
    return MAKE_RESULT(result);
}

//==============================================================================
// FLOATING-POINT CONVERSION
//==============================================================================

/**
 * @brief Convert 64-bit unsigned integer to double
 */
double __cvt_dbl_ull(uint32_t value_hi, uint32_t value_lo) {
    uint64_t value = MAKE_U64(value_hi, value_lo);
    
    // Handle conversion (may lose precision for large values)
    return (double)value;
}

/**
 * @brief Convert 64-bit signed integer to double
 */
double __cvt_dbl_ll(uint32_t value_hi, uint32_t value_lo) {
    int64_t value = (int64_t)MAKE_U64(value_hi, value_lo);
    return (double)value;
}

/**
 * @brief Convert double to 64-bit unsigned integer
 */
uint64_result_t __cvt_ull_dbl(double value) {
    // Clamp to valid range
    if (value < 0.0) {
        return (uint64_result_t){.hi = 0, .lo = 0};
    }
    if (value >= 18446744073709551616.0) {  // 2^64
        return (uint64_result_t){.hi = 0xFFFFFFFF, .lo = 0xFFFFFFFF};
    }
    
    uint64_t result = (uint64_t)value;
    return MAKE_RESULT(result);
}

/**
 * @brief Convert double to 64-bit signed integer
 */
uint64_result_t __cvt_ll_dbl(double value) {
    // Clamp to valid range
    if (value < -9223372036854775808.0) {  // -2^63
        return (uint64_result_t){.hi = 0x80000000, .lo = 0x00000000};
    }
    if (value >= 9223372036854775808.0) {  // 2^63
        return (uint64_result_t){.hi = 0x7FFFFFFF, .lo = 0xFFFFFFFF};
    }
    
    int64_t result = (int64_t)value;
    return MAKE_RESULT((uint64_t)result);
}

//==============================================================================
// ALTERNATE NAMES (Some compilers use different naming conventions)
//==============================================================================

// Some CodeWarrior versions use different names
uint64_result_t __udiv64(uint32_t dividend_hi, uint32_t dividend_lo,
                         uint32_t divisor_hi, uint32_t divisor_lo) {
    return __div2u(dividend_hi, dividend_lo, divisor_hi, divisor_lo);
}

uint64_result_t __umod64(uint32_t dividend_hi, uint32_t dividend_lo,
                         uint32_t divisor_hi, uint32_t divisor_lo) {
    return __mod2u(dividend_hi, dividend_lo, divisor_hi, divisor_lo);
}

uint64_result_t __umul64(uint32_t multiplicand_hi, uint32_t multiplicand_lo,
                         uint32_t multiplier_hi, uint32_t multiplier_lo) {
    return __mul2u(multiplicand_hi, multiplicand_lo, multiplier_hi, multiplier_lo);
}

uint64_result_t __sdiv64(uint32_t dividend_hi, uint32_t dividend_lo,
                         uint32_t divisor_hi, uint32_t divisor_lo) {
    return __div2i(dividend_hi, dividend_lo, divisor_hi, divisor_lo);
}

uint64_result_t __smod64(uint32_t dividend_hi, uint32_t dividend_lo,
                         uint32_t divisor_hi, uint32_t divisor_lo) {
    return __mod2i(dividend_hi, dividend_lo, divisor_hi, divisor_lo);
}

uint64_result_t __smul64(uint32_t multiplicand_hi, uint32_t multiplicand_lo,
                         uint32_t multiplier_hi, uint32_t multiplier_lo) {
    return __mul2i(multiplicand_hi, multiplicand_lo, multiplier_hi, multiplier_lo);
}

