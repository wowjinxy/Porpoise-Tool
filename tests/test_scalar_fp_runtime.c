#include "porpoise_lifted.h"

#include <stdint.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) abort(); } while (0)

static void set_bits(
    PorpoisePpcState *state,
    unsigned int register_index,
    uint64_t lane_zero,
    uint64_t lane_one)
{
    state->fpr[register_index].lane_bits[0] = lane_zero;
    state->fpr[register_index].lane_bits[1] = lane_one;
}

static void set_double(
    PorpoisePpcState *state,
    unsigned int register_index,
    double value)
{
    porpoise_fpr_set_f64(state, register_index, 0U, value);
}

static void test_frsp_rounding_modes(void)
{
    static const struct {
        uint32_t rounding_mode;
        uint64_t expected;
        int incremented;
    } cases[] = {
        {0U, UINT64_C(0x3FF0000000000000), 0},
        {1U, UINT64_C(0x3FF0000000000000), 0},
        {2U, UINT64_C(0x3FF0000020000000), 1},
        {3U, UINT64_C(0x3FF0000000000000), 0},
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
        PorpoisePpcState state;

        porpoise_state_init(&state, NULL);
        state.fpscr = cases[index].rounding_mode;
        set_bits(
            &state,
            1U,
            UINT64_C(0x3FF0000010000000),
            UINT64_C(0xAAAAAAAAAAAAAAAA));
        set_bits(
            &state,
            2U,
            UINT64_C(0xBBBBBBBBBBBBBBBB),
            UINT64_C(0xCCCCCCCCCCCCCCCC));
        CHECK(porpoise_frsp(&state, 2U, 1U, 0));
        CHECK(state.fpr[2].lane_bits[0] == cases[index].expected);
        CHECK(state.fpr[2].lane_bits[1] == cases[index].expected);
        CHECK((state.fpscr & PORPOISE_FPSCR_FI) != 0U);
        CHECK((state.fpscr & PORPOISE_FPSCR_XX) != 0U);
        CHECK(((state.fpscr & PORPOISE_FPSCR_FR) != 0U) ==
              cases[index].incremented);
        CHECK((state.fpscr & PORPOISE_FPSCR_FPRF_MASK) ==
              UINT32_C(0x00004000));
    }
}

static void test_frsp_specials_and_range(void)
{
    PorpoisePpcState state;
    uint32_t original_fprf;

    porpoise_state_init(&state, NULL);
    state.fpscr = PORPOISE_FPSCR_FR | PORPOISE_FPSCR_FI;
    set_bits(&state, 1U, UINT64_C(0x8000000000000000), 0U);
    CHECK(porpoise_frsp(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0x8000000000000000));
    CHECK(state.fpr[2].lane_bits[1] == UINT64_C(0x8000000000000000));
    CHECK((state.fpscr & (PORPOISE_FPSCR_FR | PORPOISE_FPSCR_FI)) == 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_FPRF_MASK) ==
          UINT32_C(0x00012000));

    porpoise_state_init(&state, NULL);
    set_bits(&state, 1U, UINT64_C(0x36A0000000000000), 0U);
    CHECK(porpoise_frsp(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0x36A0000000000000));
    CHECK((state.fpscr & (PORPOISE_FPSCR_UX |
                          PORPOISE_FPSCR_FI |
                          PORPOISE_FPSCR_XX)) == 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_FPRF_MASK) ==
          UINT32_C(0x00014000));

    porpoise_state_init(&state, NULL);
    set_bits(&state, 1U, UINT64_C(0x3690000000000000), 0U);
    CHECK(porpoise_frsp(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == 0U);
    CHECK((state.fpscr & (PORPOISE_FPSCR_UX |
                          PORPOISE_FPSCR_FI |
                          PORPOISE_FPSCR_XX)) ==
          (PORPOISE_FPSCR_UX | PORPOISE_FPSCR_FI |
           PORPOISE_FPSCR_XX));

    porpoise_state_init(&state, NULL);
    state.fpscr = PORPOISE_FPSCR_NI;
    set_bits(&state, 1U, UINT64_C(0x36A0000000000000), 0U);
    CHECK(porpoise_frsp(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_FI) != 0U);

    porpoise_state_init(&state, NULL);
    state.fpscr = PORPOISE_FPSCR_NI;
    set_bits(&state, 1U, UINT64_C(0x0000000000000001), 0U);
    CHECK(porpoise_frsp(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_FI) == 0U);

    porpoise_state_init(&state, NULL);
    set_bits(&state, 1U, UINT64_C(0x47F0000000000000), 0U);
    CHECK(porpoise_frsp(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0x7FF0000000000000));
    CHECK((state.fpscr & (PORPOISE_FPSCR_OX |
                          PORPOISE_FPSCR_FI |
                          PORPOISE_FPSCR_XX)) ==
          (PORPOISE_FPSCR_OX | PORPOISE_FPSCR_FI |
           PORPOISE_FPSCR_XX));

    porpoise_state_init(&state, NULL);
    set_bits(&state, 1U, UINT64_C(0x7FF8123456789ABC), 0U);
    CHECK(porpoise_frsp(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0x7FF8123440000000));
    CHECK((state.fpscr & PORPOISE_FPSCR_INVALID_CAUSE_MASK) == 0U);

    porpoise_state_init(&state, NULL);
    set_bits(&state, 1U, UINT64_C(0xFFF0123456789ABC), 0U);
    CHECK(porpoise_frsp(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xFFF8123440000000));
    CHECK((state.fpscr & PORPOISE_FPSCR_VXSNAN) != 0U);

    porpoise_state_init(&state, NULL);
    state.pc = UINT32_C(0x80001234);
    state.fpscr = PORPOISE_FPSCR_VE | UINT32_C(0x00004000);
    original_fprf = state.fpscr & PORPOISE_FPSCR_FPRF_MASK;
    set_bits(
        &state,
        1U,
        UINT64_C(0x7FF0123456789ABC),
        UINT64_C(0x1111111111111111));
    set_bits(
        &state,
        2U,
        UINT64_C(0x2222222222222222),
        UINT64_C(0x3333333333333333));
    CHECK(!porpoise_frsp(&state, 2U, 1U, 1));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0x2222222222222222));
    CHECK(state.fpr[2].lane_bits[1] == UINT64_C(0x3333333333333333));
    CHECK((state.fpscr & PORPOISE_FPSCR_FPRF_MASK) == original_fprf);
    CHECK((state.fpscr & (PORPOISE_FPSCR_VXSNAN |
                          PORPOISE_FPSCR_VX |
                          PORPOISE_FPSCR_FEX)) ==
          (PORPOISE_FPSCR_VXSNAN |
           PORPOISE_FPSCR_VX |
           PORPOISE_FPSCR_FEX));
    CHECK(state.fault == PORPOISE_FAULT_FLOATING_POINT_EXCEPTION);
    CHECK(porpoise_cr_get_field(&state, 1U) ==
          (uint8_t)((state.fpscr >> 28U) & 0xFU));
}

static void test_fctiwz(void)
{
    PorpoisePpcState state;

    porpoise_state_init(&state, NULL);
    set_double(&state, 1U, 1.75);
    state.fpr[2].lane_bits[1] = UINT64_C(0x1122334455667788);
    CHECK(porpoise_fctiwz(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xFFF8000000000001));
    CHECK(state.fpr[2].lane_bits[1] == UINT64_C(0x1122334455667788));
    CHECK((state.fpscr & (PORPOISE_FPSCR_FI | PORPOISE_FPSCR_XX)) ==
          (PORPOISE_FPSCR_FI | PORPOISE_FPSCR_XX));
    CHECK((state.fpscr & PORPOISE_FPSCR_FR) == 0U);

    porpoise_state_init(&state, NULL);
    set_double(&state, 1U, -0.75);
    CHECK(porpoise_fctiwz(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xFFF8000100000000));

    porpoise_state_init(&state, NULL);
    set_double(&state, 1U, 2147483647.0);
    CHECK(porpoise_fctiwz(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xFFF800007FFFFFFF));
    CHECK((state.fpscr & PORPOISE_FPSCR_VXCVI) == 0U);

    porpoise_state_init(&state, NULL);
    set_double(&state, 1U, -2147483648.0);
    CHECK(porpoise_fctiwz(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xFFF8000080000000));

    porpoise_state_init(&state, NULL);
    set_double(&state, 1U, 2147483648.0);
    CHECK(porpoise_fctiwz(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xFFF800007FFFFFFF));
    CHECK((state.fpscr & PORPOISE_FPSCR_VXCVI) != 0U);

    porpoise_state_init(&state, NULL);
    set_bits(&state, 1U, UINT64_C(0x7FF8000000001234), 0U);
    CHECK(porpoise_fctiwz(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xFFF8000080000000));
    CHECK((state.fpscr & PORPOISE_FPSCR_VXCVI) != 0U);

    porpoise_state_init(&state, NULL);
    state.fpscr = PORPOISE_FPSCR_VE | UINT32_C(0x00004000);
    set_bits(&state, 1U, UINT64_C(0x7FF0000000001234), 0U);
    set_bits(
        &state,
        2U,
        UINT64_C(0xA5A5A5A5A5A5A5A5),
        UINT64_C(0x5A5A5A5A5A5A5A5A));
    CHECK(!porpoise_fctiwz(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xA5A5A5A5A5A5A5A5));
    CHECK(state.fpr[2].lane_bits[1] == UINT64_C(0x5A5A5A5A5A5A5A5A));
    CHECK((state.fpscr & (PORPOISE_FPSCR_VXSNAN |
                          PORPOISE_FPSCR_VXCVI)) ==
          (PORPOISE_FPSCR_VXSNAN | PORPOISE_FPSCR_VXCVI));

    porpoise_state_init(&state, NULL);
    state.fpscr = PORPOISE_FPSCR_NI;
    set_bits(&state, 1U, UINT64_C(0x0000000000000001), 0U);
    CHECK(porpoise_fctiwz(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xFFF8000000000000));
    CHECK((state.fpscr & PORPOISE_FPSCR_FI) == 0U);
}

static void test_fctiw_rounding_modes(void)
{
    static const struct {
        uint32_t rounding_mode;
        double source;
        uint32_t expected_word;
        int rounded_away_from_zero;
    } cases[] = {
        {0U, 1.5, UINT32_C(2), 1},
        {0U, 2.5, UINT32_C(2), 0},
        {0U, -1.5, UINT32_C(0xFFFFFFFE), 1},
        {1U, 1.75, UINT32_C(1), 0},
        {1U, -1.75, UINT32_C(0xFFFFFFFF), 0},
        {2U, 1.25, UINT32_C(2), 1},
        {2U, -1.25, UINT32_C(0xFFFFFFFF), 0},
        {3U, 1.25, UINT32_C(1), 0},
        {3U, -1.25, UINT32_C(0xFFFFFFFE), 1},
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
        PorpoisePpcState state;

        porpoise_state_init(&state, NULL);
        state.fpscr = cases[index].rounding_mode;
        set_double(&state, 1U, cases[index].source);
        state.fpr[2].lane_bits[1] = UINT64_C(0x1122334455667788);
        CHECK(porpoise_fctiw(&state, 2U, 1U, 0));
        CHECK((uint32_t)state.fpr[2].lane_bits[0] == cases[index].expected_word);
        CHECK((state.fpr[2].lane_bits[0] >> 32U) == UINT32_C(0xFFF80000));
        CHECK(state.fpr[2].lane_bits[1] == UINT64_C(0x1122334455667788));
        CHECK((state.fpscr & (PORPOISE_FPSCR_FI | PORPOISE_FPSCR_XX)) ==
              (PORPOISE_FPSCR_FI | PORPOISE_FPSCR_XX));
        CHECK(((state.fpscr & PORPOISE_FPSCR_FR) != 0U) ==
              cases[index].rounded_away_from_zero);
    }
}

static void test_fctiw_boundaries_and_exceptions(void)
{
    PorpoisePpcState state;

    porpoise_state_init(&state, NULL);
    state.fpscr = 0U;
    set_double(&state, 1U, 2147483647.5);
    CHECK(porpoise_fctiw(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xFFF800007FFFFFFF));
    CHECK((state.fpscr & PORPOISE_FPSCR_VXCVI) != 0U);
    CHECK((state.fpscr & (PORPOISE_FPSCR_FR | PORPOISE_FPSCR_FI)) == 0U);

    porpoise_state_init(&state, NULL);
    state.fpscr = 3U;
    set_double(&state, 1U, -2147483648.25);
    CHECK(porpoise_fctiw(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xFFF8000080000000));
    CHECK((state.fpscr & PORPOISE_FPSCR_VXCVI) != 0U);

    porpoise_state_init(&state, NULL);
    state.fpscr = 2U;
    set_double(&state, 1U, -0.25);
    CHECK(porpoise_fctiw(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xFFF8000100000000));
    CHECK((state.fpscr & PORPOISE_FPSCR_FR) == 0U);

    porpoise_state_init(&state, NULL);
    state.pc = UINT32_C(0x80001234);
    state.fpscr = PORPOISE_FPSCR_VE | UINT32_C(0x00004000);
    set_bits(&state, 1U, UINT64_C(0x7FF0000000001234), 0U);
    set_bits(
        &state,
        2U,
        UINT64_C(0xA5A5A5A5A5A5A5A5),
        UINT64_C(0x5A5A5A5A5A5A5A5A));
    CHECK(!porpoise_fctiw(&state, 2U, 1U, 1));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xA5A5A5A5A5A5A5A5));
    CHECK(state.fpr[2].lane_bits[1] == UINT64_C(0x5A5A5A5A5A5A5A5A));
    CHECK((state.fpscr & (PORPOISE_FPSCR_VXSNAN |
                          PORPOISE_FPSCR_VXCVI)) ==
          (PORPOISE_FPSCR_VXSNAN | PORPOISE_FPSCR_VXCVI));
    CHECK(state.fault == PORPOISE_FAULT_FLOATING_POINT_EXCEPTION);
    CHECK(porpoise_cr_get_field(&state, 1U) ==
          (uint8_t)((state.fpscr >> 28U) & 0xFU));
}

static void test_fpscr_moves(void)
{
    PorpoisePpcState state;

    porpoise_state_init(&state, NULL);
    state.fpscr = PORPOISE_FPSCR_FX | PORPOISE_FPSCR_OX |
                  PORPOISE_FPSCR_NI | 1U;
    state.fpr[3].lane_bits[1] = UINT64_C(0x1122334455667788);
    CHECK(porpoise_mffs(&state, 3U, 1));
    CHECK(state.fpr[3].lane_bits[0] ==
          (UINT64_C(0xFFF8000000000000) | state.fpscr));
    CHECK(state.fpr[3].lane_bits[1] == UINT64_C(0x1122334455667788));
    CHECK(porpoise_cr_get_field(&state, 1U) ==
          (uint8_t)((state.fpscr >> 28U) & 0xFU));

    porpoise_state_init(&state, NULL);
    state.fpr[1].lane_bits[0] = UINT64_C(0x7FF8000010000000);
    CHECK(porpoise_mtfsf(&state, 0x80U, 1U, 0));
    CHECK((state.fpscr & (PORPOISE_FPSCR_FX | PORPOISE_FPSCR_OX)) ==
          PORPOISE_FPSCR_OX);

    state.fpr[1].lane_bits[0] = UINT64_C(0x7FF0000000000005);
    CHECK(porpoise_mtfsf(&state, 0x01U, 1U, 0));
    CHECK((state.fpscr & UINT32_C(0xF)) == UINT32_C(0x5));
    CHECK((state.fpscr & PORPOISE_FPSCR_OX) != 0U);

    state.fpscr = 0U;
    state.fpr[1].lane_bits[0] = UINT64_C(0x7FF00000DEADBEEF);
    CHECK(porpoise_mtfsf(&state, 0U, 1U, 0));
    CHECK(state.fpscr == 0U);

    porpoise_state_init(&state, NULL);
    CHECK(porpoise_mtfsb1(&state, 29U, 0));
    CHECK((state.fpscr & PORPOISE_FPSCR_NI) != 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_FX) == 0U);

    CHECK(porpoise_mtfsb1(&state, 3U, 0));
    CHECK((state.fpscr & (PORPOISE_FPSCR_OX | PORPOISE_FPSCR_FX)) ==
          (PORPOISE_FPSCR_OX | PORPOISE_FPSCR_FX));
    state.fpscr &= ~PORPOISE_FPSCR_FX;
    CHECK(porpoise_mtfsb1(&state, 3U, 0));
    CHECK((state.fpscr & PORPOISE_FPSCR_FX) == 0U);

    state.fpscr = 0U;
    CHECK(porpoise_mtfsb1(&state, 1U, 0));
    CHECK(porpoise_mtfsb1(&state, 2U, 0));
    CHECK((state.fpscr & (PORPOISE_FPSCR_FEX | PORPOISE_FPSCR_VX)) == 0U);
    CHECK(porpoise_mtfsb1(&state, 0U, 1));
    CHECK((state.fpscr & PORPOISE_FPSCR_FX) != 0U);
    CHECK(porpoise_cr_get_field(&state, 1U) == 8U);
}

static void test_fma_numeric_and_lanes(void)
{
    static const struct {
        PorpoiseFpFmaOperation operation;
        double expected;
    } cases[] = {
        {PORPOISE_FP_FMA_MADD, 10.0},
        {PORPOISE_FP_FMA_MSUB, 2.0},
        {PORPOISE_FP_FMA_NMADD, -10.0},
        {PORPOISE_FP_FMA_NMSUB, -2.0},
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
        PorpoisePpcState state;

        porpoise_state_init(&state, NULL);
        set_double(&state, 1U, 2.0);
        set_double(&state, 2U, 3.0);
        set_double(&state, 3U, 4.0);
        state.fpr[4].lane_bits[1] = UINT64_C(0x1122334455667788);
        CHECK(porpoise_fp_fma(
            &state,
            4U,
            1U,
            2U,
            3U,
            cases[index].operation,
            PORPOISE_FP_PRECISION_DOUBLE,
            0));
        CHECK(porpoise_fpr_get_f64(&state, 4U, 0U) == cases[index].expected);
        CHECK(state.fpr[4].lane_bits[1] == UINT64_C(0x1122334455667788));
    }

    {
        PorpoisePpcState state;
        porpoise_state_init(&state, NULL);
        set_double(&state, 1U, 2.0);
        set_double(&state, 2U, 3.0);
        set_double(&state, 3U, 4.0);
        CHECK(porpoise_fp_fma(
            &state,
            4U,
            1U,
            2U,
            3U,
            PORPOISE_FP_FMA_MADD,
            PORPOISE_FP_PRECISION_SINGLE,
            0));
        CHECK(state.fpr[4].lane_bits[0] == UINT64_C(0x4024000000000000));
        CHECK(state.fpr[4].lane_bits[1] == UINT64_C(0x4024000000000000));
    }
}

static void test_fma_nan_invalid_and_ni(void)
{
    PorpoisePpcState state;
    uint64_t destination_zero;
    uint64_t destination_one;

    porpoise_state_init(&state, NULL);
    set_bits(&state, 1U, UINT64_C(0xFFF80000000000A1), 0U);
    set_bits(&state, 2U, UINT64_C(0x7FF80000000000C3), 0U);
    set_bits(&state, 3U, UINT64_C(0x7FF80000000000B2), 0U);
    CHECK(porpoise_fp_fma(
        &state,
        4U,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_NMADD,
        PORPOISE_FP_PRECISION_DOUBLE,
        0));
    CHECK(state.fpr[4].lane_bits[0] == UINT64_C(0xFFF80000000000A1));

    porpoise_state_init(&state, NULL);
    set_bits(&state, 1U, UINT64_C(0x0000000000000000), 0U);
    set_bits(&state, 2U, UINT64_C(0x7FF0000000000000), 0U);
    set_bits(&state, 3U, UINT64_C(0x7FF00000000000B2), 0U);
    CHECK(porpoise_fp_fma(
        &state,
        4U,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        PORPOISE_FP_PRECISION_DOUBLE,
        0));
    CHECK(state.fpr[4].lane_bits[0] == UINT64_C(0x7FF80000000000B2));
    CHECK((state.fpscr & (PORPOISE_FPSCR_VXSNAN |
                          PORPOISE_FPSCR_VXIMZ)) ==
          (PORPOISE_FPSCR_VXSNAN | PORPOISE_FPSCR_VXIMZ));

    porpoise_state_init(&state, NULL);
    state.fpscr = PORPOISE_FPSCR_VE | UINT32_C(0x00004000);
    set_bits(&state, 1U, UINT64_C(0x0000000000000000), 0U);
    set_bits(&state, 2U, UINT64_C(0x7FF0000000000000), 0U);
    set_bits(&state, 3U, UINT64_C(0x7FF00000000000B2), 0U);
    destination_zero = UINT64_C(0x123456789ABCDEF0);
    destination_one = UINT64_C(0x0FEDCBA987654321);
    set_bits(&state, 4U, destination_zero, destination_one);
    CHECK(!porpoise_fp_fma(
        &state,
        4U,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        PORPOISE_FP_PRECISION_SINGLE,
        0));
    CHECK(state.fpr[4].lane_bits[0] == destination_zero);
    CHECK(state.fpr[4].lane_bits[1] == destination_one);
    CHECK(state.fault == PORPOISE_FAULT_FLOATING_POINT_EXCEPTION);

    porpoise_state_init(&state, NULL);
    set_bits(&state, 1U, UINT64_C(0x7FF0000000000000), 0U);
    set_double(&state, 2U, 1.0);
    set_bits(&state, 3U, UINT64_C(0xFFF0000000000000), 0U);
    CHECK(porpoise_fp_fma(
        &state,
        4U,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        PORPOISE_FP_PRECISION_DOUBLE,
        0));
    CHECK(state.fpr[4].lane_bits[0] == UINT64_C(0x7FF8000000000000));
    CHECK((state.fpscr & PORPOISE_FPSCR_VXISI) != 0U);

    porpoise_state_init(&state, NULL);
    state.fpscr = PORPOISE_FPSCR_NI;
    set_bits(&state, 1U, UINT64_C(0x36A0000000000000), 0U);
    set_double(&state, 2U, 2.0);
    set_double(&state, 3U, 0.0);
    CHECK(porpoise_fp_fma(
        &state,
        4U,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        PORPOISE_FP_PRECISION_SINGLE,
        0));
    CHECK(state.fpr[4].lane_bits[0] == 0U);
    CHECK(state.fpr[4].lane_bits[1] == 0U);
}

int main(void)
{
    test_frsp_rounding_modes();
    test_frsp_specials_and_range();
    test_fctiw_rounding_modes();
    test_fctiw_boundaries_and_exceptions();
    test_fctiwz();
    test_fpscr_moves();
    test_fma_numeric_and_lanes();
    test_fma_nan_invalid_and_ni();
    return 0;
}
