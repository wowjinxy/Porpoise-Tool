#include "porpoise_libporpoise_builtins_private.h"

#include <dolphin/dsp.h>
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

#define TEST_TASK_A UINT32_C(0x80040004)
#define TEST_TASK_B UINT32_C(0x80040104)
#define TEST_TASK_C UINT32_C(0x80040204)
#define TEST_IRAM UINT32_C(0x80060000)
#define TEST_DRAM UINT32_C(0x80070000)
#define TEST_AX_READY_WORD UINT32_C(0x80050000)

#define TEST_CALLBACK_INIT_A UINT32_C(0x80010100)
#define TEST_CALLBACK_RESUME UINT32_C(0x80010200)
#define TEST_CALLBACK_REQUEST UINT32_C(0x80010300)
#define TEST_CALLBACK_DONE_A UINT32_C(0x80010400)
#define TEST_CALLBACK_INIT_B UINT32_C(0x80010500)
#define TEST_CALLBACK_DONE_B UINT32_C(0x80010600)
#define TEST_CALLBACK_EDIT_DONE UINT32_C(0x80010700)
#define TEST_CALLBACK_DONE_ALTERNATE UINT32_C(0x80010800)
#define TEST_CALLBACK_FAULT UINT32_C(0x80010900)
#define TEST_CALLBACK_UNKNOWN UINT32_C(0x80010A00)

enum {
    TEST_DSP_TASK_SIZE = 0x50,
    TEST_TASK_CANARY_SIZE = 4,
    TEST_MAX_CALLBACKS = 32
};

typedef struct CallbackRecord {
    uint32_t callback;
    uint32_t task;
    uint32_t state;
    uint32_t flags;
    uint32_t next;
    uint32_t previous;
    uint32_t event_delivery_depth;
} CallbackRecord;

static CallbackRecord callback_records[TEST_MAX_CALLBACKS];
static size_t callback_count;
static uint32_t nested_task;
static uint32_t resubmit_task;
static int edit_done_callback;
static int write_ax_ready_word;

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
    state->pc = UINT32_C(0x80008000);
}

static void reset_callback_behavior(void)
{
    memset(callback_records, 0, sizeof(callback_records));
    callback_count = 0U;
    nested_task = 0U;
    resubmit_task = 0U;
    edit_done_callback = 0;
    write_ax_ready_word = 0;
}

static void read_guest_bytes(
    const PorpoiseHostAdapter *host,
    uint32_t address,
    void *bytes,
    size_t size)
{
    CHECK(host != NULL);
    CHECK(host->read_bytes != NULL);
    CHECK(host->read_bytes(host->context, address, bytes, size) ==
          PORPOISE_HOST_OK);
}

static void prepare_task(
    PorpoisePpcState *state,
    uint32_t guest_task,
    uint32_t priority,
    uint32_t init_callback,
    uint32_t resume_callback,
    uint32_t done_callback,
    uint32_t request_callback)
{
    uint8_t canaries[TEST_DSP_TASK_SIZE + 2 * TEST_TASK_CANARY_SIZE];

    memset(canaries, 0xA5, sizeof(canaries));
    CHECK(porpoise_store_bytes(
        state,
        guest_task - TEST_TASK_CANARY_SIZE,
        canaries,
        sizeof(canaries)));
    porpoise_store_u32(state, guest_task + UINT32_C(0x00),
                       UINT32_C(0x10203040));
    porpoise_store_u32(state, guest_task + UINT32_C(0x04), priority);
    porpoise_store_u32(state, guest_task + UINT32_C(0x08),
                       UINT32_C(0x55667788));
    porpoise_store_u32(state, guest_task + UINT32_C(0x0C), TEST_IRAM);
    porpoise_store_u32(state, guest_task + UINT32_C(0x10),
                       UINT32_C(0x160));
    porpoise_store_u32(state, guest_task + UINT32_C(0x14),
                       UINT32_C(0x00000020));
    porpoise_store_u32(state, guest_task + UINT32_C(0x18), TEST_DRAM);
    porpoise_store_u32(state, guest_task + UINT32_C(0x1C),
                       UINT32_C(0x2000));
    porpoise_store_u32(state, guest_task + UINT32_C(0x20),
                       UINT32_C(0x00000100));
    porpoise_store_u16(state, guest_task + UINT32_C(0x24),
                       UINT16_C(0x1234));
    porpoise_store_u16(state, guest_task + UINT32_C(0x26),
                       UINT16_C(0x5678));
    porpoise_store_u32(state, guest_task + UINT32_C(0x28), init_callback);
    porpoise_store_u32(state, guest_task + UINT32_C(0x2C), resume_callback);
    porpoise_store_u32(state, guest_task + UINT32_C(0x30), done_callback);
    porpoise_store_u32(state, guest_task + UINT32_C(0x34), request_callback);
    porpoise_store_u32(state, guest_task + UINT32_C(0x38),
                       UINT32_C(0x81234560));
    porpoise_store_u32(state, guest_task + UINT32_C(0x3C),
                       UINT32_C(0x81234564));
    porpoise_store_u64(state, guest_task + UINT32_C(0x40),
                       UINT64_C(0x0102030405060708));
    porpoise_store_u64(state, guest_task + UINT32_C(0x48),
                       UINT64_C(0x1122334455667788));
    CHECK(!porpoise_state_has_fault(state));
}

static void submit_task(PorpoisePpcState *state, uint32_t guest_task)
{
    state->gpr[3] = guest_task;
    porpoise_libporpoise_dsp_add_task_adapter(state);
}

static int test_call_guest(
    PorpoisePpcState *state,
    uint32_t guest_function_address)
{
    CallbackRecord *record;
    uint32_t guest_task;

    CHECK(state != NULL);
    CHECK(callback_count < TEST_MAX_CALLBACKS);
    guest_task = state->gpr[3];
    record = &callback_records[callback_count++];
    record->callback = guest_function_address;
    record->task = guest_task;
    record->state = porpoise_load_u32(
        state, guest_task + UINT32_C(0x00));
    record->flags = porpoise_load_u32(
        state, guest_task + UINT32_C(0x08));
    record->next = porpoise_load_u32(
        state, guest_task + UINT32_C(0x38));
    record->previous = porpoise_load_u32(
        state, guest_task + UINT32_C(0x3C));
    record->event_delivery_depth = state->host_event_delivery_depth;
    CHECK(!porpoise_state_has_fault(state));

    if (edit_done_callback &&
        guest_function_address == TEST_CALLBACK_EDIT_DONE) {
        edit_done_callback = 0;
        porpoise_store_u32(
            state,
            guest_task + UINT32_C(0x30),
            TEST_CALLBACK_DONE_ALTERNATE);
    }
    if (write_ax_ready_word) {
        porpoise_store_u32(state, TEST_AX_READY_WORD, UINT32_C(1));
    }
    if (nested_task != 0U &&
        guest_function_address == TEST_CALLBACK_INIT_A) {
        uint32_t task_to_post = nested_task;
        nested_task = 0U;
        submit_task(state, task_to_post);
        CHECK(!porpoise_state_has_fault(state));
        CHECK(state->gpr[3] == task_to_post);
        CHECK(porpoise_load_u32(
                  state, task_to_post + UINT32_C(0x00)) ==
              DSP_TASK_STATE_INIT);
        CHECK(porpoise_load_u32(
                  state, task_to_post + UINT32_C(0x08)) ==
              DSP_TASK_FLAG_ATTACHED);
        CHECK(porpoise_load_u32(
                  state, task_to_post + UINT32_C(0x3C)) ==
              guest_task);
        CHECK(porpoise_load_u32(
                  state, guest_task + UINT32_C(0x38)) ==
              task_to_post);
        CHECK(!porpoise_state_has_fault(state));
    }
    if (resubmit_task != 0U &&
        guest_function_address == TEST_CALLBACK_INIT_A) {
        uint32_t task_to_resubmit = resubmit_task;
        resubmit_task = 0U;
        submit_task(state, task_to_resubmit);
        CHECK(!porpoise_state_has_fault(state));
        CHECK(state->gpr[3] == task_to_resubmit);
    }
    CHECK(!porpoise_state_has_fault(state));

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
            "fixture DSP callback fault");
        return 0;
    }
    return 1;
}

static void register_callbacks(void)
{
    static const uint32_t callbacks[] = {
        TEST_CALLBACK_INIT_A,
        TEST_CALLBACK_RESUME,
        TEST_CALLBACK_REQUEST,
        TEST_CALLBACK_DONE_A,
        TEST_CALLBACK_INIT_B,
        TEST_CALLBACK_DONE_B,
        TEST_CALLBACK_EDIT_DONE,
        TEST_CALLBACK_DONE_ALTERNATE,
        TEST_CALLBACK_FAULT,
    };
    size_t index;

    PorpoiseStubDispatchReset();
    for (index = 0U;
         index < sizeof(callbacks) / sizeof(callbacks[0]);
         index++) {
        CHECK(PorpoiseStubDispatchAddAddress(callbacks[index]));
    }
}

static void check_canaries(
    const PorpoiseHostAdapter *host,
    uint32_t guest_task)
{
    uint8_t canary[TEST_TASK_CANARY_SIZE];
    size_t index;

    read_guest_bytes(
        host,
        guest_task - TEST_TASK_CANARY_SIZE,
        canary,
        sizeof(canary));
    for (index = 0U; index < sizeof(canary); index++) {
        CHECK(canary[index] == UINT8_C(0xA5));
    }
    read_guest_bytes(
        host,
        guest_task + TEST_DSP_TASK_SIZE,
        canary,
        sizeof(canary));
    for (index = 0U; index < sizeof(canary); index++) {
        CHECK(canary[index] == UINT8_C(0xA5));
    }
}

static void check_native_event(unsigned int index, uint32_t kind)
{
    CHECK(PorpoiseStubDSPEventKind(index) == kind);
    CHECK(PorpoiseStubDSPEventState(index) == DSP_TASK_STATE_RUN);
    CHECK(PorpoiseStubDSPEventFlags(index) == DSP_TASK_FLAG_ATTACHED);
}

static void snapshot_task(
    const PorpoiseHostAdapter *host,
    uint32_t guest_task,
    uint8_t bytes[TEST_DSP_TASK_SIZE + 2 * TEST_TASK_CANARY_SIZE])
{
    read_guest_bytes(
        host,
        guest_task - TEST_TASK_CANARY_SIZE,
        bytes,
        TEST_DSP_TASK_SIZE + 2 * TEST_TASK_CANARY_SIZE);
}

static void check_task_unchanged(
    const PorpoiseHostAdapter *host,
    uint32_t guest_task,
    const uint8_t expected[TEST_DSP_TASK_SIZE + 2 * TEST_TASK_CANARY_SIZE])
{
    uint8_t actual[TEST_DSP_TASK_SIZE + 2 * TEST_TASK_CANARY_SIZE];

    snapshot_task(host, guest_task, actual);
    CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
}

int main(void)
{
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    void *expected_iram;
    void *expected_dram;
    uint8_t before[TEST_DSP_TASK_SIZE + 2 * TEST_TASK_CANARY_SIZE];
    unsigned int native_calls;
    size_t index;

    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    host.call_guest = test_call_guest;
    initialize_state(&state, &host);
    register_callbacks();

    /* Exact BE 0x50 marshalling, all callback slots, and owned-field sync. */
    PorpoiseStubDSPReset();
    reset_callback_behavior();
    PorpoiseStubDSPSetCallbackMask(
        PORPOISE_STUB_DSP_CALLBACK_INIT |
        PORPOISE_STUB_DSP_CALLBACK_RESUME |
        PORPOISE_STUB_DSP_CALLBACK_REQUEST |
        PORPOISE_STUB_DSP_CALLBACK_DONE);
    prepare_task(
        &state,
        TEST_TASK_A,
        UINT32_C(0x01020304),
        TEST_CALLBACK_INIT_A,
        TEST_CALLBACK_RESUME,
        TEST_CALLBACK_DONE_A,
        TEST_CALLBACK_REQUEST);
    state.gpr[4] = UINT32_C(0xAAAAAAAA);
    state.lr = UINT32_C(0xBBBBBBBB);
    state.pc = UINT32_C(0x80008000);
    write_ax_ready_word = 1;
    porpoise_store_u32(&state, TEST_AX_READY_WORD, 0U);
    submit_task(&state, TEST_TASK_A);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == TEST_TASK_A);
    CHECK(state.gpr[4] == UINT32_C(0xAAAAAAAA));
    CHECK(state.lr == UINT32_C(0xBBBBBBBB));
    CHECK(state.pc == UINT32_C(0x80008000));
    CHECK(state.status == PORPOISE_EXECUTION_RUNNING);
    CHECK(state.host_event_delivery_depth == 0U);
    CHECK(callback_count == 4U);
    CHECK(callback_records[0].callback == TEST_CALLBACK_INIT_A);
    CHECK(callback_records[1].callback == TEST_CALLBACK_RESUME);
    CHECK(callback_records[2].callback == TEST_CALLBACK_REQUEST);
    CHECK(callback_records[3].callback == TEST_CALLBACK_DONE_A);
    for (index = 0U; index < callback_count; index++) {
        CHECK(callback_records[index].task == TEST_TASK_A);
        CHECK(callback_records[index].state == DSP_TASK_STATE_RUN);
        CHECK(callback_records[index].flags == DSP_TASK_FLAG_ATTACHED);
        CHECK(callback_records[index].event_delivery_depth != 0U);
    }
    CHECK(PorpoiseStubDSPEventCount() == 4U);
    check_native_event(0U, PORPOISE_STUB_DSP_CALLBACK_INIT);
    check_native_event(1U, PORPOISE_STUB_DSP_CALLBACK_RESUME);
    check_native_event(2U, PORPOISE_STUB_DSP_CALLBACK_REQUEST);
    check_native_event(3U, PORPOISE_STUB_DSP_CALLBACK_DONE);
    CHECK(PorpoiseStubDSPActiveTaskCount() == 0U);
    CHECK(PorpoiseStubDSPLastTask() != NULL);
    CHECK(host.decode_pointer(host.context, TEST_IRAM, &expected_iram) ==
          PORPOISE_HOST_OK);
    CHECK(host.decode_pointer(host.context, TEST_DRAM, &expected_dram) ==
          PORPOISE_HOST_OK);
    CHECK(PorpoiseStubDSPLastIramMemory() == expected_iram);
    CHECK(PorpoiseStubDSPLastDramMemory() == expected_dram);
    CHECK(PorpoiseStubDSPLastPriority() == UINT32_C(0x01020304));
    CHECK(PorpoiseStubDSPLastIramLength() == UINT32_C(0x160));
    CHECK(PorpoiseStubDSPLastIramAddress() == UINT32_C(0x20));
    CHECK(PorpoiseStubDSPLastDramLength() == UINT32_C(0x2000));
    CHECK(PorpoiseStubDSPLastDramAddress() == UINT32_C(0x100));
    CHECK(PorpoiseStubDSPLastInitVector() == UINT16_C(0x1234));
    CHECK(PorpoiseStubDSPLastResumeVector() == UINT16_C(0x5678));
    CHECK(PorpoiseStubDSPLastContextTime() ==
          INT64_C(0x0102030405060708));
    CHECK(PorpoiseStubDSPLastTaskTime() ==
          INT64_C(0x1122334455667788));
    CHECK(porpoise_load_u32(&state, TEST_TASK_A + UINT32_C(0x00)) ==
          DSP_TASK_STATE_DONE);
    CHECK(porpoise_load_u32(&state, TEST_TASK_A + UINT32_C(0x08)) ==
          DSP_TASK_FLAG_CLEARALL);
    CHECK(porpoise_load_u32(&state, TEST_TASK_A + UINT32_C(0x38)) == 0U);
    CHECK(porpoise_load_u32(&state, TEST_TASK_A + UINT32_C(0x3C)) == 0U);
    CHECK(porpoise_load_u32(&state, TEST_TASK_A + UINT32_C(0x04)) ==
          UINT32_C(0x01020304));
    CHECK(porpoise_load_u32(&state, TEST_TASK_A + UINT32_C(0x28)) ==
          TEST_CALLBACK_INIT_A);
    CHECK(porpoise_load_u64(&state, TEST_TASK_A + UINT32_C(0x40)) ==
          UINT64_C(0x0102030405060708));
    CHECK(porpoise_load_u32(&state, TEST_AX_READY_WORD) == 1U);
    check_canaries(&host, TEST_TASK_A);

    /* Reentrant posting retains both mirrors and preserves FIFO/link state. */
    PorpoiseStubDSPReset();
    reset_callback_behavior();
    prepare_task(
        &state, TEST_TASK_A, 4U,
        TEST_CALLBACK_INIT_A, 0U, TEST_CALLBACK_DONE_A, 0U);
    prepare_task(
        &state, TEST_TASK_B, 4U,
        TEST_CALLBACK_INIT_B, 0U, TEST_CALLBACK_DONE_B, 0U);
    nested_task = TEST_TASK_B;
    submit_task(&state, TEST_TASK_A);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubDSPAddTaskCallCount() == 2U);
    CHECK(PorpoiseStubDSPActiveTaskCount() == 0U);
    CHECK(callback_count == 4U);
    CHECK(callback_records[0].callback == TEST_CALLBACK_INIT_A);
    CHECK(callback_records[0].task == TEST_TASK_A);
    CHECK(callback_records[1].callback == TEST_CALLBACK_DONE_A);
    CHECK(callback_records[1].task == TEST_TASK_A);
    CHECK(callback_records[1].next == TEST_TASK_B);
    CHECK(callback_records[1].previous == 0U);
    CHECK(callback_records[2].callback == TEST_CALLBACK_INIT_B);
    CHECK(callback_records[2].task == TEST_TASK_B);
    CHECK(callback_records[2].next == 0U);
    CHECK(callback_records[2].previous == 0U);
    CHECK(callback_records[3].callback == TEST_CALLBACK_DONE_B);
    CHECK(callback_records[3].task == TEST_TASK_B);
    CHECK(porpoise_load_u32(&state, TEST_TASK_A) == DSP_TASK_STATE_DONE);
    CHECK(porpoise_load_u32(&state, TEST_TASK_B) == DSP_TASK_STATE_DONE);

    /* Active resubmission is bounded; completed tasks can be submitted again. */
    PorpoiseStubDSPReset();
    reset_callback_behavior();
    prepare_task(
        &state, TEST_TASK_A, 5U,
        TEST_CALLBACK_INIT_A, 0U, TEST_CALLBACK_DONE_A, 0U);
    resubmit_task = TEST_TASK_A;
    submit_task(&state, TEST_TASK_A);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubDSPAddTaskCallCount() == 2U);
    CHECK(PorpoiseStubDSPEventCount() == 2U);
    CHECK(callback_count == 2U);
    submit_task(&state, TEST_TASK_A);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubDSPAddTaskCallCount() == 3U);
    CHECK(PorpoiseStubDSPEventCount() == 4U);
    CHECK(callback_count == 4U);

    /* Guest callback edits are reread, and unrelated AX-style writes persist. */
    PorpoiseStubDSPReset();
    reset_callback_behavior();
    prepare_task(
        &state, TEST_TASK_A, 6U,
        TEST_CALLBACK_EDIT_DONE, 0U, TEST_CALLBACK_DONE_A, 0U);
    porpoise_store_u32(&state, TEST_AX_READY_WORD, 0U);
    edit_done_callback = 1;
    write_ax_ready_word = 1;
    submit_task(&state, TEST_TASK_A);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(callback_count == 2U);
    CHECK(callback_records[0].callback == TEST_CALLBACK_EDIT_DONE);
    CHECK(callback_records[1].callback == TEST_CALLBACK_DONE_ALTERNATE);
    CHECK(porpoise_load_u32(&state, TEST_TASK_A + UINT32_C(0x30)) ==
          TEST_CALLBACK_DONE_ALTERNATE);
    CHECK(porpoise_load_u32(&state, TEST_AX_READY_WORD) == 1U);

    /* The first callback fault is propagated; later guest callbacks stop. */
    PorpoiseStubDSPReset();
    reset_callback_behavior();
    prepare_task(
        &state, TEST_TASK_A, 7U,
        TEST_CALLBACK_FAULT, 0U, TEST_CALLBACK_DONE_A, 0U);
    state.gpr[4] = UINT32_C(0xA1A2A3A4);
    state.lr = UINT32_C(0xB1B2B3B4);
    submit_task(&state, TEST_TASK_A);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(state.fault_address == UINT32_C(0x8001F00C));
    CHECK(strcmp(
              porpoise_state_fault_message(&state),
              "fixture DSP callback fault") == 0);
    CHECK(state.gpr[3] == TEST_TASK_A);
    CHECK(state.gpr[4] == UINT32_C(0xA1A2A3A4));
    CHECK(state.lr == UINT32_C(0xB1B2B3B4));
    CHECK(callback_count == 1U);
    CHECK(PorpoiseStubDSPEventCount() == 2U);
    recover_state(&state);
    CHECK(porpoise_load_u32(&state, TEST_TASK_A) == DSP_TASK_STATE_DONE);
    CHECK(porpoise_load_u32(&state, TEST_TASK_A + UINT32_C(0x08)) ==
          DSP_TASK_FLAG_CLEARALL);

    /* Every preflight failure leaves guest bytes and native DSP untouched. */
    PorpoiseStubDSPReset();
    reset_callback_behavior();
    native_calls = PorpoiseStubDSPAddTaskCallCount();
    submit_task(&state, 0U);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == 0U);
    CHECK(PorpoiseStubDSPAddTaskCallCount() == native_calls);

    prepare_task(&state, TEST_TASK_C, 8U, 0U, 0U, 0U, 0U);
    snapshot_task(&host, TEST_TASK_C, before);
    submit_task(&state, TEST_TASK_C + UINT32_C(2));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(PorpoiseStubDSPAddTaskCallCount() == native_calls);
    check_task_unchanged(&host, TEST_TASK_C, before);
    recover_state(&state);

    state.gpr[3] = UINT32_C(0x817FFFC0);
    porpoise_libporpoise_dsp_add_task_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
    CHECK(PorpoiseStubDSPAddTaskCallCount() == native_calls);
    recover_state(&state);

    prepare_task(&state, TEST_TASK_C, 8U, 0U, 0U, 0U, 0U);
    porpoise_store_u32(
        &state, TEST_TASK_C + UINT32_C(0x0C), UINT32_C(0x817FFFF0));
    porpoise_store_u32(
        &state, TEST_TASK_C + UINT32_C(0x10), UINT32_C(0x20));
    snapshot_task(&host, TEST_TASK_C, before);
    submit_task(&state, TEST_TASK_C);
    CHECK(state.fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
    CHECK(PorpoiseStubDSPAddTaskCallCount() == native_calls);
    check_task_unchanged(&host, TEST_TASK_C, before);
    recover_state(&state);

    prepare_task(&state, TEST_TASK_C, 8U, 0U, 0U, 0U, 0U);
    porpoise_store_u32(
        &state, TEST_TASK_C + UINT32_C(0x0C), UINT32_C(0xFFFFFFF0));
    porpoise_store_u32(
        &state, TEST_TASK_C + UINT32_C(0x10), UINT32_C(0x20));
    snapshot_task(&host, TEST_TASK_C, before);
    submit_task(&state, TEST_TASK_C);
    CHECK(state.fault == PORPOISE_FAULT_ADDRESS_OVERFLOW);
    CHECK(PorpoiseStubDSPAddTaskCallCount() == native_calls);
    check_task_unchanged(&host, TEST_TASK_C, before);
    recover_state(&state);

    prepare_task(&state, TEST_TASK_C, 8U, 0U, 0U, 0U, 0U);
    porpoise_store_u32(&state, TEST_TASK_C + UINT32_C(0x18), 0U);
    porpoise_store_u32(&state, TEST_TASK_C + UINT32_C(0x1C), 4U);
    snapshot_task(&host, TEST_TASK_C, before);
    submit_task(&state, TEST_TASK_C);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_POINTER);
    CHECK(PorpoiseStubDSPAddTaskCallCount() == native_calls);
    check_task_unchanged(&host, TEST_TASK_C, before);
    recover_state(&state);

    prepare_task(
        &state, TEST_TASK_C, 8U,
        TEST_CALLBACK_INIT_A + UINT32_C(2), 0U, 0U, 0U);
    snapshot_task(&host, TEST_TASK_C, before);
    submit_task(&state, TEST_TASK_C);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(PorpoiseStubDSPAddTaskCallCount() == native_calls);
    check_task_unchanged(&host, TEST_TASK_C, before);
    recover_state(&state);

    prepare_task(
        &state, TEST_TASK_C, 8U,
        0U, 0U, 0U, TEST_CALLBACK_UNKNOWN);
    snapshot_task(&host, TEST_TASK_C, before);
    submit_task(&state, TEST_TASK_C);
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
    CHECK(state.fault_address == TEST_CALLBACK_UNKNOWN);
    CHECK(PorpoiseStubDSPAddTaskCallCount() == native_calls);
    check_task_unchanged(&host, TEST_TASK_C, before);
    recover_state(&state);

    prepare_task(
        &state, TEST_TASK_C, 8U,
        TEST_CALLBACK_INIT_A, 0U, 0U, 0U);
    snapshot_task(&host, TEST_TASK_C, before);
    host.call_guest = NULL;
    submit_task(&state, TEST_TASK_C);
    CHECK(state.fault == PORPOISE_FAULT_MISSING_HOST_CALLBACK);
    CHECK(PorpoiseStubDSPAddTaskCallCount() == native_calls);
    check_task_unchanged(&host, TEST_TASK_C, before);
    recover_state(&state);
    host.call_guest = test_call_guest;

    /* Native rejection returns NULL/zero without altering the guest task. */
    prepare_task(
        &state, TEST_TASK_C, 9U,
        TEST_CALLBACK_INIT_A, 0U, TEST_CALLBACK_DONE_A, 0U);
    snapshot_task(&host, TEST_TASK_C, before);
    PorpoiseStubDSPRejectNext(1);
    submit_task(&state, TEST_TASK_C);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == 0U);
    CHECK(PorpoiseStubDSPAddTaskCallCount() == native_calls + 1U);
    CHECK(PorpoiseStubDSPEventCount() == 0U);
    CHECK(PorpoiseStubDSPActiveTaskCount() == 0U);
    check_task_unchanged(&host, TEST_TASK_C, before);

    /* Completed mirrors and all remaining adapter allocations die at shutdown. */
    porpoise_libporpoise_adapter_shutdown(&host);
    CHECK(host.context == NULL);
    CHECK(host.read_bytes == NULL);
    porpoise_libporpoise_adapter_shutdown(&host);

    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    host.call_guest = test_call_guest;
    initialize_state(&state, &host);
    PorpoiseStubDSPReset();
    reset_callback_behavior();
    prepare_task(
        &state, TEST_TASK_A, 10U,
        TEST_CALLBACK_INIT_A, 0U, TEST_CALLBACK_DONE_A, 0U);
    submit_task(&state, TEST_TASK_A);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubDSPActiveTaskCount() == 0U);
    porpoise_libporpoise_adapter_shutdown(&host);
    return 0;
}
