#include "porpoise_libporpoise_builtins_private.h"
#include "porpoise_libporpoise_private.h"

#include <dolphin/ar.h>
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

#define TEST_GUEST_BASE UINT32_C(0x80000000)
#define TEST_MEMORY_SIZE 4096U
#define TEST_TABLE (TEST_GUEST_BASE + UINT32_C(0x100))
#define TEST_TABLE_B (TEST_GUEST_BASE + UINT32_C(0x200))
#define TEST_LENGTH_OUT (TEST_GUEST_BASE + UINT32_C(0x300))
#define TEST_AR_BASE UINT32_C(0x00004000)
#define TEST_AR_SIZE UINT32_C(0x01000000)

typedef struct TestMemory {
    uint8_t bytes[TEST_MEMORY_SIZE];
    uint32_t unmapped_begin;
    uint32_t unmapped_end;
    unsigned int write_calls;
    unsigned int fail_write_call;
    int read_only;
} TestMemory;

static int ranges_overlap(
    size_t first_begin,
    size_t first_end,
    size_t second_begin,
    size_t second_end)
{
    return first_begin < second_end && second_begin < first_end;
}

static PorpoiseHostResult validate_range(
    const TestMemory *memory,
    uint32_t guest_address,
    size_t size,
    size_t *offset_out)
{
    uint64_t offset;
    uint64_t end;

    if (size == 0U) {
        *offset_out = 0U;
        return PORPOISE_HOST_OK;
    }
    if (guest_address < TEST_GUEST_BASE) {
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    }
    offset = (uint64_t)guest_address - (uint64_t)TEST_GUEST_BASE;
    end = offset + (uint64_t)size;
    if (end > TEST_MEMORY_SIZE) {
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    }
    if (memory->unmapped_end > memory->unmapped_begin &&
        ranges_overlap(
            (size_t)offset,
            (size_t)end,
            (size_t)memory->unmapped_begin,
            (size_t)memory->unmapped_end)) {
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    }
    *offset_out = (size_t)offset;
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult test_read(
    void *context,
    uint32_t guest_address,
    void *destination,
    size_t size)
{
    TestMemory *memory = (TestMemory *)context;
    PorpoiseHostResult result;
    size_t offset;

    if (memory == NULL || (destination == NULL && size != 0U)) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    result = validate_range(memory, guest_address, size, &offset);
    if (result != PORPOISE_HOST_OK) {
        return result;
    }
    if (size != 0U) {
        memcpy(destination, &memory->bytes[offset], size);
    }
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult test_write(
    void *context,
    uint32_t guest_address,
    const void *source,
    size_t size)
{
    TestMemory *memory = (TestMemory *)context;
    PorpoiseHostResult result;
    size_t offset;

    if (memory == NULL || (source == NULL && size != 0U)) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    result = validate_range(memory, guest_address, size, &offset);
    if (result != PORPOISE_HOST_OK) {
        return result;
    }
    memory->write_calls++;
    if (memory->read_only) {
        return PORPOISE_HOST_IO_ERROR;
    }
    if (memory->fail_write_call != 0U &&
        memory->write_calls == memory->fail_write_call) {
        /* Exercise rollback after a hostile callback partially writes before
         * reporting failure. */
        if (size != 0U) {
            memcpy(&memory->bytes[offset], source, size / 2U);
        }
        return PORPOISE_HOST_IO_ERROR;
    }
    if (size != 0U) {
        memcpy(&memory->bytes[offset], source, size);
    }
    return PORPOISE_HOST_OK;
}

static PorpoiseHostAdapter make_host(TestMemory *memory)
{
    PorpoiseHostAdapter host;

    memset(&host, 0, sizeof(host));
    host.context = memory;
    host.read_bytes = test_read;
    host.write_bytes = test_write;
    return host;
}

static void prepare_state(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    porpoise_state_init(state, host);
    state->pc = UINT32_C(0x80001234);
}

static void recover_state(PorpoisePpcState *state)
{
    porpoise_state_clear_fault(state);
    state->pc = UINT32_C(0x80001234);
}

static uint32_t call_init(
    PorpoisePpcState *state,
    uint32_t table,
    uint32_t capacity)
{
    state->gpr[3] = table;
    state->gpr[4] = capacity;
    porpoise_libporpoise_ar_init_adapter(state);
    return state->gpr[3];
}

static uint32_t call_alloc(
    PorpoisePpcState *state,
    uint32_t length)
{
    state->gpr[3] = length;
    porpoise_libporpoise_ar_alloc_adapter(state);
    return state->gpr[3];
}

static uint32_t call_free(
    PorpoisePpcState *state,
    uint32_t length_out)
{
    state->gpr[3] = length_out;
    porpoise_libporpoise_ar_free_adapter(state);
    return state->gpr[3];
}

static uint32_t memory_offset(uint32_t guest_address)
{
    CHECK(guest_address >= TEST_GUEST_BASE);
    CHECK(guest_address - TEST_GUEST_BASE < TEST_MEMORY_SIZE);
    return guest_address - TEST_GUEST_BASE;
}

static void set_word_bytes(
    TestMemory *memory,
    uint32_t guest_address,
    uint8_t a,
    uint8_t b,
    uint8_t c,
    uint8_t d)
{
    uint32_t offset = memory_offset(guest_address);

    memory->bytes[offset + 0U] = a;
    memory->bytes[offset + 1U] = b;
    memory->bytes[offset + 2U] = c;
    memory->bytes[offset + 3U] = d;
}

static void check_word_bytes(
    const TestMemory *memory,
    uint32_t guest_address,
    uint8_t a,
    uint8_t b,
    uint8_t c,
    uint8_t d)
{
    uint32_t offset = memory_offset(guest_address);

    CHECK(memory->bytes[offset + 0U] == a);
    CHECK(memory->bytes[offset + 1U] == b);
    CHECK(memory->bytes[offset + 2U] == c);
    CHECK(memory->bytes[offset + 3U] == d);
}

static void reset_owned_boundary(PorpoisePpcState *state)
{
    recover_state(state);
    porpoise_libporpoise_ar_reset_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(!ARCheckInit());
}

static void test_init_validation_and_reinit(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    const uint32_t *native_table;

    memset(&memory, 0xA5, sizeof(memory));
    memory.unmapped_begin = 0U;
    memory.unmapped_end = 0U;
    memory.write_calls = 0U;
    memory.fail_write_call = 0U;
    memory.read_only = 0;
    host = make_host(&memory);
    prepare_state(&state, &host);
    PorpoiseStubARAllocatorResetState();

    porpoise_libporpoise_ar_get_size_adapter(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == 0U);

    CHECK(call_init(&state, TEST_TABLE, 3U) == TEST_AR_BASE);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubARAllocatorInitCount() == 1U);
    native_table = PorpoiseStubARAllocatorBlockTable();
    CHECK(native_table != NULL);
    CHECK((const void *)native_table !=
          (const void *)&memory.bytes[memory_offset(TEST_TABLE)]);
    check_word_bytes(&memory, TEST_TABLE, 0xA5, 0xA5, 0xA5, 0xA5);

    CHECK(call_init(&state, TEST_TABLE, 3U) == TEST_AR_BASE);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubARAllocatorInitCount() == 1U);

    CHECK(call_init(&state, TEST_TABLE_B, 3U) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(PorpoiseStubARAllocatorInitCount() == 1U);
    reset_owned_boundary(&state);
    porpoise_libporpoise_ar_get_size_adapter(&state);
    CHECK(state.gpr[3] == TEST_AR_SIZE);

    prepare_state(&state, &host);
    CHECK(call_init(&state, 0U, 0U) == TEST_AR_BASE);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubARAllocatorBlockTable() == NULL);
    CHECK(call_alloc(&state, UINT32_C(0x20)) == 0U);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubARAllocatorAllocCount() == 0U);
    reset_owned_boundary(&state);

    prepare_state(&state, &host);
    CHECK(call_init(&state, TEST_TABLE + UINT32_C(2), 1U) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(!ARCheckInit());
    recover_state(&state);

    CHECK(call_init(&state, UINT32_C(0xFFFFFFFC), 2U) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_ADDRESS_OVERFLOW);
    CHECK(!ARCheckInit());
    recover_state(&state);

    memory.unmapped_begin = memory_offset(TEST_TABLE) + 4U;
    memory.unmapped_end = memory.unmapped_begin + 4U;
    CHECK(call_init(&state, TEST_TABLE, 3U) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
    CHECK(!ARCheckInit());
    recover_state(&state);
    memory.unmapped_begin = 0U;
    memory.unmapped_end = 0U;

    memory.read_only = 1;
    CHECK(call_init(&state, TEST_TABLE, 3U) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    CHECK(!ARCheckInit());
    memory.read_only = 0;
    recover_state(&state);

    PorpoiseStubARAllocatorSetSize(TEST_AR_BASE - UINT32_C(0x20));
    CHECK(call_init(&state, TEST_TABLE, 1U) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(ARCheckInit());
    reset_owned_boundary(&state);
}

static void test_lifo_endian_exhaustion_and_size(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    unsigned int native_alloc_count;

    memset(&memory, 0xCC, sizeof(memory));
    memory.unmapped_begin = 0U;
    memory.unmapped_end = 0U;
    memory.write_calls = 0U;
    memory.fail_write_call = 0U;
    memory.read_only = 0;
    host = make_host(&memory);
    prepare_state(&state, &host);
    PorpoiseStubARAllocatorResetState();

    CHECK(call_init(&state, TEST_TABLE, 2U) == TEST_AR_BASE);
    CHECK(call_alloc(&state, UINT32_C(0x20)) == TEST_AR_BASE);
    check_word_bytes(&memory, TEST_TABLE, 0x00, 0x00, 0x00, 0x20);
    CHECK(PorpoiseStubARAllocatorBlockValue(0U) == UINT32_C(0x20));
    CHECK(call_alloc(&state, UINT32_C(0x40)) ==
          TEST_AR_BASE + UINT32_C(0x20));
    check_word_bytes(
        &memory, TEST_TABLE + UINT32_C(4), 0x00, 0x00, 0x00, 0x40);
    CHECK(PorpoiseStubARAllocatorBlockValue(1U) == UINT32_C(0x40));

    native_alloc_count = PorpoiseStubARAllocatorAllocCount();
    CHECK(call_alloc(&state, UINT32_C(0x20)) == 0U);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubARAllocatorAllocCount() == native_alloc_count);

    /* A nullable output may not overwrite a committed table entry with a
     * different popped length. Reject it before native state changes. */
    CHECK(call_free(&state, TEST_TABLE) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(PorpoiseStubARAllocatorFreeCount() == 0U);
    check_word_bytes(&memory, TEST_TABLE, 0x00, 0x00, 0x00, 0x20);
    recover_state(&state);

    set_word_bytes(&memory, TEST_LENGTH_OUT, 0xDE, 0xAD, 0xBE, 0xEF);
    CHECK(call_free(&state, TEST_LENGTH_OUT) ==
          TEST_AR_BASE + UINT32_C(0x20));
    check_word_bytes(
        &memory, TEST_LENGTH_OUT, 0x00, 0x00, 0x00, 0x40);
    CHECK(call_free(&state, 0U) == TEST_AR_BASE);
    CHECK(call_free(&state, TEST_LENGTH_OUT) == 0U);
    check_word_bytes(
        &memory, TEST_LENGTH_OUT, 0x00, 0x00, 0x00, 0x00);

    porpoise_libporpoise_ar_get_size_adapter(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == TEST_AR_SIZE);

    CHECK(call_alloc(&state, UINT32_C(1)) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    reset_owned_boundary(&state);

    prepare_state(&state, &host);
    PorpoiseStubARAllocatorResetState();
    PorpoiseStubARAllocatorSetSize(
        TEST_AR_BASE + UINT32_C(0x20));
    CHECK(call_init(&state, TEST_TABLE, 2U) == TEST_AR_BASE);
    CHECK(call_alloc(&state, UINT32_C(0x20)) == TEST_AR_BASE);
    native_alloc_count = PorpoiseStubARAllocatorAllocCount();
    CHECK(call_alloc(&state, UINT32_C(0x20)) == 0U);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubARAllocatorAllocCount() == native_alloc_count);
    porpoise_libporpoise_ar_get_size_adapter(&state);
    CHECK(state.gpr[3] == TEST_AR_BASE + UINT32_C(0x20));
    reset_owned_boundary(&state);
}

static void test_fault_atomicity(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    unsigned int free_count;

    memset(&memory, 0x77, sizeof(memory));
    memory.unmapped_begin = 0U;
    memory.unmapped_end = 0U;
    memory.write_calls = 0U;
    memory.fail_write_call = 0U;
    memory.read_only = 0;
    host = make_host(&memory);
    prepare_state(&state, &host);
    PorpoiseStubARAllocatorResetState();

    CHECK(call_init(&state, TEST_TABLE, 2U) == TEST_AR_BASE);
    CHECK(call_alloc(&state, UINT32_C(0x20)) == TEST_AR_BASE);
    set_word_bytes(&memory, TEST_LENGTH_OUT, 0x11, 0x22, 0x33, 0x44);

    free_count = PorpoiseStubARAllocatorFreeCount();
    CHECK(call_free(&state, TEST_LENGTH_OUT + UINT32_C(2)) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(PorpoiseStubARAllocatorFreeCount() == free_count);
    recover_state(&state);

    memory.read_only = 1;
    CHECK(call_free(&state, TEST_LENGTH_OUT) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    CHECK(PorpoiseStubARAllocatorFreeCount() == free_count);
    check_word_bytes(
        &memory, TEST_LENGTH_OUT, 0x11, 0x22, 0x33, 0x44);
    memory.read_only = 0;
    recover_state(&state);

    memory.unmapped_begin = memory_offset(TEST_LENGTH_OUT);
    memory.unmapped_end = memory.unmapped_begin + 4U;
    CHECK(call_free(&state, TEST_LENGTH_OUT) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
    CHECK(PorpoiseStubARAllocatorFreeCount() == free_count);
    memory.unmapped_begin = 0U;
    memory.unmapped_end = 0U;
    recover_state(&state);

    memory.write_calls = 0U;
    memory.fail_write_call = 2U;
    CHECK(call_free(&state, TEST_LENGTH_OUT) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    CHECK(PorpoiseStubARAllocatorFreeCount() == free_count);
    check_word_bytes(
        &memory, TEST_LENGTH_OUT, 0x11, 0x22, 0x33, 0x44);
    memory.fail_write_call = 0U;
    reset_owned_boundary(&state);

    prepare_state(&state, &host);
    CHECK(call_init(&state, TEST_TABLE_B, 1U) == TEST_AR_BASE);
    set_word_bytes(&memory, TEST_TABLE_B, 0x55, 0x66, 0x77, 0x88);
    memory.write_calls = 0U;
    memory.fail_write_call = 2U;
    CHECK(call_alloc(&state, UINT32_C(0x20)) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    check_word_bytes(
        &memory, TEST_TABLE_B, 0x55, 0x66, 0x77, 0x88);
    memory.fail_write_call = 0U;
    reset_owned_boundary(&state);
}

static void test_divergence_reset_and_shutdown(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoiseHostAdapter other_host;
    PorpoisePpcState state;
    PorpoisePpcState other_state;

    memset(&memory, 0, sizeof(memory));
    host = make_host(&memory);
    other_host = make_host(&memory);
    prepare_state(&state, &host);
    prepare_state(&other_state, &other_host);
    PorpoiseStubARAllocatorResetState();

    CHECK(call_init(&state, TEST_TABLE, 3U) == TEST_AR_BASE);
    CHECK(call_init(&other_state, TEST_TABLE, 3U) == 0U);
    CHECK(other_state.fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(PorpoiseStubARAllocatorInitCount() == 1U);

    CHECK(call_alloc(&state, UINT32_C(0x20)) == TEST_AR_BASE);
    set_word_bytes(&memory, TEST_TABLE, 0x00, 0x00, 0x00, 0x21);
    CHECK(call_free(&state, 0U) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(PorpoiseStubARAllocatorFreeCount() == 0U);
    reset_owned_boundary(&state);

    prepare_state(&state, &host);
    CHECK(call_init(&state, TEST_TABLE, 3U) == TEST_AR_BASE);
    CHECK(call_alloc(&state, UINT32_C(0x20)) == TEST_AR_BASE);
    CHECK(ARAlloc(UINT32_C(0x20)) ==
          TEST_AR_BASE + UINT32_C(0x20));
    porpoise_libporpoise_ar_get_size_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    reset_owned_boundary(&state);

    prepare_state(&state, &host);
    CHECK(call_init(&state, TEST_TABLE, 3U) == TEST_AR_BASE);
    CHECK(call_alloc(&state, UINT32_C(0x20)) == TEST_AR_BASE);
    CHECK(call_alloc(&state, UINT32_C(0x40)) ==
          TEST_AR_BASE + UINT32_C(0x20));
    CHECK(ARFree(NULL) == TEST_AR_BASE + UINT32_C(0x20));
    /* The current native API has no stack-position snapshot, so a direct
     * native free is detected at the next mutating boundary. That boundary
     * rolls its speculative guest word back before poisoning. */
    porpoise_libporpoise_ar_get_size_adapter(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == TEST_AR_SIZE);
    set_word_bytes(
        &memory,
        TEST_TABLE + UINT32_C(8),
        0x12,
        0x34,
        0x56,
        0x78);
    CHECK(call_alloc(&state, UINT32_C(0x20)) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    check_word_bytes(
        &memory,
        TEST_TABLE + UINT32_C(8),
        0x12,
        0x34,
        0x56,
        0x78);
    reset_owned_boundary(&state);

    prepare_state(&state, &host);
    CHECK(call_init(&state, TEST_TABLE, 1U) == TEST_AR_BASE);
    ARReset();
    porpoise_libporpoise_ar_get_size_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    reset_owned_boundary(&state);

    prepare_state(&state, &host);
    CHECK(call_init(&state, TEST_TABLE, 1U) == TEST_AR_BASE);
    porpoise_libporpoise_ar_shutdown(&host);
    CHECK(!ARCheckInit());
    CHECK(PorpoiseStubARAllocatorBlockTable() == NULL);

    prepare_state(&other_state, &other_host);
    CHECK(call_init(&other_state, TEST_TABLE_B, 1U) == TEST_AR_BASE);
    reset_owned_boundary(&other_state);
}

int main(void)
{
    test_init_validation_and_reinit();
    test_lifo_endian_exhaustion_and_size();
    test_fault_atomicity();
    test_divergence_reset_and_shutdown();
    return 0;
}
