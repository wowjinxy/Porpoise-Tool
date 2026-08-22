#include "porpoise_libporpoise_builtins_private.h"

#include <porpoise/stub.h>
#include <porpoise/thread_carrier_stub.h>

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

#define TEST_MAIN_THREAD UINT32_C(0x80010000)
#define TEST_CHILD_THREAD UINT32_C(0x80011000)
#define TEST_FAULT_CHILD_THREAD UINT32_C(0x80012000)
#define TEST_CHILD_STACK_END UINT32_C(0x80014000)
#define TEST_CHILD_STACK_BASE UINT32_C(0x80015000)
#define TEST_FAULT_CHILD_STACK_END UINT32_C(0x80016000)
#define TEST_FAULT_CHILD_STACK_BASE UINT32_C(0x80017000)
#define TEST_MAIN_RESUME UINT32_C(0x80008000)
#define TEST_CHILD_ENTRY UINT32_C(0x80008004)
#define TEST_CHILD_EXIT UINT32_C(0x80008008)
#define TEST_CHILD_CONTINUATION UINT32_C(0x8000800C)
#define TEST_MAIN_SRR0 UINT32_C(0x81234000)
#define TEST_MAIN_SRR1 UINT32_C(0x00001000)
#define TEST_MAIN_SECOND_SRR0 UINT32_C(0x85678000)
#define TEST_MAIN_SECOND_SRR1 UINT32_C(0x00002000)
#define TEST_CURRENT_CONTEXT UINT32_C(0x800000D4)
#define TEST_CURRENT_FPU_CONTEXT UINT32_C(0x800000D8)
#define TEST_CURRENT_THREAD UINT32_C(0x800000E4)
#define TEST_STACK_MAGIC UINT32_C(0xDEADBABE)
#define TEST_RETURN_VALUE UINT32_C(0xC0DEC0DE)

enum {
    TEST_THREAD_SIZE = 0x318,
    TEST_CONTEXT_LR_OFFSET = 0x084,
    TEST_CONTEXT_SRR0_OFFSET = 0x198,
    TEST_CONTEXT_SRR1_OFFSET = 0x19C,
    TEST_CONTEXT_STATE_OFFSET = 0x1A2,
    TEST_THREAD_STATE_OFFSET = 0x2C8,
    TEST_THREAD_ATTR_OFFSET = 0x2CA,
    TEST_THREAD_SUSPEND_OFFSET = 0x2CC,
    TEST_THREAD_PRIORITY_OFFSET = 0x2D0,
    TEST_THREAD_BASE_PRIORITY_OFFSET = 0x2D4,
    TEST_THREAD_VALUE_OFFSET = 0x2D8,
    TEST_THREAD_ACTIVE_NEXT_OFFSET = 0x2FC,
    TEST_THREAD_ACTIVE_PREVIOUS_OFFSET = 0x300,
    TEST_THREAD_STACK_BASE_OFFSET = 0x304,
    TEST_THREAD_STACK_END_OFFSET = 0x308,
    TEST_THREAD_STATE_READY = 1,
    TEST_THREAD_STATE_RUNNING = 2,
    TEST_THREAD_STATE_MORIBUND = 8
};

static __thread PorpoisePpcState *bound_export_state;
static unsigned int bind_count;
static unsigned int unbind_count;
static unsigned int child_entry_count;
static unsigned int child_resume_count;
static int corrupt_main_on_carrier_bind;
static uint32_t resume_guest_thread = TEST_CHILD_THREAD;

static void write_be_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8U);
    destination[1] = (uint8_t)value;
}

static void write_be_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24U);
    destination[1] = (uint8_t)(value >> 16U);
    destination[2] = (uint8_t)(value >> 8U);
    destination[3] = (uint8_t)value;
}

static uint16_t read_be_u16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8U) | source[1]);
}

static uint32_t read_be_u32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24U) |
           ((uint32_t)source[1] << 16U) |
           ((uint32_t)source[2] << 8U) |
           source[3];
}

static void test_bind_export_state(PorpoisePpcState *state)
{
    bound_export_state = state;
    if (state == NULL) {
        unbind_count++;
    } else {
        CHECK(!PorpoiseStubInterruptsEnabled());
        bind_count++;
        if (corrupt_main_on_carrier_bind) {
            uint8_t waiting_state[2];
            write_be_u16(waiting_state, 4U);
            CHECK(state->host->write_bytes(
                      state->host->context,
                      TEST_MAIN_THREAD + TEST_THREAD_STATE_OFFSET,
                      waiting_state,
                      sizeof(waiting_state)) == PORPOISE_HOST_OK);
            corrupt_main_on_carrier_bind = 0;
        }
    }
}

static int test_dispatch(
    PorpoisePpcState *state,
    uint32_t guest_function_address)
{
    CHECK(state != NULL);
    CHECK(!PorpoiseStubInterruptsEnabled());

    if (guest_function_address == TEST_MAIN_RESUME) {
        CHECK(bound_export_state == NULL);
        state->gpr[3] = resume_guest_thread;
        porpoise_libporpoise_os_resume_thread_adapter(state);
        return !porpoise_state_has_fault(state);
    }
    if (guest_function_address == TEST_CHILD_ENTRY) {
        CHECK(bound_export_state == state);
        CHECK(child_entry_count == 0U);
        CHECK(state->srr0 == TEST_CHILD_ENTRY);
        CHECK(state->srr1 == PORPOISE_MSR_EE);
        child_entry_count++;

        porpoise_libporpoise_os_get_current_thread_adapter(state);
        CHECK(!porpoise_state_has_fault(state));
        CHECK(state->gpr[3] == TEST_CHILD_THREAD);

        state->lr = TEST_CHILD_CONTINUATION;
        state->gpr[3] = TEST_CHILD_THREAD;
        porpoise_libporpoise_os_suspend_thread_adapter(state);
        CHECK(!porpoise_state_has_fault(state));
        CHECK(state->gpr[3] == 0U);
        CHECK(state->srr0 == TEST_CHILD_CONTINUATION);
        CHECK(state->srr1 == PORPOISE_MSR_EE);
        CHECK(bound_export_state == state);
        child_resume_count++;

        porpoise_libporpoise_os_get_current_thread_adapter(state);
        CHECK(!porpoise_state_has_fault(state));
        CHECK(state->gpr[3] == TEST_CHILD_THREAD);
        state->gpr[3] = TEST_RETURN_VALUE;
        porpoise_libporpoise_os_exit_thread_adapter(state);
        CHECK(!porpoise_state_has_fault(state));
        CHECK(state->status == PORPOISE_EXECUTION_RETURNED);
        return 1;
    }
    return 0;
}

static void store_bytes(
    PorpoiseHostAdapter *host,
    uint32_t address,
    const void *bytes,
    size_t size)
{
    CHECK(host->write_bytes(host->context, address, bytes, size) ==
          PORPOISE_HOST_OK);
}

static void load_bytes(
    PorpoiseHostAdapter *host,
    uint32_t address,
    void *bytes,
    size_t size)
{
    CHECK(host->read_bytes(host->context, address, bytes, size) ==
          PORPOISE_HOST_OK);
}

static void initialize_guest_child(
    PorpoiseHostAdapter *host,
    uint32_t guest_thread,
    uint32_t stack_end,
    uint32_t stack_base)
{
    uint8_t child_thread[TEST_THREAD_SIZE];
    uint8_t word[4];

    memset(child_thread, 0, sizeof(child_thread));
    write_be_u32(child_thread + 4U, stack_base - 8U);
    write_be_u32(
        child_thread + TEST_CONTEXT_LR_OFFSET, TEST_CHILD_EXIT);
    write_be_u32(
        child_thread + TEST_CONTEXT_SRR0_OFFSET, TEST_CHILD_ENTRY);
    write_be_u32(
        child_thread + TEST_CONTEXT_SRR1_OFFSET, PORPOISE_MSR_EE);
    write_be_u16(child_thread + TEST_CONTEXT_STATE_OFFSET, 1U);
    write_be_u16(
        child_thread + TEST_THREAD_STATE_OFFSET,
        TEST_THREAD_STATE_READY);
    write_be_u16(child_thread + TEST_THREAD_ATTR_OFFSET, 0U);
    write_be_u32(child_thread + TEST_THREAD_SUSPEND_OFFSET, 1U);
    write_be_u32(child_thread + TEST_THREAD_PRIORITY_OFFSET, 8U);
    write_be_u32(child_thread + TEST_THREAD_BASE_PRIORITY_OFFSET, 8U);
    write_be_u32(
        child_thread + TEST_THREAD_STACK_BASE_OFFSET,
        stack_base);
    write_be_u32(
        child_thread + TEST_THREAD_STACK_END_OFFSET,
        stack_end);
    /* OSCreateThread links even a suspended non-detached thread into the
     * active-thread queue. Carrier transitions must preserve these links. */
    write_be_u32(
        child_thread + TEST_THREAD_ACTIVE_PREVIOUS_OFFSET,
        TEST_MAIN_THREAD);
    store_bytes(host, guest_thread, child_thread, sizeof(child_thread));

    write_be_u32(word, TEST_STACK_MAGIC);
    store_bytes(host, stack_end, word, sizeof(word));
}

static void initialize_guest_threads(PorpoiseHostAdapter *host)
{
    uint8_t main_thread[TEST_THREAD_SIZE];
    uint8_t word[4];

    memset(main_thread, 0, sizeof(main_thread));
    write_be_u16(
        main_thread + TEST_THREAD_STATE_OFFSET,
        TEST_THREAD_STATE_RUNNING);
    write_be_u32(main_thread + TEST_THREAD_SUSPEND_OFFSET, 0U);
    write_be_u32(main_thread + TEST_THREAD_PRIORITY_OFFSET, 16U);
    write_be_u32(main_thread + TEST_THREAD_BASE_PRIORITY_OFFSET, 16U);
    store_bytes(host, TEST_MAIN_THREAD, main_thread, sizeof(main_thread));
    initialize_guest_child(
        host,
        TEST_CHILD_THREAD,
        TEST_CHILD_STACK_END,
        TEST_CHILD_STACK_BASE);

    write_be_u32(word, TEST_MAIN_THREAD);
    store_bytes(host, TEST_CURRENT_CONTEXT, word, sizeof(word));
    store_bytes(host, TEST_CURRENT_THREAD, word, sizeof(word));
    write_be_u32(word, 0U);
    store_bytes(host, TEST_CURRENT_FPU_CONTEXT, word, sizeof(word));
}

static void check_guest_word(
    PorpoiseHostAdapter *host,
    uint32_t address,
    uint32_t expected)
{
    uint8_t bytes[4];
    load_bytes(host, address, bytes, sizeof(bytes));
    CHECK(read_be_u32(bytes) == expected);
}

int main(void)
{
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    uint8_t child_thread[TEST_THREAD_SIZE];
    uint8_t word[4];
    uint64_t preserved_fpr0_lane0;
    uint64_t preserved_fpr0_lane1;
    uint32_t preserved_fpscr;

    memset(&host, 0, sizeof(host));
    CHECK(porpoise_libporpoise_has_host_thread_carrier_v1());
    PorpoiseStubDispatchReset();
    PorpoiseThreadCarrierStubResetObservers();
    CHECK(PorpoiseStubDispatchAddAddress(TEST_MAIN_RESUME));
    CHECK(PorpoiseStubDispatchAddAddress(TEST_CHILD_ENTRY));
    CHECK(PorpoiseStubDispatchAddAddress(TEST_CHILD_EXIT));
    CHECK(PorpoiseStubDispatchAddAddress(TEST_CHILD_CONTINUATION));
    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    CHECK(porpoise_libporpoise_bind_guest_runtime(
              &host, test_dispatch, test_bind_export_state) ==
          PORPOISE_HOST_OK);
    initialize_guest_threads(&host);

    porpoise_state_init(&state, &host);
    state.status = PORPOISE_EXECUTION_RUNNING;
    state.pc = TEST_MAIN_RESUME;
    state.srr0 = TEST_MAIN_SRR0;
    state.srr1 = TEST_MAIN_SRR1;
    state.fpr[0].lane_bits[0] = UINT64_C(0x3FF0000000000000);
    state.fpr[0].lane_bits[1] = UINT64_C(0x4000000000000000);
    state.fpscr = UINT32_C(0x12345678);
    preserved_fpr0_lane0 = state.fpr[0].lane_bits[0];
    preserved_fpr0_lane1 = state.fpr[0].lane_bits[1];
    preserved_fpscr = state.fpscr;

    /* A different non-null FPU owner is incoherent at initial main-thread
     * binding and must fail without changing either guest word. */
    write_be_u32(word, TEST_CHILD_THREAD);
    store_bytes(&host, TEST_CURRENT_FPU_CONTEXT, word, sizeof(word));
    CHECK(porpoise_libporpoise_bind_guest_main_thread(&state) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(porpoise_state_has_fault(&state));
    CHECK(strstr(
              porpoise_state_fault_message(&state),
              "conflicting owner") != NULL);
    check_guest_word(&host, TEST_CURRENT_FPU_CONTEXT, TEST_CHILD_THREAD);
    check_guest_word(
        &host,
        TEST_MAIN_THREAD + TEST_CONTEXT_SRR1_OFFSET,
        0U);
    CHECK((state.msr & PORPOISE_MSR_FP) == 0U);
    porpoise_state_clear_fault(&state);
    state.status = PORPOISE_EXECUTION_RUNNING;
    write_be_u32(word, 0U);
    store_bytes(&host, TEST_CURRENT_FPU_CONTEXT, word, sizeof(word));

    CHECK(porpoise_libporpoise_bind_guest_main_thread(&state) ==
          PORPOISE_HOST_OK);
    CHECK((state.msr & PORPOISE_MSR_FP) != 0U);
    CHECK(state.fpr[0].lane_bits[0] == preserved_fpr0_lane0);
    CHECK(state.fpr[0].lane_bits[1] == preserved_fpr0_lane1);
    CHECK(state.fpscr == preserved_fpscr);
    check_guest_word(&host, TEST_CURRENT_FPU_CONTEXT, TEST_MAIN_THREAD);
    check_guest_word(
        &host,
        TEST_MAIN_THREAD + TEST_CONTEXT_SRR1_OFFSET,
        PORPOISE_MSR_FP);
    porpoise_libporpoise_os_get_current_thread_adapter(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == TEST_MAIN_THREAD);

    CHECK(porpoise_libporpoise_run_guest(&state, TEST_MAIN_RESUME));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == 1U);
    CHECK(child_entry_count == 1U);
    CHECK(child_resume_count == 0U);
    CHECK(PorpoiseThreadCarrierStubCreateCount() == 1U);
    CHECK(PorpoiseThreadCarrierStubResumeCount() == 1U);
    check_guest_word(&host, TEST_CURRENT_CONTEXT, TEST_MAIN_THREAD);
    check_guest_word(&host, TEST_CURRENT_THREAD, TEST_MAIN_THREAD);
    load_bytes(
        &host, TEST_CHILD_THREAD, child_thread, sizeof(child_thread));
    CHECK(read_be_u16(child_thread + TEST_THREAD_STATE_OFFSET) ==
          TEST_THREAD_STATE_READY);
    CHECK(read_be_u32(child_thread + TEST_THREAD_SUSPEND_OFFSET) == 1U);
    CHECK(read_be_u32(child_thread + TEST_CONTEXT_SRR0_OFFSET) ==
          TEST_CHILD_CONTINUATION);

    state.srr0 = TEST_MAIN_SECOND_SRR0;
    state.srr1 = TEST_MAIN_SECOND_SRR1;
    CHECK(porpoise_libporpoise_run_guest(&state, TEST_MAIN_RESUME));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == 1U);
    CHECK(child_entry_count == 1U);
    CHECK(child_resume_count == 1U);
    CHECK(PorpoiseThreadCarrierStubResumeCount() == 2U);
    CHECK(PorpoiseThreadCarrierStubSuspendCount() == 1U);
    check_guest_word(&host, TEST_CURRENT_CONTEXT, TEST_MAIN_THREAD);
    check_guest_word(&host, TEST_CURRENT_THREAD, TEST_MAIN_THREAD);
    load_bytes(
        &host, TEST_CHILD_THREAD, child_thread, sizeof(child_thread));
    CHECK(read_be_u16(child_thread + TEST_THREAD_STATE_OFFSET) ==
          TEST_THREAD_STATE_MORIBUND);
    CHECK(read_be_u32(child_thread + TEST_THREAD_SUSPEND_OFFSET) == 0U);
    CHECK(read_be_u32(child_thread + TEST_THREAD_VALUE_OFFSET) ==
          TEST_RETURN_VALUE);
    CHECK(read_be_u32(child_thread + TEST_THREAD_ACTIVE_NEXT_OFFSET) == 0U);
    CHECK(read_be_u32(child_thread + TEST_THREAD_ACTIVE_PREVIOUS_OFFSET) ==
          TEST_MAIN_THREAD);
    CHECK(bind_count == 1U);
    CHECK(unbind_count == 1U);

    /* A callback-side handoff failure happens after the main thread has
     * precommitted the target RUNNING/0. Verify cleanup terminalizes that
     * target even though the low-memory owner words never left main. */
    initialize_guest_child(
        &host,
        TEST_FAULT_CHILD_THREAD,
        TEST_FAULT_CHILD_STACK_END,
        TEST_FAULT_CHILD_STACK_BASE);
    resume_guest_thread = TEST_FAULT_CHILD_THREAD;
    corrupt_main_on_carrier_bind = 1;
    CHECK(!porpoise_libporpoise_run_guest(&state, TEST_MAIN_RESUME));
    CHECK(porpoise_state_has_fault(&state));
    check_guest_word(&host, TEST_CURRENT_CONTEXT, TEST_MAIN_THREAD);
    check_guest_word(&host, TEST_CURRENT_THREAD, TEST_MAIN_THREAD);
    load_bytes(
        &host,
        TEST_FAULT_CHILD_THREAD,
        child_thread,
        sizeof(child_thread));
    CHECK(read_be_u16(child_thread + TEST_THREAD_STATE_OFFSET) ==
          TEST_THREAD_STATE_MORIBUND);
    load_bytes(&host, TEST_MAIN_THREAD, child_thread, sizeof(child_thread));
    CHECK(read_be_u16(child_thread + TEST_THREAD_STATE_OFFSET) ==
          TEST_THREAD_STATE_RUNNING);
    CHECK(bind_count == 2U);
    CHECK(unbind_count == 2U);

    porpoise_libporpoise_adapter_shutdown(&host);
    CHECK(host.context == NULL);
    CHECK(PorpoiseThreadCarrierStubStopCount() == 2U);
    CHECK(PorpoiseThreadCarrierStubJoinCount() == 2U);
    CHECK(PorpoiseThreadCarrierStubDestroyCount() == 2U);
    return 0;
}
