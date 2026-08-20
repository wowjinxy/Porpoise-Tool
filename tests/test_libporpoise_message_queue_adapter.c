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

#define TEST_MEMORY_BASE UINT32_C(0x80000000)
#define TEST_MEMORY_SIZE UINT32_C(0x00002000)
#define TEST_LBAUDIO_BASE (TEST_MEMORY_BASE + UINT32_C(0x0100))
#define TEST_QUEUE_0 (TEST_LBAUDIO_BASE + UINT32_C(0x34))
#define TEST_ARRAY_0 (TEST_LBAUDIO_BASE + UINT32_C(0x54))
#define TEST_QUEUE_1 (TEST_LBAUDIO_BASE + UINT32_C(0x58))
#define TEST_ARRAY_1 (TEST_LBAUDIO_BASE + UINT32_C(0x78))
#define TEST_OUTPUT (TEST_MEMORY_BASE + UINT32_C(0x0800))

enum {
    TEST_QUEUE_SIZE = 0x20,
    TEST_MEMORY_BYTES = 0x2000
};

typedef struct TestMemory {
    uint8_t bytes[TEST_MEMORY_BYTES];
    size_t write_call_count;
    size_t fail_write_call;
    uint32_t noncontiguous_decode_address;
} TestMemory;

static int test_translate(
    uint32_t guest_address,
    size_t size,
    size_t *offset_out)
{
    uint64_t offset;
    uint32_t segment;
    uint32_t physical_address;

    if (offset_out == NULL) {
        return 0;
    }
    segment = guest_address & UINT32_C(0xC0000000);
    if (segment == UINT32_C(0x80000000) ||
        segment == UINT32_C(0xC0000000)) {
        physical_address = guest_address & UINT32_C(0x3FFFFFFF);
    } else if (segment == UINT32_C(0)) {
        physical_address = guest_address;
    } else {
        return 0;
    }
    offset = physical_address;
    if (offset > TEST_MEMORY_SIZE || size > TEST_MEMORY_SIZE - offset) {
        return 0;
    }
    *offset_out = (size_t)offset;
    return 1;
}

static PorpoiseHostResult test_read_bytes(
    void *context,
    uint32_t guest_address,
    void *destination,
    size_t size)
{
    TestMemory *memory;
    size_t offset;

    memory = (TestMemory *)context;
    if (memory == NULL || destination == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (!test_translate(guest_address, size, &offset)) {
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    }
    memcpy(destination, memory->bytes + offset, size);
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult test_write_bytes(
    void *context,
    uint32_t guest_address,
    const void *source,
    size_t size)
{
    TestMemory *memory;
    size_t offset;

    memory = (TestMemory *)context;
    if (memory == NULL || source == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (!test_translate(guest_address, size, &offset)) {
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    }
    memory->write_call_count++;
    if (memory->fail_write_call != 0U &&
        memory->write_call_count == memory->fail_write_call) {
        return PORPOISE_HOST_IO_ERROR;
    }
    memcpy(memory->bytes + offset, source, size);
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult test_decode_pointer(
    void *context,
    uint32_t guest_address,
    void **pointer_out)
{
    TestMemory *memory;
    size_t offset;

    memory = (TestMemory *)context;
    if (memory == NULL || pointer_out == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    *pointer_out = NULL;
    if (!test_translate(guest_address, 1U, &offset)) {
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    }
    *pointer_out = memory->bytes + offset;
    if (guest_address == memory->noncontiguous_decode_address &&
        offset + 1U < sizeof(memory->bytes)) {
        *pointer_out = memory->bytes + offset + 1U;
    }
    return PORPOISE_HOST_OK;
}

static uint32_t test_read_be32(
    const TestMemory *memory,
    uint32_t guest_address)
{
    size_t offset;

    CHECK(memory != NULL);
    CHECK(test_translate(guest_address, 4U, &offset));
    return ((uint32_t)memory->bytes[offset] << 24U) |
           ((uint32_t)memory->bytes[offset + 1U] << 16U) |
           ((uint32_t)memory->bytes[offset + 2U] << 8U) |
           (uint32_t)memory->bytes[offset + 3U];
}

static void test_write_be32(
    TestMemory *memory,
    uint32_t guest_address,
    uint32_t value)
{
    size_t offset;

    CHECK(memory != NULL);
    CHECK(test_translate(guest_address, 4U, &offset));
    memory->bytes[offset] = (uint8_t)(value >> 24U);
    memory->bytes[offset + 1U] = (uint8_t)(value >> 16U);
    memory->bytes[offset + 2U] = (uint8_t)(value >> 8U);
    memory->bytes[offset + 3U] = (uint8_t)value;
}

static uint8_t test_read_u8(
    const TestMemory *memory,
    uint32_t guest_address)
{
    size_t offset;

    CHECK(memory != NULL);
    CHECK(test_translate(guest_address, 1U, &offset));
    return memory->bytes[offset];
}

static void test_initialize(
    TestMemory *memory,
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    memset(memory, 0, sizeof(*memory));
    memset(memory->bytes, 0xA5, sizeof(memory->bytes));
    memset(host, 0, sizeof(*host));
    host->context = memory;
    host->read_bytes = test_read_bytes;
    host->write_bytes = test_write_bytes;
    host->decode_pointer = test_decode_pointer;
    porpoise_state_init(state, host);
    state->status = PORPOISE_EXECUTION_RUNNING;
    state->pc = UINT32_C(0x803D792C);
}

static void test_recover(PorpoisePpcState *state)
{
    porpoise_state_clear_fault(state);
    state->status = PORPOISE_EXECUTION_RUNNING;
}

static void test_init_queue(
    PorpoisePpcState *state,
    uint32_t queue,
    uint32_t array,
    uint32_t capacity)
{
    state->gpr[3] = queue;
    state->gpr[4] = array;
    state->gpr[5] = capacity;
    porpoise_libporpoise_os_init_message_queue_adapter(state);
}

static void test_send(
    PorpoisePpcState *state,
    uint32_t queue,
    uint32_t message,
    uint32_t flags)
{
    state->gpr[3] = queue;
    state->gpr[4] = message;
    state->gpr[5] = flags;
    porpoise_libporpoise_os_send_message_adapter(state);
}

static void test_receive(
    PorpoisePpcState *state,
    uint32_t queue,
    uint32_t output,
    uint32_t flags)
{
    state->gpr[3] = queue;
    state->gpr[4] = output;
    state->gpr[5] = flags;
    porpoise_libporpoise_os_receive_message_adapter(state);
}

static void test_lbaudio_sequence(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    uint32_t address;

    test_initialize(&memory, &host, &state);
    test_init_queue(&state, TEST_QUEUE_0, TEST_ARRAY_0, UINT32_C(1));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(test_read_u8(&memory, TEST_QUEUE_0 - UINT32_C(1)) == 0xA5U);
    for (address = TEST_QUEUE_0;
         address < TEST_QUEUE_0 + UINT32_C(0x10);
         address += UINT32_C(4)) {
        CHECK(test_read_be32(&memory, address) == UINT32_C(0));
    }
    CHECK(test_read_be32(&memory, TEST_QUEUE_0 + UINT32_C(0x10)) ==
          TEST_ARRAY_0);
    CHECK(test_read_be32(&memory, TEST_QUEUE_0 + UINT32_C(0x14)) ==
          UINT32_C(1));
    CHECK(test_read_be32(&memory, TEST_QUEUE_0 + UINT32_C(0x18)) ==
          UINT32_C(0));
    CHECK(test_read_be32(&memory, TEST_QUEUE_0 + UINT32_C(0x1C)) ==
          UINT32_C(0));
    CHECK(test_read_u8(&memory, TEST_QUEUE_0 + TEST_QUEUE_SIZE) == 0xA5U);

    test_init_queue(&state, TEST_QUEUE_1, TEST_ARRAY_1, UINT32_C(1));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(test_read_u8(&memory, TEST_ARRAY_0) == 0xA5U);
    CHECK(test_read_be32(&memory, TEST_QUEUE_1 + UINT32_C(0x10)) ==
          TEST_ARRAY_1);
    CHECK(test_read_u8(&memory, TEST_QUEUE_1 + TEST_QUEUE_SIZE) == 0xA5U);

    test_send(&state, TEST_QUEUE_0, UINT32_C(1), UINT32_C(0));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == UINT32_C(1));
    CHECK(test_read_be32(&memory, TEST_ARRAY_0) == UINT32_C(1));
    CHECK(test_read_be32(&memory, TEST_QUEUE_0 + UINT32_C(0x1C)) ==
          UINT32_C(1));
    CHECK(test_read_be32(&memory, TEST_QUEUE_1 + UINT32_C(0x1C)) ==
          UINT32_C(0));

    test_receive(&state, TEST_QUEUE_0, TEST_OUTPUT, UINT32_C(1));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == UINT32_C(1));
    CHECK(test_read_be32(&memory, TEST_OUTPUT) == UINT32_C(1));
    CHECK(test_read_be32(&memory, TEST_QUEUE_0 + UINT32_C(0x18)) ==
          UINT32_C(0));
    CHECK(test_read_be32(&memory, TEST_QUEUE_0 + UINT32_C(0x1C)) ==
          UINT32_C(0));
}

static void test_flags_and_scheduler_faults(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    uint8_t snapshot[TEST_QUEUE_SIZE + 4U];
    size_t queue_offset;

    test_initialize(&memory, &host, &state);
    test_init_queue(&state, TEST_QUEUE_0, TEST_ARRAY_0, UINT32_C(1));
    CHECK(!porpoise_state_has_fault(&state));

    test_receive(&state, TEST_QUEUE_0, TEST_OUTPUT, UINT32_C(2));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == UINT32_C(0));

    CHECK(test_translate(
        TEST_QUEUE_0,
        sizeof(snapshot),
        &queue_offset));
    memcpy(snapshot, memory.bytes + queue_offset, sizeof(snapshot));
    test_receive(&state, TEST_QUEUE_0, TEST_OUTPUT, UINT32_C(3));
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
    test_recover(&state);

    test_send(&state, TEST_QUEUE_0, UINT32_C(0xCAFEBABE), UINT32_C(0));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == UINT32_C(1));
    memcpy(snapshot, memory.bytes + queue_offset, sizeof(snapshot));
    test_send(&state, TEST_QUEUE_0, UINT32_C(0xDEADBEEF), UINT32_C(2));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == UINT32_C(0));
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);

    test_send(&state, TEST_QUEUE_0, UINT32_C(0xDEADBEEF), UINT32_C(1));
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
}

static void test_waiters_fault_before_mutation(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    uint8_t snapshot[TEST_QUEUE_SIZE + 4U];
    size_t queue_offset;

    test_initialize(&memory, &host, &state);
    test_init_queue(&state, TEST_QUEUE_0, TEST_ARRAY_0, UINT32_C(1));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(test_translate(
        TEST_QUEUE_0,
        sizeof(snapshot),
        &queue_offset));

    test_write_be32(
        &memory,
        TEST_QUEUE_0 + UINT32_C(0x08),
        TEST_MEMORY_BASE + UINT32_C(0x1000));
    test_write_be32(
        &memory,
        TEST_QUEUE_0 + UINT32_C(0x0C),
        TEST_MEMORY_BASE + UINT32_C(0x1000));
    memcpy(snapshot, memory.bytes + queue_offset, sizeof(snapshot));
    test_send(&state, TEST_QUEUE_0, UINT32_C(0x12345678), UINT32_C(0));
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
    test_recover(&state);

    test_write_be32(&memory, TEST_QUEUE_0 + UINT32_C(0x08), UINT32_C(0));
    test_write_be32(&memory, TEST_QUEUE_0 + UINT32_C(0x0C), UINT32_C(0));
    test_send(&state, TEST_QUEUE_0, UINT32_C(0x12345678), UINT32_C(0));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == UINT32_C(1));
    test_write_be32(
        &memory,
        TEST_QUEUE_0 + UINT32_C(0x00),
        TEST_MEMORY_BASE + UINT32_C(0x1100));
    test_write_be32(
        &memory,
        TEST_QUEUE_0 + UINT32_C(0x04),
        TEST_MEMORY_BASE + UINT32_C(0x1100));
    test_write_be32(&memory, TEST_OUTPUT, UINT32_C(0xAAAAAAAA));
    memcpy(snapshot, memory.bytes + queue_offset, sizeof(snapshot));
    test_receive(&state, TEST_QUEUE_0, TEST_OUTPUT, UINT32_C(0));
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
    CHECK(test_read_be32(&memory, TEST_OUTPUT) == UINT32_C(0xAAAAAAAA));
    test_recover(&state);

    test_write_be32(&memory, TEST_QUEUE_0 + UINT32_C(0x04), UINT32_C(0));
    memcpy(snapshot, memory.bytes + queue_offset, sizeof(snapshot));
    test_receive(&state, TEST_QUEUE_0, TEST_OUTPUT, UINT32_C(0));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
}

static void test_wrap_discard_and_alias(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    const uint32_t queue = TEST_MEMORY_BASE + UINT32_C(0x0900);
    const uint32_t array = TEST_MEMORY_BASE + UINT32_C(0x0920);
    uint8_t snapshot[TEST_QUEUE_SIZE + 12U];
    size_t queue_offset;

    test_initialize(&memory, &host, &state);
    test_init_queue(&state, queue, array, UINT32_C(3));
    CHECK(!porpoise_state_has_fault(&state));
    test_write_be32(&memory, queue + UINT32_C(0x18), UINT32_C(2));
    test_write_be32(&memory, queue + UINT32_C(0x1C), UINT32_C(2));
    test_write_be32(&memory, array + UINT32_C(0), UINT32_C(0xAAAAAAAA));
    test_write_be32(&memory, array + UINT32_C(4), UINT32_C(0xBBBBBBBB));
    test_write_be32(&memory, array + UINT32_C(8), UINT32_C(0xCCCCCCCC));

    test_send(&state, queue, UINT32_C(0x12345678), UINT32_C(0));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == UINT32_C(1));
    CHECK(test_read_be32(&memory, array + UINT32_C(4)) ==
          UINT32_C(0x12345678));
    CHECK(test_read_be32(&memory, queue + UINT32_C(0x1C)) == UINT32_C(3));

    test_receive(&state, queue, UINT32_C(0), UINT32_C(0));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == UINT32_C(1));
    CHECK(test_read_be32(&memory, queue + UINT32_C(0x18)) == UINT32_C(0));
    CHECK(test_read_be32(&memory, queue + UINT32_C(0x1C)) == UINT32_C(2));

    CHECK(test_translate(queue, sizeof(snapshot), &queue_offset));
    memcpy(snapshot, memory.bytes + queue_offset, sizeof(snapshot));
    test_receive(
        &state,
        queue,
        queue + UINT32_C(0x10),
        UINT32_C(0));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
}

static void test_segment_alias_rejection(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    const uint32_t queue = TEST_MEMORY_BASE + UINT32_C(0x0900);
    const uint32_t array = TEST_MEMORY_BASE + UINT32_C(0x0A00);
    const uint32_t uncached_queue_word =
        UINT32_C(0xC0000000) | ((queue + UINT32_C(0x10)) &
                                UINT32_C(0x3FFFFFFF));
    const uint32_t physical_queue_word =
        (queue + UINT32_C(0x10)) & UINT32_C(0x3FFFFFFF);
    uint8_t snapshot[TEST_QUEUE_SIZE + 4U];
    size_t queue_offset;

    test_initialize(&memory, &host, &state);
    CHECK(test_translate(queue, sizeof(snapshot), &queue_offset));
    memcpy(snapshot, memory.bytes + queue_offset, sizeof(snapshot));

    test_init_queue(&state, queue, uncached_queue_word, UINT32_C(1));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
    test_recover(&state);

    test_init_queue(&state, queue, physical_queue_word, UINT32_C(1));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
    test_recover(&state);

    test_init_queue(&state, queue, array, UINT32_C(1));
    CHECK(!porpoise_state_has_fault(&state));
    test_send(&state, queue, UINT32_C(0x12345678), UINT32_C(0));
    CHECK(!porpoise_state_has_fault(&state));
    memcpy(snapshot, memory.bytes + queue_offset, sizeof(snapshot));

    test_receive(
        &state,
        queue,
        uncached_queue_word,
        UINT32_C(0));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
    test_recover(&state);

    test_receive(
        &state,
        queue,
        physical_queue_word,
        UINT32_C(0));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
}

static void test_invalid_init_and_zero_capacity(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    uint8_t snapshot[TEST_QUEUE_SIZE];
    size_t queue_offset;

    test_initialize(&memory, &host, &state);
    CHECK(test_translate(TEST_QUEUE_0, sizeof(snapshot), &queue_offset));
    memcpy(snapshot, memory.bytes + queue_offset, sizeof(snapshot));
    test_init_queue(
        &state,
        TEST_QUEUE_0,
        TEST_ARRAY_0,
        UINT32_C(0xFFFFFFFF));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
    test_recover(&state);

    test_init_queue(
        &state,
        TEST_QUEUE_0,
        UINT32_C(0xFFFFFFFC),
        UINT32_C(2));
    CHECK(state.fault == PORPOISE_FAULT_ADDRESS_OVERFLOW);
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
    test_recover(&state);

    test_init_queue(&state, TEST_QUEUE_0, UINT32_C(0), UINT32_C(0));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(test_read_be32(&memory, TEST_QUEUE_0 + UINT32_C(0x10)) ==
          UINT32_C(0));
    CHECK(test_read_be32(&memory, TEST_QUEUE_0 + UINT32_C(0x14)) ==
          UINT32_C(0));
    test_send(&state, TEST_QUEUE_0, UINT32_C(0x11223344), UINT32_C(0));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == UINT32_C(0));
    test_receive(&state, TEST_QUEUE_0, TEST_OUTPUT, UINT32_C(0));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == UINT32_C(0));
    test_send(&state, TEST_QUEUE_0, UINT32_C(0x11223344), UINT32_C(1));
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
}

static void test_unmapped_and_preflight_failures(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    uint8_t queue_snapshot[TEST_QUEUE_SIZE];
    size_t queue_offset;

    test_initialize(&memory, &host, &state);
    test_init_queue(
        &state,
        TEST_QUEUE_0,
        TEST_MEMORY_BASE + TEST_MEMORY_SIZE,
        UINT32_C(1));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(test_translate(
        TEST_QUEUE_0,
        sizeof(queue_snapshot),
        &queue_offset));
    memcpy(
        queue_snapshot,
        memory.bytes + queue_offset,
        sizeof(queue_snapshot));
    test_send(&state, TEST_QUEUE_0, UINT32_C(0x11223344), UINT32_C(0));
    CHECK(state.fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
    CHECK(memcmp(
        queue_snapshot,
        memory.bytes + queue_offset,
        sizeof(queue_snapshot)) == 0);
    test_recover(&state);

    test_init_queue(&state, TEST_QUEUE_0, TEST_ARRAY_0, UINT32_C(1));
    CHECK(!porpoise_state_has_fault(&state));
    test_send(&state, TEST_QUEUE_0, UINT32_C(0x55667788), UINT32_C(0));
    CHECK(!porpoise_state_has_fault(&state));
    memcpy(
        queue_snapshot,
        memory.bytes + queue_offset,
        sizeof(queue_snapshot));
    test_receive(
        &state,
        TEST_QUEUE_0,
        TEST_MEMORY_BASE + TEST_MEMORY_SIZE,
        UINT32_C(0));
    CHECK(state.fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
    CHECK(memcmp(
        queue_snapshot,
        memory.bytes + queue_offset,
        sizeof(queue_snapshot)) == 0);
    CHECK(test_read_be32(&memory, TEST_ARRAY_0) == UINT32_C(0x55667788));
}

static void test_terminal_and_noncontiguous_preflight(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    uint8_t snapshot[TEST_QUEUE_SIZE + 4U];
    size_t queue_offset;

    test_initialize(&memory, &host, &state);
    CHECK(test_translate(TEST_QUEUE_0, sizeof(snapshot), &queue_offset));
    memcpy(snapshot, memory.bytes + queue_offset, sizeof(snapshot));
    state.status = PORPOISE_EXECUTION_RETURNED;
    test_init_queue(&state, TEST_QUEUE_0, TEST_ARRAY_0, UINT32_C(1));
    CHECK(state.status == PORPOISE_EXECUTION_RETURNED);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
    test_send(&state, TEST_QUEUE_0, UINT32_C(0x11223344), UINT32_C(0));
    CHECK(state.status == PORPOISE_EXECUTION_RETURNED);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
    test_receive(&state, TEST_QUEUE_0, TEST_OUTPUT, UINT32_C(0));
    CHECK(state.status == PORPOISE_EXECUTION_RETURNED);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);

    state.status = PORPOISE_EXECUTION_RUNNING;
    memory.noncontiguous_decode_address =
        TEST_QUEUE_0 + (uint32_t)(TEST_QUEUE_SIZE - 1);
    test_init_queue(&state, TEST_QUEUE_0, TEST_ARRAY_0, UINT32_C(1));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_POINTER);
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
}

static void test_commit_rollback(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    uint8_t snapshot[TEST_QUEUE_SIZE + 4U];
    size_t queue_offset;

    test_initialize(&memory, &host, &state);
    test_init_queue(&state, TEST_QUEUE_0, TEST_ARRAY_0, UINT32_C(1));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(test_translate(TEST_QUEUE_0, sizeof(snapshot), &queue_offset));
    memcpy(snapshot, memory.bytes + queue_offset, sizeof(snapshot));

    memory.fail_write_call = memory.write_call_count + 2U;
    test_send(&state, TEST_QUEUE_0, UINT32_C(0x11223344), UINT32_C(0));
    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    CHECK(state.fault_address == TEST_QUEUE_0 + UINT32_C(0x1C));
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
    test_recover(&state);

    memory.fail_write_call = 0U;
    test_send(&state, TEST_QUEUE_0, UINT32_C(0x55667788), UINT32_C(0));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.gpr[3] == UINT32_C(1));
    test_write_be32(&memory, TEST_OUTPUT, UINT32_C(0xAABBCCDD));
    memcpy(snapshot, memory.bytes + queue_offset, sizeof(snapshot));

    memory.fail_write_call = memory.write_call_count + 2U;
    test_receive(&state, TEST_QUEUE_0, TEST_OUTPUT, UINT32_C(0));
    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    CHECK(state.fault_address == TEST_QUEUE_0 + UINT32_C(0x18));
    CHECK(memcmp(
        snapshot,
        memory.bytes + queue_offset,
        sizeof(snapshot)) == 0);
    CHECK(test_read_be32(&memory, TEST_OUTPUT) == UINT32_C(0xAABBCCDD));
}

int main(void)
{
    test_lbaudio_sequence();
    test_flags_and_scheduler_faults();
    test_waiters_fault_before_mutation();
    test_wrap_discard_and_alias();
    test_segment_alias_rejection();
    test_invalid_init_and_zero_capacity();
    test_unmapped_and_preflight_failures();
    test_terminal_and_noncontiguous_preflight();
    test_commit_rollback();
    return 0;
}
