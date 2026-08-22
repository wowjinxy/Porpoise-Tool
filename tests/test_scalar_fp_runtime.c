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

static void test_frsqrte_reference_vectors(void)
{
    static const struct {
        uint64_t source;
        uint64_t expected;
    } cases[] = {
        {UINT64_C(0x3FF0000000000000), UINT64_C(0x3FEFFE8000000000)},
        {UINT64_C(0x4000000000000000), UINT64_C(0x3FE69FA000000000)},
        {UINT64_C(0x4010000000000000), UINT64_C(0x3FDFFE8000000000)},
        {UINT64_C(0x3FE0000000000000), UINT64_C(0x3FF69FA000000000)},
        {UINT64_C(0x3FF8000000000000), UINT64_C(0x3FEA204000000000)},
        {UINT64_C(0x4008000000000000), UINT64_C(0x3FE2794000000000)},
        {UINT64_C(0x0010000000000000), UINT64_C(0x5FDFFE8000000000)},
        {UINT64_C(0x0000000000000001), UINT64_C(0x617FFE8000000000)},
        {UINT64_C(0x7FEFFFFFFFFFFFFF), UINT64_C(0x1FF000082C000000)},
        {UINT64_C(0x3FF0002000000000), UINT64_C(0x3FEFFE6170000000)},
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
        PorpoisePpcState state;

        porpoise_state_init(&state, NULL);
        state.msr |= PORPOISE_MSR_FP;
        state.fpscr = PORPOISE_FPSCR_FR | PORPOISE_FPSCR_FI;
        set_bits(
            &state,
            1U,
            cases[index].source,
            UINT64_C(0x1111111111111111));
        set_bits(
            &state,
            2U,
            UINT64_C(0x2222222222222222),
            UINT64_C(0x3333333333333333));
        CHECK(porpoise_frsqrte(&state, 2U, 1U, 0));
        CHECK(state.fpr[2].lane_bits[0] == cases[index].expected);
        CHECK(state.fpr[2].lane_bits[1] ==
              UINT64_C(0x3333333333333333));
        CHECK((state.fpscr & (PORPOISE_FPSCR_FR | PORPOISE_FPSCR_FI)) ==
              (PORPOISE_FPSCR_FR | PORPOISE_FPSCR_FI));
        CHECK((state.fpscr & PORPOISE_FPSCR_FPRF_MASK) ==
              UINT32_C(0x00004000));
    }
}

static void test_frsqrte_special_values(void)
{
    PorpoisePpcState state;
    uint32_t original_fprf;

    porpoise_state_init(&state, NULL);
    state.fpscr = PORPOISE_FPSCR_FR | PORPOISE_FPSCR_FI;
    set_bits(&state, 1U, 0U, 0U);
    CHECK(porpoise_frsqrte(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0x7FF0000000000000));
    CHECK((state.fpscr & PORPOISE_FPSCR_ZX) != 0U);
    CHECK((state.fpscr & (PORPOISE_FPSCR_FR | PORPOISE_FPSCR_FI)) == 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_FPRF_MASK) ==
          UINT32_C(0x00005000));

    porpoise_state_init(&state, NULL);
    set_bits(&state, 1U, UINT64_C(0x8000000000000000), 0U);
    CHECK(porpoise_frsqrte(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xFFF0000000000000));
    CHECK((state.fpscr & PORPOISE_FPSCR_FPRF_MASK) ==
          UINT32_C(0x00009000));

    porpoise_state_init(&state, NULL);
    state.fpscr = PORPOISE_FPSCR_FR | PORPOISE_FPSCR_FI;
    set_bits(&state, 1U, UINT64_C(0x7FF0000000000000), 0U);
    CHECK(porpoise_frsqrte(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == 0U);
    CHECK((state.fpscr & (PORPOISE_FPSCR_FR | PORPOISE_FPSCR_FI)) == 0U);

    porpoise_state_init(&state, NULL);
    set_bits(&state, 1U, UINT64_C(0xBFF0000000000000), 0U);
    CHECK(porpoise_frsqrte(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0x7FF8000000000000));
    CHECK((state.fpscr & (PORPOISE_FPSCR_VXSQRT |
                          PORPOISE_FPSCR_VX)) ==
          (PORPOISE_FPSCR_VXSQRT | PORPOISE_FPSCR_VX));

    porpoise_state_init(&state, NULL);
    set_bits(&state, 1U, UINT64_C(0xFFF0000000000000), 0U);
    CHECK(porpoise_frsqrte(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0x7FF8000000000000));
    CHECK((state.fpscr & PORPOISE_FPSCR_VXSQRT) != 0U);

    porpoise_state_init(&state, NULL);
    set_bits(&state, 1U, UINT64_C(0xFFF8123456789ABC), 0U);
    CHECK(porpoise_frsqrte(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xFFF8123456789ABC));
    CHECK((state.fpscr & PORPOISE_FPSCR_INVALID_CAUSE_MASK) == 0U);

    porpoise_state_init(&state, NULL);
    set_bits(&state, 1U, UINT64_C(0xFFF0123456789ABC), 0U);
    CHECK(porpoise_frsqrte(&state, 2U, 1U, 0));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xFFF8123456789ABC));
    CHECK((state.fpscr & PORPOISE_FPSCR_VXSNAN) != 0U);

    porpoise_state_init(&state, NULL);
    state.pc = UINT32_C(0x80001234);
    state.fpscr = PORPOISE_FPSCR_ZE | UINT32_C(0x00004000);
    original_fprf = state.fpscr & PORPOISE_FPSCR_FPRF_MASK;
    set_bits(&state, 1U, 0U, 0U);
    set_bits(
        &state,
        2U,
        UINT64_C(0x2222222222222222),
        UINT64_C(0x3333333333333333));
    CHECK(!porpoise_frsqrte(&state, 2U, 1U, 1));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0x2222222222222222));
    CHECK(state.fpr[2].lane_bits[1] == UINT64_C(0x3333333333333333));
    CHECK((state.fpscr & PORPOISE_FPSCR_FPRF_MASK) == original_fprf);
    CHECK((state.fpscr & (PORPOISE_FPSCR_ZX |
                          PORPOISE_FPSCR_FEX)) ==
          (PORPOISE_FPSCR_ZX | PORPOISE_FPSCR_FEX));
    CHECK(state.fault == PORPOISE_FAULT_FLOATING_POINT_EXCEPTION);
    CHECK(porpoise_cr_get_field(&state, 1U) ==
          (uint8_t)((state.fpscr >> 28U) & 0xFU));

    porpoise_state_init(&state, NULL);
    state.pc = UINT32_C(0x80005678);
    state.fpscr = PORPOISE_FPSCR_VE | UINT32_C(0x00004000);
    original_fprf = state.fpscr & PORPOISE_FPSCR_FPRF_MASK;
    set_bits(&state, 1U, UINT64_C(0x7FF0123456789ABC), 0U);
    set_bits(&state, 2U, UINT64_C(0xAAAAAAAAAAAAAAAA), 0U);
    CHECK(!porpoise_frsqrte(&state, 2U, 1U, 1));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xAAAAAAAAAAAAAAAA));
    CHECK((state.fpscr & PORPOISE_FPSCR_FPRF_MASK) == original_fprf);
    CHECK((state.fpscr & (PORPOISE_FPSCR_VXSNAN |
                          PORPOISE_FPSCR_VX |
                          PORPOISE_FPSCR_FEX)) ==
          (PORPOISE_FPSCR_VXSNAN |
           PORPOISE_FPSCR_VX |
           PORPOISE_FPSCR_FEX));
    CHECK(state.fault == PORPOISE_FAULT_FLOATING_POINT_EXCEPTION);
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

static void set_binary32_pair(
    PorpoisePpcState *state,
    unsigned int register_index,
    uint32_t lane_zero,
    uint32_t lane_one)
{
    set_bits(
        state,
        register_index,
        porpoise_binary32_to_binary64_bits(lane_zero),
        porpoise_binary32_to_binary64_bits(lane_one));
}

static void enable_exact_fp_domain(PorpoisePpcState *state)
{
    state->msr |= PORPOISE_MSR_FP;
    state->hid2 |= PORPOISE_HID2_PSE;
}

static void test_conservative_scalar_exact_domain(void)
{
    static const struct {
        uint32_t left;
        uint32_t right;
        PorpoiseFpBinaryOperation operation;
        uint32_t expected;
        int handled;
        int inexact;
        int incremented;
    } cases[] = {
        {UINT32_C(0x43020000), UINT32_C(0x43020000),
         PORPOISE_FP_BINARY_SUBTRACT, UINT32_C(0x00000000), 1, 0, 0},
        {UINT32_C(0x3FC00000), UINT32_C(0x40000000),
         PORPOISE_FP_BINARY_MULTIPLY, UINT32_C(0x40400000), 1, 0, 0},
        {UINT32_C(0x40C00000), UINT32_C(0x40000000),
         PORPOISE_FP_BINARY_DIVIDE, UINT32_C(0x40400000), 1, 0, 0},
        /* 1 + 2^-24 is an exact binary64 tie but not an exact binary32. */
        {UINT32_C(0x3F800000), UINT32_C(0x33800000),
         PORPOISE_FP_BINARY_ADD, UINT32_C(0x3F800000), 1, 1, 0},
        /* Divide the exact binary32 rational directly: the nearest result is
         * one ULP above the truncated repeating fraction. */
        {UINT32_C(0x3F800000), UINT32_C(0x40400000),
         PORPOISE_FP_BINARY_DIVIDE, UINT32_C(0x3EAAAAAB), 1, 1, 1},
        {UINT32_C(0xBF800000), UINT32_C(0x40400000),
         PORPOISE_FP_BINARY_DIVIDE, UINT32_C(0xBEAAAAAB), 1, 1, 1},
        /* Exceptional and subnormal-result divisions remain outside the
         * exact helper's deliberately narrow contract. */
        {UINT32_C(0x3F800000), UINT32_C(0x00000000),
         PORPOISE_FP_BINARY_DIVIDE, UINT32_C(0), 0, 0, 0},
        {UINT32_C(0x00800000), UINT32_C(0x40400000),
         PORPOISE_FP_BINARY_DIVIDE, UINT32_C(0), 0, 0, 0},
        /* Exact binary64 products use deterministic architectural rounding. */
        {UINT32_C(0x3F8CCCCD), UINT32_C(0x3F8CCCCD),
         PORPOISE_FP_BINARY_MULTIPLY, UINT32_C(0x3F9AE148), 1, 1, 0},
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
        PorpoisePpcState state;
        uint64_t destination_before = UINT64_C(0x0123456789ABCDEF);
        int handled;

        porpoise_state_init(&state, NULL);
        enable_exact_fp_domain(&state);
        set_binary32_pair(&state, 1U, cases[index].left, cases[index].left);
        set_binary32_pair(&state, 2U, cases[index].right, cases[index].right);
        set_bits(&state, 3U, destination_before, destination_before);
        handled = porpoise_fp_binary_single_try_exact(
            &state,
            3U,
            1U,
            2U,
            cases[index].operation,
            0);
        CHECK(handled == cases[index].handled);
        if (handled) {
            uint64_t expected = porpoise_binary32_to_binary64_bits(
                cases[index].expected);
            CHECK(state.fpr[3].lane_bits[0] == expected);
            CHECK(state.fpr[3].lane_bits[1] == expected);
            CHECK(((state.fpscr & PORPOISE_FPSCR_FI) != 0U) ==
                  cases[index].inexact);
            CHECK(((state.fpscr & PORPOISE_FPSCR_FR) != 0U) ==
                  cases[index].incremented);
            CHECK(((state.fpscr & PORPOISE_FPSCR_XX) != 0U) ==
                  cases[index].inexact);
        } else {
            CHECK(state.fpr[3].lane_bits[0] == destination_before);
            CHECK(state.fpr[3].lane_bits[1] == destination_before);
        }
    }

    {
        PorpoisePpcState state;

        /* CodeWarrior's unsigned-integer conversion constructs 2^52+n as a
         * binary64 value and subtracts 2^52 with fsubs. The mathematical
         * intermediate is exact even though the source is not binary32. */
        porpoise_state_init(&state, NULL);
        enable_exact_fp_domain(&state);
        set_bits(
            &state,
            1U,
            UINT64_C(0x4330000000000280),
            UINT64_C(0xAAAAAAAAAAAAAAAA));
        set_bits(
            &state,
            2U,
            UINT64_C(0x4330000000000000),
            UINT64_C(0xBBBBBBBBBBBBBBBB));
        CHECK(porpoise_fp_binary_single_try_exact(
            &state,
            3U,
            1U,
            2U,
            PORPOISE_FP_BINARY_SUBTRACT,
            0));
        CHECK(state.fpr[3].lane_bits[0] == UINT64_C(0x4084000000000000));
        CHECK(state.fpr[3].lane_bits[1] == UINT64_C(0x4084000000000000));
        CHECK((state.fpscr & (PORPOISE_FPSCR_FR |
                              PORPOISE_FPSCR_FI |
                              PORPOISE_FPSCR_XX)) == 0U);

        /* A full-precision multiplicand is still rejected if multiplying it
         * by the Force25 operand rounds the binary64 intermediate. */
        set_bits(
            &state,
            3U,
            UINT64_C(0x3FF0000000000001),
            UINT64_C(0));
        set_bits(
            &state,
            2U,
            UINT64_C(0x4008000000000000),
            UINT64_C(0));
        CHECK(!porpoise_fp_binary_single_try_exact(
            &state,
            4U,
            3U,
            2U,
            PORPOISE_FP_BINARY_MULTIPLY,
            0));
    }

    {
        PorpoisePpcState state;

        /* This is the frsqrte estimate reached by OneTri's PSVECNormalize.
         * The encoded fmuls operands are frA=f5 and frC=f5; Force25 rounds
         * frC to 0x3FF517E690000000 before the exact product is rounded once
         * to binary32. */
        porpoise_state_init(&state, NULL);
        enable_exact_fp_domain(&state);
        set_bits(
            &state,
            5U,
            UINT64_C(0x3FF517E688000000),
            UINT64_C(0x1111111111111111));
        CHECK(porpoise_fp_binary_single_try_exact(
            &state,
            6U,
            5U,
            5U,
            PORPOISE_FP_BINARY_MULTIPLY,
            0));
        CHECK(state.fpr[6].lane_bits[0] ==
              UINT64_C(0x3FFBCEE0E0000000));
        CHECK(state.fpr[6].lane_bits[1] ==
              UINT64_C(0x3FFBCEE0E0000000));
        CHECK((state.fpscr & (PORPOISE_FPSCR_FI |
                              PORPOISE_FPSCR_XX)) ==
              (PORPOISE_FPSCR_FI | PORPOISE_FPSCR_XX));
        CHECK((state.fpscr & PORPOISE_FPSCR_FR) == 0U);
    }

    {
        PorpoisePpcState state;
        porpoise_state_init(&state, NULL);
        set_binary32_pair(&state, 1U, UINT32_C(0x3F800000), 0U);
        set_binary32_pair(&state, 2U, UINT32_C(0x40000000), 0U);
        CHECK(!porpoise_fp_binary_single_try_exact(
            &state, 3U, 1U, 2U, PORPOISE_FP_BINARY_ADD, 0));
        enable_exact_fp_domain(&state);
        state.fpscr = PORPOISE_FPSCR_NI;
        CHECK(!porpoise_fp_binary_single_try_exact(
            &state, 3U, 1U, 2U, PORPOISE_FP_BINARY_ADD, 0));
        state.fpscr = 1U;
        CHECK(!porpoise_fp_binary_single_try_exact(
            &state, 3U, 1U, 2U, PORPOISE_FP_BINARY_ADD, 0));
    }
}

static void test_conservative_fma_exact_domain(void)
{
    PorpoisePpcState state;

    porpoise_state_init(&state, NULL);
    enable_exact_fp_domain(&state);
    set_binary32_pair(&state, 1U, UINT32_C(0x40000000), 0U);
    set_binary32_pair(&state, 2U, UINT32_C(0x40400000), 0U);
    set_binary32_pair(&state, 3U, UINT32_C(0x40800000), 0U);
    CHECK(porpoise_fp_fma_execution_is_exact(
        &state,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        PORPOISE_FP_PRECISION_SINGLE));
    CHECK(!porpoise_fp_fma_execution_is_exact(
        &state,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        PORPOISE_FP_PRECISION_DOUBLE));

    /* Published PowerPC single-FMA double-rounding counterexample. The
     * conservative domain must reject it instead of relying on the rounded
     * host binary64 result. */
    set_binary32_pair(&state, 1U, UINT32_C(0x42480000), 0U);
    set_binary32_pair(&state, 2U, UINT32_C(0xBC88CC38), 0U);
    set_binary32_pair(&state, 3U, UINT32_C(0x1B1C72A0), 0U);
    CHECK(!porpoise_fp_fma_execution_is_exact(
        &state,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        PORPOISE_FP_PRECISION_SINGLE));
}

static void test_conservative_paired_exact_domain(void)
{
    PorpoisePpcState state;

    porpoise_state_init(&state, NULL);
    enable_exact_fp_domain(&state);
    state.fpscr = PORPOISE_FPSCR_FX;
    set_binary32_pair(
        &state, 1U, UINT32_C(0x3F800000), UINT32_C(0x40000000));
    set_binary32_pair(
        &state, 2U, UINT32_C(0x40400000), UINT32_C(0x40800000));
    CHECK(porpoise_ps_binary_try_exact(
        &state, 3U, 1U, 2U, PORPOISE_FP_BINARY_ADD, 1));
    CHECK(state.fpr[3].lane_bits[0] == UINT64_C(0x4010000000000000));
    CHECK(state.fpr[3].lane_bits[1] == UINT64_C(0x4018000000000000));
    CHECK(porpoise_cr_get_field(&state, 1U) == 8U);

    set_binary32_pair(
        &state, 1U, UINT32_C(0x40000000), UINT32_C(0x40400000));
    set_binary32_pair(
        &state, 2U, UINT32_C(0x40A00000), UINT32_C(0x40E00000));
    CHECK(porpoise_ps_scalar_multiply_try_exact(
        &state, 4U, 1U, 2U, 0U, 0));
    CHECK(state.fpr[4].lane_bits[0] == UINT64_C(0x4024000000000000));
    CHECK(state.fpr[4].lane_bits[1] == UINT64_C(0x402E000000000000));

    set_binary32_pair(
        &state, 3U, UINT32_C(0x41300000), UINT32_C(0x41500000));
    CHECK(porpoise_ps_fma_try_exact(
        &state,
        5U,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        -1,
        0));
    CHECK(state.fpr[5].lane_bits[0] == UINT64_C(0x4035000000000000));
    CHECK(state.fpr[5].lane_bits[1] == UINT64_C(0x4041000000000000));

    CHECK(porpoise_ps_sum_try_exact(&state, 6U, 1U, 2U, 3U, 0U, 0));
    CHECK(state.fpr[6].lane_bits[0] == UINT64_C(0x402E000000000000));
    CHECK(state.fpr[6].lane_bits[1] == UINT64_C(0x401C000000000000));

    set_binary32_pair(
        &state, 1U, UINT32_C(0x3F800000), UINT32_C(0x3F800000));
    set_binary32_pair(
        &state, 2U, UINT32_C(0x00800000), UINT32_C(0x00800000));
    set_bits(
        &state,
        7U,
        UINT64_C(0xAAAAAAAAAAAAAAAA),
        UINT64_C(0xBBBBBBBBBBBBBBBB));
    CHECK(!porpoise_ps_binary_try_exact(
        &state, 7U, 1U, 2U, PORPOISE_FP_BINARY_ADD, 0));
    CHECK(state.fpr[7].lane_bits[0] == UINT64_C(0xAAAAAAAAAAAAAAAA));
    CHECK(state.fpr[7].lane_bits[1] == UINT64_C(0xBBBBBBBBBBBBBBBB));
}

static void check_binary32_pair(
    const PorpoisePpcState *state,
    unsigned int register_index,
    uint32_t lane_zero,
    uint32_t lane_one)
{
    CHECK(state->fpr[register_index].lane_bits[0] ==
          porpoise_binary32_to_binary64_bits(lane_zero));
    CHECK(state->fpr[register_index].lane_bits[1] ==
          porpoise_binary32_to_binary64_bits(lane_one));
}

static void test_exact_paired_scalar_runtime(void)
{
    PorpoisePpcState state;

    porpoise_state_init(&state, NULL);
    enable_exact_fp_domain(&state);
    set_binary32_pair(
        &state, 1U, UINT32_C(0x40800000), UINT32_C(0x40A00000));
    set_binary32_pair(
        &state, 2U, UINT32_C(0x40000000), UINT32_C(0x40400000));
    set_binary32_pair(
        &state, 3U, UINT32_C(0x40C00000), UINT32_C(0x40E00000));

    CHECK(porpoise_ps_fma_try_exact(
        &state,
        4U,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        0,
        0));
    check_binary32_pair(
        &state, 4U, UINT32_C(0x41600000), UINT32_C(0x41880000));

    CHECK(porpoise_ps_fma_try_exact(
        &state,
        4U,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        1,
        0));
    check_binary32_pair(
        &state, 4U, UINT32_C(0x41900000), UINT32_C(0x41B00000));

    CHECK(porpoise_ps_scalar_multiply_try_exact(
        &state, 4U, 1U, 2U, 0U, 0));
    check_binary32_pair(
        &state, 4U, UINT32_C(0x41000000), UINT32_C(0x41200000));

    /* D == C must not replace the scalar before lane one is evaluated. */
    set_binary32_pair(
        &state, 2U, UINT32_C(0x40000000), UINT32_C(0x40400000));
    CHECK(porpoise_ps_fma_try_exact(
        &state,
        2U,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        0,
        0));
    check_binary32_pair(
        &state, 2U, UINT32_C(0x41600000), UINT32_C(0x41880000));

    /* Published single-FMA double-rounding discriminator. */
    set_binary32_pair(
        &state, 1U, UINT32_C(0x42480000), UINT32_C(0x42480000));
    set_binary32_pair(
        &state, 2U, UINT32_C(0xBC88CC38), UINT32_C(0xBC88CC38));
    set_binary32_pair(
        &state, 3U, UINT32_C(0x1B1C72A0), UINT32_C(0x1B1C72A0));
    CHECK(porpoise_ps_fma_try_exact(
        &state,
        4U,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        0,
        0));
    check_binary32_pair(
        &state, 4U, UINT32_C(0xBF55BF17), UINT32_C(0xBF55BF17));
}

static void test_paired_scalar_status_and_gates(void)
{
    PorpoisePpcState state;
    uint32_t original_fprf;

    /* A lane-one sNaN contributes status; FPRF still describes ps0. */
    porpoise_state_init(&state, NULL);
    enable_exact_fp_domain(&state);
    set_binary32_pair(
        &state, 1U, UINT32_C(0x40000000), UINT32_C(0x7F812345));
    set_binary32_pair(
        &state, 2U, UINT32_C(0x40400000), UINT32_C(0x40400000));
    set_binary32_pair(&state, 3U, 0U, 0U);
    CHECK(porpoise_ps_fma_try_exact(
        &state,
        4U,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        0,
        0));
    check_binary32_pair(
        &state, 4U, UINT32_C(0x40C00000), UINT32_C(0x7FC12345));
    CHECK((state.fpscr &
           (PORPOISE_FPSCR_FX | PORPOISE_FPSCR_VX |
            PORPOISE_FPSCR_VXSNAN)) ==
          (PORPOISE_FPSCR_FX | PORPOISE_FPSCR_VX |
           PORPOISE_FPSCR_VXSNAN));
    CHECK((state.fpscr & PORPOISE_FPSCR_FPRF_MASK) ==
          UINT32_C(0x00004000));

    /* VE suppresses both lanes and FPRF. With FE0/FE1 clear the architecture
     * does not dispatch a guest program exception. */
    porpoise_state_init(&state, NULL);
    enable_exact_fp_domain(&state);
    state.fpscr = PORPOISE_FPSCR_VE | UINT32_C(0x00012000);
    original_fprf = state.fpscr & PORPOISE_FPSCR_FPRF_MASK;
    set_binary32_pair(
        &state, 1U, UINT32_C(0x40000000), UINT32_C(0x7F812345));
    set_binary32_pair(
        &state, 2U, UINT32_C(0x40400000), UINT32_C(0x40400000));
    set_binary32_pair(&state, 3U, 0U, 0U);
    set_bits(
        &state,
        4U,
        UINT64_C(0xAAAAAAAAAAAAAAAA),
        UINT64_C(0xBBBBBBBBBBBBBBBB));
    CHECK(porpoise_ps_fma_try_exact(
        &state,
        4U,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        0,
        1));
    CHECK(state.fpr[4].lane_bits[0] == UINT64_C(0xAAAAAAAAAAAAAAAA));
    CHECK(state.fpr[4].lane_bits[1] == UINT64_C(0xBBBBBBBBBBBBBBBB));
    CHECK((state.fpscr & PORPOISE_FPSCR_FPRF_MASK) == original_fprf);
    CHECK(state.fault == PORPOISE_FAULT_NONE);

    /* If guest exception mode is active, suppression still occurs, then the
     * lifted runtime stops at its explicit missing-vector boundary. */
    porpoise_state_init(&state, NULL);
    enable_exact_fp_domain(&state);
    state.msr |= PORPOISE_MSR_FE0;
    state.fpscr = PORPOISE_FPSCR_VE;
    set_binary32_pair(
        &state, 1U, UINT32_C(0x40000000), UINT32_C(0x7F812345));
    set_binary32_pair(
        &state, 2U, UINT32_C(0x40400000), UINT32_C(0x40400000));
    set_binary32_pair(&state, 3U, 0U, 0U);
    set_bits(
        &state,
        4U,
        UINT64_C(0xAAAAAAAAAAAAAAAA),
        UINT64_C(0xBBBBBBBBBBBBBBBB));
    CHECK(porpoise_ps_fma_try_exact(
        &state,
        4U,
        1U,
        2U,
        3U,
        PORPOISE_FP_FMA_MADD,
        0,
        0));
    CHECK(state.fpr[4].lane_bits[0] == UINT64_C(0xAAAAAAAAAAAAAAAA));
    CHECK(state.fpr[4].lane_bits[1] == UINT64_C(0xBBBBBBBBBBBBBBBB));
    CHECK(state.fault == PORPOISE_FAULT_FLOATING_POINT_EXCEPTION);

    /* UE selects the +192 exponent-adjusted result even for an exact tiny
     * value, which sets UX without FI/XX. */
    porpoise_state_init(&state, NULL);
    enable_exact_fp_domain(&state);
    state.fpscr = PORPOISE_FPSCR_UE;
    set_binary32_pair(
        &state, 1U, UINT32_C(0x00000001), UINT32_C(0x00000001));
    set_binary32_pair(
        &state, 2U, UINT32_C(0x3F800000), UINT32_C(0x3F800000));
    CHECK(porpoise_ps_scalar_multiply_try_exact(
        &state, 3U, 1U, 2U, 0U, 0));
    CHECK(state.fpr[3].lane_bits[0] == UINT64_C(0x42A0000000000000));
    CHECK(state.fpr[3].lane_bits[1] == UINT64_C(0x42A0000000000000));
    CHECK((state.fpscr & PORPOISE_FPSCR_UX) != 0U);
    CHECK((state.fpscr &
           (PORPOISE_FPSCR_FI | PORPOISE_FPSCR_XX)) == 0U);

    /* Gekko takes every enabled IEEE exception precisely when either FE bit
     * is set.  Until the runtime has a guest program-exception vector, the
     * adjusted/rounded result is committed and execution stops explicitly. */
    porpoise_state_init(&state, NULL);
    enable_exact_fp_domain(&state);
    state.msr |= PORPOISE_MSR_FE0;
    state.fpscr = PORPOISE_FPSCR_OE;
    set_binary32_pair(
        &state, 1U, UINT32_C(0x7F7FFFFF), UINT32_C(0x7F7FFFFF));
    set_binary32_pair(
        &state, 2U, UINT32_C(0x40000000), UINT32_C(0x40000000));
    CHECK(porpoise_ps_scalar_multiply_try_exact(
        &state, 3U, 1U, 2U, 0U, 0));
    CHECK(state.fpr[3].lane_bits[0] == UINT64_C(0x3BFFFFFFE0000000));
    CHECK(state.fpr[3].lane_bits[1] == UINT64_C(0x3BFFFFFFE0000000));
    CHECK((state.fpscr & PORPOISE_FPSCR_OX) != 0U);
    CHECK(state.fault == PORPOISE_FAULT_FLOATING_POINT_EXCEPTION);

    porpoise_state_init(&state, NULL);
    enable_exact_fp_domain(&state);
    state.msr |= PORPOISE_MSR_FE1;
    state.fpscr = PORPOISE_FPSCR_UE;
    set_binary32_pair(
        &state, 1U, UINT32_C(0x00000001), UINT32_C(0x00000001));
    set_binary32_pair(
        &state, 2U, UINT32_C(0x3F800000), UINT32_C(0x3F800000));
    CHECK(porpoise_ps_scalar_multiply_try_exact(
        &state, 3U, 1U, 2U, 0U, 0));
    CHECK(state.fpr[3].lane_bits[0] == UINT64_C(0x42A0000000000000));
    CHECK(state.fpr[3].lane_bits[1] == UINT64_C(0x42A0000000000000));
    CHECK((state.fpscr & PORPOISE_FPSCR_UX) != 0U);
    CHECK(state.fault == PORPOISE_FAULT_FLOATING_POINT_EXCEPTION);

    porpoise_state_init(&state, NULL);
    enable_exact_fp_domain(&state);
    state.msr |= PORPOISE_MSR_FE0;
    state.fpscr = PORPOISE_FPSCR_XE;
    set_binary32_pair(
        &state, 1U, UINT32_C(0x3F800001), UINT32_C(0x3F800001));
    set_binary32_pair(
        &state, 2U, UINT32_C(0x3F800001), UINT32_C(0x3F800001));
    CHECK(porpoise_ps_scalar_multiply_try_exact(
        &state, 3U, 1U, 2U, 0U, 0));
    check_binary32_pair(
        &state, 3U, UINT32_C(0x3F800002), UINT32_C(0x3F800002));
    CHECK((state.fpscr & PORPOISE_FPSCR_XX) != 0U);
    CHECK(state.fault == PORPOISE_FAULT_FLOATING_POINT_EXCEPTION);

    /* FP and paired-single availability are architectural gates, not reasons
     * to fall through to host arithmetic. */
    porpoise_state_init(&state, NULL);
    state.hid2 = PORPOISE_HID2_PSE;
    set_binary32_pair(&state, 1U, UINT32_C(0x3F800000), 0U);
    set_binary32_pair(&state, 2U, UINT32_C(0x3F800000), 0U);
    CHECK(!porpoise_ps_scalar_multiply_try_exact(
        &state, 3U, 1U, 2U, 0U, 0));
    CHECK(state.fault == PORPOISE_FAULT_FLOATING_POINT_UNAVAILABLE);

    porpoise_state_init(&state, NULL);
    state.msr = PORPOISE_MSR_FP;
    set_binary32_pair(&state, 1U, UINT32_C(0x3F800000), 0U);
    set_binary32_pair(&state, 2U, UINT32_C(0x3F800000), 0U);
    CHECK(!porpoise_ps_scalar_multiply_try_exact(
        &state, 3U, 1U, 2U, 0U, 0));
    CHECK(state.fault == PORPOISE_FAULT_ILLEGAL_INSTRUCTION);
}

static void test_direct_paired_scalar_instructions(void)
{
    PorpoisePpcState state;

    porpoise_state_init(&state, NULL);
    enable_exact_fp_domain(&state);
    set_binary32_pair(
        &state, 1U, UINT32_C(0x40800000), UINT32_C(0x40A00000));
    set_binary32_pair(
        &state, 2U, UINT32_C(0x40000000), UINT32_C(0x40400000));
    set_binary32_pair(
        &state, 3U, UINT32_C(0x40C00000), UINT32_C(0x40E00000));

    CHECK(porpoise_ps_madds_scalar(
        &state, 4U, 1U, 2U, 3U, 0U, 1));
    check_binary32_pair(
        &state, 4U, UINT32_C(0x41600000), UINT32_C(0x41880000));
    CHECK(porpoise_cr_get_field(&state, 1U) ==
          (uint8_t)((state.fpscr >> 28U) & 0xFU));

    CHECK(porpoise_ps_muls_scalar(
        &state, 4U, 1U, 2U, 1U, 0));
    check_binary32_pair(
        &state, 4U, UINT32_C(0x41400000), UINT32_C(0x41700000));

    /* Arbitrary binary64 contents are outside Gekko's defined paired-single
     * domain and must stop instead of falling back to host arithmetic. */
    porpoise_state_init(&state, NULL);
    enable_exact_fp_domain(&state);
    set_bits(
        &state,
        1U,
        UINT64_C(0x3FF0000000000001),
        UINT64_C(0x3FF0000000000000));
    set_binary32_pair(
        &state, 2U, UINT32_C(0x3F800000), UINT32_C(0x3F800000));
    CHECK(!porpoise_ps_muls_scalar(
        &state, 3U, 1U, 2U, 0U, 0));
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
}

int main(void)
{
    test_frsp_rounding_modes();
    test_frsp_specials_and_range();
    test_frsqrte_reference_vectors();
    test_frsqrte_special_values();
    test_fctiw_rounding_modes();
    test_fctiw_boundaries_and_exceptions();
    test_fctiwz();
    test_fpscr_moves();
    test_fma_numeric_and_lanes();
    test_fma_nan_invalid_and_ni();
    test_conservative_scalar_exact_domain();
    test_conservative_fma_exact_domain();
    test_conservative_paired_exact_domain();
    test_exact_paired_scalar_runtime();
    test_paired_scalar_status_and_gates();
    test_direct_paired_scalar_instructions();
    return 0;
}
