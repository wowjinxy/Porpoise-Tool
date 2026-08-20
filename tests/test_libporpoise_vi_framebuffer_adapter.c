#include "porpoise_libporpoise_builtins_private.h"

#include <dolphin/os/OSHostAddress.h>
#include <dolphin/vi.h>
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
                "check failed at %s:%d: %s\n",                           \
                __FILE__,                                                  \
                __LINE__,                                                  \
                #condition);                                               \
            abort();                                                       \
        }                                                                  \
    } while (0)

#define TEST_PC UINT32_C(0x80004000)
#define TEST_XFB UINT32_C(0x8067A3C0)
#define TEST_MEMORY_END UINT32_C(0x81800000)

static void prepare_call(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    porpoise_state_init(state, host);
    state->pc = TEST_PC;
    state->status = PORPOISE_EXECUTION_RUNNING;
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

int main(void)
{
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    uint32_t token;

    memset(&host, 0, sizeof(host));
    PorpoiseStubSetSystemCallVectorMapped(1);
    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    PorpoiseStubVIReset();

    prepare_call(&state, &host);
    state.gpr[3] = 0U;
    porpoise_libporpoise_vi_set_next_frame_buffer_adapter(&state);
    check_fault(&state, PORPOISE_FAULT_INVALID_POINTER, 0U);
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 0U);

    prepare_call(&state, &host);
    state.gpr[3] = TEST_XFB + UINT32_C(4);
    porpoise_libporpoise_vi_set_next_frame_buffer_adapter(&state);
    check_fault(
        &state,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        TEST_XFB + UINT32_C(4));
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 0U);

    prepare_call(&state, &host);
    state.gpr[3] = UINT32_C(0xCC000000);
    porpoise_libporpoise_vi_set_next_frame_buffer_adapter(&state);
    check_fault(&state, PORPOISE_FAULT_UNSUPPORTED_MMIO, UINT32_C(0xCC000000));
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 0U);

    token = __OSHostEncodeAddress(PorpoiseStubNativePointer());
    CHECK(token != 0U);
    prepare_call(&state, &host);
    state.gpr[3] = token;
    porpoise_libporpoise_vi_set_next_frame_buffer_adapter(&state);
    check_fault(&state, PORPOISE_FAULT_INVALID_POINTER, token);
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 0U);
    __OSHostReleaseAddress(token);

    prepare_call(&state, &host);
    state.gpr[3] = TEST_MEMORY_END;
    porpoise_libporpoise_vi_set_next_frame_buffer_adapter(&state);
    check_fault(&state, PORPOISE_FAULT_UNMAPPED_ADDRESS, TEST_MEMORY_END);
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 0U);

#if defined(LIBPORPOISE_VI_SET_NEXT_FRAME_BUFFER_GUEST_ADDRESS_API_VERSION)
    prepare_call(&state, &host);
    state.gpr[3] = TEST_MEMORY_END - UINT32_C(32);
    porpoise_libporpoise_vi_set_next_frame_buffer_adapter(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 1U);
    CHECK(PorpoiseStubVINextFrameBufferGuestAddress() ==
          TEST_MEMORY_END - UINT32_C(32));

    prepare_call(&state, &host);
    state.gpr[3] = TEST_XFB;
    porpoise_libporpoise_vi_set_next_frame_buffer_adapter(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 2U);
    CHECK(PorpoiseStubVINextFrameBufferGuestAddress() == TEST_XFB);

    PorpoiseStubVISetNextFrameBufferResult(0);
    prepare_call(&state, &host);
    state.gpr[3] = TEST_XFB + UINT32_C(32);
    porpoise_libporpoise_vi_set_next_frame_buffer_adapter(&state);
    check_fault(
        &state,
        PORPOISE_FAULT_HOST_IO,
        TEST_XFB + UINT32_C(32));
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 3U);
    CHECK(PorpoiseStubVINextFrameBufferGuestAddress() ==
          TEST_XFB + UINT32_C(32));

    state.gpr[3] = TEST_XFB + UINT32_C(64);
    porpoise_libporpoise_vi_set_next_frame_buffer_adapter(&state);
    check_fault(
        &state,
        PORPOISE_FAULT_HOST_IO,
        TEST_XFB + UINT32_C(32));
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 3U);
#else
    prepare_call(&state, &host);
    state.gpr[3] = TEST_XFB;
    porpoise_libporpoise_vi_set_next_frame_buffer_adapter(&state);
    check_fault(&state, PORPOISE_FAULT_UNSUPPORTED_OPERATION, TEST_XFB);
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 0U);
#endif

    porpoise_libporpoise_adapter_shutdown(&host);
    return 0;
}
