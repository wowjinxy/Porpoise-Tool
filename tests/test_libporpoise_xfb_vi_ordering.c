#include "porpoise_libporpoise_builtins_private.h"

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

#define TEST_PC UINT32_C(0x80411160)
#define FIFO_ADDRESS UINT32_C(0x80010000)
#define FIFO_SIZE UINT32_C(0x00010000)
#define XFB_B UINT32_C(0x8067A3C0)
#define SCRATCH_XFB UINT32_C(0x80589C80)
#define REJECTED_XFB UINT32_C(0x80680000)

static void prepare_call(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    porpoise_state_init(state, host);
    state->pc = TEST_PC;
    state->status = PORPOISE_EXECUTION_RUNNING;
}

static void initialize_gx(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    PorpoiseStubGXInitReset();
    prepare_call(state, host);
    state->gpr[3] = FIFO_ADDRESS;
    state->gpr[4] = FIFO_SIZE;
    porpoise_libporpoise_gx_init_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXInitCallCount() == 1U);
}

static void copy_disp(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host,
    uint32_t destination,
    uint32_t clear)
{
    prepare_call(state, host);
    state->gpr[3] = destination;
    state->gpr[4] = clear;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
}

static void select_xfb(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host,
    uint32_t destination)
{
    prepare_call(state, host);
    state->gpr[3] = destination;
    porpoise_libporpoise_vi_set_next_frame_buffer_adapter(state);
}

static void check_successful_copy(
    const PorpoisePpcState *state,
    unsigned int expected_calls,
    uint32_t expected_destination,
    uint32_t expected_clear)
{
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == expected_calls);
    CHECK(PorpoiseStubGXCopyDispGuestAddress() == expected_destination);
    CHECK(PorpoiseStubGXCopyDispGuestAddressClearFlag() == expected_clear);
    CHECK(PorpoiseStubGXCopyDispAcceptedCallCount() == expected_calls);
    CHECK(PorpoiseStubGXCopyDispAcceptedGuestAddress() ==
          expected_destination);
    CHECK(PorpoiseStubGXCopyDispAcceptedClearFlag() == expected_clear);
    CHECK(PorpoiseStubGXCopyDispCallCount() == 0U);
}

static void test_copy_then_select(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    PorpoiseStubGXBoundaryReset();
    PorpoiseStubVIReset();

    /* Multi-XFB ordering: materializing B does not select it. */
    copy_disp(state, host, XFB_B, 1U);
    check_successful_copy(state, 1U, XFB_B, 1U);
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 0U);
    CHECK(PorpoiseStubVIPendingFrameBufferGuestAddress() == 0U);

    select_xfb(state, host, XFB_B);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 1U);
    CHECK(PorpoiseStubVINextFrameBufferGuestAddress() == XFB_B);
    CHECK(PorpoiseStubVIPendingFrameBufferGuestAddress() == XFB_B);
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == 1U);

    /* A later scratch copy is still forwarded exactly once, including its
     * clear flag, but it cannot replace VI's independently selected XFB. */
    copy_disp(state, host, SCRATCH_XFB, 0U);
    check_successful_copy(state, 2U, SCRATCH_XFB, 0U);
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 1U);
    CHECK(PorpoiseStubVIPendingFrameBufferGuestAddress() == XFB_B);
}

static void test_select_then_copy(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    PorpoiseStubGXBoundaryReset();
    PorpoiseStubVIReset();

    /* Single-XFB ordering: selecting B before its next copy is valid. */
    select_xfb(state, host, XFB_B);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 1U);
    CHECK(PorpoiseStubVINextFrameBufferGuestAddress() == XFB_B);
    CHECK(PorpoiseStubVIPendingFrameBufferGuestAddress() == XFB_B);
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() == 0U);

    copy_disp(state, host, XFB_B, 0U);
    check_successful_copy(state, 1U, XFB_B, 0U);
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 1U);
    CHECK(PorpoiseStubVIPendingFrameBufferGuestAddress() == XFB_B);

    copy_disp(state, host, SCRATCH_XFB, 1U);
    check_successful_copy(state, 2U, SCRATCH_XFB, 1U);
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 1U);
    CHECK(PorpoiseStubVIPendingFrameBufferGuestAddress() == XFB_B);
}

static void test_rejected_copy_is_transactional(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    unsigned int accepted_before;
    unsigned int requests_before;
    uint32_t accepted_address;
    uint32_t accepted_clear;

    PorpoiseStubGXBoundaryReset();
    PorpoiseStubVIReset();

    select_xfb(state, host, XFB_B);
    CHECK(!porpoise_state_has_fault(state));
    copy_disp(state, host, XFB_B, 0U);
    check_successful_copy(state, 1U, XFB_B, 0U);

    accepted_before = PorpoiseStubGXCopyDispAcceptedCallCount();
    accepted_address = PorpoiseStubGXCopyDispAcceptedGuestAddress();
    accepted_clear = PorpoiseStubGXCopyDispAcceptedClearFlag();
    requests_before = PorpoiseStubGXCopyDispGuestAddressCallCount();
    PorpoiseStubGXSetGuestAddressCopyResults(0, 1);

    copy_disp(state, host, REJECTED_XFB, 1U);
    CHECK(state->status == PORPOISE_EXECUTION_FAULTED);
    CHECK(state->fault == PORPOISE_FAULT_HOST_IO);
    CHECK(state->fault_address == REJECTED_XFB);

    /* The request, exact u32 address, and clear flag cross the boundary once.
     * A rejected native transaction commits neither a copy nor a VI change. */
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() ==
          requests_before + 1U);
    CHECK(PorpoiseStubGXCopyDispGuestAddress() == REJECTED_XFB);
    CHECK(PorpoiseStubGXCopyDispGuestAddressClearFlag() == 1U);
    CHECK(PorpoiseStubGXCopyDispAcceptedCallCount() == accepted_before);
    CHECK(PorpoiseStubGXCopyDispAcceptedGuestAddress() == accepted_address);
    CHECK(PorpoiseStubGXCopyDispAcceptedClearFlag() == accepted_clear);
    CHECK(PorpoiseStubVIPendingFrameBufferGuestAddress() == XFB_B);
    CHECK(PorpoiseStubVISetNextFrameBufferCallCount() == 1U);
    CHECK(PorpoiseStubGXCopyDispCallCount() == 0U);

    /* Faults are sticky: re-entering cannot retry the copy or forward clear a
     * second time. */
    state->gpr[3] = SCRATCH_XFB;
    state->gpr[4] = 0U;
    porpoise_libporpoise_gx_copy_disp_adapter(state);
    CHECK(PorpoiseStubGXCopyDispGuestAddressCallCount() ==
          requests_before + 1U);
    CHECK(PorpoiseStubGXCopyDispAcceptedCallCount() == accepted_before);
    CHECK(PorpoiseStubVIPendingFrameBufferGuestAddress() == XFB_B);
}

int main(void)
{
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    memset(&host, 0, sizeof(host));
    PorpoiseStubSetSystemCallVectorMapped(1);
    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    initialize_gx(&state, &host);

    test_copy_then_select(&state, &host);
    test_select_then_copy(&state, &host);
    test_rejected_copy_is_transactional(&state, &host);

    porpoise_libporpoise_adapter_shutdown(&host);
    return 0;
}
