#include "porpoise_libporpoise_builtins_private.h"

#include <dolphin/gx.h>
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

#define TEST_PC UINT32_C(0x803C9000)
#define FIFO_ADDRESS UINT32_C(0x80010000)
#define FIFO_SIZE UINT32_C(0x00010000)
#define DATA_ADDRESS UINT32_C(0x80030000)
#define SAMPLE_ADDRESS (DATA_ADDRESS + UINT32_C(0x000))
#define FILTER_ADDRESS (DATA_ADDRESS + UINT32_C(0x040))
#define COLOR_ADDRESS (DATA_ADDRESS + UINT32_C(0x080))
#define LIGHT_ADDRESS (DATA_ADDRESS + UINT32_C(0x0C0))
#define DISPLAY_COPY_ADDRESS UINT32_C(0x80080000)
#define TEXTURE_COPY_ADDRESS UINT32_C(0x80180000)
#define MEMORY_END UINT32_C(0x81800000)
#define DISPLAY_OVERLAP_COPY_SIZE \
    (UINT32_C(640) * UINT32_C(4) * UINT32_C(2))
#define CALLBACK_A UINT32_C(0x80020100)
#define CALLBACK_B UINT32_C(0x80020200)
#define CALLBACK_UNKNOWN UINT32_C(0x80020300)

#if defined(LIBPORPOISE_GX_COPY_DISP_GUEST_ADDRESS_API_VERSION) && \
    LIBPORPOISE_GX_COPY_DISP_GUEST_ADDRESS_API_VERSION >= 1
#define TEST_HAS_GX_COPY_DISP_GUEST_ADDRESS 1
#else
#define TEST_HAS_GX_COPY_DISP_GUEST_ADDRESS 0
#endif

#if defined(LIBPORPOISE_GX_COPY_TEX_GUEST_ADDRESS_API_VERSION) && \
    LIBPORPOISE_GX_COPY_TEX_GUEST_ADDRESS_API_VERSION >= 1
#define TEST_HAS_GX_COPY_TEX_GUEST_ADDRESS 1
#else
#define TEST_HAS_GX_COPY_TEX_GUEST_ADDRESS 0
#endif

typedef struct CallbackRecord {
    uint32_t address;
    uint32_t original_r3;
    uint32_t delivery_depth;
} CallbackRecord;

static CallbackRecord callback_records[8];
static size_t callback_count;

static void store_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
}

static uint32_t float_bits(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
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

static void prepare_call(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    porpoise_state_init(state, host);
    state->pc = TEST_PC;
    state->msr |= PORPOISE_MSR_EE;
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

static int test_guest_dispatch(
    PorpoisePpcState *state,
    uint32_t guest_function_address)
{
    CallbackRecord *record;

    CHECK(state != NULL);
    CHECK(callback_count <
          sizeof(callback_records) / sizeof(callback_records[0]));
    record = &callback_records[callback_count++];
    record->address = guest_function_address;
    record->original_r3 = state->gpr[3];
    record->delivery_depth = state->host_event_delivery_depth;

    /* These mutations must remain confined to the cloned callback state. */
    state->gpr[3] = UINT32_C(0xDEADBEEF);
    state->lr = UINT32_C(0x13579BDF);
    return 1;
}

static void initialize_gx(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    PorpoiseStubGXInitReset();
    prepare_call(state, host);
    state->gpr[3] = FIFO_ADDRESS;
    state->gpr[4] = FIFO_SIZE;
    porpoise_libporpoise_gx_init_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(state->gpr[3] == PorpoiseStubTokenAddress());
    CHECK(PorpoiseStubGXInitCallCount() == 1U);
}

static void check_no_boundary_native_calls(void)
{
    CHECK(PorpoiseStubGXDrawDoneSetterCallCount() == 0U);
    CHECK(PorpoiseStubGXCopyFilterCallCount() == 0U);
    CHECK(PorpoiseStubGXCopyClearCallCount() == 0U);
    CHECK(PorpoiseStubGXSetDispCopyDstCallCount() == 0U);
    CHECK(PorpoiseStubGXSetTexCopyDstCallCount() == 0U);
    CHECK(PorpoiseStubGXCopyDispCallCount() == 0U);
    CHECK(PorpoiseStubGXCopyTexCallCount() == 0U);
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == 0U);
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() == 0U);
    CHECK(PorpoiseStubGXLoadLightCallCount() == 0U);
}

static void test_all_calls_require_active_gx(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    PorpoiseStubGXBoundaryReset();

    prepare_call(state, host);
    state->gpr[3] = 0U;
    porpoise_libporpoise_gx_set_draw_done_callback_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_STATE, TEST_PC);

    prepare_call(state, host);
    porpoise_libporpoise_gx_set_copy_filter_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_STATE, TEST_PC);

    prepare_call(state, host);
    state->gpr[3] = COLOR_ADDRESS;
    porpoise_libporpoise_gx_set_copy_clear_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_STATE, TEST_PC);

    prepare_call(state, host);
    state->gpr[3] = 16U;
    state->gpr[4] = 2U;
    porpoise_libporpoise_gx_set_disp_copy_dst_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_STATE, TEST_PC);

    prepare_call(state, host);
    state->gpr[3] = 8U;
    state->gpr[4] = 8U;
    porpoise_libporpoise_gx_set_tex_copy_dst_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_STATE, TEST_PC);

    prepare_call(state, host);
    state->gpr[3] = DISPLAY_COPY_ADDRESS;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_STATE, TEST_PC);

    prepare_call(state, host);
    state->gpr[3] = TEXTURE_COPY_ADDRESS;
    porpoise_libporpoise_gx_copy_tex_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_STATE, TEST_PC);

    prepare_call(state, host);
    state->gpr[3] = LIGHT_ADDRESS;
    state->gpr[4] = 1U;
    porpoise_libporpoise_gx_load_light_obj_imm_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_STATE, TEST_PC);

    check_no_boundary_native_calls();
}

static void test_draw_done_callback(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    unsigned int setters_before;

    callback_count = 0U;
    memset(callback_records, 0, sizeof(callback_records));
    PorpoiseStubDispatchReset();

    prepare_call(state, host);
    state->gpr[3] = CALLBACK_A;
    porpoise_libporpoise_gx_set_draw_done_callback_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_MISSING_HOST_CALLBACK,
        CALLBACK_A);
    CHECK(state->gpr[3] == CALLBACK_A);
    CHECK(PorpoiseStubGXDrawDoneSetterCallCount() == 0U);

    CHECK(PorpoiseStubDispatchAddAddress(CALLBACK_A));
    CHECK(PorpoiseStubDispatchAddAddress(CALLBACK_B));
    CHECK(porpoise_libporpoise_bind_guest_dispatch(
              host, test_guest_dispatch) == PORPOISE_HOST_OK);

    prepare_call(state, host);
    state->gpr[3] = CALLBACK_UNKNOWN;
    porpoise_libporpoise_gx_set_draw_done_callback_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        CALLBACK_UNKNOWN);
    CHECK(state->gpr[3] == CALLBACK_UNKNOWN);

    prepare_call(state, host);
    state->gpr[3] = CALLBACK_A + UINT32_C(2);
    porpoise_libporpoise_gx_set_draw_done_callback_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        CALLBACK_A + UINT32_C(2));
    CHECK(state->gpr[3] == CALLBACK_A + UINT32_C(2));

    prepare_call(state, host);
    state->gpr[3] = CALLBACK_A;
    porpoise_libporpoise_gx_set_draw_done_callback_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(state->gpr[3] == 0U);
    CHECK(PorpoiseStubGXDrawDoneSetterCallCount() == 1U);

    PorpoiseStubGXTriggerDrawDone();
    CHECK(callback_count == 0U);
    prepare_call(state, host);
    state->gpr[3] = UINT32_C(0xA5A5A5A5);
    state->msr &= ~PORPOISE_MSR_EE;
    CHECK(porpoise_poll_host_events(state, TEST_PC));
    CHECK(callback_count == 0U);
    state->msr |= PORPOISE_MSR_EE;
    CHECK(porpoise_poll_host_events(state, TEST_PC));
    CHECK(callback_count == 1U);
    CHECK(callback_records[0].address == CALLBACK_A);
    CHECK(callback_records[0].original_r3 == UINT32_C(0xA5A5A5A5));
    CHECK(callback_records[0].delivery_depth == 2U);
    CHECK(state->gpr[3] == UINT32_C(0xA5A5A5A5));
    CHECK(state->lr == 0U);

    /* The native trampoline snapshots the guest callback at signal time. */
    PorpoiseStubGXTriggerDrawDone();
    prepare_call(state, host);
    state->gpr[3] = CALLBACK_B;
    porpoise_libporpoise_gx_set_draw_done_callback_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(state->gpr[3] == CALLBACK_A);
    CHECK(callback_count == 1U);

    prepare_call(state, host);
    state->gpr[3] = UINT32_C(0x11223344);
    CHECK(porpoise_poll_host_events(state, TEST_PC));
    CHECK(callback_count == 2U);
    CHECK(callback_records[1].address == CALLBACK_A);

    prepare_call(state, host);
    state->gpr[3] = 0U;
    porpoise_libporpoise_gx_set_draw_done_callback_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(state->gpr[3] == CALLBACK_B);

    /* A foreign native mutation is rolled back and never changes the mirror. */
    PorpoiseStubGXSetForeignDrawDoneCallback(1);
    setters_before = PorpoiseStubGXDrawDoneSetterCallCount();
    prepare_call(state, host);
    state->gpr[3] = CALLBACK_A;
    porpoise_libporpoise_gx_set_draw_done_callback_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_STATE, CALLBACK_A);
    CHECK(state->gpr[3] == CALLBACK_A);
    CHECK(PorpoiseStubGXDrawDoneSetterCallCount() == setters_before + 2U);
    PorpoiseStubGXSetForeignDrawDoneCallback(0);
}

static void test_copy_filter(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    uint8_t samples[24];
    uint8_t vertical[7];
    unsigned int calls_before;
    size_t index;

    for (index = 0U; index < sizeof(samples); index++) {
        samples[index] = (uint8_t)(index + 1U);
    }
    for (index = 0U; index < sizeof(vertical); index++) {
        vertical[index] = (uint8_t)(0x40U + index);
    }
    write_guest(host, SAMPLE_ADDRESS, samples, sizeof(samples));
    write_guest(host, FILTER_ADDRESS, vertical, sizeof(vertical));

    prepare_call(state, host);
    state->gpr[3] = 1U;
    state->gpr[4] = SAMPLE_ADDRESS;
    state->gpr[5] = 1U;
    state->gpr[6] = FILTER_ADDRESS;
    porpoise_libporpoise_gx_set_copy_filter_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyFilterCallCount() == 1U);
    CHECK(PorpoiseStubGXCopyFilterUseAA() == 1U);
    CHECK(PorpoiseStubGXCopyFilterUseVertical() == 1U);
    for (index = 0U; index < sizeof(samples); index++) {
        CHECK(PorpoiseStubGXCopyFilterSample((unsigned int)index) ==
              samples[index]);
    }
    for (index = 0U; index < sizeof(vertical); index++) {
        CHECK(PorpoiseStubGXCopyFilterVertical((unsigned int)index) ==
              vertical[index]);
    }

    /* Disabled arrays are nullable and are not decoded speculatively. */
    prepare_call(state, host);
    state->gpr[3] = 0U;
    state->gpr[4] = UINT32_C(0xCC000000);
    state->gpr[5] = 0U;
    state->gpr[6] = PorpoiseStubTokenAddress();
    porpoise_libporpoise_gx_set_copy_filter_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyFilterCallCount() == 2U);
    CHECK(PorpoiseStubGXCopyFilterUseAA() == 0U);
    CHECK(PorpoiseStubGXCopyFilterUseVertical() == 0U);

    calls_before = PorpoiseStubGXCopyFilterCallCount();
    prepare_call(state, host);
    state->gpr[3] = 2U;
    porpoise_libporpoise_gx_set_copy_filter_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_ARGUMENT, 2U);
    CHECK(PorpoiseStubGXCopyFilterCallCount() == calls_before);

    prepare_call(state, host);
    state->gpr[3] = 1U;
    state->gpr[4] = 0U;
    porpoise_libporpoise_gx_set_copy_filter_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_POINTER, 0U);
    CHECK(PorpoiseStubGXCopyFilterCallCount() == calls_before);

    prepare_call(state, host);
    state->gpr[3] = 1U;
    state->gpr[4] = MEMORY_END - UINT32_C(16);
    porpoise_libporpoise_gx_set_copy_filter_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNMAPPED_ADDRESS,
        MEMORY_END - UINT32_C(16));
    CHECK(PorpoiseStubGXCopyFilterCallCount() == calls_before);
}

static void test_copy_clear(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    static const uint8_t color[4] = {0x12U, 0x34U, 0x56U, 0x78U};
    unsigned int calls_before;

    write_guest(host, COLOR_ADDRESS, color, sizeof(color));
    prepare_call(state, host);
    state->gpr[3] = COLOR_ADDRESS;
    state->gpr[4] = UINT32_C(0x00ABCDEF);
    porpoise_libporpoise_gx_set_copy_clear_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyClearCallCount() == 1U);
    CHECK(PorpoiseStubGXCopyClearColor() == UINT32_C(0x12345678));
    CHECK(PorpoiseStubGXCopyClearDepth() == UINT32_C(0x00ABCDEF));

    calls_before = PorpoiseStubGXCopyClearCallCount();
    prepare_call(state, host);
    state->gpr[3] = COLOR_ADDRESS;
    state->gpr[4] = UINT32_C(0x01000000);
    porpoise_libporpoise_gx_set_copy_clear_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_ARGUMENT, UINT32_C(0x01000000));
    CHECK(PorpoiseStubGXCopyClearCallCount() == calls_before);

    prepare_call(state, host);
    state->gpr[3] = COLOR_ADDRESS + UINT32_C(1);
    porpoise_libporpoise_gx_set_copy_clear_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        COLOR_ADDRESS + UINT32_C(1));
    CHECK(PorpoiseStubGXCopyClearCallCount() == calls_before);
}

static void set_display_destination(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state,
    uint32_t width,
    uint32_t height)
{
    prepare_call(state, host);
    state->gpr[3] = width;
    state->gpr[4] = height;
    porpoise_libporpoise_gx_set_disp_copy_dst_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
}

static void set_texture_destination(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t mipmap)
{
    prepare_call(state, host);
    state->gpr[3] = width;
    state->gpr[4] = height;
    state->gpr[5] = format;
    state->gpr[6] = mipmap;
    porpoise_libporpoise_gx_set_tex_copy_dst_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
}

static void check_texture_copy_exact_span(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state,
    uint32_t format,
    uint32_t expected_size)
{
    uint32_t exact_address;
    uint32_t short_address;
    unsigned int calls_before;

    CHECK(expected_size > UINT32_C(32));
    CHECK((expected_size & UINT32_C(31)) == 0U);
    exact_address = MEMORY_END - expected_size;
    short_address = exact_address + UINT32_C(32);

    set_texture_destination(host, state, 9U, 5U, format, 0U);
    calls_before = PorpoiseStubGXCopyTexGuestAddressCallCount();

    prepare_call(state, host);
    state->gpr[3] = exact_address;
    porpoise_libporpoise_gx_copy_tex_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() == calls_before + 1U);
    CHECK(PorpoiseStubGXCopyTexGuestAddress() == exact_address);
    CHECK(PorpoiseStubGXCopyTexCallCount() == 0U);

    prepare_call(state, host);
    state->gpr[3] = short_address;
    porpoise_libporpoise_gx_copy_tex_adapter(state);
    check_fault(state, PORPOISE_FAULT_UNMAPPED_ADDRESS, short_address);
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() == calls_before + 1U);
}

typedef struct TextureCopyFormatMapping {
    uint32_t guest_format;
    uint32_t native_format;
} TextureCopyFormatMapping;

static void test_texture_copy_format_mapping(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    static const TextureCopyFormatMapping mappings[] = {
        {UINT32_C(0x00), (uint32_t)GX_TF_I4},
        {UINT32_C(0x01), (uint32_t)GX_TF_I8},
        {UINT32_C(0x02), (uint32_t)GX_TF_IA4},
        {UINT32_C(0x03), (uint32_t)GX_TF_IA8},
        {UINT32_C(0x04), (uint32_t)GX_TF_RGB565},
        {UINT32_C(0x05), (uint32_t)GX_TF_RGB5A3},
        {UINT32_C(0x06), (uint32_t)GX_TF_RGBA8},
        {UINT32_C(0x08), (uint32_t)GX_TF_C4},
        {UINT32_C(0x09), (uint32_t)GX_TF_C8},
        {UINT32_C(0x0A), (uint32_t)GX_TF_C14X2},
        {UINT32_C(0x0E), (uint32_t)GX_TF_CMPR},
        {UINT32_C(0x11), (uint32_t)GX_TF_Z8},
        {UINT32_C(0x13), (uint32_t)GX_TF_Z16},
        {UINT32_C(0x16), (uint32_t)GX_TF_Z24X8},
        {UINT32_C(0x20), (uint32_t)GX_CTF_R4},
        {UINT32_C(0x22), (uint32_t)GX_CTF_RA4},
        {UINT32_C(0x23), (uint32_t)GX_CTF_RA8},
        {UINT32_C(0x26), (uint32_t)GX_CTF_YUVA8},
        {UINT32_C(0x27), (uint32_t)GX_CTF_A8},
        {UINT32_C(0x28), (uint32_t)GX_CTF_R8},
        {UINT32_C(0x29), (uint32_t)GX_CTF_G8},
        {UINT32_C(0x2A), (uint32_t)GX_CTF_B8},
        {UINT32_C(0x2B), (uint32_t)GX_CTF_RG8},
        {UINT32_C(0x2C), (uint32_t)GX_CTF_GB8},
        {UINT32_C(0x30), (uint32_t)GX_CTF_Z4},
        {UINT32_C(0x39), (uint32_t)GX_CTF_Z8M},
        {UINT32_C(0x3A), (uint32_t)GX_CTF_Z8L},
        {UINT32_C(0x3C), (uint32_t)GX_CTF_Z16L}
    };
    unsigned int calls_before = PorpoiseStubGXSetTexCopyDstCallCount();
    size_t index;

    for (index = 0U; index < sizeof(mappings) / sizeof(mappings[0]); index++) {
        set_texture_destination(
            host,
            state,
            8U,
            8U,
            mappings[index].guest_format,
            0U);
        CHECK(PorpoiseStubGXSetTexCopyDstCallCount() ==
              calls_before + (unsigned int)index + 1U);
        CHECK(PorpoiseStubGXTexCopyFormat() ==
              mappings[index].native_format);
    }
}

#if defined(PORPOISE_STUB_SHIFTED_GX_COPY_FORMATS)
static void test_shifted_texture_copy_format_mapping(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    static const TextureCopyFormatMapping mappings[] = {
        {UINT32_C(0x27), (uint32_t)GX_CTF_A8},
        {UINT32_C(0x28), (uint32_t)GX_CTF_R8},
        {UINT32_C(0x29), (uint32_t)GX_CTF_G8},
        {UINT32_C(0x2A), (uint32_t)GX_CTF_B8},
        {UINT32_C(0x2B), (uint32_t)GX_CTF_RG8},
        {UINT32_C(0x2C), (uint32_t)GX_CTF_GB8}
    };
    unsigned int calls_before;
    size_t index;

    CHECK((uint32_t)GX_CTF_YUVA8 == UINT32_C(0x26));
    CHECK((uint32_t)GX_CTF_A8 == UINT32_C(0x26));
    CHECK((uint32_t)GX_CTF_R8 == UINT32_C(0x27));
    CHECK((uint32_t)GX_CTF_G8 == UINT32_C(0x28));
    CHECK((uint32_t)GX_CTF_B8 == UINT32_C(0x29));
    CHECK((uint32_t)GX_CTF_RG8 == UINT32_C(0x2A));
    CHECK((uint32_t)GX_CTF_GB8 == UINT32_C(0x2B));

    calls_before = PorpoiseStubGXSetTexCopyDstCallCount();
    for (index = 0U; index < sizeof(mappings) / sizeof(mappings[0]); index++) {
        set_texture_destination(
            host,
            state,
            8U,
            8U,
            mappings[index].guest_format,
            0U);
        CHECK(PorpoiseStubGXSetTexCopyDstCallCount() ==
              calls_before + (unsigned int)index + 1U);
        CHECK(PorpoiseStubGXTexCopyFormat() ==
              mappings[index].native_format);
    }

    calls_before = PorpoiseStubGXSetTexCopyDstCallCount();
    prepare_call(state, host);
    state->gpr[3] = 8U;
    state->gpr[4] = 8U;
    state->gpr[5] = UINT32_C(0x26);
    state->gpr[6] = 0U;
    porpoise_libporpoise_gx_set_tex_copy_dst_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        UINT32_C(0x26));
    CHECK(PorpoiseStubGXSetTexCopyDstCallCount() == calls_before);

    /* Guest RG8 is 0x2B and remains 4x4/32 for span accounting even
     * though this native header names it with the shifted value 0x2A. */
    check_texture_copy_exact_span(
        host, state, UINT32_C(0x2B), UINT32_C(192));
}
#endif

static void test_copy_destinations_and_spans(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    unsigned int calls_before;

    prepare_call(state, host);
    state->gpr[3] = DISPLAY_COPY_ADDRESS;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == 1U);
    CHECK(PorpoiseStubGXCopyDispGuestAddress() == DISPLAY_COPY_ADDRESS);

    prepare_call(state, host);
    state->gpr[3] = TEXTURE_COPY_ADDRESS;
    porpoise_libporpoise_gx_copy_tex_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_STATE, TEST_PC);
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() == 0U);

    prepare_call(state, host);
    state->gpr[3] = 15U;
    state->gpr[4] = 480U;
    porpoise_libporpoise_gx_set_disp_copy_dst_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_ARGUMENT, 15U);
    CHECK(PorpoiseStubGXSetDispCopyDstCallCount() == 0U);

    set_display_destination(host, state, 640U, 480U);
    CHECK(PorpoiseStubGXSetDispCopyDstCallCount() == 1U);
    CHECK(PorpoiseStubGXDispCopyWidth() == 640U);
    CHECK(PorpoiseStubGXDispCopyHeight() == 480U);
    calls_before = PorpoiseStubGXCopyDispGuestAddressCallCount();
    prepare_call(state, host);
    state->gpr[3] = DISPLAY_COPY_ADDRESS;
    state->gpr[4] = 1U;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == calls_before + 1U);
    CHECK(PorpoiseStubGXCopyDispGuestAddress() == DISPLAY_COPY_ADDRESS);
    CHECK(PorpoiseStubGXCopyDispGuestAddressClearFlag() == 1U);
    CHECK(PorpoiseStubGXCopyDispCallCount() == 0U);

    calls_before = PorpoiseStubGXCopyDispGuestAddressCallCount();
    prepare_call(state, host);
    state->gpr[3] = DISPLAY_COPY_ADDRESS + UINT32_C(4);
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        DISPLAY_COPY_ADDRESS + UINT32_C(4));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == calls_before);

    prepare_call(state, host);
    state->gpr[3] = PorpoiseStubTokenAddress();
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_INVALID_POINTER,
        PorpoiseStubTokenAddress());
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == calls_before);

    prepare_call(state, host);
    state->gpr[3] = UINT32_C(0xCC000000);
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_MMIO,
        UINT32_C(0xCC000000));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == calls_before);

    /* KAR's AA overlap path retains a 640x480 destination configuration but
     * changes the native source height to four lines. Model the native span
     * independently: Tool must not reject the exact-end address using stale
     * destination height, while the endpoint must reject one 32-byte block
     * short after deriving the complete native-state span. */
    CHECK(PorpoiseStubGXDispCopyWidth() == 640U);
    CHECK(PorpoiseStubGXDispCopyHeight() == 480U);
    PorpoiseStubGXSetGuestAddressDisplayCopySpan(
        DISPLAY_OVERLAP_COPY_SIZE);
    calls_before = PorpoiseStubGXCopyDispGuestAddressCallCount();
    prepare_call(state, host);
    state->gpr[3] = MEMORY_END - DISPLAY_OVERLAP_COPY_SIZE;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == calls_before + 1U);
    CHECK(PorpoiseStubGXCopyDispGuestAddress() ==
          MEMORY_END - DISPLAY_OVERLAP_COPY_SIZE);
    CHECK(PorpoiseStubGXCopyDispCallCount() == 0U);

    prepare_call(state, host);
    state->gpr[3] =
        MEMORY_END - (DISPLAY_OVERLAP_COPY_SIZE - UINT32_C(32));
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_HOST_IO,
        MEMORY_END - (DISPLAY_OVERLAP_COPY_SIZE - UINT32_C(32)));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == calls_before + 2U);
    PorpoiseStubGXSetGuestAddressDisplayCopySpan(0U);

    prepare_call(state, host);
    state->gpr[3] = 8U;
    state->gpr[4] = 8U;
    state->gpr[5] = UINT32_C(0xDE);
    porpoise_libporpoise_gx_set_tex_copy_dst_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_ARGUMENT, UINT32_C(0xDE));
    CHECK(PorpoiseStubGXSetTexCopyDstCallCount() == 0U);

    prepare_call(state, host);
    state->gpr[3] = 8U;
    state->gpr[4] = 8U;
    state->gpr[5] = 0U;
    state->gpr[6] = 2U;
    porpoise_libporpoise_gx_set_tex_copy_dst_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_ARGUMENT, 2U);
    CHECK(PorpoiseStubGXSetTexCopyDstCallCount() == 0U);

    /* CMPR is an 8x8, 32-byte tile: 9x9 requires exactly four tiles. */
    set_texture_destination(host, state, 9U, 9U, UINT32_C(0x0E), 0U);
    CHECK(PorpoiseStubGXSetTexCopyDstCallCount() == 1U);
    CHECK(PorpoiseStubGXTexCopyFormat() == UINT32_C(0x0E));
    calls_before = PorpoiseStubGXCopyTexGuestAddressCallCount();
    prepare_call(state, host);
    state->gpr[3] = MEMORY_END - UINT32_C(96);
    porpoise_libporpoise_gx_copy_tex_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNMAPPED_ADDRESS,
        MEMORY_END - UINT32_C(96));
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() == calls_before);

    prepare_call(state, host);
    state->gpr[3] = TEXTURE_COPY_ADDRESS;
    state->gpr[4] = 0U;
    porpoise_libporpoise_gx_copy_tex_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() == calls_before + 1U);
    CHECK(PorpoiseStubGXCopyTexGuestAddress() == TEXTURE_COPY_ADDRESS);
    CHECK(PorpoiseStubGXCopyTexGuestAddressClearFlag() == 0U);
    CHECK(PorpoiseStubGXCopyTexCallCount() == 0U);

    /* These exact-end and one-block-short checks pin the guest copy ABI:
     * YUVA8 is 4x4/64, B8 is 8x4/32, and GB8 is 4x4/32. */
    check_texture_copy_exact_span(
        host, state, UINT32_C(0x26), UINT32_C(384));
    check_texture_copy_exact_span(
        host, state, UINT32_C(0x2A), UINT32_C(128));
    check_texture_copy_exact_span(
        host, state, UINT32_C(0x2C), UINT32_C(192));
}

static void test_light_object(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    static const float values[12] = {
        1.0f, -2.0f, 3.5f,
        4.0f, 5.25f, -6.0f,
        7.0f, -8.5f, 9.0f,
        10.0f, 11.5f, -12.0f
    };
    uint8_t guest[0x40];
    GXLightObjPriv expected;
    const uint8_t *expected_bytes = (const uint8_t *)(const void *)&expected;
    unsigned int calls_before;
    size_t index;

    CHECK(sizeof(expected) == sizeof(guest));
    memset(guest, 0, sizeof(guest));
    store_be32(&guest[0x00], UINT32_C(0x01234567));
    store_be32(&guest[0x04], UINT32_C(0x89ABCDEF));
    store_be32(&guest[0x08], UINT32_C(0x10203040));
    guest[0x0C] = 0x11U;
    guest[0x0D] = 0x22U;
    guest[0x0E] = 0x33U;
    guest[0x0F] = 0x44U;
    for (index = 0U; index < 12U; index++) {
        store_be32(&guest[0x10U + index * 4U], float_bits(values[index]));
    }
    write_guest(host, LIGHT_ADDRESS, guest, sizeof(guest));

    memset(&expected, 0, sizeof(expected));
    expected.reserved[0] = UINT32_C(0x01234567);
    expected.reserved[1] = UINT32_C(0x89ABCDEF);
    expected.reserved[2] = UINT32_C(0x10203040);
    expected.color.r = 0x11U;
    expected.color.g = 0x22U;
    expected.color.b = 0x33U;
    expected.color.a = 0x44U;
    for (index = 0U; index < 3U; index++) {
        expected.a[index] = values[index];
        expected.k[index] = values[3U + index];
        expected.lpos[index] = values[6U + index];
        expected.ldir[index] = values[9U + index];
    }

    prepare_call(state, host);
    state->gpr[3] = LIGHT_ADDRESS;
    state->gpr[4] = 4U;
    porpoise_libporpoise_gx_load_light_obj_imm_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXLoadLightCallCount() == 1U);
    CHECK(PorpoiseStubGXLoadLightId() == 4U);
    for (index = 0U; index < sizeof(expected); index++) {
        CHECK(PorpoiseStubGXLoadLightByte((unsigned int)index) ==
              expected_bytes[index]);
    }

    calls_before = PorpoiseStubGXLoadLightCallCount();
    prepare_call(state, host);
    state->gpr[3] = LIGHT_ADDRESS;
    state->gpr[4] = 3U;
    porpoise_libporpoise_gx_load_light_obj_imm_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_ARGUMENT, 3U);
    CHECK(PorpoiseStubGXLoadLightCallCount() == calls_before);

    prepare_call(state, host);
    state->gpr[3] = LIGHT_ADDRESS + UINT32_C(2);
    state->gpr[4] = 1U;
    porpoise_libporpoise_gx_load_light_obj_imm_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        LIGHT_ADDRESS + UINT32_C(2));
    CHECK(PorpoiseStubGXLoadLightCallCount() == calls_before);
}

#if !TEST_HAS_GX_COPY_DISP_GUEST_ADDRESS && \
    !TEST_HAS_GX_COPY_TEX_GUEST_ADDRESS
static void test_missing_copy_contracts(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    set_display_destination(host, state, 16U, 2U);
    prepare_call(state, host);
    state->gpr[3] = DISPLAY_COPY_ADDRESS;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        DISPLAY_COPY_ADDRESS);
    CHECK(PorpoiseStubGXCopyDispCallCount() == 0U);
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == 0U);

    set_texture_destination(host, state, 8U, 8U, 0U, 0U);
    prepare_call(state, host);
    state->gpr[3] = TEXTURE_COPY_ADDRESS;
    porpoise_libporpoise_gx_copy_tex_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        TEXTURE_COPY_ADDRESS);
    CHECK(PorpoiseStubGXCopyTexCallCount() == 0U);
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() == 0U);
}
#endif

#if !TEST_HAS_GX_COPY_DISP_GUEST_ADDRESS && \
    TEST_HAS_GX_COPY_TEX_GUEST_ADDRESS
static void test_missing_display_copy_contract(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    set_display_destination(host, state, 16U, 2U);
    set_texture_destination(host, state, 8U, 8U, 0U, 0U);

    prepare_call(state, host);
    state->gpr[3] = DISPLAY_COPY_ADDRESS;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        DISPLAY_COPY_ADDRESS);
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == 0U);
    CHECK(PorpoiseStubGXCopyDispCallCount() == 0U);

    prepare_call(state, host);
    state->gpr[3] = TEXTURE_COPY_ADDRESS;
    state->gpr[4] = 1U;
    porpoise_libporpoise_gx_copy_tex_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() == 1U);
    CHECK(PorpoiseStubGXCopyTexGuestAddress() == TEXTURE_COPY_ADDRESS);
    CHECK(PorpoiseStubGXCopyTexGuestAddressClearFlag() == 1U);
    CHECK(PorpoiseStubGXCopyTexCallCount() == 0U);
}
#endif

#if TEST_HAS_GX_COPY_DISP_GUEST_ADDRESS && \
    !TEST_HAS_GX_COPY_TEX_GUEST_ADDRESS
static void test_missing_texture_copy_contract(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    set_display_destination(host, state, 16U, 2U);
    set_texture_destination(host, state, 8U, 8U, 0U, 0U);

    prepare_call(state, host);
    state->gpr[3] = DISPLAY_COPY_ADDRESS;
    state->gpr[4] = 1U;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == 1U);
    CHECK(PorpoiseStubGXCopyDispGuestAddress() == DISPLAY_COPY_ADDRESS);
    CHECK(PorpoiseStubGXCopyDispGuestAddressClearFlag() == 1U);
    CHECK(PorpoiseStubGXCopyDispCallCount() == 0U);

    prepare_call(state, host);
    state->gpr[3] = TEXTURE_COPY_ADDRESS;
    porpoise_libporpoise_gx_copy_tex_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        TEXTURE_COPY_ADDRESS);
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() == 0U);
    CHECK(PorpoiseStubGXCopyTexCallCount() == 0U);
}
#endif

#if TEST_HAS_GX_COPY_DISP_GUEST_ADDRESS && \
    TEST_HAS_GX_COPY_TEX_GUEST_ADDRESS
static void test_guest_address_copy_contract(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    unsigned int display_calls;
    unsigned int texture_calls;

    CHECK(PorpoiseStubGXCopyDispCallCount() == 0U);
    CHECK(PorpoiseStubGXCopyTexCallCount() == 0U);
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == 0U);
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() == 0U);

    /* Display geometry is native endpoint state, so absence of Tool's private
     * destination mirror cannot block a valid guest-address request. */
    prepare_call(state, host);
    state->gpr[3] = DISPLAY_COPY_ADDRESS;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == 1U);
    CHECK(PorpoiseStubGXCopyDispGuestAddress() == DISPLAY_COPY_ADDRESS);

    prepare_call(state, host);
    state->gpr[3] = TEXTURE_COPY_ADDRESS;
    porpoise_libporpoise_gx_copy_tex_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_STATE, TEST_PC);
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() == 0U);

    set_display_destination(host, state, 16U, 2U);
    set_texture_destination(host, state, 8U, 8U, UINT32_C(0x00), 0U);

    prepare_call(state, host);
    state->gpr[3] = DISPLAY_COPY_ADDRESS;
    state->gpr[4] = 2U;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    check_fault(state, PORPOISE_FAULT_INVALID_ARGUMENT, 2U);
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == 1U);

    prepare_call(state, host);
    state->gpr[3] = DISPLAY_COPY_ADDRESS + UINT32_C(4);
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        DISPLAY_COPY_ADDRESS + UINT32_C(4));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == 1U);

    prepare_call(state, host);
    state->gpr[3] = UINT32_C(0xCC000000);
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_MMIO,
        UINT32_C(0xCC000000));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == 1U);

    prepare_call(state, host);
    state->gpr[3] = MEMORY_END;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    check_fault(state, PORPOISE_FAULT_UNMAPPED_ADDRESS, MEMORY_END);
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == 1U);

    prepare_call(state, host);
    state->gpr[3] = MEMORY_END;
    porpoise_libporpoise_gx_copy_tex_adapter(state);
    check_fault(state, PORPOISE_FAULT_UNMAPPED_ADDRESS, MEMORY_END);
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() == 0U);

    /* Tool probes only the mapped origin. The native endpoint accepts the
     * exact-end span and rejects the aligned address that is one block short. */
    PorpoiseStubGXSetGuestAddressDisplayCopySpan(UINT32_C(64));
    display_calls = PorpoiseStubGXCopyDispGuestAddressCallCount();
    prepare_call(state, host);
    state->gpr[3] = MEMORY_END - UINT32_C(64);
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() ==
          display_calls + 1U);

    prepare_call(state, host);
    state->gpr[3] = MEMORY_END - UINT32_C(32);
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    check_fault(
        state,
        PORPOISE_FAULT_HOST_IO,
        MEMORY_END - UINT32_C(32));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() ==
          display_calls + 2U);
    PorpoiseStubGXSetGuestAddressDisplayCopySpan(0U);

    /* When both contracts are visible, the lossless guest-address endpoint
     * wins and receives the original u32 verbatim. */
    display_calls = PorpoiseStubGXCopyDispGuestAddressCallCount();
    prepare_call(state, host);
    state->gpr[3] = DISPLAY_COPY_ADDRESS;
    state->gpr[4] = 1U;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() ==
          display_calls + 1U);
    CHECK(PorpoiseStubGXCopyDispGuestAddress() == DISPLAY_COPY_ADDRESS);
    CHECK(PorpoiseStubGXCopyDispGuestAddressClearFlag() == 1U);
    CHECK(PorpoiseStubGXCopyDispCallCount() == 0U);

    /* Split-frame paths pass aligned subspans within one XFB. Preserve that
     * adjusted guest address rather than rebasing it to the decoded mapping. */
    prepare_call(state, host);
    state->gpr[3] = DISPLAY_COPY_ADDRESS + UINT32_C(32);
    state->gpr[4] = 0U;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() ==
          display_calls + 2U);
    CHECK(PorpoiseStubGXCopyDispGuestAddress() ==
          DISPLAY_COPY_ADDRESS + UINT32_C(32));
    CHECK(PorpoiseStubGXCopyDispGuestAddressClearFlag() == 0U);
    CHECK(PorpoiseStubGXCopyDispCallCount() == 0U);

    prepare_call(state, host);
    state->gpr[3] = TEXTURE_COPY_ADDRESS;
    state->gpr[4] = 0U;
    porpoise_libporpoise_gx_copy_tex_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() == 1U);
    CHECK(PorpoiseStubGXCopyTexGuestAddress() == TEXTURE_COPY_ADDRESS);
    CHECK(PorpoiseStubGXCopyTexGuestAddressClearFlag() == 0U);
    CHECK(PorpoiseStubGXCopyTexCallCount() == 0U);

    /* A false native result becomes the first sticky execution fault. Once
     * faulted, re-entry cannot call the endpoint or replace fault metadata. */
    PorpoiseStubGXSetGuestAddressCopyResults(0, 1);
    display_calls = PorpoiseStubGXCopyDispGuestAddressCallCount();
    prepare_call(state, host);
    state->gpr[3] = DISPLAY_COPY_ADDRESS;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    check_fault(state, PORPOISE_FAULT_HOST_IO, DISPLAY_COPY_ADDRESS);
    CHECK(strcmp(
              porpoise_state_fault_message(state),
              "libPorpoise rejected the GXCopyDisp guest-address request") ==
          0);
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() ==
          display_calls + 1U);
    state->gpr[3] = DISPLAY_COPY_ADDRESS + UINT32_C(32);
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    check_fault(state, PORPOISE_FAULT_HOST_IO, DISPLAY_COPY_ADDRESS);
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() ==
          display_calls + 1U);

    PorpoiseStubGXSetGuestAddressCopyResults(1, 0);
    texture_calls = PorpoiseStubGXCopyTexGuestAddressCallCount();
    prepare_call(state, host);
    state->gpr[3] = TEXTURE_COPY_ADDRESS;
    porpoise_libporpoise_gx_copy_tex_adapter(state);
    check_fault(state, PORPOISE_FAULT_HOST_IO, TEXTURE_COPY_ADDRESS);
    CHECK(strcmp(
              porpoise_state_fault_message(state),
              "libPorpoise rejected the GXCopyTex guest-address request") ==
          0);
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() ==
          texture_calls + 1U);
    state->gpr[3] = TEXTURE_COPY_ADDRESS + UINT32_C(32);
    porpoise_libporpoise_gx_copy_tex_adapter(state);
    check_fault(state, PORPOISE_FAULT_HOST_IO, TEXTURE_COPY_ADDRESS);
    CHECK(PorpoiseStubGXCopyTexGuestAddressCallCount() ==
          texture_calls + 1U);
}
#endif

static void test_draw_done_queue_overflow(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    unsigned int index;

    prepare_call(state, host);
    state->gpr[3] = CALLBACK_A;
    porpoise_libporpoise_gx_set_draw_done_callback_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(state->gpr[3] == 0U);

    for (index = 0U; index < 65U; index++) {
        PorpoiseStubGXTriggerDrawDone();
    }
    prepare_call(state, host);
    CHECK(!porpoise_poll_host_events(state, TEST_PC));
    check_fault(state, PORPOISE_FAULT_HOST_IO, CALLBACK_A);
}

int main(int argc, char **argv)
{
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    memset(&host, 0, sizeof(host));
    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    test_all_calls_require_active_gx(&host, &state);
    initialize_gx(&host, &state);
    PorpoiseStubGXBoundaryReset();

#if !TEST_HAS_GX_COPY_DISP_GUEST_ADDRESS && \
    !TEST_HAS_GX_COPY_TEX_GUEST_ADDRESS
    if (argc == 2 && strcmp(argv[1], "no-copy-contract") == 0) {
        test_missing_copy_contracts(&host, &state);
        porpoise_libporpoise_adapter_shutdown(&host);
        return 0;
    }
#endif
#if !TEST_HAS_GX_COPY_DISP_GUEST_ADDRESS && \
    TEST_HAS_GX_COPY_TEX_GUEST_ADDRESS
    if (argc == 2 && strcmp(argv[1], "missing-display-contract") == 0) {
        test_missing_display_copy_contract(&host, &state);
        porpoise_libporpoise_adapter_shutdown(&host);
        return 0;
    }
#endif
#if TEST_HAS_GX_COPY_DISP_GUEST_ADDRESS && \
    !TEST_HAS_GX_COPY_TEX_GUEST_ADDRESS
    if (argc == 2 && strcmp(argv[1], "missing-texture-contract") == 0) {
        test_missing_texture_copy_contract(&host, &state);
        porpoise_libporpoise_adapter_shutdown(&host);
        return 0;
    }
#endif
#if TEST_HAS_GX_COPY_DISP_GUEST_ADDRESS && \
    TEST_HAS_GX_COPY_TEX_GUEST_ADDRESS
    if (argc == 2 && strcmp(argv[1], "guest-address-contract") == 0) {
        test_guest_address_copy_contract(&host, &state);
        porpoise_libporpoise_adapter_shutdown(&host);
        return 0;
    }
#endif
#if defined(PORPOISE_STUB_SHIFTED_GX_COPY_FORMATS)
    if (argc == 2 && strcmp(argv[1], "shifted-formats") == 0) {
        test_shifted_texture_copy_format_mapping(&host, &state);
        porpoise_libporpoise_adapter_shutdown(&host);
        return 0;
    }
#endif
    if (argc != 1) {
        (void)fprintf(stderr, "unknown GX boundary test mode\n");
        porpoise_libporpoise_adapter_shutdown(&host);
        return 2;
    }

    test_draw_done_callback(&host, &state);
    test_copy_filter(&host, &state);
    test_copy_clear(&host, &state);
    test_copy_destinations_and_spans(&host, &state);
    test_texture_copy_format_mapping(&host, &state);
    test_light_object(&host, &state);
    test_draw_done_queue_overflow(&host, &state);
    porpoise_libporpoise_adapter_shutdown(&host);
    return 0;
}
