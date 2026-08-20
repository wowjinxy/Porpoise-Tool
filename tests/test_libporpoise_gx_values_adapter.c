#include "porpoise_libporpoise_builtins_private.h"

#include <porpoise/gx_values_stub.h>
#include <porpoise/stub.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            (void)fprintf(                                                 \
                stderr,                                                    \
                "check failed at %s:%d: %s\n",                            \
                __FILE__,                                                  \
                __LINE__,                                                  \
                #condition);                                               \
            abort();                                                       \
        }                                                                  \
    } while (0)

#define FIFO_BASE UINT32_C(0x80010000)
#define FIFO_SIZE UINT32_C(0x00010000)
#define DATA_BASE UINT32_C(0x80030000)
#define STACK_BASE UINT32_C(0x80050000)
#define MEMORY_END UINT32_C(0x81800000)
#define TEST_PC UINT32_C(0x803CE000)

static void store_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void store_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static uint32_t load_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static uint32_t float_bits(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void encode_floats(
    uint8_t *bytes,
    const float *values,
    size_t count)
{
    size_t index;

    for (index = 0U; index < count; index++) {
        store_be32(&bytes[index * 4U], float_bits(values[index]));
    }
}

static void write_guest(
    PorpoiseHostAdapter *host,
    uint32_t address,
    const void *bytes,
    size_t size)
{
    CHECK(host->write_bytes(host->context, address, bytes, size) ==
          PORPOISE_HOST_OK);
}

static void read_guest(
    PorpoiseHostAdapter *host,
    uint32_t address,
    void *bytes,
    size_t size)
{
    CHECK(host->read_bytes(host->context, address, bytes, size) ==
          PORPOISE_HOST_OK);
}

static void prepare_call(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    porpoise_state_init(state, host);
    state->pc = TEST_PC;
}

static void initialize_gx(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    prepare_call(state, host);
    state->gpr[3] = FIFO_BASE;
    state->gpr[4] = FIFO_SIZE;
    porpoise_libporpoise_gx_init_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXInitCallCount() == 1U);
}

static void check_fault(
    const PorpoisePpcState *state,
    PorpoiseFault fault,
    uint32_t address)
{
    CHECK(state->status == PORPOISE_EXECUTION_FAULTED);
    CHECK(state->fault == fault);
    CHECK(state->fault_address == address);
}

static void test_active_gate(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    prepare_call(state, host);
    state->gpr[1] = STACK_BASE;
    porpoise_libporpoise_gx_set_tev_indirect_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_STATE, TEST_PC);
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_SET_TEV_INDIRECT) == 0U);
}

static void test_display_list(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    uint8_t display_list[32];
    void *expected_pointer;
    unsigned int before;
    size_t index;

    for (index = 0U; index < sizeof(display_list); index++) {
        display_list[index] = (uint8_t)(0x80U + index);
    }
    write_guest(host, DATA_BASE, display_list, sizeof(display_list));
    expected_pointer = NULL;
    CHECK(host->decode_pointer(
              host->context, DATA_BASE, &expected_pointer) ==
          PORPOISE_HOST_OK);

    prepare_call(state, host);
    state->gpr[3] = DATA_BASE;
    state->gpr[4] = (uint32_t)sizeof(display_list);
    porpoise_libporpoise_gx_call_display_list_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_CALL_DISPLAY_LIST) == 1U);
    CHECK(PorpoiseStubGXValueLastPointer() == expected_pointer);
    CHECK(PorpoiseStubGXValueLastU32(0U) == sizeof(display_list));
    CHECK(PorpoiseStubGXValueLastByteCount() == sizeof(display_list));
    for (index = 0U; index < sizeof(display_list); index++) {
        CHECK(PorpoiseStubGXValueLastByte((unsigned int)index) ==
              display_list[index]);
    }

    before = PorpoiseStubGXValueCallCount(
        PORPOISE_STUB_GX_CALL_DISPLAY_LIST);
    prepare_call(state, host);
    state->gpr[3] = DATA_BASE;
    state->gpr[4] = 31U;
    porpoise_libporpoise_gx_call_display_list_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_ARGUMENT, 31U);
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_CALL_DISPLAY_LIST) == before);

    prepare_call(state, host);
    state->gpr[3] = DATA_BASE + 4U;
    state->gpr[4] = 32U;
    porpoise_libporpoise_gx_call_display_list_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        DATA_BASE + 4U);
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_CALL_DISPLAY_LIST) == before);

    prepare_call(state, host);
    state->gpr[3] = MEMORY_END - 32U;
    state->gpr[4] = 64U;
    porpoise_libporpoise_gx_call_display_list_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNMAPPED_ADDRESS,
        MEMORY_END - 32U);
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_CALL_DISPLAY_LIST) == before);

    PorpoiseStubSetDecodeBias(1U);
    prepare_call(state, host);
    state->gpr[3] = DATA_BASE;
    state->gpr[4] = 32U;
    porpoise_libporpoise_gx_call_display_list_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_POINTER, DATA_BASE);
    PorpoiseStubSetDecodeBias(0U);
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_CALL_DISPLAY_LIST) == before);
}

static void test_projection(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    static const float input[16] = {
        1.0f, -2.0f, 3.25f, 4.5f,
        5.0f, 6.0f, -7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        -13.0f, 14.0f, 15.0f, 16.0f
    };
    static const float output[7] = {
        1.0f, -0.5f, 2.25f, 3.5f, -4.75f, 5.0f, 6.125f
    };
    uint8_t raw[64];
    uint8_t actual[28];
    unsigned int before;
    size_t index;

    encode_floats(raw, input, 16U);
    write_guest(host, DATA_BASE, raw, sizeof(raw));
    prepare_call(state, host);
    state->gpr[3] = DATA_BASE;
    state->gpr[4] = UINT32_C(0x12345678);
    porpoise_libporpoise_gx_set_projection_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_SET_PROJECTION) == 1U);
    CHECK(PorpoiseStubGXValueLastU32(0U) == UINT32_C(0x12345678));
    for (index = 0U; index < 16U; index++) {
        CHECK(float_bits(PorpoiseStubGXValueLastFloat((unsigned int)index)) ==
              float_bits(input[index]));
    }

    PorpoiseStubGXValueSetProjectionOutput(output);
    memset(actual, 0xA5, sizeof(actual));
    write_guest(host, DATA_BASE + 0x100U, actual, sizeof(actual));
    prepare_call(state, host);
    state->gpr[3] = DATA_BASE + 0x100U;
    porpoise_libporpoise_gx_get_projectionv_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_GET_PROJECTIONV) == 1U);
    read_guest(host, DATA_BASE + 0x100U, actual, sizeof(actual));
    for (index = 0U; index < 7U; index++) {
        CHECK(load_be32(&actual[index * 4U]) == float_bits(output[index]));
    }

    before = PorpoiseStubGXValueCallCount(
        PORPOISE_STUB_GX_GET_PROJECTIONV);
    prepare_call(state, host);
    state->gpr[3] = MEMORY_END - 16U;
    porpoise_libporpoise_gx_get_projectionv_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNMAPPED_ADDRESS,
        MEMORY_END - 16U);
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_GET_PROJECTIONV) == before);
}

static void test_matrices(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    static const float matrix[12] = {
        1.0f, 2.0f, 3.0f, 4.0f,
        -5.0f, -6.0f, -7.0f, -8.0f,
        9.5f, 10.5f, 11.5f, 12.5f
    };
    uint8_t raw[48];
    unsigned int before;
    size_t index;

    encode_floats(raw, matrix, 12U);
    write_guest(host, DATA_BASE, raw, sizeof(raw));

    prepare_call(state, host);
    state->gpr[3] = DATA_BASE;
    state->gpr[4] = 7U;
    porpoise_libporpoise_gx_load_pos_mtx_imm_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_LOAD_POS_MTX_IMM) == 1U);
    CHECK(PorpoiseStubGXValueLastU32(0U) == 7U);
    for (index = 0U; index < 12U; index++) {
        CHECK(float_bits(PorpoiseStubGXValueLastFloat((unsigned int)index)) ==
              float_bits(matrix[index]));
    }

    prepare_call(state, host);
    state->gpr[3] = DATA_BASE;
    state->gpr[4] = 9U;
    porpoise_libporpoise_gx_load_nrm_mtx_imm_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_LOAD_NRM_MTX_IMM) == 1U);
    CHECK(PorpoiseStubGXValueLastU32(0U) == 9U);
    for (index = 0U; index < 12U; index++) {
        CHECK(float_bits(PorpoiseStubGXValueLastFloat((unsigned int)index)) ==
              float_bits(matrix[index]));
    }

    write_guest(host, MEMORY_END - 32U, raw, 32U);
    prepare_call(state, host);
    state->gpr[3] = MEMORY_END - 32U;
    state->gpr[4] = 11U;
    state->gpr[5] = 1U;
    porpoise_libporpoise_gx_load_tex_mtx_imm_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_LOAD_TEX_MTX_IMM) == 1U);
    CHECK(PorpoiseStubGXValueLastU32(0U) == 11U);
    CHECK(PorpoiseStubGXValueLastU32(1U) == 1U);
    for (index = 0U; index < 8U; index++) {
        CHECK(float_bits(PorpoiseStubGXValueLastFloat((unsigned int)index)) ==
              float_bits(matrix[index]));
    }

    before = PorpoiseStubGXValueCallCount(
        PORPOISE_STUB_GX_LOAD_TEX_MTX_IMM);
    prepare_call(state, host);
    state->gpr[3] = MEMORY_END - 32U;
    state->gpr[4] = 12U;
    state->gpr[5] = 0U;
    porpoise_libporpoise_gx_load_tex_mtx_imm_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNMAPPED_ADDRESS,
        MEMORY_END - 32U);
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_LOAD_TEX_MTX_IMM) == before);

    prepare_call(state, host);
    state->gpr[3] = DATA_BASE;
    state->gpr[4] = 12U;
    state->gpr[5] = 2U;
    porpoise_libporpoise_gx_load_tex_mtx_imm_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_ARGUMENT, 2U);
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_LOAD_TEX_MTX_IMM) == before);
}

static void test_viewport_and_indirect_matrix(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    static const float viewport[6] = {
        -1.0f, 2.0f, 640.0f, 480.0f, 0.125f, 1.0f
    };
    static const float indirect[6] = {
        0.5f, -0.25f, 1.0f, 2.0f, 3.0f, -4.0f
    };
    uint8_t raw[24];
    size_t index;

    PorpoiseStubGXValueSetViewportOutput(viewport);
    memset(raw, 0xA5, sizeof(raw));
    write_guest(host, DATA_BASE + 0x200U, raw, sizeof(raw));
    prepare_call(state, host);
    state->gpr[3] = DATA_BASE + 0x200U;
    porpoise_libporpoise_gx_get_viewportv_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_GET_VIEWPORTV) == 1U);
    read_guest(host, DATA_BASE + 0x200U, raw, sizeof(raw));
    for (index = 0U; index < 6U; index++) {
        CHECK(load_be32(&raw[index * 4U]) == float_bits(viewport[index]));
    }

    encode_floats(raw, indirect, 6U);
    write_guest(host, DATA_BASE + 0x240U, raw, sizeof(raw));
    prepare_call(state, host);
    state->gpr[3] = 4U;
    state->gpr[4] = DATA_BASE + 0x240U;
    state->gpr[5] = UINT32_C(0xFFFFFF80);
    porpoise_libporpoise_gx_set_ind_tex_mtx_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_SET_IND_TEX_MTX) == 1U);
    CHECK(PorpoiseStubGXValueLastU32(0U) == 4U);
    CHECK(PorpoiseStubGXValueLastS32(1U) == -128);
    for (index = 0U; index < 6U; index++) {
        CHECK(float_bits(PorpoiseStubGXValueLastFloat((unsigned int)index)) ==
              float_bits(indirect[index]));
    }
}

static void test_colors_and_fog(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    const uint8_t color[4] = {0x12U, 0x34U, 0x56U, 0x78U};
    uint8_t signed_color[8];
    uint8_t fog_table[20];
    unsigned int before;
    unsigned int index;

    write_guest(host, DATA_BASE + 0x300U, color, sizeof(color));
    prepare_call(state, host);
    state->gpr[3] = 2U;
    state->gpr[4] = DATA_BASE + 0x300U;
    porpoise_libporpoise_gx_set_tev_color_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_SET_TEV_COLOR) == 1U);
    CHECK(PorpoiseStubGXValueLastU32(0U) == 2U);
    CHECK(PorpoiseStubGXValueLastU32(1U) == UINT32_C(0x12345678));

    store_be16(&signed_color[0], (uint16_t)(int16_t)-512);
    store_be16(&signed_color[2], (uint16_t)(int16_t)511);
    store_be16(&signed_color[4], (uint16_t)(int16_t)-1);
    store_be16(&signed_color[6], 0U);
    write_guest(host, DATA_BASE + 0x308U, signed_color, sizeof(signed_color));
    prepare_call(state, host);
    state->gpr[3] = 3U;
    state->gpr[4] = DATA_BASE + 0x308U;
    porpoise_libporpoise_gx_set_tev_color_s10_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_SET_TEV_COLOR_S10) == 1U);
    CHECK(PorpoiseStubGXValueLastU32(0U) == 3U);
    CHECK(PorpoiseStubGXValueLastS16(0U) == -512);
    CHECK(PorpoiseStubGXValueLastS16(1U) == 511);
    CHECK(PorpoiseStubGXValueLastS16(2U) == -1);
    CHECK(PorpoiseStubGXValueLastS16(3U) == 0);

    prepare_call(state, host);
    state->gpr[3] = 1U;
    state->gpr[4] = DATA_BASE + 0x300U;
    porpoise_libporpoise_gx_set_tev_kcolor_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_SET_TEV_KCOLOR) == 1U);
    CHECK(PorpoiseStubGXValueLastU32(0U) == 1U);
    CHECK(PorpoiseStubGXValueLastU32(1U) == UINT32_C(0x12345678));

    prepare_call(state, host);
    state->gpr[3] = 5U;
    state->gpr[4] = DATA_BASE + 0x300U;
    porpoise_fpr_set_f64(state, 1U, 0U, 1.25);
    porpoise_fpr_set_f64(state, 2U, 0U, 2.5);
    porpoise_fpr_set_f64(state, 3U, 0U, 3.75);
    porpoise_fpr_set_f64(state, 4U, 0U, 4.5);
    porpoise_libporpoise_gx_set_fog_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_SET_FOG) == 1U);
    CHECK(PorpoiseStubGXValueLastU32(0U) == 5U);
    CHECK(PorpoiseStubGXValueLastU32(1U) == UINT32_C(0x12345678));
    CHECK(PorpoiseStubGXValueLastFloat(0U) == 1.25f);
    CHECK(PorpoiseStubGXValueLastFloat(1U) == 2.5f);
    CHECK(PorpoiseStubGXValueLastFloat(2U) == 3.75f);
    CHECK(PorpoiseStubGXValueLastFloat(3U) == 4.5f);

    for (index = 0U; index < 10U; index++) {
        store_be16(&fog_table[index * 2U], (uint16_t)(0x100U + index));
    }
    write_guest(host, DATA_BASE + 0x320U, fog_table, sizeof(fog_table));
    prepare_call(state, host);
    state->gpr[3] = 1U;
    state->gpr[4] = UINT32_C(0x12345);
    state->gpr[5] = DATA_BASE + 0x320U;
    porpoise_libporpoise_gx_set_fog_range_adj_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_SET_FOG_RANGE_ADJ) == 1U);
    CHECK(PorpoiseStubGXValueLastU32(0U) == 1U);
    CHECK(PorpoiseStubGXValueLastU32(1U) == UINT32_C(0x2345));
    CHECK(PorpoiseStubGXValueLastU32(2U) == 1U);
    for (index = 0U; index < 10U; index++) {
        CHECK(PorpoiseStubGXValueLastU16(index) ==
              (uint16_t)(0x100U + index));
    }

    prepare_call(state, host);
    state->gpr[3] = 0U;
    state->gpr[4] = 77U;
    state->gpr[5] = 0U;
    porpoise_libporpoise_gx_set_fog_range_adj_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_SET_FOG_RANGE_ADJ) == 2U);
    CHECK(PorpoiseStubGXValueLastU32(0U) == 0U);
    CHECK(PorpoiseStubGXValueLastU32(1U) == 77U);
    CHECK(PorpoiseStubGXValueLastU32(2U) == 0U);

    before = PorpoiseStubGXValueCallCount(
        PORPOISE_STUB_GX_SET_FOG_RANGE_ADJ);
    prepare_call(state, host);
    state->gpr[3] = 1U;
    state->gpr[4] = 77U;
    state->gpr[5] = 0U;
    porpoise_libporpoise_gx_set_fog_range_adj_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_POINTER, 0U);
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_SET_FOG_RANGE_ADJ) == before);
}

static void test_tev_indirect_stack_arguments(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    uint8_t stack[16] = {0U};
    unsigned int before;
    unsigned int index;

    store_be32(&stack[8], UINT32_C(0x00000100));
    store_be32(&stack[12], UINT32_C(0x12345679));
    write_guest(host, STACK_BASE, stack, sizeof(stack));

    prepare_call(state, host);
    state->gpr[1] = STACK_BASE;
    for (index = 3U; index <= 10U; index++) {
        state->gpr[index] = index - 2U;
    }
    state->gpr[10] = UINT32_C(0x00000101);
    porpoise_libporpoise_gx_set_tev_indirect_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_SET_TEV_INDIRECT) == 1U);
    for (index = 0U; index < 7U; index++) {
        CHECK(PorpoiseStubGXValueLastU32(index) == index + 1U);
    }
    CHECK(PorpoiseStubGXValueLastU32(7U) == 1U);
    CHECK(PorpoiseStubGXValueLastU32(8U) == 0U);
    CHECK(PorpoiseStubGXValueLastU32(9U) == UINT32_C(0x12345679));

    before = PorpoiseStubGXValueCallCount(
        PORPOISE_STUB_GX_SET_TEV_INDIRECT);
    prepare_call(state, host);
    state->gpr[1] = UINT32_C(0xFFFFFFF8);
    porpoise_libporpoise_gx_set_tev_indirect_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_ADDRESS_OVERFLOW,
        UINT32_C(0xFFFFFFF8));
    CHECK(PorpoiseStubGXValueCallCount(
              PORPOISE_STUB_GX_SET_TEV_INDIRECT) == before);
}

int main(void)
{
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    memset(&host, 0, sizeof(host));
    PorpoiseStubGXInitReset();
    PorpoiseStubGXValuesReset();
    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);

    test_active_gate(&state, &host);
    initialize_gx(&state, &host);
    test_display_list(&state, &host);
    test_projection(&state, &host);
    test_matrices(&state, &host);
    test_viewport_and_indirect_matrix(&state, &host);
    test_colors_and_fog(&state, &host);
    test_tev_indirect_stack_arguments(&state, &host);

    porpoise_libporpoise_adapter_shutdown(&host);
    return 0;
}
