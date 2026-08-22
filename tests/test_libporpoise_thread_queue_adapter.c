#include "porpoise_libporpoise_builtins_private.h"

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

#define TEST_MEMORY_BASE UINT32_C(0x80004000)
#define TEST_QUEUE_ADDRESS (TEST_MEMORY_BASE + UINT32_C(20))
#define TEST_THREAD_ADDRESS (TEST_MEMORY_BASE + UINT32_C(128))
#define TEST_MMIO_ADDRESS UINT32_C(0xCC000000)

enum {
    TEST_MEMORY_SIZE = 1024,
    TEST_QUEUE_OFFSET = 20,
    TEST_QUEUE_SIZE = 8,
    TEST_THREAD_OFFSET = 128,
    TEST_THREAD_SIZE = 0x318,
    TEST_THREAD_STATE_OFFSET = 0x2C8
};

typedef struct TestMemory {
    uint8_t bytes[TEST_MEMORY_SIZE];
    size_t read_count;
    uint32_t last_read_address;
    size_t last_read_size;
    PorpoiseHostResult forced_result;
} TestMemory;

typedef void (*ThreadAdapter)(PorpoisePpcState *state);

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

static PorpoiseHostResult test_read_bytes(
    void *context,
    uint32_t guest_address,
    void *destination,
    size_t size)
{
    TestMemory *memory = (TestMemory *)context;
    uint64_t end_address;
    uint64_t memory_end;
    size_t offset;

    if (memory == NULL) {
        return PORPOISE_HOST_INVALID_POINTER;
    }
    memory->read_count++;
    memory->last_read_address = guest_address;
    memory->last_read_size = size;

    if (memory->forced_result != PORPOISE_HOST_OK) {
        return memory->forced_result;
    }
    if (destination == NULL) {
        return PORPOISE_HOST_INVALID_POINTER;
    }
    if (size == 0U) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }

    end_address = (uint64_t)guest_address + (uint64_t)size;
    if (end_address > (UINT64_C(1) << 32U)) {
        return PORPOISE_HOST_ADDRESS_OVERFLOW;
    }
    if (guest_address >= TEST_MMIO_ADDRESS &&
        guest_address < TEST_MMIO_ADDRESS + UINT32_C(0x01000000)) {
        return PORPOISE_HOST_UNSUPPORTED_MMIO;
    }

    memory_end = (uint64_t)TEST_MEMORY_BASE + TEST_MEMORY_SIZE;
    if (guest_address < TEST_MEMORY_BASE || end_address > memory_end) {
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    }

    offset = (size_t)(guest_address - TEST_MEMORY_BASE);
    memcpy(destination, memory->bytes + offset, size);
    return PORPOISE_HOST_OK;
}

static void initialize_fixture(
    TestMemory *memory,
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state,
    uint32_t head,
    uint32_t tail)
{
    memset(memory, 0, sizeof(*memory));
    memset(memory->bytes, 0xA5, sizeof(memory->bytes));
    write_be_u32(memory->bytes + TEST_QUEUE_OFFSET, head);
    write_be_u32(
        memory->bytes + TEST_QUEUE_OFFSET + sizeof(uint32_t),
        tail);

    memset(host, 0, sizeof(*host));
    host->context = memory;
    host->read_bytes = test_read_bytes;

    porpoise_state_init(state, host);
    state->status = PORPOISE_EXECUTION_RUNNING;
    state->pc = UINT32_C(0x80008000);
    state->gpr[3] = TEST_QUEUE_ADDRESS;
}

static void initialize_thread_fixture(
    TestMemory *memory,
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state,
    uint16_t thread_state)
{
    memset(memory, 0, sizeof(*memory));
    memset(memory->bytes, 0xA5, sizeof(memory->bytes));
    memset(
        memory->bytes + TEST_THREAD_OFFSET,
        0,
        TEST_THREAD_SIZE);
    write_be_u16(
        memory->bytes + TEST_THREAD_OFFSET + TEST_THREAD_STATE_OFFSET,
        thread_state);

    memset(host, 0, sizeof(*host));
    host->context = memory;
    host->read_bytes = test_read_bytes;

    porpoise_state_init(state, host);
    state->status = PORPOISE_EXECUTION_RUNNING;
    state->pc = UINT32_C(0x80008004);
    state->gpr[0] = UINT32_C(0x01234567);
    state->gpr[3] = TEST_THREAD_ADDRESS;
    state->gpr[4] = UINT32_C(0x89ABCDEF);
    state->fpr[1].lane_bits[0] = UINT64_C(0x0123456789ABCDEF);
    state->fpr[1].lane_bits[1] = UINT64_C(0xFEDCBA9876543210);
    state->cr = UINT32_C(0x13579BDF);
    state->xer = UINT32_C(0x2468ACE0);
    state->lr = UINT32_C(0x80001234);
    state->ctr = UINT32_C(0x80005678);
    state->msr = PORPOISE_MSR_EE;
}

static void check_exact_queue_read(const TestMemory *memory)
{
    CHECK(memory->read_count == 1U);
    CHECK(memory->last_read_address == TEST_QUEUE_ADDRESS);
    CHECK(memory->last_read_size == TEST_QUEUE_SIZE);
}

static void test_empty_wakeup_is_guest_safe_noop(uint32_t msr)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    uint8_t before[TEST_MEMORY_SIZE];

    initialize_fixture(&memory, &host, &state, 0U, 0U);
    state.msr = msr;
    memcpy(before, memory.bytes, sizeof(before));

    porpoise_libporpoise_os_wakeup_thread_adapter(&state);

    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.status == PORPOISE_EXECUTION_RUNNING);
    CHECK(state.msr == msr);
    check_exact_queue_read(&memory);
    CHECK(memcmp(before, memory.bytes, sizeof(before)) == 0);
}

static void test_nonempty_wakeup_fails_closed(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    uint8_t before[TEST_MEMORY_SIZE];

    initialize_fixture(
        &memory,
        &host,
        &state,
        UINT32_C(0x80005000),
        UINT32_C(0x80005020));
    memcpy(before, memory.bytes, sizeof(before));

    porpoise_libporpoise_os_wakeup_thread_adapter(&state);

    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
    CHECK(state.fault_address == TEST_QUEUE_ADDRESS);
    CHECK(state.status == PORPOISE_EXECUTION_FAULTED);
    check_exact_queue_read(&memory);
    CHECK(memcmp(before, memory.bytes, sizeof(before)) == 0);
}

static void test_inconsistent_queue_is_rejected(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    initialize_fixture(
        &memory,
        &host,
        &state,
        UINT32_C(0x80005000),
        0U);
    porpoise_libporpoise_os_wakeup_thread_adapter(&state);

    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(state.fault_address == TEST_QUEUE_ADDRESS);
    check_exact_queue_read(&memory);
}

static void test_sleep_returns_immediately_and_fails_closed(uint32_t msr)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    uint8_t before[TEST_MEMORY_SIZE];
    int returned = 0;

    initialize_fixture(&memory, &host, &state, 0U, 0U);
    state.msr = msr;
    memcpy(before, memory.bytes, sizeof(before));

    porpoise_libporpoise_os_sleep_thread_adapter(&state);
    returned = 1;

    CHECK(returned);
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
    CHECK(state.fault_address == TEST_QUEUE_ADDRESS);
    CHECK(state.status == PORPOISE_EXECUTION_FAULTED);
    CHECK(state.msr == msr);
    check_exact_queue_read(&memory);
    CHECK(memcmp(before, memory.bytes, sizeof(before)) == 0);
}

static void test_prefault_is_preserved(
    void (*adapter)(PorpoisePpcState *state))
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    PorpoisePpcState before;

    initialize_fixture(&memory, &host, &state, 0U, 0U);
    porpoise_state_set_fault(
        &state,
        PORPOISE_FAULT_ILLEGAL_INSTRUCTION,
        UINT32_C(0x80001234),
        "existing fault");
    before = state;

    adapter(&state);

    CHECK(memory.read_count == 0U);
    CHECK(memcmp(&before, &state, sizeof(state)) == 0);
}

static void test_local_pointer_validation(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    initialize_fixture(&memory, &host, &state, 0U, 0U);
    state.gpr[3] = 0U;
    porpoise_libporpoise_os_wakeup_thread_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_POINTER);
    CHECK(memory.read_count == 0U);

    initialize_fixture(&memory, &host, &state, 0U, 0U);
    state.gpr[3] = TEST_QUEUE_ADDRESS + UINT32_C(1);
    porpoise_libporpoise_os_wakeup_thread_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(state.fault_address == TEST_QUEUE_ADDRESS + UINT32_C(1));
    CHECK(memory.read_count == 0U);
}

static void test_missing_host_validation(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    initialize_fixture(&memory, &host, &state, 0U, 0U);
    state.host = NULL;
    porpoise_libporpoise_os_wakeup_thread_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_NO_HOST_ADAPTER);
    CHECK(memory.read_count == 0U);

    initialize_fixture(&memory, &host, &state, 0U, 0U);
    host.read_bytes = NULL;
    porpoise_libporpoise_os_wakeup_thread_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_MISSING_HOST_CALLBACK);
    CHECK(memory.read_count == 0U);
}

static void check_host_fault(
    uint32_t address,
    PorpoiseHostResult forced_result,
    PorpoiseFault expected_fault)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    initialize_fixture(&memory, &host, &state, 0U, 0U);
    state.gpr[3] = address;
    memory.forced_result = forced_result;

    porpoise_libporpoise_os_wakeup_thread_adapter(&state);

    CHECK(state.fault == expected_fault);
    CHECK(state.fault_address == address);
    CHECK(state.status == PORPOISE_EXECUTION_FAULTED);
    CHECK(memory.read_count == 1U);
    CHECK(memory.last_read_address == address);
    CHECK(memory.last_read_size == TEST_QUEUE_SIZE);
}

static void test_host_range_faults(void)
{
    check_host_fault(
        TEST_MEMORY_BASE + TEST_MEMORY_SIZE - UINT32_C(4),
        PORPOISE_HOST_OK,
        PORPOISE_FAULT_UNMAPPED_ADDRESS);
    check_host_fault(
        UINT32_C(0xFFFFFFFC),
        PORPOISE_HOST_OK,
        PORPOISE_FAULT_ADDRESS_OVERFLOW);
    check_host_fault(
        TEST_MMIO_ADDRESS,
        PORPOISE_HOST_OK,
        PORPOISE_FAULT_UNSUPPORTED_MMIO);
}

static void test_host_result_mapping(void)
{
    check_host_fault(
        TEST_QUEUE_ADDRESS,
        PORPOISE_HOST_INVALID_ARGUMENT,
        PORPOISE_FAULT_INVALID_ARGUMENT);
    check_host_fault(
        TEST_QUEUE_ADDRESS,
        PORPOISE_HOST_INVALID_POINTER,
        PORPOISE_FAULT_INVALID_POINTER);
    check_host_fault(
        TEST_QUEUE_ADDRESS,
        PORPOISE_HOST_UNMAPPED_ADDRESS,
        PORPOISE_FAULT_UNMAPPED_ADDRESS);
    check_host_fault(
        TEST_QUEUE_ADDRESS,
        PORPOISE_HOST_UNSUPPORTED_MMIO,
        PORPOISE_FAULT_UNSUPPORTED_MMIO);
    check_host_fault(
        TEST_QUEUE_ADDRESS,
        PORPOISE_HOST_ADDRESS_OVERFLOW,
        PORPOISE_FAULT_ADDRESS_OVERFLOW);
    check_host_fault(
        TEST_QUEUE_ADDRESS,
        PORPOISE_HOST_IO_ERROR,
        PORPOISE_FAULT_HOST_IO);
}

static void check_exact_thread_read(const TestMemory *memory)
{
    CHECK(memory->read_count == 1U);
    CHECK(memory->last_read_address == TEST_THREAD_ADDRESS);
    CHECK(memory->last_read_size == TEST_THREAD_SIZE);
}

static void test_thread_operation_fails_closed(
    ThreadAdapter adapter,
    const char *expected_message)
{
    static const uint16_t valid_states[] = {0U, 1U, 2U, 4U, 8U};
    size_t state_index;

    for (state_index = 0U;
         state_index < sizeof(valid_states) / sizeof(valid_states[0]);
         state_index++) {
        TestMemory memory;
        PorpoiseHostAdapter host;
        PorpoisePpcState state;
        PorpoisePpcState expected;
        uint8_t before[TEST_MEMORY_SIZE];

        initialize_thread_fixture(
            &memory,
            &host,
            &state,
            valid_states[state_index]);
        expected = state;
        porpoise_state_set_fault(
            &expected,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            TEST_THREAD_ADDRESS,
            expected_message);
        memcpy(before, memory.bytes, sizeof(before));

        adapter(&state);

        check_exact_thread_read(&memory);
        CHECK(memcmp(before, memory.bytes, sizeof(before)) == 0);
        CHECK(memcmp(&expected, &state, sizeof(state)) == 0);
    }
}

static void test_thread_lifecycle_state_validation(ThreadAdapter adapter)
{
    static const uint16_t invalid_states[] = {3U, 5U, UINT16_MAX};
    size_t state_index;

    for (state_index = 0U;
         state_index < sizeof(invalid_states) / sizeof(invalid_states[0]);
         state_index++) {
        TestMemory memory;
        PorpoiseHostAdapter host;
        PorpoisePpcState state;
        PorpoisePpcState expected;
        uint8_t before[TEST_MEMORY_SIZE];

        initialize_thread_fixture(
            &memory,
            &host,
            &state,
            invalid_states[state_index]);
        expected = state;
        porpoise_state_set_fault(
            &expected,
            PORPOISE_FAULT_INVALID_STATE,
            TEST_THREAD_ADDRESS,
            "guest OSThread has an invalid lifecycle state");
        memcpy(before, memory.bytes, sizeof(before));

        adapter(&state);

        check_exact_thread_read(&memory);
        CHECK(memcmp(before, memory.bytes, sizeof(before)) == 0);
        CHECK(memcmp(&expected, &state, sizeof(state)) == 0);
    }
}

static void test_thread_local_pointer_validation(ThreadAdapter adapter)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    initialize_thread_fixture(&memory, &host, &state, 1U);
    state.gpr[3] = 0U;
    adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_POINTER);
    CHECK(state.fault_address == 0U);
    CHECK(memory.read_count == 0U);

    initialize_thread_fixture(&memory, &host, &state, 1U);
    state.gpr[3] = TEST_THREAD_ADDRESS + UINT32_C(1);
    adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(state.fault_address == TEST_THREAD_ADDRESS + UINT32_C(1));
    CHECK(memory.read_count == 0U);
}

static void test_thread_missing_host_validation(ThreadAdapter adapter)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    initialize_thread_fixture(&memory, &host, &state, 1U);
    state.host = NULL;
    adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_NO_HOST_ADAPTER);
    CHECK(state.fault_address == TEST_THREAD_ADDRESS);
    CHECK(memory.read_count == 0U);

    initialize_thread_fixture(&memory, &host, &state, 1U);
    host.read_bytes = NULL;
    adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_MISSING_HOST_CALLBACK);
    CHECK(state.fault_address == TEST_THREAD_ADDRESS);
    CHECK(memory.read_count == 0U);
}

static void check_thread_host_fault(
    ThreadAdapter adapter,
    uint32_t address,
    PorpoiseHostResult forced_result,
    PorpoiseFault expected_fault)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    initialize_thread_fixture(&memory, &host, &state, 1U);
    state.gpr[3] = address;
    memory.forced_result = forced_result;

    adapter(&state);

    CHECK(state.fault == expected_fault);
    CHECK(state.fault_address == address);
    CHECK(state.status == PORPOISE_EXECUTION_FAULTED);
    CHECK(memory.read_count == 1U);
    CHECK(memory.last_read_address == address);
    CHECK(memory.last_read_size == TEST_THREAD_SIZE);
}

static void test_thread_host_faults(ThreadAdapter adapter)
{
    check_thread_host_fault(
        adapter,
        TEST_MEMORY_BASE + TEST_MEMORY_SIZE - UINT32_C(4),
        PORPOISE_HOST_OK,
        PORPOISE_FAULT_UNMAPPED_ADDRESS);
    check_thread_host_fault(
        adapter,
        UINT32_C(0xFFFFFFFC),
        PORPOISE_HOST_OK,
        PORPOISE_FAULT_ADDRESS_OVERFLOW);
    check_thread_host_fault(
        adapter,
        TEST_MMIO_ADDRESS,
        PORPOISE_HOST_OK,
        PORPOISE_FAULT_UNSUPPORTED_MMIO);
    check_thread_host_fault(
        adapter,
        TEST_THREAD_ADDRESS,
        PORPOISE_HOST_INVALID_ARGUMENT,
        PORPOISE_FAULT_INVALID_ARGUMENT);
    check_thread_host_fault(
        adapter,
        TEST_THREAD_ADDRESS,
        PORPOISE_HOST_INVALID_POINTER,
        PORPOISE_FAULT_INVALID_POINTER);
    check_thread_host_fault(
        adapter,
        TEST_THREAD_ADDRESS,
        PORPOISE_HOST_UNMAPPED_ADDRESS,
        PORPOISE_FAULT_UNMAPPED_ADDRESS);
    check_thread_host_fault(
        adapter,
        TEST_THREAD_ADDRESS,
        PORPOISE_HOST_UNSUPPORTED_MMIO,
        PORPOISE_FAULT_UNSUPPORTED_MMIO);
    check_thread_host_fault(
        adapter,
        TEST_THREAD_ADDRESS,
        PORPOISE_HOST_ADDRESS_OVERFLOW,
        PORPOISE_FAULT_ADDRESS_OVERFLOW);
    check_thread_host_fault(
        adapter,
        TEST_THREAD_ADDRESS,
        PORPOISE_HOST_IO_ERROR,
        PORPOISE_FAULT_HOST_IO);
}

static void test_exit_treats_r3_as_an_opaque_value(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    PorpoisePpcState expected;
    uint8_t before[TEST_MEMORY_SIZE];

    initialize_thread_fixture(&memory, &host, &state, 1U);
    state.gpr[3] = UINT32_C(0xCC000001);
    memory.forced_result = PORPOISE_HOST_IO_ERROR;
    expected = state;
    porpoise_state_set_fault(
        &expected,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        state.pc,
        "OSExitThread requires libPorpoise host-thread carrier API v1; "
        "this runtime supports single-thread guest execution only");
    memcpy(before, memory.bytes, sizeof(before));

    porpoise_libporpoise_os_exit_thread_adapter(&state);

    CHECK(memory.read_count == 0U);
    CHECK(memcmp(before, memory.bytes, sizeof(before)) == 0);
    CHECK(memcmp(&expected, &state, sizeof(state)) == 0);
}

int main(void)
{
    CHECK(!porpoise_libporpoise_has_host_thread_carrier_v1());
    test_empty_wakeup_is_guest_safe_noop(0U);
    test_empty_wakeup_is_guest_safe_noop(PORPOISE_MSR_EE);
    test_nonempty_wakeup_fails_closed();
    test_inconsistent_queue_is_rejected();
    test_sleep_returns_immediately_and_fails_closed(0U);
    test_sleep_returns_immediately_and_fails_closed(PORPOISE_MSR_EE);
    test_prefault_is_preserved(
        porpoise_libporpoise_os_wakeup_thread_adapter);
    test_prefault_is_preserved(
        porpoise_libporpoise_os_sleep_thread_adapter);
    test_prefault_is_preserved(
        porpoise_libporpoise_os_resume_thread_adapter);
    test_prefault_is_preserved(
        porpoise_libporpoise_os_suspend_thread_adapter);
    test_prefault_is_preserved(
        porpoise_libporpoise_os_exit_thread_adapter);
    test_local_pointer_validation();
    test_missing_host_validation();
    test_host_range_faults();
    test_host_result_mapping();
    test_thread_operation_fails_closed(
        porpoise_libporpoise_os_resume_thread_adapter,
        "OSResumeThread requires libPorpoise host-thread carrier API v1; "
        "this runtime supports single-thread guest execution only");
    test_thread_operation_fails_closed(
        porpoise_libporpoise_os_suspend_thread_adapter,
        "OSSuspendThread requires libPorpoise host-thread carrier API v1; "
        "this runtime supports single-thread guest execution only");
    test_thread_lifecycle_state_validation(
        porpoise_libporpoise_os_resume_thread_adapter);
    test_thread_lifecycle_state_validation(
        porpoise_libporpoise_os_suspend_thread_adapter);
    test_thread_local_pointer_validation(
        porpoise_libporpoise_os_resume_thread_adapter);
    test_thread_local_pointer_validation(
        porpoise_libporpoise_os_suspend_thread_adapter);
    test_thread_missing_host_validation(
        porpoise_libporpoise_os_resume_thread_adapter);
    test_thread_missing_host_validation(
        porpoise_libporpoise_os_suspend_thread_adapter);
    test_thread_host_faults(
        porpoise_libporpoise_os_resume_thread_adapter);
    test_thread_host_faults(
        porpoise_libporpoise_os_suspend_thread_adapter);
    test_exit_treats_r3_as_an_opaque_value();
    porpoise_libporpoise_os_resume_thread_adapter(NULL);
    porpoise_libporpoise_os_suspend_thread_adapter(NULL);
    porpoise_libporpoise_os_exit_thread_adapter(NULL);
    return 0;
}
