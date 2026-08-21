#include "porpoise_libporpoise_builtins_private.h"

#include <dolphin/ar.h>
#include <porpoise/stub.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "porpoise_dispatch_private.h"

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

#define TEST_REQUEST_A UINT32_C(0x80040004)
#define TEST_REQUEST_B UINT32_C(0x80040044)
#define TEST_REQUEST_C UINT32_C(0x80040084)
#define TEST_CALLBACK_A UINT32_C(0x80010100)
#define TEST_CALLBACK_B UINT32_C(0x80010200)
#define TEST_CALLBACK_FAULT UINT32_C(0x80010300)
#define TEST_CALLBACK_UNKNOWN UINT32_C(0x80010400)
#define TEST_CALLBACK_MEMORY UINT32_C(0x80050000)

enum {
    TEST_REQUEST_SIZE = 0x20,
    TEST_MAX_CALLBACKS = 16
};

typedef struct CallbackRecord {
    uint32_t address;
    uint32_t request;
    uint32_t depth;
} CallbackRecord;

static CallbackRecord callback_records[TEST_MAX_CALLBACKS];
static size_t callback_count;
static int callback_reentrant_poll;
static int callback_post_nested_request;

static void post_request(
    PorpoisePpcState *state,
    uint32_t guest_request,
    uint32_t type,
    uint32_t priority,
    uint32_t source,
    uint32_t destination,
    uint32_t length,
    uint32_t callback,
    uint32_t depth);

static void clear_callback_records(void)
{
    memset(callback_records, 0, sizeof(callback_records));
    callback_count = 0U;
    callback_reentrant_poll = 0;
    callback_post_nested_request = 0;
}

static int test_call_guest(
    PorpoisePpcState *state,
    uint32_t guest_function_address)
{
    size_t record_index;

    CHECK(state != NULL);
    CHECK(callback_count < TEST_MAX_CALLBACKS);
    record_index = callback_count++;
    callback_records[record_index].address = guest_function_address;
    callback_records[record_index].request = state->gpr[3];
    callback_records[record_index].depth = state->lifted_call_depth;
    porpoise_store_u32(
        state,
        TEST_CALLBACK_MEMORY,
        guest_function_address ^ state->gpr[3]);
    CHECK(!porpoise_state_has_fault(state));

    if (callback_reentrant_poll) {
        size_t count_before_poll = callback_count;
        CHECK(porpoise_poll_host_events(state, UINT32_C(0x8001FFF0)));
        CHECK(callback_count == count_before_poll);
    }
    if (callback_post_nested_request &&
        guest_function_address == TEST_CALLBACK_A) {
        callback_post_nested_request = 0;
        post_request(
            state,
            TEST_REQUEST_C,
            ARQ_TYPE_MRAM_TO_ARAM,
            ARQ_PRIORITY_HIGH,
            UINT32_C(0x800F0000),
            UINT32_C(0x16000),
            UINT32_C(0x20),
            TEST_CALLBACK_B,
            state->lifted_call_depth + 1U);
        CHECK(!porpoise_state_has_fault(state));
        state->lifted_call_depth--;
        CHECK(porpoise_poll_host_events(state, UINT32_C(0x8001FFE0)));
        CHECK(callback_count == record_index + 1U);
    }

    /* Callback register/status edits belong only to the cloned event state. */
    state->gpr[3] = UINT32_C(0xDEADBEEF);
    state->gpr[4] = UINT32_C(0x13579BDF);
    state->lr = UINT32_C(0x2468ACE0);
    state->pc = guest_function_address;
    state->status = PORPOISE_EXECUTION_READY;

    if (guest_function_address == TEST_CALLBACK_FAULT) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            UINT32_C(0x8001F00C),
            "fixture callback fault");
        return 0;
    }
    if (guest_function_address == TEST_CALLBACK_UNKNOWN) {
        return 0;
    }
    return 1;
}

static void initialize_state(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    porpoise_state_init(state, host);
    state->gpr[1] = UINT32_C(0x817FF000);
    state->gpr[2] = UINT32_C(0x80001000);
    state->gpr[13] = UINT32_C(0x80002000);
    CHECK(porpoise_state_prepare_title_entry(state));
    state->status = PORPOISE_EXECUTION_RUNNING;
    state->pc = UINT32_C(0x80008000);
}

static void recover_state(PorpoisePpcState *state)
{
    porpoise_state_clear_fault(state);
    state->status = PORPOISE_EXECUTION_RUNNING;
    state->msr |= PORPOISE_MSR_EE;
}

static void prepare_request(
    PorpoisePpcState *state,
    uint32_t guest_request,
    uint32_t preserved_priority)
{
    uint8_t bytes[TEST_REQUEST_SIZE + 8U];

    memset(bytes, 0xA5, sizeof(bytes));
    CHECK(porpoise_store_bytes(
        state,
        guest_request - UINT32_C(4),
        bytes,
        sizeof(bytes)));
    porpoise_store_u32(
        state, guest_request + UINT32_C(0x0C), preserved_priority);
    CHECK(!porpoise_state_has_fault(state));
}

static void post_request(
    PorpoisePpcState *state,
    uint32_t guest_request,
    uint32_t type,
    uint32_t priority,
    uint32_t source,
    uint32_t destination,
    uint32_t length,
    uint32_t callback,
    uint32_t depth)
{
    state->gpr[3] = guest_request;
    state->gpr[4] = UINT32_C(0x10203040);
    state->gpr[5] = type;
    state->gpr[6] = priority;
    state->gpr[7] = source;
    state->gpr[8] = destination;
    state->gpr[9] = length;
    state->gpr[10] = callback;
    state->lifted_call_depth = depth;
    porpoise_libporpoise_arq_post_request_adapter(state);
}

static void check_marshaled_request(
    PorpoisePpcState *state,
    uint32_t guest_request,
    uint32_t type,
    uint32_t preserved_priority,
    uint32_t source,
    uint32_t destination,
    uint32_t length,
    uint32_t callback)
{
    CHECK(porpoise_load_u8(state, guest_request - UINT32_C(1)) ==
          UINT8_C(0xA5));
    CHECK(porpoise_load_u32(state, guest_request + UINT32_C(0x00)) == 0U);
    CHECK(porpoise_load_u32(state, guest_request + UINT32_C(0x04)) ==
          UINT32_C(0x10203040));
    CHECK(porpoise_load_u32(state, guest_request + UINT32_C(0x08)) ==
          type);
    CHECK(porpoise_load_u32(state, guest_request + UINT32_C(0x0C)) ==
          preserved_priority);
    CHECK(porpoise_load_u32(state, guest_request + UINT32_C(0x10)) ==
          source);
    CHECK(porpoise_load_u32(state, guest_request + UINT32_C(0x14)) ==
          destination);
    CHECK(porpoise_load_u32(state, guest_request + UINT32_C(0x18)) ==
          length);
    CHECK(porpoise_load_u32(state, guest_request + UINT32_C(0x1C)) ==
          callback);
    CHECK(porpoise_load_u8(
              state, guest_request + (uint32_t)TEST_REQUEST_SIZE) ==
          UINT8_C(0xA5));
    CHECK(!porpoise_state_has_fault(state));
}

int main(void)
{
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    PorpoisePpcState state_before_poll;
    PorpoiseHostPollEventsFn installed_poll_events;
    unsigned int dma_count;

    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    CHECK(host.poll_events != NULL);
    CHECK(host.call_guest == NULL);
    installed_poll_events = host.poll_events;
    host.call_guest = test_call_guest;
    initialize_state(&state, &host);
    PorpoiseStubARReset();
    clear_callback_records();

    /* MRAM-to-ARAM marshalling preserves +0x0C and queues after DMA. */
    prepare_request(&state, TEST_REQUEST_A, UINT32_C(0xCAFEBABE));
    post_request(
        &state,
        TEST_REQUEST_A,
        ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH,
        UINT32_C(0x80060000),
        UINT32_C(0x00004000),
        UINT32_C(0x80),
        TEST_CALLBACK_A,
        2U);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubARDMACallCount() == 1U);
    CHECK(PorpoiseStubARLastDMAType() == ARQ_TYPE_MRAM_TO_ARAM);
    CHECK(PorpoiseStubARLastDMAMainMemory() == UINT32_C(0x80060000));
    CHECK(PorpoiseStubARLastDMAAram() == UINT32_C(0x00004000));
    CHECK(PorpoiseStubARLastDMALength() == UINT32_C(0x80));
    check_marshaled_request(
        &state,
        TEST_REQUEST_A,
        ARQ_TYPE_MRAM_TO_ARAM,
        UINT32_C(0xCAFEBABE),
        UINT32_C(0x80060000),
        UINT32_C(0x00004000),
        UINT32_C(0x80),
        TEST_CALLBACK_A);

    CHECK(porpoise_poll_host_events(&state, state.pc));
    CHECK(callback_count == 0U);
    state.lifted_call_depth = 1U;
    state_before_poll = state;
    CHECK(porpoise_poll_host_events(&state, state.pc));
    CHECK(callback_count == 1U);
    CHECK(callback_records[0].address == TEST_CALLBACK_A);
    CHECK(callback_records[0].request == TEST_REQUEST_A);
    CHECK(callback_records[0].depth == 1U);
    CHECK(memcmp(&state, &state_before_poll, sizeof(state)) == 0);
    CHECK(porpoise_load_u32(&state, TEST_CALLBACK_MEMORY) ==
          (TEST_CALLBACK_A ^ TEST_REQUEST_A));

    /* ARAM-to-MRAM swaps the SDK source/destination roles for native DMA. */
    prepare_request(&state, TEST_REQUEST_B, UINT32_C(0x0BADF00D));
    post_request(
        &state,
        TEST_REQUEST_B,
        ARQ_TYPE_ARAM_TO_MRAM,
        ARQ_PRIORITY_HIGH,
        UINT32_C(0x00006000),
        UINT32_C(0x80070000),
        UINT32_C(0x40),
        0U,
        1U);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubARDMACallCount() == 2U);
    CHECK(PorpoiseStubARLastDMAType() == ARQ_TYPE_ARAM_TO_MRAM);
    CHECK(PorpoiseStubARLastDMAMainMemory() == UINT32_C(0x80070000));
    CHECK(PorpoiseStubARLastDMAAram() == UINT32_C(0x00006000));
    CHECK(PorpoiseStubARLastDMALength() == UINT32_C(0x40));
    check_marshaled_request(
        &state,
        TEST_REQUEST_B,
        ARQ_TYPE_ARAM_TO_MRAM,
        UINT32_C(0x0BADF00D),
        UINT32_C(0x00006000),
        UINT32_C(0x80070000),
        UINT32_C(0x40),
        PORPOISE_GUEST_ARQ_CALLBACK_HACK_ADDRESS);
    state.lifted_call_depth = 0U;
    CHECK(porpoise_poll_host_events(&state, state.pc));
    CHECK(callback_count == 1U);

    /* EE masks completion; the 0-to-1 edge releases it at ready depth. */
    prepare_request(&state, TEST_REQUEST_B, UINT32_C(0x11111111));
    post_request(
        &state,
        TEST_REQUEST_B,
        ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH,
        UINT32_C(0x80080000),
        UINT32_C(0x00008000),
        UINT32_C(0x20),
        TEST_CALLBACK_B,
        1U);
    CHECK(!porpoise_state_has_fault(&state));
    state.lifted_call_depth = 0U;
    state.msr &= ~PORPOISE_MSR_EE;
    CHECK(porpoise_poll_host_events(&state, state.pc));
    CHECK(callback_count == 1U);
    CHECK(porpoise_write_msr(
        &state, state.pc, state.msr | PORPOISE_MSR_EE));
    CHECK(callback_count == 2U);
    CHECK(callback_records[1].address == TEST_CALLBACK_B);
    CHECK(callback_records[1].request == TEST_REQUEST_B);

    /* FIFO order is stable, and a callback cannot recursively drain it. */
    prepare_request(&state, TEST_REQUEST_A, UINT32_C(1));
    prepare_request(&state, TEST_REQUEST_B, UINT32_C(2));
    post_request(
        &state, TEST_REQUEST_A, ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH, UINT32_C(0x80090000), UINT32_C(0xA000),
        UINT32_C(0x20), TEST_CALLBACK_A, 1U);
    post_request(
        &state, TEST_REQUEST_B, ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH, UINT32_C(0x800A0000), UINT32_C(0xC000),
        UINT32_C(0x20), TEST_CALLBACK_B, 1U);
    CHECK(!porpoise_state_has_fault(&state));
    state.lifted_call_depth = 0U;
    callback_reentrant_poll = 1;
    CHECK(porpoise_poll_host_events(&state, state.pc));
    CHECK(callback_count == 4U);
    CHECK(callback_records[2].address == TEST_CALLBACK_A);
    CHECK(callback_records[2].request == TEST_REQUEST_A);
    CHECK(callback_records[3].address == TEST_CALLBACK_B);
    CHECK(callback_records[3].request == TEST_REQUEST_B);
    callback_reentrant_poll = 0;

    /* An event posted by a callback waits for the next bounded drain. */
    prepare_request(&state, TEST_REQUEST_A, UINT32_C(6));
    prepare_request(&state, TEST_REQUEST_C, UINT32_C(7));
    post_request(
        &state, TEST_REQUEST_A, ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH, UINT32_C(0x800E0000), UINT32_C(0x14000),
        UINT32_C(0x20), TEST_CALLBACK_A, 1U);
    CHECK(!porpoise_state_has_fault(&state));
    state.lifted_call_depth = 0U;
    callback_post_nested_request = 1;
    CHECK(porpoise_poll_host_events(&state, state.pc));
    CHECK(callback_count == 5U);
    CHECK(callback_records[4].address == TEST_CALLBACK_A);
    CHECK(callback_records[4].request == TEST_REQUEST_A);
    CHECK(porpoise_poll_host_events(&state, state.pc));
    CHECK(callback_count == 6U);
    CHECK(callback_records[5].address == TEST_CALLBACK_B);
    CHECK(callback_records[5].request == TEST_REQUEST_C);

    /* Every preflight rejection leaves the request and native DMA untouched. */
    dma_count = PorpoiseStubARDMACallCount();
    post_request(
        &state, 0U, ARQ_TYPE_MRAM_TO_ARAM, ARQ_PRIORITY_HIGH,
        UINT32_C(0x800B0000), UINT32_C(0xE000), UINT32_C(0x20),
        TEST_CALLBACK_A, 1U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_POINTER);
    CHECK(PorpoiseStubARDMACallCount() == dma_count);
    recover_state(&state);

    post_request(
        &state, TEST_REQUEST_A + UINT32_C(2), ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH, UINT32_C(0x800B0000), UINT32_C(0xE000),
        UINT32_C(0x20), TEST_CALLBACK_A, 1U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(PorpoiseStubARDMACallCount() == dma_count);
    recover_state(&state);

    post_request(
        &state, UINT32_C(0x817FFFF0), ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH, UINT32_C(0x800B0000), UINT32_C(0xE000),
        UINT32_C(0x20), TEST_CALLBACK_A, 1U);
    CHECK(state.fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
    CHECK(PorpoiseStubARDMACallCount() == dma_count);
    recover_state(&state);

    prepare_request(&state, TEST_REQUEST_C, UINT32_C(0x55667788));
    post_request(
        &state, TEST_REQUEST_C, ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_LOW, UINT32_C(0x800B0000), UINT32_C(0xE000),
        UINT32_C(0x20), TEST_CALLBACK_A, 1U);
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
    CHECK(PorpoiseStubARDMACallCount() == dma_count);
    recover_state(&state);
    CHECK(porpoise_load_u32(&state, TEST_REQUEST_C) ==
          UINT32_C(0xA5A5A5A5));

    post_request(
        &state, TEST_REQUEST_C, UINT32_C(2), ARQ_PRIORITY_HIGH,
        UINT32_C(0x800B0000), UINT32_C(0xE000), UINT32_C(0x20),
        TEST_CALLBACK_A, 1U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(PorpoiseStubARDMACallCount() == dma_count);
    recover_state(&state);
    CHECK(porpoise_load_u32(&state, TEST_REQUEST_C) ==
          UINT32_C(0xA5A5A5A5));

    post_request(
        &state, TEST_REQUEST_C, ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH, UINT32_C(0x800B0000), UINT32_C(0xE000),
        UINT32_C(0x20), TEST_CALLBACK_A + UINT32_C(2), 1U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(PorpoiseStubARDMACallCount() == dma_count);
    recover_state(&state);
    CHECK(porpoise_load_u32(&state, TEST_REQUEST_C) ==
          UINT32_C(0xA5A5A5A5));

    post_request(
        &state, TEST_REQUEST_C, ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH, UINT32_C(0x800B0000), UINT32_C(0xE000),
        UINT32_C(0x20), TEST_CALLBACK_A, 0U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(PorpoiseStubARDMACallCount() == dma_count);
    recover_state(&state);
    CHECK(porpoise_load_u32(&state, TEST_REQUEST_C) ==
          UINT32_C(0xA5A5A5A5));

    host.poll_events = NULL;
    post_request(
        &state, TEST_REQUEST_C, ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH, UINT32_C(0x800B0000), UINT32_C(0xE000),
        UINT32_C(0x20), TEST_CALLBACK_A, 1U);
    CHECK(state.fault == PORPOISE_FAULT_MISSING_HOST_CALLBACK);
    CHECK(PorpoiseStubARDMACallCount() == dma_count);
    recover_state(&state);
    CHECK(porpoise_load_u32(&state, TEST_REQUEST_C) ==
          UINT32_C(0xA5A5A5A5));
    host.poll_events = installed_poll_events;

    host.call_guest = NULL;
    post_request(
        &state, TEST_REQUEST_C, ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH, UINT32_C(0x800B0000), UINT32_C(0xE000),
        UINT32_C(0x20), TEST_CALLBACK_A, 1U);
    CHECK(state.fault == PORPOISE_FAULT_MISSING_HOST_CALLBACK);
    CHECK(PorpoiseStubARDMACallCount() == dma_count);
    recover_state(&state);
    CHECK(porpoise_load_u32(&state, TEST_REQUEST_C) ==
          UINT32_C(0xA5A5A5A5));
    host.call_guest = test_call_guest;

    /* A dispatcher removed after submission faults instead of losing work. */
    prepare_request(&state, TEST_REQUEST_C, UINT32_C(0x66778899));
    post_request(
        &state, TEST_REQUEST_C, ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH, UINT32_C(0x800B0000), UINT32_C(0xE000),
        UINT32_C(0x20), TEST_CALLBACK_A, 1U);
    CHECK(!porpoise_state_has_fault(&state));
    host.call_guest = NULL;
    state.lifted_call_depth = 0U;
    CHECK(!porpoise_poll_host_events(&state, state.pc));
    CHECK(state.fault == PORPOISE_FAULT_MISSING_HOST_CALLBACK);
    CHECK(state.fault_address == TEST_CALLBACK_A);
    CHECK(callback_count == 6U);
    recover_state(&state);
    host.call_guest = test_call_guest;
    dma_count = PorpoiseStubARDMACallCount();

    /* Native rejection is surfaced and never creates a completion event. */
    PorpoiseStubARSetDMAResult(AR_DMA_RESULT_INVALID_ALIGNMENT);
    post_request(
        &state, TEST_REQUEST_C, ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH, UINT32_C(0x800B0000), UINT32_C(0xE001),
        UINT32_C(0x20), TEST_CALLBACK_A, 1U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(PorpoiseStubARDMACallCount() == dma_count + 1U);
    recover_state(&state);
    CHECK(porpoise_load_u32(
              &state, TEST_REQUEST_C + UINT32_C(0x04)) ==
          UINT32_C(0x10203040));
    PorpoiseStubARSetDMAResult(AR_DMA_RESULT_SUCCESS);
    state.lifted_call_depth = 0U;
    CHECK(porpoise_poll_host_events(&state, state.pc));
    CHECK(callback_count == 6U);

    /* A queued callback fault is copied back without leaking its registers. */
    prepare_request(&state, TEST_REQUEST_C, UINT32_C(3));
    post_request(
        &state, TEST_REQUEST_C, ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH, UINT32_C(0x800C0000), UINT32_C(0x10000),
        UINT32_C(0x20), TEST_CALLBACK_FAULT, 1U);
    CHECK(!porpoise_state_has_fault(&state));
    state.lifted_call_depth = 0U;
    state.gpr[3] = UINT32_C(0xAAAAAAAA);
    state.gpr[4] = UINT32_C(0xBBBBBBBB);
    state.lr = UINT32_C(0xCCCCCCCC);
    CHECK(!porpoise_poll_host_events(&state, state.pc));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(state.fault_address == UINT32_C(0x8001F00C));
    CHECK(strcmp(porpoise_state_fault_message(&state),
                 "fixture callback fault") == 0);
    CHECK(state.gpr[3] == UINT32_C(0xAAAAAAAA));
    CHECK(state.gpr[4] == UINT32_C(0xBBBBBBBB));
    CHECK(state.lr == UINT32_C(0xCCCCCCCC));
    recover_state(&state);

    /* A dispatcher failure without a guest fault is itself fail-closed. */
    prepare_request(&state, TEST_REQUEST_C, UINT32_C(4));
    post_request(
        &state, TEST_REQUEST_C, ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH, UINT32_C(0x800D0000), UINT32_C(0x12000),
        UINT32_C(0x20), TEST_CALLBACK_UNKNOWN, 1U);
    CHECK(!porpoise_state_has_fault(&state));
    state.lifted_call_depth = 0U;
    CHECK(!porpoise_poll_host_events(&state, state.pc));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(state.fault_address == TEST_CALLBACK_UNKNOWN);
    recover_state(&state);

    /* Shutdown discards queued completions and remains idempotent. */
    prepare_request(&state, TEST_REQUEST_C, UINT32_C(5));
    post_request(
        &state, TEST_REQUEST_C, ARQ_TYPE_MRAM_TO_ARAM,
        ARQ_PRIORITY_HIGH, UINT32_C(0x800E0000), UINT32_C(0x14000),
        UINT32_C(0x20), TEST_CALLBACK_A, 1U);
    CHECK(!porpoise_state_has_fault(&state));
    porpoise_libporpoise_adapter_shutdown(&host);
    porpoise_libporpoise_adapter_shutdown(&host);

    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    host.call_guest = test_call_guest;
    initialize_state(&state, &host);
    state.lifted_call_depth = 0U;
    CHECK(porpoise_poll_host_events(&state, state.pc));
    CHECK(callback_count == 8U);
    porpoise_libporpoise_adapter_shutdown(&host);
    return 0;
}
