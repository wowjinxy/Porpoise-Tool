#include "porpoise_libporpoise_builtins_private.h"

#include <dolphin/gx.h>
#include <porpoise/gx_objects_stub.h>
#include <porpoise/stub.h>

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

#define TEST_PC UINT32_C(0x803CB69C)
#define FIFO_ADDRESS UINT32_C(0x80080000)
#define FIFO_SIZE UINT32_C(0x00010000)
#define ARRAY_ADDRESS UINT32_C(0x80001000)
#define TEX_OBJECT_ADDRESS UINT32_C(0x80020000)
#define TLUT_OBJECT_ADDRESS UINT32_C(0x80020100)
#define COLOR_ADDRESS UINT32_C(0x80020200)
#define USER_DATA_ADDRESS UINT32_C(0x80020300)
#define IMAGE_PHYSICAL_ADDRESS UINT32_C(0x00040000)
#define IMAGE_CACHED_ADDRESS UINT32_C(0x80040000)
#define TLUT_PHYSICAL_ADDRESS UINT32_C(0x00050000)
#define TLUT_CACHED_ADDRESS UINT32_C(0x80050000)
#define CONSOLE_MEMORY_SIZE UINT32_C(0x01800000)

static void write_be16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static void write_be32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static void *decode_guest(
    PorpoiseHostAdapter *host,
    uint32_t address)
{
    void *pointer = NULL;

    CHECK(host->decode_pointer(host->context, address, &pointer) ==
          PORPOISE_HOST_OK);
    CHECK(pointer != NULL);
    return pointer;
}

static void prepare_call(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    porpoise_state_init(state, host);
    state->pc = TEST_PC;
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

static void check_no_object_mutation(void)
{
    const PorpoiseStubGxObjectsSnapshot *calls =
        PorpoiseStubGxObjectsGet();

    CHECK(calls->set_array_count == 0U);
    CHECK(calls->init_texture_count == 0U);
    CHECK(calls->init_ci_texture_count == 0U);
    CHECK(calls->init_lod_count == 0U);
    CHECK(calls->init_user_data_count == 0U);
    CHECK(calls->load_texture_count == 0U);
    CHECK(calls->init_tlut_count == 0U);
    CHECK(calls->load_tlut_count == 0U);
    CHECK(calls->ambient_color_count == 0U);
    CHECK(calls->material_color_count == 0U);
}

static void initialize_native_gx(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    PorpoiseStubGXInitReset();
    prepare_call(state, host);
    state->gpr[3] = FIFO_ADDRESS;
    state->gpr[4] = FIFO_SIZE;
    porpoise_libporpoise_gx_init_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXInitCallCount() == 1U);
    CHECK(state->gpr[3] == PorpoiseStubTokenAddress());
}

static void build_texture_object(
    PorpoiseHostAdapter *host,
    uint32_t object_address,
    uint32_t image_address,
    uint32_t format,
    uint32_t tlut_name,
    uint8_t flags,
    uint32_t user_data)
{
    uint8_t *bytes = (uint8_t *)decode_guest(host, object_address);
    uint32_t mode0;
    uint32_t mode1;
    uint32_t image0;

    memset(bytes, 0, 0x20U);
    mode0 = UINT32_C(1) | (UINT32_C(2) << 2) |
            (UINT32_C(1) << 4) | (UINT32_C(6) << 5) |
            (UINT32_C(0xD0) << 9) | (UINT32_C(1) << 19) |
            (UINT32_C(1) << 21);
    mode1 = UINT32_C(24) | (UINT32_C(48) << 8);
    image0 = UINT32_C(63) | (UINT32_C(31) << 10) |
             ((format & UINT32_C(0xF)) << 20);
    write_be32(&bytes[0x00], mode0);
    write_be32(&bytes[0x04], mode1);
    write_be32(&bytes[0x08], image0);
    write_be32(
        &bytes[0x0C],
        (image_address >> 5) & UINT32_C(0x001FFFFF));
    write_be32(&bytes[0x10], user_data);
    write_be32(&bytes[0x14], format);
    write_be32(&bytes[0x18], tlut_name);
    write_be16(&bytes[0x1C], UINT16_C(32));
    bytes[0x1E] = 2U;
    bytes[0x1F] = flags;
}

static void build_tlut_object(
    PorpoiseHostAdapter *host,
    uint32_t object_address,
    uint32_t table_address,
    uint32_t format,
    uint32_t entries)
{
    uint8_t *bytes = (uint8_t *)decode_guest(host, object_address);

    memset(bytes, 0, 0x0CU);
    write_be32(&bytes[0x00], format << 10);
    write_be32(
        &bytes[0x04],
        UINT32_C(0x64000000) |
            ((table_address >> 5) & UINT32_C(0x001FFFFF)));
    write_be16(&bytes[0x08], (uint16_t)entries);
}

static void test_requires_active_gx(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    PorpoiseStubGxObjectsReset();
    prepare_call(state, host);
    state->gpr[3] = GX_VA_POS;
    state->gpr[4] = ARRAY_ADDRESS;
    state->gpr[5] = 12U;
    porpoise_libporpoise_gx_set_array_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_STATE, TEST_PC);
    check_no_object_mutation();
}

static void test_set_array(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    const PorpoiseStubGxObjectsSnapshot *calls;
    void *expected_base;

    PorpoiseStubGxObjectsReset();
    expected_base = decode_guest(host, ARRAY_ADDRESS);
    prepare_call(state, host);
    state->gpr[3] = GX_VA_NBT;
    state->gpr[4] = ARRAY_ADDRESS;
    state->gpr[5] = 12U;
    porpoise_libporpoise_gx_set_array_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    calls = PorpoiseStubGxObjectsGet();
    CHECK(calls->set_array_count == 1U);
    CHECK(calls->array_attribute == GX_VA_NBT);
    CHECK(calls->array_base == expected_base);
    CHECK(calls->array_size == CONSOLE_MEMORY_SIZE - 0x1000U);
    CHECK(calls->array_stride == 12U);

    PorpoiseStubGxObjectsReset();
    prepare_call(state, host);
    state->gpr[3] = GX_VA_POS - 1U;
    state->gpr[4] = ARRAY_ADDRESS;
    state->gpr[5] = 12U;
    porpoise_libporpoise_gx_set_array_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_ARGUMENT, GX_VA_POS - 1U);
    check_no_object_mutation();

    prepare_call(state, host);
    state->gpr[3] = GX_VA_POS;
    state->gpr[4] = ARRAY_ADDRESS;
    state->gpr[5] = 256U;
    porpoise_libporpoise_gx_set_array_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_ARGUMENT, 256U);
    check_no_object_mutation();

    prepare_call(state, host);
    state->gpr[3] = GX_VA_POS;
    state->gpr[4] = UINT32_C(0xCC005000);
    state->gpr[5] = 12U;
    porpoise_libporpoise_gx_set_array_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_MMIO,
        UINT32_C(0xCC005000));
    check_no_object_mutation();

    prepare_call(state, host);
    state->gpr[3] = GX_VA_POS;
    state->gpr[4] = PorpoiseStubTokenAddress();
    state->gpr[5] = 12U;
    porpoise_libporpoise_gx_set_array_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_INVALID_POINTER,
        PorpoiseStubTokenAddress());
    check_no_object_mutation();
}

static void test_texture_load(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    const PorpoiseStubGxObjectsSnapshot *calls;
    void *expected_image;

    build_texture_object(
        host,
        TEX_OBJECT_ADDRESS,
        IMAGE_PHYSICAL_ADDRESS,
        GX_TF_RGBA8,
        0U,
        3U,
        0U);
    PorpoiseStubGxObjectsReset();
    PorpoiseStubGxObjectsSetTextureSize(0x180U);
    expected_image = decode_guest(host, IMAGE_CACHED_ADDRESS);
    prepare_call(state, host);
    state->gpr[3] = TEX_OBJECT_ADDRESS;
    state->gpr[4] = GX_TEXMAP3;
    porpoise_libporpoise_gx_load_tex_obj_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    calls = PorpoiseStubGxObjectsGet();
    CHECK(calls->get_texture_size_count == 1U);
    CHECK(calls->texture_size_width == 64U);
    CHECK(calls->texture_size_height == 32U);
    CHECK(calls->texture_size_format == GX_TF_RGBA8);
    CHECK(calls->texture_size_mipmap == 1U);
    CHECK(calls->texture_size_max_lod == 3U);
    CHECK(calls->init_texture_count == 1U);
    CHECK(calls->init_ci_texture_count == 0U);
    CHECK(calls->init_lod_count == 1U);
    CHECK(calls->init_user_data_count == 0U);
    CHECK(calls->load_texture_count == 1U);
    CHECK(calls->texture_image == expected_image);
    CHECK(calls->texture_user_data == NULL);
    CHECK(calls->texture_width == 64U);
    CHECK(calls->texture_height == 32U);
    CHECK(calls->texture_format == GX_TF_RGBA8);
    CHECK(calls->texture_wrap_s == GX_REPEAT);
    CHECK(calls->texture_wrap_t == GX_MIRROR);
    CHECK(calls->texture_mipmap == 1U);
    CHECK(calls->texture_min_filter == GX_LIN_MIP_LIN);
    CHECK(calls->texture_mag_filter == GX_LINEAR);
    CHECK(calls->texture_min_lod == 1.5f);
    CHECK(calls->texture_max_lod == 3.0f);
    CHECK(calls->texture_lod_bias == -1.5f);
    CHECK(calls->texture_bias_clamp == 1U);
    CHECK(calls->texture_edge_lod == 1U);
    CHECK(calls->texture_max_aniso == GX_ANISO_2);
    CHECK(calls->texture_map_id == GX_TEXMAP3);

    build_texture_object(
        host,
        TEX_OBJECT_ADDRESS,
        IMAGE_PHYSICAL_ADDRESS,
        GX_TF_C8,
        7U,
        0U,
        0U);
    PorpoiseStubGxObjectsReset();
    PorpoiseStubGxObjectsSetTextureSize(0x80U);
    prepare_call(state, host);
    state->gpr[3] = TEX_OBJECT_ADDRESS;
    state->gpr[4] = GX_TEXMAP1;
    porpoise_libporpoise_gx_load_tex_obj_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    calls = PorpoiseStubGxObjectsGet();
    CHECK(calls->init_texture_count == 0U);
    CHECK(calls->init_ci_texture_count == 1U);
    CHECK(calls->texture_tlut_name == 7U);
    CHECK(calls->load_texture_count == 1U);
}

static void test_texture_validation(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    build_texture_object(
        host,
        TEX_OBJECT_ADDRESS,
        IMAGE_PHYSICAL_ADDRESS,
        UINT32_C(0x20),
        0U,
        2U,
        0U);
    PorpoiseStubGxObjectsReset();
    prepare_call(state, host);
    state->gpr[3] = TEX_OBJECT_ADDRESS;
    state->gpr[4] = GX_TEXMAP0;
    porpoise_libporpoise_gx_load_tex_obj_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_ARGUMENT, UINT32_C(0x20));
    check_no_object_mutation();
    CHECK(PorpoiseStubGxObjectsGet()->get_texture_size_count == 0U);

    build_texture_object(
        host,
        TEX_OBJECT_ADDRESS,
        IMAGE_PHYSICAL_ADDRESS,
        GX_TF_I4,
        0U,
        2U,
        0U);
    write_be32(
        &((uint8_t *)decode_guest(host, TEX_OBJECT_ADDRESS))[0x08],
        UINT32_C(63) | (UINT32_C(31) << 10) |
            ((uint32_t)GX_TF_RGBA8 << 20));
    PorpoiseStubGxObjectsReset();
    prepare_call(state, host);
    state->gpr[3] = TEX_OBJECT_ADDRESS;
    state->gpr[4] = GX_TEXMAP0;
    porpoise_libporpoise_gx_load_tex_obj_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_ARGUMENT, GX_TF_I4);
    check_no_object_mutation();
    CHECK(PorpoiseStubGxObjectsGet()->get_texture_size_count == 0U);

    build_texture_object(
        host,
        TEX_OBJECT_ADDRESS,
        IMAGE_PHYSICAL_ADDRESS,
        GX_TF_I4,
        0U,
        2U,
        USER_DATA_ADDRESS);
    PorpoiseStubGxObjectsReset();
    prepare_call(state, host);
    state->gpr[3] = TEX_OBJECT_ADDRESS;
    state->gpr[4] = GX_TEXMAP0;
    porpoise_libporpoise_gx_load_tex_obj_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        USER_DATA_ADDRESS);
    check_no_object_mutation();
    CHECK(PorpoiseStubGxObjectsGet()->get_texture_size_count == 0U);

    build_texture_object(
        host,
        TEX_OBJECT_ADDRESS,
        CONSOLE_MEMORY_SIZE - 32U,
        GX_TF_I4,
        0U,
        2U,
        0U);
    PorpoiseStubGxObjectsReset();
    PorpoiseStubGxObjectsSetTextureSize(33U);
    prepare_call(state, host);
    state->gpr[3] = TEX_OBJECT_ADDRESS;
    state->gpr[4] = GX_TEXMAP0;
    porpoise_libporpoise_gx_load_tex_obj_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNMAPPED_ADDRESS,
        CONSOLE_MEMORY_SIZE - 32U);
    check_no_object_mutation();
    CHECK(PorpoiseStubGxObjectsGet()->get_texture_size_count == 1U);

    PorpoiseStubGxObjectsReset();
    prepare_call(state, host);
    state->gpr[3] = TEX_OBJECT_ADDRESS + 2U;
    state->gpr[4] = GX_TEXMAP0;
    porpoise_libporpoise_gx_load_tex_obj_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        TEX_OBJECT_ADDRESS + 2U);
    check_no_object_mutation();

    prepare_call(state, host);
    state->gpr[3] = UINT32_C(0xCC005000);
    state->gpr[4] = GX_TEXMAP0;
    porpoise_libporpoise_gx_load_tex_obj_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_MMIO,
        UINT32_C(0xCC005000));
    check_no_object_mutation();

    prepare_call(state, host);
    state->gpr[3] = PorpoiseStubTokenAddress();
    state->gpr[4] = GX_TEXMAP0;
    porpoise_libporpoise_gx_load_tex_obj_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_INVALID_POINTER,
        PorpoiseStubTokenAddress());
    check_no_object_mutation();
}

static void test_tlut_load(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    const PorpoiseStubGxObjectsSnapshot *calls;
    void *expected_table;

    build_tlut_object(
        host,
        TLUT_OBJECT_ADDRESS,
        TLUT_PHYSICAL_ADDRESS,
        GX_TL_RGB5A3,
        16U);
    PorpoiseStubGxObjectsReset();
    expected_table = decode_guest(host, TLUT_CACHED_ADDRESS);
    prepare_call(state, host);
    state->gpr[3] = TLUT_OBJECT_ADDRESS;
    state->gpr[4] = GX_BIGTLUT0;
    porpoise_libporpoise_gx_load_tlut_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    calls = PorpoiseStubGxObjectsGet();
    CHECK(calls->init_tlut_count == 1U);
    CHECK(calls->load_tlut_count == 1U);
    CHECK(calls->tlut_table == expected_table);
    CHECK(calls->tlut_format == GX_TL_RGB5A3);
    CHECK(calls->tlut_entries == 16U);
    CHECK(calls->tlut_name == GX_BIGTLUT0);

    build_tlut_object(
        host,
        TLUT_OBJECT_ADDRESS,
        CONSOLE_MEMORY_SIZE - 32U,
        GX_TL_RGB565,
        17U);
    PorpoiseStubGxObjectsReset();
    prepare_call(state, host);
    state->gpr[3] = TLUT_OBJECT_ADDRESS;
    state->gpr[4] = GX_TLUT0;
    porpoise_libporpoise_gx_load_tlut_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNMAPPED_ADDRESS,
        CONSOLE_MEMORY_SIZE - 32U);
    check_no_object_mutation();
}

static void test_channel_colors(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    const PorpoiseStubGxObjectsSnapshot *calls;
    uint8_t *color = (uint8_t *)decode_guest(host, COLOR_ADDRESS);

    color[0] = 0x11U;
    color[1] = 0x22U;
    color[2] = 0x33U;
    color[3] = 0x44U;

    PorpoiseStubGxObjectsReset();
    prepare_call(state, host);
    state->gpr[3] = GX_COLOR0A0;
    state->gpr[4] = COLOR_ADDRESS;
    porpoise_libporpoise_gx_set_chan_amb_color_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    calls = PorpoiseStubGxObjectsGet();
    CHECK(calls->ambient_color_count == 1U);
    CHECK(calls->material_color_count == 0U);
    CHECK(calls->color_channel == GX_COLOR0A0);
    CHECK(calls->color[0] == 0x11U);
    CHECK(calls->color[1] == 0x22U);
    CHECK(calls->color[2] == 0x33U);
    CHECK(calls->color[3] == 0x44U);

    PorpoiseStubGxObjectsReset();
    prepare_call(state, host);
    state->gpr[3] = GX_COLOR1A1;
    state->gpr[4] = COLOR_ADDRESS;
    porpoise_libporpoise_gx_set_chan_mat_color_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    calls = PorpoiseStubGxObjectsGet();
    CHECK(calls->ambient_color_count == 0U);
    CHECK(calls->material_color_count == 1U);
    CHECK(calls->color_channel == GX_COLOR1A1);

    PorpoiseStubGxObjectsReset();
    prepare_call(state, host);
    state->gpr[3] = GX_COLOR1A1 + 1U;
    state->gpr[4] = COLOR_ADDRESS;
    porpoise_libporpoise_gx_set_chan_amb_color_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    check_no_object_mutation();

    prepare_call(state, host);
    state->gpr[3] = UINT32_MAX;
    state->gpr[4] = COLOR_ADDRESS;
    porpoise_libporpoise_gx_set_chan_mat_color_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    check_no_object_mutation();

    prepare_call(state, host);
    state->gpr[3] = GX_COLOR0;
    state->gpr[4] = COLOR_ADDRESS + 1U;
    porpoise_libporpoise_gx_set_chan_mat_color_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        COLOR_ADDRESS + 1U);
    check_no_object_mutation();
}

int main(void)
{
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    memset(&host, 0, sizeof(host));
    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    test_requires_active_gx(&host, &state);
    initialize_native_gx(&host, &state);
    test_set_array(&host, &state);
    test_texture_load(&host, &state);
    test_texture_validation(&host, &state);
    test_tlut_load(&host, &state);
    test_channel_colors(&host, &state);
    porpoise_libporpoise_adapter_shutdown(&host);
    return 0;
}
