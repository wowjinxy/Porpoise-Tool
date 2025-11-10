/**
 * @file ppc_runtime.h
 * @brief PowerPC Runtime Library - Compiler Intrinsics
 * 
 * This provides the compiler intrinsics that CodeWarrior's runtime provided
 * for 64-bit arithmetic operations on the PowerPC (which doesn't have native
 * 64-bit integer division/modulo instructions).
 * 
 * These functions match the CodeWarrior EABI calling convention.
 */

#ifndef PPC_RUNTIME_H
#define PPC_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//==============================================================================
// 64-BIT UNSIGNED ARITHMETIC INTRINSICS
//==============================================================================

/**
 * @brief 64-bit unsigned division (__div2u)
 * @param dividend_hi High 32 bits of dividend (r3)
 * @param dividend_lo Low 32 bits of dividend (r4)
 * @param divisor_hi High 32 bits of divisor (r5)
 * @param divisor_lo Low 32 bits of divisor (r6)
 * @return Quotient as 64-bit value (r3:r4 = high:low)
 */
typedef struct {
    uint32_t hi;  // r3
    uint32_t lo;  // r4
} uint64_result_t;

uint64_result_t __div2u(uint32_t dividend_hi, uint32_t dividend_lo,
                        uint32_t divisor_hi, uint32_t divisor_lo);

/**
 * @brief 64-bit unsigned modulo (__mod2u)
 * @param dividend_hi High 32 bits of dividend (r3)
 * @param dividend_lo Low 32 bits of dividend (r4)
 * @param divisor_hi High 32 bits of divisor (r5)
 * @param divisor_lo Low 32 bits of divisor (r6)
 * @return Remainder as 64-bit value (r3:r4 = high:low)
 */
uint64_result_t __mod2u(uint32_t dividend_hi, uint32_t dividend_lo,
                        uint32_t divisor_hi, uint32_t divisor_lo);

/**
 * @brief 64-bit unsigned multiplication (__mul2u)
 * @param multiplicand_hi High 32 bits of multiplicand (r3)
 * @param multiplicand_lo Low 32 bits of multiplicand (r4)
 * @param multiplier_hi High 32 bits of multiplier (r5)
 * @param multiplier_lo Low 32 bits of multiplier (r6)
 * @return Product as 64-bit value (r3:r4 = high:low)
 */
uint64_result_t __mul2u(uint32_t multiplicand_hi, uint32_t multiplicand_lo,
                        uint32_t multiplier_hi, uint32_t multiplier_lo);

//==============================================================================
// 64-BIT SIGNED ARITHMETIC INTRINSICS
//==============================================================================

/**
 * @brief 64-bit signed division (__div2i)
 */
uint64_result_t __div2i(uint32_t dividend_hi, uint32_t dividend_lo,
                        uint32_t divisor_hi, uint32_t divisor_lo);

/**
 * @brief 64-bit signed modulo (__mod2i)
 */
uint64_result_t __mod2i(uint32_t dividend_hi, uint32_t dividend_lo,
                        uint32_t divisor_hi, uint32_t divisor_lo);

/**
 * @brief 64-bit signed multiplication (__mul2i)
 */
uint64_result_t __mul2i(uint32_t multiplicand_hi, uint32_t multiplicand_lo,
                        uint32_t multiplier_hi, uint32_t multiplier_lo);

//==============================================================================
// 64-BIT SHIFT OPERATIONS
//==============================================================================

/**
 * @brief 64-bit logical shift right (__lshr2)
 * @param value_hi High 32 bits (r3)
 * @param value_lo Low 32 bits (r4)
 * @param shift_count Shift amount (r5)
 * @return Shifted value (r3:r4)
 */
uint64_result_t __lshr2(uint32_t value_hi, uint32_t value_lo, uint32_t shift_count);

/**
 * @brief 64-bit arithmetic shift right (__ashr2)
 * @param value_hi High 32 bits (r3)
 * @param value_lo Low 32 bits (r4)
 * @param shift_count Shift amount (r5)
 * @return Shifted value (r3:r4)
 */
uint64_result_t __ashr2(uint32_t value_hi, uint32_t value_lo, uint32_t shift_count);

/**
 * @brief 64-bit logical shift left (__lshl2)
 * @param value_hi High 32 bits (r3)
 * @param value_lo Low 32 bits (r4)
 * @param shift_count Shift amount (r5)
 * @return Shifted value (r3:r4)
 */
uint64_result_t __lshl2(uint32_t value_hi, uint32_t value_lo, uint32_t shift_count);

//==============================================================================
// FLOATING-POINT CONVERSION INTRINSICS
//==============================================================================

/**
 * @brief Convert 64-bit unsigned integer to double (__cvt_dbl_ull)
 */
double __cvt_dbl_ull(uint32_t value_hi, uint32_t value_lo);

/**
 * @brief Convert 64-bit signed integer to double (__cvt_dbl_ll)
 */
double __cvt_dbl_ll(uint32_t value_hi, uint32_t value_lo);

/**
 * @brief Convert double to 64-bit unsigned integer (__cvt_ull_dbl)
 */
uint64_result_t __cvt_ull_dbl(double value);

/**
 * @brief Convert double to 64-bit signed integer (__cvt_ll_dbl)
 */
uint64_result_t __cvt_ll_dbl(double value);

//==============================================================================
// HELPER MACROS FOR 64-BIT OPERATIONS
//==============================================================================

// Combine two 32-bit values into a 64-bit value
#define MAKE_U64(hi, lo) (((uint64_t)(hi) << 32) | (uint32_t)(lo))

// Extract high 32 bits
#define HIGH32(val) ((uint32_t)((val) >> 32))

// Extract low 32 bits
#define LOW32(val) ((uint32_t)(val))

// Create result structure from 64-bit value
#define MAKE_RESULT(val) ((uint64_result_t){.hi = HIGH32(val), .lo = LOW32(val)})

#ifdef __cplusplus
}
#endif

#endif // PPC_RUNTIME_H

