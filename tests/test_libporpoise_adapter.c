#include "porpoise_libporpoise_builtins_private.h"

#include <dolphin/vi.h>
#include <porpoise/stub.h>

#include <pthread.h>
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

#define TEST_GUEST_PATH UINT32_C(0x80010000)
#define TEST_GUEST_LONG_PATH UINT32_C(0x80011000)
#define TEST_GUEST_FILE_CANARY UINT32_C(0x80012000)
#define TEST_GUEST_FILE_INFO UINT32_C(0x80012004)
#define TEST_GUEST_READ_DESTINATION UINT32_C(0x80013003)
#define TEST_GUEST_COMMAND_BLOCK UINT32_C(0x80014000)
#define TEST_GUEST_MIRROR_BASE UINT32_C(0x80020000)
#define TEST_GUEST_MIRROR_STRIDE UINT32_C(0x40)
#define TEST_GUEST_RENDER_MODE UINT32_C(0x80015000)
#define TEST_GX_FIFO_ADDRESS UINT32_C(0xCC008000)
#define TEST_SYSTEM_CALL_VECTOR UINT32_C(0x80000C00)
#define TEST_SYSTEM_CALL_INSTRUCTION UINT32_C(0x803D8740)
#define TEST_GUEST_DISPATCH_OUTER UINT32_C(0x80008000)
#define TEST_GUEST_DISPATCH_INNER UINT32_C(0x80008004)
#define TEST_GUEST_DISPATCH_FAULT UINT32_C(0x80008008)
#define TEST_GUEST_DISPATCH_RETURNED UINT32_C(0x8000800C)
#define TEST_GUEST_DISPATCH_CONTENTION_FIRST UINT32_C(0x80008010)
#define TEST_GUEST_DISPATCH_CONTENTION_SECOND UINT32_C(0x80008014)
#define TEST_GUEST_OS_ARENA_LO UINT32_C(0x80002100)
#define TEST_GUEST_OS_ARENA_HI UINT32_C(0x80002104)
#define TEST_GUEST_OS_INITIALIZED UINT32_C(0x80002108)
#define TEST_GUEST_OS_BOOT_INFO UINT32_C(0x8000210C)
#define TEST_GUEST_OS_BI2_DEBUG_FLAG UINT32_C(0x80002110)
#define TEST_GUEST_DVD_LONG_FILE_NAME_FLAG UINT32_C(0x80002114)
#define TEST_GUEST_PAD_STATUS UINT32_C(0x80016000)

static const uint8_t test_canonical_system_call_vector[28] = {
    0x7DU, 0x30U, 0xFAU, 0xA6U,
    0x61U, 0x2AU, 0x00U, 0x08U,
    0x7DU, 0x50U, 0xFBU, 0xA6U,
    0x4CU, 0x00U, 0x01U, 0x2CU,
    0x7CU, 0x00U, 0x04U, 0xACU,
    0x7DU, 0x30U, 0xFBU, 0xA6U,
    0x4CU, 0x00U, 0x00U, 0x64U
};

enum {
    TEST_DVD_FILE_INFO_SIZE = 0x3C,
    TEST_DVD_COMMAND_BLOCK_SIZE = 0x30,
    TEST_DVD_STATE_OFFSET = 0x0C,
    TEST_DVD_CURRENT_TRANSFER_OFFSET = 0x1C,
    TEST_DVD_TRANSFERRED_OFFSET = 0x20,
    TEST_DVD_START_OFFSET = 0x30,
    TEST_DVD_LENGTH_OFFSET = 0x34,
    TEST_DVD_CALLBACK_OFFSET = 0x38
};

static uint32_t guest_dispatch_addresses[4];
static unsigned int guest_dispatch_count;
static pthread_mutex_t contention_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t contention_condition = PTHREAD_COND_INITIALIZER;
static unsigned int contention_active_dispatches;
static unsigned int contention_max_active_dispatches;
static int contention_first_entered;
static int contention_first_completed;
static int contention_second_entered;

typedef struct GuestDispatchWorker {
    PorpoisePpcState *state;
    uint32_t guest_function_address;
    int wait_for_first;
    int result;
} GuestDispatchWorker;

static void check_thread_result(int result)
{
    CHECK(result == 0);
}

static void contention_dispatch_enter(void)
{
    contention_active_dispatches++;
    if (contention_active_dispatches > contention_max_active_dispatches) {
        contention_max_active_dispatches = contention_active_dispatches;
    }
}

static int test_guest_dispatch(
    PorpoisePpcState *state,
    uint32_t guest_function_address)
{
    CHECK(state != NULL);
    CHECK(state->host != NULL);
    CHECK(!PorpoiseStubInterruptsEnabled());

    if (guest_function_address == TEST_GUEST_DISPATCH_CONTENTION_FIRST) {
        check_thread_result(pthread_mutex_lock(&contention_mutex));
        contention_dispatch_enter();
        contention_first_entered = 1;
        check_thread_result(pthread_cond_broadcast(&contention_condition));
        check_thread_result(pthread_mutex_unlock(&contention_mutex));

        PorpoiseStubWaitForInterruptWaiter();

        check_thread_result(pthread_mutex_lock(&contention_mutex));
        CHECK(contention_active_dispatches == 1U);
        CHECK(!contention_second_entered);
        contention_first_completed = 1;
        contention_active_dispatches--;
        check_thread_result(pthread_cond_broadcast(&contention_condition));
        check_thread_result(pthread_mutex_unlock(&contention_mutex));
        return 1;
    }
    if (guest_function_address == TEST_GUEST_DISPATCH_CONTENTION_SECOND) {
        check_thread_result(pthread_mutex_lock(&contention_mutex));
        contention_dispatch_enter();
        contention_second_entered = 1;
        CHECK(contention_first_completed);
        contention_active_dispatches--;
        check_thread_result(pthread_mutex_unlock(&contention_mutex));
        return 1;
    }

    CHECK(guest_dispatch_count <
          sizeof(guest_dispatch_addresses) /
              sizeof(guest_dispatch_addresses[0]));
    guest_dispatch_addresses[guest_dispatch_count++] =
        guest_function_address;

    if (guest_function_address == TEST_GUEST_DISPATCH_OUTER) {
        int result = state->host->call_guest(
            state, TEST_GUEST_DISPATCH_INNER);
        CHECK(!PorpoiseStubInterruptsEnabled());
        return result;
    }
    if (guest_function_address == TEST_GUEST_DISPATCH_INNER) {
        state->gpr[3] = UINT32_C(0xC0DEC0DE);
        return 1;
    }
    if (guest_function_address == TEST_GUEST_DISPATCH_FAULT) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            guest_function_address,
            "serialized dispatch fault");
        return 1;
    }
    if (guest_function_address == TEST_GUEST_DISPATCH_RETURNED) {
        state->status = PORPOISE_EXECUTION_RETURNED;
        return 1;
    }
    return 0;
}

static void *run_guest_worker(void *argument)
{
    GuestDispatchWorker *worker = (GuestDispatchWorker *)argument;

    CHECK(worker != NULL);
    if (worker->wait_for_first) {
        check_thread_result(pthread_mutex_lock(&contention_mutex));
        while (!contention_first_entered) {
            check_thread_result(pthread_cond_wait(
                &contention_condition,
                &contention_mutex));
        }
        check_thread_result(pthread_mutex_unlock(&contention_mutex));
    }
    worker->result = porpoise_libporpoise_run_guest(
        worker->state,
        worker->guest_function_address);
    return NULL;
}

static void test_process_wide_guest_dispatch_lock(
    PorpoiseHostAdapter *host)
{
    PorpoisePpcState first_state;
    PorpoisePpcState second_state;
    GuestDispatchWorker first_worker;
    GuestDispatchWorker second_worker;
    pthread_t first_thread;
    pthread_t second_thread;

    porpoise_state_init(&first_state, host);
    porpoise_state_init(&second_state, host);
    first_state.status = PORPOISE_EXECUTION_RUNNING;
    second_state.status = PORPOISE_EXECUTION_RUNNING;

    check_thread_result(pthread_mutex_lock(&contention_mutex));
    contention_active_dispatches = 0U;
    contention_max_active_dispatches = 0U;
    contention_first_entered = 0;
    contention_first_completed = 0;
    contention_second_entered = 0;
    check_thread_result(pthread_mutex_unlock(&contention_mutex));

    memset(&first_worker, 0, sizeof(first_worker));
    first_worker.state = &first_state;
    first_worker.guest_function_address =
        TEST_GUEST_DISPATCH_CONTENTION_FIRST;
    memset(&second_worker, 0, sizeof(second_worker));
    second_worker.state = &second_state;
    second_worker.guest_function_address =
        TEST_GUEST_DISPATCH_CONTENTION_SECOND;
    second_worker.wait_for_first = 1;

    PorpoiseStubInterruptReset();
    check_thread_result(pthread_create(
        &first_thread,
        NULL,
        run_guest_worker,
        &first_worker));
    check_thread_result(pthread_create(
        &second_thread,
        NULL,
        run_guest_worker,
        &second_worker));
    check_thread_result(pthread_join(first_thread, NULL));
    check_thread_result(pthread_join(second_thread, NULL));

    CHECK(first_worker.result);
    CHECK(second_worker.result);
    CHECK(!porpoise_state_has_fault(&first_state));
    CHECK(!porpoise_state_has_fault(&second_state));
    check_thread_result(pthread_mutex_lock(&contention_mutex));
    CHECK(contention_first_entered);
    CHECK(contention_first_completed);
    CHECK(contention_second_entered);
    CHECK(contention_active_dispatches == 0U);
    CHECK(contention_max_active_dispatches == 1U);
    check_thread_result(pthread_mutex_unlock(&contention_mutex));
    CHECK(PorpoiseStubInterruptDisableCount() == 2U);
    CHECK(PorpoiseStubInterruptRestoreCount() == 2U);
    CHECK(PorpoiseStubInterruptDisableTransitionCount() == 2U);
    CHECK(PorpoiseStubInterruptRestoreTransitionCount() == 2U);
    CHECK(PorpoiseStubInterruptsEnabled());
}

static int test_different_guest_dispatch(
    PorpoisePpcState *state,
    uint32_t guest_function_address)
{
    (void)state;
    (void)guest_function_address;
    return 1;
}

static void test_serialized_guest_dispatch(
    PorpoiseHostAdapter *host,
    PorpoiseHostAdapter *second_host,
    PorpoisePpcState *state)
{
    PorpoiseHostAdapter copied_host;
    PorpoisePpcState invalid_state;
    PorpoisePpcState state_before;
    unsigned int dispatch_before;

    CHECK(porpoise_libporpoise_bind_guest_dispatch(
              NULL, test_guest_dispatch) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(porpoise_libporpoise_bind_guest_dispatch(host, NULL) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(porpoise_libporpoise_bind_guest_dispatch(
              host, porpoise_libporpoise_run_guest) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(porpoise_libporpoise_bind_guest_dispatch(
              second_host, test_guest_dispatch) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    copied_host = *host;
    CHECK(porpoise_libporpoise_bind_guest_dispatch(
              &copied_host, test_guest_dispatch) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(copied_host.call_guest == NULL);

    CHECK(!porpoise_libporpoise_run_guest(
        NULL, TEST_GUEST_DISPATCH_OUTER));
    porpoise_state_init(&invalid_state, NULL);
    PorpoiseStubInterruptReset();
    CHECK(!porpoise_libporpoise_run_guest(
        &invalid_state, TEST_GUEST_DISPATCH_OUTER));
    CHECK(invalid_state.fault == PORPOISE_FAULT_NO_HOST_ADAPTER);
    CHECK(PorpoiseStubInterruptDisableCount() == 0U);
    CHECK(PorpoiseStubInterruptRestoreCount() == 0U);
    porpoise_state_init(&invalid_state, &copied_host);
    CHECK(!porpoise_libporpoise_run_guest(
        &invalid_state, TEST_GUEST_DISPATCH_OUTER));
    CHECK(invalid_state.fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(PorpoiseStubInterruptDisableCount() == 0U);
    CHECK(PorpoiseStubInterruptRestoreCount() == 0U);

    PorpoiseStubInterruptReset();
    CHECK(!porpoise_libporpoise_run_guest(
        state, TEST_GUEST_DISPATCH_OUTER));
    CHECK(state->fault == PORPOISE_FAULT_MISSING_HOST_CALLBACK);
    CHECK(state->fault_address == TEST_GUEST_DISPATCH_OUTER);
    CHECK(PorpoiseStubInterruptDisableCount() == 1U);
    CHECK(PorpoiseStubInterruptRestoreCount() == 1U);
    CHECK(PorpoiseStubInterruptsEnabled());
    porpoise_state_clear_fault(state);

    host->call_guest = test_different_guest_dispatch;
    CHECK(porpoise_libporpoise_bind_guest_dispatch(
              host, test_guest_dispatch) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(host->call_guest == test_different_guest_dispatch);
    host->call_guest = NULL;

    CHECK(porpoise_libporpoise_bind_guest_dispatch(
              host, test_guest_dispatch) == PORPOISE_HOST_OK);
    CHECK(host->call_guest == porpoise_libporpoise_run_guest);
    CHECK(porpoise_libporpoise_bind_guest_dispatch(
              host, test_guest_dispatch) == PORPOISE_HOST_OK);
    CHECK(porpoise_libporpoise_bind_guest_dispatch(
              host, test_different_guest_dispatch) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(host->call_guest == porpoise_libporpoise_run_guest);

    guest_dispatch_count = 0U;
    memset(guest_dispatch_addresses, 0, sizeof(guest_dispatch_addresses));
    PorpoiseStubInterruptReset();
    state->status = PORPOISE_EXECUTION_RUNNING;
    CHECK(host->call_guest(state, TEST_GUEST_DISPATCH_OUTER));
    CHECK(guest_dispatch_count == 2U);
    CHECK(guest_dispatch_addresses[0] == TEST_GUEST_DISPATCH_OUTER);
    CHECK(guest_dispatch_addresses[1] == TEST_GUEST_DISPATCH_INNER);
    CHECK(state->gpr[3] == UINT32_C(0xC0DEC0DE));
    CHECK(PorpoiseStubInterruptDisableCount() == 2U);
    CHECK(PorpoiseStubInterruptRestoreCount() == 2U);
    CHECK(PorpoiseStubInterruptDisableTransitionCount() == 1U);
    CHECK(PorpoiseStubInterruptRestoreTransitionCount() == 1U);
    CHECK(PorpoiseStubInterruptsEnabled());

    test_process_wide_guest_dispatch_lock(host);

    PorpoiseStubInterruptReset();
    CHECK(!porpoise_libporpoise_run_guest(
        state, TEST_GUEST_DISPATCH_FAULT));
    CHECK(state->fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
    CHECK(state->fault_address == TEST_GUEST_DISPATCH_FAULT);
    CHECK(strcmp(
              porpoise_state_fault_message(state),
              "serialized dispatch fault") == 0);
    CHECK(PorpoiseStubInterruptDisableCount() == 1U);
    CHECK(PorpoiseStubInterruptRestoreCount() == 1U);
    CHECK(PorpoiseStubInterruptsEnabled());
    porpoise_state_clear_fault(state);

    state->status = PORPOISE_EXECUTION_RUNNING;
    PorpoiseStubInterruptReset();
    CHECK(!porpoise_libporpoise_run_guest(
        state, TEST_GUEST_DISPATCH_RETURNED));
    CHECK(state->status == PORPOISE_EXECUTION_RETURNED);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubInterruptDisableCount() == 1U);
    CHECK(PorpoiseStubInterruptRestoreCount() == 1U);
    CHECK(PorpoiseStubInterruptsEnabled());

    state->status = PORPOISE_EXECUTION_RUNNING;
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_INVALID_STATE,
        UINT32_C(0x81234560),
        "preexisting dispatch fault");
    state_before = *state;
    dispatch_before = guest_dispatch_count;
    PorpoiseStubInterruptReset();
    CHECK(!porpoise_libporpoise_run_guest(
        state, TEST_GUEST_DISPATCH_OUTER));
    CHECK(memcmp(state, &state_before, sizeof(*state)) == 0);
    CHECK(guest_dispatch_count == dispatch_before);
    CHECK(PorpoiseStubInterruptDisableCount() == 0U);
    CHECK(PorpoiseStubInterruptRestoreCount() == 0U);
    porpoise_state_clear_fault(state);

    state->status = PORPOISE_EXECUTION_RUNNING;
    host->call_guest = test_different_guest_dispatch;
    PorpoiseStubInterruptReset();
    dispatch_before = guest_dispatch_count;
    CHECK(!porpoise_libporpoise_run_guest(
        state, TEST_GUEST_DISPATCH_OUTER));
    CHECK(state->fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(state->fault_address == TEST_GUEST_DISPATCH_OUTER);
    CHECK(guest_dispatch_count == dispatch_before);
    CHECK(PorpoiseStubInterruptDisableCount() == 1U);
    CHECK(PorpoiseStubInterruptRestoreCount() == 1U);
    CHECK(PorpoiseStubInterruptsEnabled());
    host->call_guest = porpoise_libporpoise_run_guest;
    porpoise_state_clear_fault(state);
    state->status = PORPOISE_EXECUTION_READY;
}

static void fill_guest_bytes(
    PorpoisePpcState *state,
    uint32_t guest_address,
    uint8_t value,
    size_t size)
{
    uint8_t bytes[1024];

    CHECK(size <= sizeof(bytes));
    memset(bytes, value, size);
    CHECK(porpoise_store_bytes(state, guest_address, bytes, size));
}

static void store_guest_string(
    PorpoisePpcState *state,
    uint32_t guest_address,
    const char *value)
{
    size_t size = strlen(value) + 1U;

    CHECK(porpoise_store_bytes(
        state,
        guest_address,
        (const uint8_t *)value,
        size));
}

static PorpoiseHostReadBytesFn system_call_base_read_bytes;
static PorpoiseHostResult system_call_injected_read_result;
static unsigned int system_call_read_count;

static PorpoiseHostResult injected_system_call_read_bytes(
    void *context,
    uint32_t guest_address,
    void *destination,
    size_t size)
{
    system_call_read_count++;
    if (system_call_injected_read_result != PORPOISE_HOST_OK) {
        return system_call_injected_read_result;
    }
    return system_call_base_read_bytes(
        context, guest_address, destination, size);
}

static void store_canonical_system_call_vector(PorpoisePpcState *state)
{
    static const uint32_t words[7] = {
        UINT32_C(0x7D30FAA6),
        UINT32_C(0x612A0008),
        UINT32_C(0x7D50FBA6),
        UINT32_C(0x4C00012C),
        UINT32_C(0x7C0004AC),
        UINT32_C(0x7D30FBA6),
        UINT32_C(0x4C000064)
    };
    size_t index;

    for (index = 0U; index < sizeof(words) / sizeof(words[0]); index++) {
        porpoise_store_u32(
            state,
            TEST_SYSTEM_CALL_VECTOR + (uint32_t)(index * sizeof(uint32_t)),
            words[index]);
    }
    CHECK(!porpoise_state_has_fault(state));
}

static void check_canonical_system_call_vector(
    PorpoiseHostAdapter *host)
{
    uint8_t actual[sizeof(test_canonical_system_call_vector)];

    CHECK(host != NULL);
    CHECK(host->read_bytes != NULL);
    CHECK(host->read_bytes(
              host->context,
              TEST_SYSTEM_CALL_VECTOR,
              actual,
              sizeof(actual)) == PORPOISE_HOST_OK);
    CHECK(memcmp(
              actual,
              test_canonical_system_call_vector,
              sizeof(actual)) == 0);
}

static void test_system_call_adapter(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    PorpoisePpcState state_before;
    PorpoiseHostReadBytesFn adapter_read_bytes;

    state->gpr[3] = UINT32_C(0x11223344);
    state->hid0 = UINT32_C(0x55667788);
    state->msr = UINT32_C(0x00002000);
    state_before = *state;
    CHECK(porpoise_system_call_event(
        state, TEST_SYSTEM_CALL_INSTRUCTION));
    CHECK(memcmp(state, &state_before, sizeof(*state)) == 0);

    porpoise_store_u32(
        state,
        TEST_SYSTEM_CALL_VECTOR + UINT32_C(0x0C),
        UINT32_C(0x60000000));
    CHECK(!porpoise_state_has_fault(state));
    state_before = *state;
    CHECK(!porpoise_system_call_event(
        state, TEST_SYSTEM_CALL_INSTRUCTION));
    CHECK(state->fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
    CHECK(state->fault_address == TEST_SYSTEM_CALL_INSTRUCTION);
    CHECK(strcmp(
              porpoise_state_fault_message(state),
              "guest system-call vector word 3 at 0x80000C0C is "
              "0x60000000; expected 0x4C00012C") == 0);
    CHECK(state->gpr[3] == state_before.gpr[3]);
    CHECK(state->hid0 == state_before.hid0);
    CHECK(state->msr == state_before.msr);
    porpoise_state_clear_fault(state);
    CHECK(porpoise_load_u32(
              state,
              TEST_SYSTEM_CALL_VECTOR + UINT32_C(0x0C)) ==
          UINT32_C(0x60000000));
    store_canonical_system_call_vector(state);

    adapter_read_bytes = host->read_bytes;
    system_call_base_read_bytes = adapter_read_bytes;
    host->read_bytes = injected_system_call_read_bytes;

    system_call_injected_read_result = PORPOISE_HOST_UNMAPPED_ADDRESS;
    system_call_read_count = 0U;
    state_before = *state;
    CHECK(!porpoise_system_call_event(
        state, TEST_SYSTEM_CALL_INSTRUCTION + UINT32_C(4)));
    CHECK(system_call_read_count == 1U);
    CHECK(state->fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
    CHECK(state->fault_address ==
          TEST_SYSTEM_CALL_INSTRUCTION + UINT32_C(4));
    CHECK(state->gpr[3] == state_before.gpr[3]);
    CHECK(state->hid0 == state_before.hid0);
    porpoise_state_clear_fault(state);

    system_call_injected_read_result = PORPOISE_HOST_UNSUPPORTED_MMIO;
    system_call_read_count = 0U;
    state_before = *state;
    CHECK(!porpoise_system_call_event(
        state, TEST_SYSTEM_CALL_INSTRUCTION + UINT32_C(8)));
    CHECK(system_call_read_count == 1U);
    CHECK(state->fault == PORPOISE_FAULT_UNSUPPORTED_MMIO);
    CHECK(state->fault_address ==
          TEST_SYSTEM_CALL_INSTRUCTION + UINT32_C(8));
    CHECK(state->gpr[3] == state_before.gpr[3]);
    CHECK(state->hid0 == state_before.hid0);
    porpoise_state_clear_fault(state);

    system_call_injected_read_result = PORPOISE_HOST_IO_ERROR;
    system_call_read_count = 0U;
    state_before = *state;
    CHECK(!porpoise_system_call_event(
        state, TEST_SYSTEM_CALL_INSTRUCTION + UINT32_C(0x0C)));
    CHECK(system_call_read_count == 1U);
    CHECK(state->fault == PORPOISE_FAULT_HOST_IO);
    CHECK(state->fault_address ==
          TEST_SYSTEM_CALL_INSTRUCTION + UINT32_C(0x0C));
    CHECK(state->gpr[3] == state_before.gpr[3]);
    CHECK(state->hid0 == state_before.hid0);
    porpoise_state_clear_fault(state);

    system_call_read_count = 0U;
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_INVALID_STATE,
        UINT32_C(0x81234560),
        "preexisting fault");
    state_before = *state;
    CHECK(!porpoise_system_call_event(
        state, TEST_SYSTEM_CALL_INSTRUCTION + UINT32_C(0x10)));
    CHECK(system_call_read_count == 0U);
    CHECK(memcmp(state, &state_before, sizeof(*state)) == 0);
    porpoise_state_clear_fault(state);

    system_call_injected_read_result = PORPOISE_HOST_OK;
    system_call_read_count = 0U;
    state_before = *state;
    CHECK(porpoise_system_call_event(
        state, TEST_SYSTEM_CALL_INSTRUCTION + UINT32_C(0x14)));
    CHECK(system_call_read_count == 1U);
    CHECK(memcmp(state, &state_before, sizeof(*state)) == 0);

    host->read_bytes = adapter_read_bytes;
    system_call_base_read_bytes = NULL;
}

static void test_pad_read_adapter(PorpoisePpcState *state)
{
    static const uint8_t expected[48] = {
        0x12U, 0x34U, 0x80U, 0x7FU, 0xFEU, 0x02U,
        0x56U, 0x78U, 0x9AU, 0xBCU, 0xFDU, 0xCCU,
        0xABU, 0xCDU, 0xFFU, 0x01U, 0xC0U, 0x40U,
        0x10U, 0x20U, 0x30U, 0x40U, 0x00U, 0xCCU,
        0x10U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0xFFU, 0xCCU,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0xFEU, 0xCCU
    };
    uint8_t preserved[sizeof(expected)];
    uint8_t actual[sizeof(expected)];
    size_t index;

    PorpoiseStubPADReadReset();
    fill_guest_bytes(
        state, TEST_GUEST_PAD_STATUS, UINT8_C(0xCC), sizeof(expected));
    state->gpr[3] = TEST_GUEST_PAD_STATUS;
    porpoise_libporpoise_pad_read_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(PorpoiseStubPADReadCount() == 1U);
    CHECK(state->gpr[3] == UINT32_C(0xA0000000));
    CHECK(state->host->read_bytes(
              state->host->context,
              TEST_GUEST_PAD_STATUS,
              actual,
              sizeof(actual)) == PORPOISE_HOST_OK);
    CHECK(memcmp(actual, expected, sizeof(expected)) == 0);
    for (index = 0U; index < 4U; index++) {
        CHECK(actual[index * 12U + 11U] == UINT8_C(0xCC));
    }

    memcpy(preserved, actual, sizeof(preserved));
    state->gpr[3] = TEST_GUEST_PAD_STATUS + UINT32_C(1);
    porpoise_libporpoise_pad_read_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(state->gpr[3] == TEST_GUEST_PAD_STATUS + UINT32_C(1));
    CHECK(PorpoiseStubPADReadCount() == 1U);
    CHECK(state->host->read_bytes(
              state->host->context,
              TEST_GUEST_PAD_STATUS,
              actual,
              sizeof(actual)) == PORPOISE_HOST_OK);
    CHECK(memcmp(actual, preserved, sizeof(actual)) == 0);
    porpoise_state_clear_fault(state);

    state->gpr[3] = 0U;
    porpoise_libporpoise_pad_read_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_INVALID_POINTER);
    CHECK(PorpoiseStubPADReadCount() == 1U);
    porpoise_state_clear_fault(state);

    state->gpr[3] = UINT32_C(0x817FFFE0);
    porpoise_libporpoise_pad_read_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
    CHECK(PorpoiseStubPADReadCount() == 1U);
    porpoise_state_clear_fault(state);

    state->gpr[3] = TEST_GX_FIFO_ADDRESS;
    porpoise_libporpoise_pad_read_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_UNSUPPORTED_MMIO);
    CHECK(PorpoiseStubPADReadCount() == 1U);
    porpoise_state_clear_fault(state);

    state->gpr[3] = UINT32_C(0xFFFFFFE0);
    porpoise_libporpoise_pad_read_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_ADDRESS_OVERFLOW);
    CHECK(PorpoiseStubPADReadCount() == 1U);
    porpoise_state_clear_fault(state);
}

int main(void)
{
    PorpoiseHostAdapter host;
    PorpoiseHostAdapter second_host;
    PorpoisePpcState state;
    void *decoded = NULL;
    void *native_pointer = PorpoiseStubNativePointer();
    uint8_t boundary_bytes[2];
    uint8_t invalid_fifo_bytes[3] = {0U, 0U, 0U};
    const GXRenderModeObj *native_render_mode;
    uint32_t token;
    uint32_t repeated_token;
    uint32_t next_generation_token;
    unsigned int token_encodes_before = PorpoiseStubTokenEncodeCount();
    unsigned int releases_before = PorpoiseStubTokenReleaseCount();

    memset(&second_host, 0, sizeof(second_host));
    CHECK(porpoise_libporpoise_adapter_init_for_title(
              &host, "", 1) == PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(porpoise_libporpoise_adapter_init_for_title(
              &host, "adapter-files", 0) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(porpoise_libporpoise_adapter_init_for_title(
              &host, NULL, 2) == PORPOISE_HOST_INVALID_ARGUMENT);
    PorpoiseStubSetSystemCallVectorMapped(0);
    CHECK(porpoise_libporpoise_adapter_init(&host) ==
          PORPOISE_HOST_UNMAPPED_ADDRESS);
    CHECK(host.context == NULL);
    CHECK(host.read_bytes == NULL);
    CHECK(host.write_bytes == NULL);
    CHECK(host.system_call == NULL);
    PorpoiseStubSetSystemCallVectorMapped(1);
    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    CHECK(host.context != NULL);
    CHECK(host.read_bytes != NULL);
    CHECK(host.write_bytes != NULL);
    CHECK(host.write_gx_fifo_u8 != NULL);
    CHECK(host.decode_pointer != NULL);
    CHECK(host.encode_pointer != NULL);
    CHECK(host.read_time_base != NULL);
    CHECK(host.trap == NULL);
    CHECK(host.system_call != NULL);
    check_canonical_system_call_vector(&host);
    CHECK(porpoise_libporpoise_adapter_init(&second_host) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(porpoise_libporpoise_adapter_init(&host) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(porpoise_libporpoise_configure_title_arena(&host, 0U, 0U) ==
          PORPOISE_HOST_OK);
    CHECK(porpoise_libporpoise_configure_title_arena(
              &host, UINT32_C(0x80003000), 0U) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(porpoise_libporpoise_configure_title_arena(
              &host,
              UINT32_C(0x80003000),
              UINT32_C(0x80004000)) == PORPOISE_HOST_OK);

    porpoise_state_init(&state, &host);
    test_serialized_guest_dispatch(&host, &second_host, &state);
    test_system_call_adapter(&host, &state);
    state.gpr[1] = UINT32_C(0x817FF000);
    state.gpr[2] = UINT32_C(0x80001000);
    state.gpr[13] = UINT32_C(0x80002000);
    CHECK(porpoise_state_prepare_title_entry(&state));
    CHECK(state.gpr[1] == UINT32_C(0x817FF000));
    CHECK(state.gpr[2] == UINT32_C(0x80001000));
    CHECK(state.gpr[13] == UINT32_C(0x80002000));
    CHECK(!porpoise_libporpoise_has_host_thread_carrier_v1());

    {
        uint32_t preserved_r3 = UINT32_C(0xA1B2C3D4);
        uint32_t preserved_gprs[32];
        const uint32_t guest_layout_addresses[] = {
            TEST_GUEST_OS_ARENA_LO,
            TEST_GUEST_OS_ARENA_HI,
            TEST_GUEST_OS_INITIALIZED,
            TEST_GUEST_OS_BOOT_INFO,
            TEST_GUEST_OS_BI2_DEBUG_FLAG,
            TEST_GUEST_DVD_LONG_FILE_NAME_FLAG
        };
        PorpoiseLibporpoiseGuestSdkLayoutV1 guest_layout = {
            TEST_GUEST_OS_ARENA_LO,
            TEST_GUEST_OS_ARENA_HI,
            TEST_GUEST_OS_INITIALIZED,
            TEST_GUEST_OS_BOOT_INFO,
            TEST_GUEST_OS_BI2_DEBUG_FLAG,
            TEST_GUEST_DVD_LONG_FILE_NAME_FLAG
        };
        PorpoiseLibporpoiseGuestSdkLayoutV1 changed_layout;
        unsigned int os_init_before = PorpoiseStubOSInitCount();
        size_t layout_index;

        /* A manually authored ABI import has no exact SDK owner/layout and
         * retains the old validate-only behavior. */
        state.gpr[3] = preserved_r3;
        porpoise_libporpoise_os_init_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == preserved_r3);
        CHECK(PorpoiseStubOSInitCount() == os_init_before);

        changed_layout = guest_layout;
        changed_layout.os_initialized_address++;
        CHECK(porpoise_libporpoise_bind_guest_sdk_layout_v1(
                  NULL, &guest_layout) == PORPOISE_HOST_INVALID_ARGUMENT);
        CHECK(porpoise_libporpoise_bind_guest_sdk_layout_v1(
                  &host, NULL) == PORPOISE_HOST_INVALID_ARGUMENT);
        CHECK(porpoise_libporpoise_bind_guest_sdk_layout_v1(
                  &host, &changed_layout) ==
              PORPOISE_HOST_INVALID_ARGUMENT);
        CHECK(porpoise_libporpoise_bind_guest_sdk_layout_v1(
                  &host, &guest_layout) == PORPOISE_HOST_OK);
        CHECK(porpoise_libporpoise_bind_guest_sdk_layout_v1(
                  &host, &guest_layout) == PORPOISE_HOST_OK);
        changed_layout = guest_layout;
        changed_layout.os_initialized_address = UINT32_C(0x80002118);
        CHECK(porpoise_libporpoise_bind_guest_sdk_layout_v1(
                  &host, &changed_layout) ==
              PORPOISE_HOST_INVALID_ARGUMENT);
        for (layout_index = 0U;
             layout_index < sizeof(guest_layout_addresses) /
                                    sizeof(guest_layout_addresses[0]);
             layout_index++) {
            porpoise_store_u32(
                &state,
                guest_layout_addresses[layout_index],
                UINT32_C(0xCCCCCCCC));
        }
        CHECK(!porpoise_state_has_fault(&state));
        memcpy(preserved_gprs, state.gpr, sizeof(preserved_gprs));
        porpoise_libporpoise_os_init_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(memcmp(
                  state.gpr,
                  preserved_gprs,
                  sizeof(preserved_gprs)) == 0);
        CHECK(porpoise_load_u32(&state, TEST_GUEST_OS_ARENA_LO) ==
              UINT32_C(0x80003000));
        CHECK(porpoise_load_u32(&state, TEST_GUEST_OS_ARENA_HI) ==
              UINT32_C(0x80004000));
        CHECK(porpoise_load_u32(&state, TEST_GUEST_OS_INITIALIZED) == 1U);
        CHECK(porpoise_load_u32(&state, TEST_GUEST_OS_BOOT_INFO) ==
              UINT32_C(0x80000000));
        CHECK(porpoise_load_u32(&state, TEST_GUEST_OS_BI2_DEBUG_FLAG) == 0U);
        CHECK(porpoise_load_u32(
                  &state, TEST_GUEST_DVD_LONG_FILE_NAME_FLAG) == 1U);
        CHECK(PorpoiseStubOSInitCount() == os_init_before);

        /* Repeated exact OSInit calls are guest-idempotent just like the SDK
         * AreWeInitialized guard. */
        porpoise_libporpoise_os_init_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(memcmp(
                  state.gpr,
                  preserved_gprs,
                  sizeof(preserved_gprs)) == 0);

        porpoise_libporpoise_dvd_init_adapter(&state);
        CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
        CHECK(strcmp(
                  porpoise_state_fault_message(&state),
                  "DVDInit import requires title-host native DVD initialization") ==
              0);
        CHECK(PorpoiseStubDVDInitCount() == 0U);
        porpoise_state_clear_fault(&state);

        porpoise_libporpoise_vi_init_adapter(&state);
        porpoise_libporpoise_vi_init_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(PorpoiseStubVIInitCount() == 1U);
        CHECK(state.gpr[3] == preserved_r3);

        porpoise_libporpoise_demo_pad_init_adapter(&state);
        porpoise_libporpoise_demo_pad_init_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(PorpoiseStubDEMOPadInitCount() == 1U);
        CHECK(PorpoiseStubPADInitCount() == 1U);
        CHECK(state.gpr[3] == preserved_r3);
    }
    test_pad_read_adapter(&state);
    token = porpoise_encode_pointer(&state, native_pointer);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(token == PorpoiseStubTokenAddress());
    CHECK(PorpoiseStubTokenEncodeCount() == token_encodes_before + 1U);
    CHECK(host.decode_pointer(host.context, token, &decoded) == PORPOISE_HOST_OK);
    CHECK(decoded == native_pointer);

    repeated_token = porpoise_encode_pointer(&state, native_pointer);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(repeated_token == token);
    CHECK(PorpoiseStubTokenEncodeCount() == token_encodes_before + 1U);

    (void)porpoise_load_u8(&state, token);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_POINTER);
    porpoise_state_clear_fault(&state);

    (void)porpoise_load_u8(&state, token + UINT32_C(4));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_POINTER);
    porpoise_state_clear_fault(&state);

    decoded = NULL;
    CHECK(host.decode_pointer(host.context, token + UINT32_C(4), &decoded) ==
          PORPOISE_HOST_INVALID_POINTER);
    CHECK(decoded == NULL);

    CHECK(host.read_bytes(
              host.context,
              UINT32_C(0xAFFFFFFF),
              boundary_bytes,
              sizeof(boundary_bytes)) == PORPOISE_HOST_INVALID_POINTER);

    (void)porpoise_load_u8(&state, UINT32_C(0x88000000));
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_MMIO);
    porpoise_state_clear_fault(&state);

    PorpoiseStubGXFifoReset();
    porpoise_store_u8(&state, TEST_GX_FIFO_ADDRESS, UINT8_C(0x61));
    porpoise_store_gx_fifo_u8(&state, UINT8_C(0x42));
    porpoise_store_u16(&state, TEST_GX_FIFO_ADDRESS, UINT16_C(0xABCD));
    porpoise_store_u32(
        &state, TEST_GX_FIFO_ADDRESS, UINT32_C(0x12345678));
    porpoise_store_f32(&state, TEST_GX_FIFO_ADDRESS, 1.0f);
    porpoise_store_f64(&state, TEST_GX_FIFO_ADDRESS, 1.0);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubGXFifoCallCount() == 0U);
    CHECK(porpoise_libporpoise_gx_flush_pending(&state));
    CHECK(PorpoiseStubGXFifoCallCount() == 1U);
    CHECK(PorpoiseStubGXFifoQueuedCallCount() == 1U);
    CHECK(PorpoiseStubGXFifoSynchronousCallCount() == 0U);
    CHECK(PorpoiseStubGXFifoCallSize(0U) == 20U);
    CHECK(PorpoiseStubGXFifoByteCount() == 20U);
    {
        static const uint8_t expected_fifo_bytes[20] = {
            0x61U,
            0x42U,
            0xABU, 0xCDU,
            0x12U, 0x34U, 0x56U, 0x78U,
            0x3FU, 0x80U, 0x00U, 0x00U,
            0x3FU, 0xF0U, 0x00U, 0x00U,
            0x00U, 0x00U, 0x00U, 0x00U
        };
        size_t index;
        for (index = 0U; index < sizeof(expected_fifo_bytes); index++) {
            CHECK(PorpoiseStubGXFifoByte((unsigned int)index) ==
                  expected_fifo_bytes[index]);
        }
    }
    CHECK(PorpoiseStubGXNumericWriteCount() == 0U);

    CHECK(host.write_bytes(
              host.context,
              TEST_GX_FIFO_ADDRESS,
              invalid_fifo_bytes,
              sizeof(invalid_fifo_bytes)) ==
          PORPOISE_HOST_UNSUPPORTED_MMIO);
    CHECK(PorpoiseStubGXFifoCallCount() == 1U);
    porpoise_store_u8(
        &state, TEST_GX_FIFO_ADDRESS + UINT32_C(1), UINT8_C(0xFF));
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_MMIO);
    CHECK(PorpoiseStubGXFifoCallCount() == 1U);
    porpoise_state_clear_fault(&state);
    (void)porpoise_load_u8(&state, TEST_GX_FIFO_ADDRESS);
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_MMIO);
    CHECK(PorpoiseStubGXFifoCallCount() == 1U);
    porpoise_state_clear_fault(&state);
    PorpoiseStubGXFifoSetAccept(0);
    porpoise_store_u8(&state, TEST_GX_FIFO_ADDRESS, UINT8_C(0x00));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(!porpoise_libporpoise_gx_flush_pending(&state));
    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    CHECK(PorpoiseStubGXFifoCallCount() == 1U);
    CHECK(PorpoiseStubGXNumericWriteCount() == 0U);
    porpoise_state_clear_fault(&state);
    PorpoiseStubGXFifoSetAccept(1);
    CHECK(porpoise_libporpoise_gx_flush_pending(&state));
    CHECK(PorpoiseStubGXFifoCallCount() == 2U);

    CHECK(sizeof(GXRenderModeObj) == 0x3CU);
    fill_guest_bytes(
        &state, TEST_GUEST_RENDER_MODE, UINT8_C(0xEE), 0x3CU);
    porpoise_store_u32(
        &state, TEST_GUEST_RENDER_MODE + UINT32_C(0x00),
        UINT32_C(0x10203040));
    porpoise_store_u16(
        &state, TEST_GUEST_RENDER_MODE + UINT32_C(0x04),
        UINT16_C(0x0506));
    porpoise_store_u16(
        &state, TEST_GUEST_RENDER_MODE + UINT32_C(0x06),
        UINT16_C(0x0708));
    porpoise_store_u16(
        &state, TEST_GUEST_RENDER_MODE + UINT32_C(0x08),
        UINT16_C(0x090A));
    porpoise_store_u16(
        &state, TEST_GUEST_RENDER_MODE + UINT32_C(0x0A),
        UINT16_C(0x0B0C));
    porpoise_store_u16(
        &state, TEST_GUEST_RENDER_MODE + UINT32_C(0x0C),
        UINT16_C(0x0D0E));
    porpoise_store_u16(
        &state, TEST_GUEST_RENDER_MODE + UINT32_C(0x0E),
        UINT16_C(0x0F10));
    porpoise_store_u16(
        &state, TEST_GUEST_RENDER_MODE + UINT32_C(0x10),
        UINT16_C(0x1112));
    porpoise_store_u32(
        &state, TEST_GUEST_RENDER_MODE + UINT32_C(0x14),
        UINT32_C(0x20212223));
    porpoise_store_u8(
        &state, TEST_GUEST_RENDER_MODE + UINT32_C(0x18), UINT8_C(0x24));
    porpoise_store_u8(
        &state, TEST_GUEST_RENDER_MODE + UINT32_C(0x19), UINT8_C(0x25));
    {
        size_t index;
        for (index = 0U; index < 24U; index++) {
            porpoise_store_u8(
                &state,
                TEST_GUEST_RENDER_MODE + UINT32_C(0x1A) +
                    (uint32_t)index,
                (uint8_t)(UINT8_C(0x30) + (uint8_t)index));
        }
        for (index = 0U; index < 7U; index++) {
            porpoise_store_u8(
                &state,
                TEST_GUEST_RENDER_MODE + UINT32_C(0x32) +
                    (uint32_t)index,
                (uint8_t)(UINT8_C(0x60) + (uint8_t)index));
        }
    }
    CHECK(!porpoise_state_has_fault(&state));
    PorpoiseStubVIReset();
    state.gpr[3] = TEST_GUEST_RENDER_MODE;
    porpoise_libporpoise_vi_configure_adapter(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == TEST_GUEST_RENDER_MODE);
    CHECK(PorpoiseStubVIConfigureCount() == 1U);
    native_render_mode = (const GXRenderModeObj *)
        PorpoiseStubVILastRenderMode();
    CHECK((uint32_t)native_render_mode->viTVmode ==
          UINT32_C(0x10203040));
    CHECK(native_render_mode->fbWidth == UINT16_C(0x0506));
    CHECK(native_render_mode->efbHeight == UINT16_C(0x0708));
    CHECK(native_render_mode->xfbHeight == UINT16_C(0x090A));
    CHECK(native_render_mode->viXOrigin == UINT16_C(0x0B0C));
    CHECK(native_render_mode->viYOrigin == UINT16_C(0x0D0E));
    CHECK(native_render_mode->viWidth == UINT16_C(0x0F10));
    CHECK(native_render_mode->viHeight == UINT16_C(0x1112));
    CHECK((uint32_t)native_render_mode->xFBmode ==
          UINT32_C(0x20212223));
    CHECK(native_render_mode->field_rendering == UINT8_C(0x24));
    CHECK(native_render_mode->aa == UINT8_C(0x25));
    {
        size_t index;
        for (index = 0U; index < 24U; index++) {
            CHECK(native_render_mode->sample_pattern[index / 2U]
                                                     [index % 2U] ==
                  (uint8_t)(UINT8_C(0x30) + (uint8_t)index));
        }
        for (index = 0U; index < 7U; index++) {
            CHECK(native_render_mode->vfilter[index] ==
                  (uint8_t)(UINT8_C(0x60) + (uint8_t)index));
        }
    }

    state.gpr[3] = 0U;
    porpoise_libporpoise_vi_configure_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_POINTER);
    CHECK(PorpoiseStubVIConfigureCount() == 1U);
    porpoise_state_clear_fault(&state);
    state.gpr[3] = TEST_GUEST_RENDER_MODE + UINT32_C(2);
    porpoise_libporpoise_vi_configure_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(PorpoiseStubVIConfigureCount() == 1U);
    porpoise_state_clear_fault(&state);
    state.gpr[3] = UINT32_C(0x817FFFE0);
    porpoise_libporpoise_vi_configure_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
    CHECK(PorpoiseStubVIConfigureCount() == 1U);
    porpoise_state_clear_fault(&state);
    state.gpr[3] = TEST_GX_FIFO_ADDRESS;
    porpoise_libporpoise_vi_configure_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_MMIO);
    CHECK(PorpoiseStubVIConfigureCount() == 1U);
    porpoise_state_clear_fault(&state);

    porpoise_store_u32(
        &state,
        TEST_SYSTEM_CALL_VECTOR + UINT32_C(0x18),
        UINT32_C(0));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(porpoise_load_u32(
              &state,
              TEST_SYSTEM_CALL_VECTOR + UINT32_C(0x18)) == 0U);
    porpoise_libporpoise_adapter_shutdown(&host);
    CHECK(PorpoiseStubTokenReleaseCount() == releases_before + 1U);
    CHECK(host.context == NULL);
    CHECK(host.read_bytes == NULL);
    CHECK(host.decode_pointer == NULL);
    porpoise_libporpoise_adapter_shutdown(&host);
    CHECK(PorpoiseStubTokenReleaseCount() == releases_before + 1U);

    CHECK(porpoise_libporpoise_adapter_init_for_title(
              &host, "adapter-files", 1) == PORPOISE_HOST_OK);
    check_canonical_system_call_vector(&host);
    CHECK(PorpoiseStubDVDInitCount() == 1U);
    CHECK(strcmp(PorpoiseStubDVDRoot(), "adapter-files") == 0);
    porpoise_state_init(&state, &host);
    porpoise_libporpoise_os_init_adapter(&state);
    porpoise_libporpoise_dvd_init_adapter(&state);
    porpoise_libporpoise_vi_init_adapter(&state);
    porpoise_libporpoise_demo_pad_init_adapter(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubOSInitCount() == 1U);
    CHECK(PorpoiseStubDVDInitCount() == 1U);
    CHECK(PorpoiseStubVIInitCount() == 1U);
    CHECK(PorpoiseStubDEMOPadInitCount() == 1U);
    CHECK(PorpoiseStubPADInitCount() == 1U);
    next_generation_token = porpoise_encode_pointer(&state, native_pointer);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(next_generation_token == PorpoiseStubTokenAddress());
    CHECK(next_generation_token != token);
    CHECK(PorpoiseStubTokenEncodeCount() == token_encodes_before + 2U);
    decoded = NULL;
    CHECK(host.decode_pointer(host.context, token, &decoded) ==
          PORPOISE_HOST_INVALID_POINTER);
    CHECK(decoded == NULL);
    CHECK(host.decode_pointer(
              host.context,
              next_generation_token,
              &decoded) == PORPOISE_HOST_OK);
    CHECK(decoded == native_pointer);

    {
        unsigned int convert_before;
        unsigned int open_before;
        unsigned int fast_open_before;
        unsigned int read_before;
        unsigned int close_before;
        unsigned int cancel_before;
        const void *first_native_file_info;
        const void *capacity_first_native_file_info = NULL;
        void *guest_file_info_pointer = NULL;
        size_t index;

        store_guest_string(&state, TEST_GUEST_PATH, "/test.bin");
        convert_before = PorpoiseStubDVDConvertCallCount();
        state.gpr[3] = TEST_GUEST_PATH;
        porpoise_libporpoise_dvd_convert_path_to_entry_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == UINT32_C(7));
        CHECK(PorpoiseStubDVDConvertCallCount() == convert_before + 1U);

        fill_guest_bytes(
            &state, TEST_GUEST_LONG_PATH, (uint8_t)'x', 1024U);
        convert_before = PorpoiseStubDVDConvertCallCount();
        state.gpr[3] = TEST_GUEST_LONG_PATH;
        porpoise_libporpoise_dvd_convert_path_to_entry_adapter(&state);
        CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
        CHECK(PorpoiseStubDVDConvertCallCount() == convert_before);
        porpoise_state_clear_fault(&state);

        porpoise_store_u8(&state, UINT32_C(0x817FFFFF), (uint8_t)'x');
        CHECK(!porpoise_state_has_fault(&state));
        convert_before = PorpoiseStubDVDConvertCallCount();
        state.gpr[3] = UINT32_C(0x817FFFFF);
        porpoise_libporpoise_dvd_convert_path_to_entry_adapter(&state);
        CHECK(state.fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
        CHECK(PorpoiseStubDVDConvertCallCount() == convert_before);
        porpoise_state_clear_fault(&state);

        fill_guest_bytes(
            &state,
            TEST_GUEST_FILE_CANARY,
            UINT8_C(0xA5),
            TEST_DVD_FILE_INFO_SIZE + 8U);
        open_before = PorpoiseStubDVDOpenCallCount();
        state.gpr[3] = TEST_GUEST_PATH;
        state.gpr[4] = TEST_GUEST_FILE_INFO;
        porpoise_libporpoise_dvd_open_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == 1U);
        CHECK(PorpoiseStubDVDOpenCallCount() == open_before + 1U);
        CHECK(PorpoiseStubDVDActiveFileCount() == 1U);
        first_native_file_info = PorpoiseStubDVDLastOpenFileInfo();
        CHECK(first_native_file_info != NULL);
        CHECK(host.decode_pointer(
                  host.context,
                  TEST_GUEST_FILE_INFO,
                  &guest_file_info_pointer) == PORPOISE_HOST_OK);
        CHECK(first_native_file_info != guest_file_info_pointer);
        CHECK(porpoise_load_u32(
                  &state,
                  TEST_GUEST_FILE_INFO + TEST_DVD_STATE_OFFSET) == 0U);
        CHECK(porpoise_load_u32(
                  &state,
                  TEST_GUEST_FILE_INFO + TEST_DVD_START_OFFSET) ==
              UINT32_C(0x00123000));
        CHECK(porpoise_load_u32(
                  &state,
                  TEST_GUEST_FILE_INFO + TEST_DVD_LENGTH_OFFSET) == 70U);
        CHECK(porpoise_load_u32(
                  &state,
                  TEST_GUEST_FILE_INFO + TEST_DVD_CALLBACK_OFFSET) == 0U);
        CHECK(porpoise_load_u8(&state, TEST_GUEST_FILE_CANARY) ==
              UINT8_C(0xA5));
        CHECK(porpoise_load_u8(&state, TEST_GUEST_FILE_INFO) ==
              UINT8_C(0xA5));
        CHECK(porpoise_load_u8(
                  &state,
                  TEST_GUEST_FILE_INFO + TEST_DVD_FILE_INFO_SIZE) ==
              UINT8_C(0xA5));

        open_before = PorpoiseStubDVDOpenCallCount();
        state.gpr[3] = TEST_GUEST_PATH;
        state.gpr[4] = TEST_GUEST_FILE_INFO;
        porpoise_libporpoise_dvd_open_adapter(&state);
        CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
        CHECK(PorpoiseStubDVDOpenCallCount() == open_before);
        porpoise_state_clear_fault(&state);

        porpoise_store_u32(
            &state,
            TEST_GUEST_FILE_INFO + TEST_DVD_STATE_OFFSET,
            UINT32_C(3));
        state.gpr[3] = TEST_GUEST_FILE_INFO;
        porpoise_libporpoise_dvd_get_command_block_status_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == 1U);
        porpoise_store_u32(
            &state,
            TEST_GUEST_FILE_INFO + TEST_DVD_STATE_OFFSET,
            0U);

        fill_guest_bytes(
            &state,
            TEST_GUEST_READ_DESTINATION,
            UINT8_C(0xCC),
            40U);
        read_before = PorpoiseStubDVDReadCallCount();
        state.gpr[3] = TEST_GUEST_FILE_INFO;
        state.gpr[4] = TEST_GUEST_READ_DESTINATION;
        state.gpr[5] = 32U;
        state.gpr[6] = 5U;
        state.gpr[7] = 2U;
        porpoise_libporpoise_dvd_read_prio_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == 32U);
        CHECK(PorpoiseStubDVDReadCallCount() == read_before + 1U);
        CHECK(PorpoiseStubDVDLastReadFileInfo() == first_native_file_info);
        for (index = 0U; index < 32U; index++) {
            CHECK(porpoise_load_u8(
                      &state,
                      TEST_GUEST_READ_DESTINATION + (uint32_t)index) ==
                  PorpoiseStubDVDExpectedByte(5U + (uint32_t)index));
        }
        CHECK(porpoise_load_u8(
                  &state,
                  TEST_GUEST_READ_DESTINATION + 32U) == UINT8_C(0xCC));
        CHECK(porpoise_load_u32(
                  &state,
                  TEST_GUEST_FILE_INFO +
                      TEST_DVD_CURRENT_TRANSFER_OFFSET) == 32U);
        CHECK(porpoise_load_u32(
                  &state,
                  TEST_GUEST_FILE_INFO + TEST_DVD_TRANSFERRED_OFFSET) ==
              32U);

        fill_guest_bytes(
            &state,
            TEST_GUEST_READ_DESTINATION,
            UINT8_C(0xDD),
            8U);
        read_before = PorpoiseStubDVDReadCallCount();
        state.gpr[3] = TEST_GUEST_FILE_INFO;
        state.gpr[4] = TEST_GUEST_READ_DESTINATION;
        state.gpr[5] = 8U;
        state.gpr[6] = 70U;
        state.gpr[7] = 2U;
        porpoise_libporpoise_dvd_read_prio_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == UINT32_MAX);
        CHECK(PorpoiseStubDVDReadCallCount() == read_before + 1U);
        CHECK(porpoise_load_u8(&state, TEST_GUEST_READ_DESTINATION) ==
              UINT8_C(0xDD));
        CHECK(porpoise_load_u32(
                  &state,
                  TEST_GUEST_FILE_INFO +
                      TEST_DVD_CURRENT_TRANSFER_OFFSET) == 0U);

        read_before = PorpoiseStubDVDReadCallCount();
        state.gpr[3] = TEST_GUEST_FILE_INFO;
        state.gpr[4] = UINT32_C(0x817FFFF0);
        state.gpr[5] = 32U;
        state.gpr[6] = 0U;
        state.gpr[7] = 2U;
        porpoise_libporpoise_dvd_read_prio_adapter(&state);
        CHECK(state.fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
        CHECK(PorpoiseStubDVDReadCallCount() == read_before);
        porpoise_state_clear_fault(&state);

        state.gpr[3] = TEST_GUEST_FILE_INFO;
        state.gpr[4] = TEST_GUEST_READ_DESTINATION;
        state.gpr[5] = UINT32_MAX;
        state.gpr[6] = 0U;
        state.gpr[7] = 2U;
        porpoise_libporpoise_dvd_read_prio_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == UINT32_MAX);
        CHECK(PorpoiseStubDVDReadCallCount() == read_before);

        cancel_before = PorpoiseStubDVDCancelCallCount();
        state.gpr[3] = TEST_GUEST_FILE_INFO;
        porpoise_libporpoise_dvd_cancel_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == 0U);
        CHECK(PorpoiseStubDVDCancelCallCount() == cancel_before + 1U);

        close_before = PorpoiseStubDVDCloseCallCount();
        state.gpr[3] = TEST_GUEST_FILE_INFO;
        porpoise_libporpoise_dvd_close_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == 1U);
        CHECK(PorpoiseStubDVDCloseCallCount() == close_before + 1U);
        CHECK(PorpoiseStubDVDActiveFileCount() == 0U);
        state.gpr[3] = TEST_GUEST_FILE_INFO;
        porpoise_libporpoise_dvd_close_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == 0U);
        CHECK(PorpoiseStubDVDCloseCallCount() == close_before + 1U);

        CHECK(porpoise_zero_bytes(
            &state,
            TEST_GUEST_FILE_INFO + UINT32_C(0x80),
            TEST_DVD_FILE_INFO_SIZE));
        fast_open_before = PorpoiseStubDVDFastOpenCallCount();
        state.gpr[3] = 7U;
        state.gpr[4] = TEST_GUEST_FILE_INFO + UINT32_C(0x80);
        porpoise_libporpoise_dvd_fast_open_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == 1U);
        CHECK(PorpoiseStubDVDFastOpenCallCount() == fast_open_before + 1U);
        state.gpr[3] = TEST_GUEST_FILE_INFO + UINT32_C(0x80);
        porpoise_libporpoise_dvd_close_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == 1U);

        CHECK(porpoise_zero_bytes(
            &state,
            TEST_GUEST_COMMAND_BLOCK,
            TEST_DVD_COMMAND_BLOCK_SIZE));
        cancel_before = PorpoiseStubDVDCancelCallCount();
        state.gpr[3] = TEST_GUEST_COMMAND_BLOCK;
        porpoise_libporpoise_dvd_cancel_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == 0U);
        CHECK(PorpoiseStubDVDCancelCallCount() == cancel_before);
        porpoise_store_u32(
            &state,
            TEST_GUEST_COMMAND_BLOCK + TEST_DVD_STATE_OFFSET,
            1U);
        state.gpr[3] = TEST_GUEST_COMMAND_BLOCK;
        porpoise_libporpoise_dvd_cancel_adapter(&state);
        CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
        CHECK(PorpoiseStubDVDCancelCallCount() == cancel_before);
        porpoise_state_clear_fault(&state);

        open_before = PorpoiseStubDVDOpenCallCount();
        for (index = 0U; index < 64U; index++) {
            uint32_t guest_file_info =
                TEST_GUEST_MIRROR_BASE +
                (uint32_t)index * TEST_GUEST_MIRROR_STRIDE;
            CHECK(porpoise_zero_bytes(
                &state, guest_file_info, TEST_DVD_FILE_INFO_SIZE));
            state.gpr[3] = TEST_GUEST_PATH;
            state.gpr[4] = guest_file_info;
            porpoise_libporpoise_dvd_open_adapter(&state);
            CHECK(!porpoise_state_has_fault(&state));
            CHECK(state.gpr[3] == 1U);
            if (index == 0U) {
                capacity_first_native_file_info =
                    PorpoiseStubDVDLastOpenFileInfo();
            }
        }
        CHECK(PorpoiseStubDVDOpenCallCount() == open_before + 64U);
        CHECK(PorpoiseStubDVDActiveFileCount() == 64U);

        CHECK(porpoise_zero_bytes(
            &state,
            TEST_GUEST_MIRROR_BASE + 64U * TEST_GUEST_MIRROR_STRIDE,
            TEST_DVD_FILE_INFO_SIZE));
        state.gpr[3] = TEST_GUEST_PATH;
        state.gpr[4] =
            TEST_GUEST_MIRROR_BASE + 64U * TEST_GUEST_MIRROR_STRIDE;
        porpoise_libporpoise_dvd_open_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == 0U);
        CHECK(PorpoiseStubDVDOpenCallCount() == open_before + 64U);

        state.gpr[3] = TEST_GUEST_MIRROR_BASE;
        state.gpr[4] = TEST_GUEST_READ_DESTINATION;
        state.gpr[5] = 1U;
        state.gpr[6] = 0U;
        state.gpr[7] = 2U;
        porpoise_libporpoise_dvd_read_prio_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == 1U);
        CHECK(PorpoiseStubDVDLastReadFileInfo() ==
              capacity_first_native_file_info);

        state.gpr[3] =
            TEST_GUEST_MIRROR_BASE + 17U * TEST_GUEST_MIRROR_STRIDE;
        porpoise_libporpoise_dvd_close_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == 1U);
        CHECK(PorpoiseStubDVDActiveFileCount() == 63U);
        state.gpr[3] = TEST_GUEST_PATH;
        state.gpr[4] =
            TEST_GUEST_MIRROR_BASE + 64U * TEST_GUEST_MIRROR_STRIDE;
        porpoise_libporpoise_dvd_open_adapter(&state);
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(state.gpr[3] == 1U);
        CHECK(PorpoiseStubDVDActiveFileCount() == 64U);
        CHECK(PorpoiseStubDVDOpenCallCount() == open_before + 65U);

        close_before = PorpoiseStubDVDCloseCallCount();
        porpoise_libporpoise_adapter_shutdown(&host);
        CHECK(PorpoiseStubDVDActiveFileCount() == 0U);
        CHECK(PorpoiseStubDVDCloseCallCount() == close_before + 64U);
    }
    CHECK(PorpoiseStubTokenReleaseCount() == releases_before + 2U);
    CHECK(porpoise_libporpoise_adapter_init_for_title(
              &host, "different-files", 1) ==
          PORPOISE_HOST_INVALID_ARGUMENT);

    return 0;
}
