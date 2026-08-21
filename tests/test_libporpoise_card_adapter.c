#include "porpoise_libporpoise_builtins_private.h"

#include <dolphin/card.h>

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
#define TEST_MEMORY_SIZE UINT32_C(0x00001000)
#define TEST_MEM_SIZE (TEST_MEMORY_BASE + UINT32_C(0x100))
#define TEST_SECTOR_SIZE (TEST_MEMORY_BASE + UINT32_C(0x104))
#define TEST_MMIO_POINTER UINT32_C(0xCC006800)

typedef struct TestMemory {
    uint8_t bytes[TEST_MEMORY_SIZE];
    unsigned int read_count;
    unsigned int write_count;
    unsigned int fail_write_call;
} TestMemory;

static unsigned int native_call_count;
static s32 native_result;
static int native_write_outputs;
static s32 native_memory_size;
static s32 native_sector_size;
static s32 native_last_channel;
static int native_mem_size_was_null;
static int native_sector_size_was_null;

s32 CARDProbeEx(s32 channel, s32 *mem_size, s32 *sector_size)
{
    native_call_count++;
    native_last_channel = channel;
    native_mem_size_was_null = mem_size == NULL;
    native_sector_size_was_null = sector_size == NULL;
    if (native_write_outputs) {
        if (mem_size != NULL) *mem_size = native_memory_size;
        if (sector_size != NULL) *sector_size = native_sector_size;
    }
    return native_result;
}

static int test_translate(
    uint32_t guest_address,
    size_t size,
    size_t *offset_out)
{
    uint64_t offset;

    if (offset_out == NULL || guest_address < TEST_MEMORY_BASE) {
        return 0;
    }
    offset = (uint64_t)guest_address - TEST_MEMORY_BASE;
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
    TestMemory *memory = (TestMemory *)context;
    size_t offset;

    if (memory == NULL || destination == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    memory->read_count++;
    if (guest_address >= UINT32_C(0xCC000000) &&
        guest_address < UINT32_C(0xCD000000)) {
        return PORPOISE_HOST_UNSUPPORTED_MMIO;
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
    TestMemory *memory = (TestMemory *)context;
    size_t offset;

    if (memory == NULL || source == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    memory->write_count++;
    if (memory->fail_write_call != 0U &&
        memory->write_count == memory->fail_write_call) {
        return PORPOISE_HOST_IO_ERROR;
    }
    if (!test_translate(guest_address, size, &offset)) {
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    }
    memcpy(memory->bytes + offset, source, size);
    return PORPOISE_HOST_OK;
}

static void test_write_be32(
    TestMemory *memory,
    uint32_t guest_address,
    uint32_t value)
{
    size_t offset;

    CHECK(test_translate(guest_address, 4U, &offset));
    memory->bytes[offset] = (uint8_t)(value >> 24U);
    memory->bytes[offset + 1U] = (uint8_t)(value >> 16U);
    memory->bytes[offset + 2U] = (uint8_t)(value >> 8U);
    memory->bytes[offset + 3U] = (uint8_t)value;
}

static uint32_t test_read_be32(
    const TestMemory *memory,
    uint32_t guest_address)
{
    size_t offset;

    CHECK(test_translate(guest_address, 4U, &offset));
    return ((uint32_t)memory->bytes[offset] << 24U) |
           ((uint32_t)memory->bytes[offset + 1U] << 16U) |
           ((uint32_t)memory->bytes[offset + 2U] << 8U) |
           (uint32_t)memory->bytes[offset + 3U];
}

static void test_initialize(
    TestMemory *memory,
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    memset(memory, 0, sizeof(*memory));
    memset(host, 0, sizeof(*host));
    host->context = memory;
    host->read_bytes = test_read_bytes;
    host->write_bytes = test_write_bytes;
    porpoise_state_init(state, host);
    state->status = PORPOISE_EXECUTION_RUNNING;
    state->pc = UINT32_C(0x803E6014);
    state->gpr[3] = 1U;
    state->gpr[4] = TEST_MEM_SIZE;
    state->gpr[5] = TEST_SECTOR_SIZE;
    native_call_count = 0U;
    native_result = CARD_RESULT_READY;
    native_write_outputs = 0;
    native_memory_size = 0;
    native_sector_size = 0;
    native_last_channel = 0;
    native_mem_size_was_null = 0;
    native_sector_size_was_null = 0;
}

static void test_success_marshals_big_endian_outputs(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    test_initialize(&memory, &host, &state);
    test_write_be32(&memory, TEST_MEM_SIZE, UINT32_C(0xAAAAAAAA));
    test_write_be32(&memory, TEST_SECTOR_SIZE, UINT32_C(0xBBBBBBBB));
    native_write_outputs = 1;
    native_memory_size = 59;
    native_sector_size = 8192;

    porpoise_libporpoise_card_probe_ex_adapter(&state);

    CHECK(!porpoise_state_has_fault(&state));
    CHECK(native_call_count == 1U);
    CHECK(native_last_channel == 1);
    CHECK(!native_mem_size_was_null);
    CHECK(!native_sector_size_was_null);
    CHECK(test_read_be32(&memory, TEST_MEM_SIZE) == 59U);
    CHECK(test_read_be32(&memory, TEST_SECTOR_SIZE) == 8192U);
    CHECK(state.gpr[3] == (uint32_t)CARD_RESULT_READY);
    CHECK(memory.read_count == 2U);
    CHECK(memory.write_count == 4U);
}

static void test_no_card_preserves_preloaded_outputs(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    test_initialize(&memory, &host, &state);
    test_write_be32(&memory, TEST_MEM_SIZE, UINT32_C(0x12345678));
    test_write_be32(&memory, TEST_SECTOR_SIZE, UINT32_C(0x89ABCDEF));
    native_result = CARD_RESULT_NOCARD;

    porpoise_libporpoise_card_probe_ex_adapter(&state);

    CHECK(!porpoise_state_has_fault(&state));
    CHECK(native_call_count == 1U);
    CHECK(test_read_be32(&memory, TEST_MEM_SIZE) == UINT32_C(0x12345678));
    CHECK(test_read_be32(&memory, TEST_SECTOR_SIZE) == UINT32_C(0x89ABCDEF));
    CHECK(state.gpr[3] == (uint32_t)CARD_RESULT_NOCARD);
    CHECK(memory.write_count == 4U);
}

static void test_nullable_outputs(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    test_initialize(&memory, &host, &state);
    state.gpr[3] = UINT32_MAX;
    state.gpr[4] = 0U;
    state.gpr[5] = 0U;
    native_result = CARD_RESULT_FATAL_ERROR;
    porpoise_libporpoise_card_probe_ex_adapter(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(native_call_count == 1U);
    CHECK(native_last_channel == -1);
    CHECK(native_mem_size_was_null);
    CHECK(native_sector_size_was_null);
    CHECK(memory.read_count == 0U);
    CHECK(memory.write_count == 0U);
    CHECK(state.gpr[3] == (uint32_t)CARD_RESULT_FATAL_ERROR);

    test_initialize(&memory, &host, &state);
    state.gpr[5] = 0U;
    test_write_be32(&memory, TEST_MEM_SIZE, 7U);
    native_write_outputs = 1;
    native_memory_size = 31;
    porpoise_libporpoise_card_probe_ex_adapter(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(!native_mem_size_was_null);
    CHECK(native_sector_size_was_null);
    CHECK(test_read_be32(&memory, TEST_MEM_SIZE) == 31U);
    CHECK(memory.read_count == 1U);
    CHECK(memory.write_count == 2U);

    test_initialize(&memory, &host, &state);
    state.gpr[4] = 0U;
    test_write_be32(&memory, TEST_SECTOR_SIZE, 8U);
    native_write_outputs = 1;
    native_sector_size = 4096;
    porpoise_libporpoise_card_probe_ex_adapter(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(native_mem_size_was_null);
    CHECK(!native_sector_size_was_null);
    CHECK(test_read_be32(&memory, TEST_SECTOR_SIZE) == 4096U);
    CHECK(memory.read_count == 1U);
    CHECK(memory.write_count == 2U);
}

static void test_prevalidation_faults_before_native_call(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    test_initialize(&memory, &host, &state);
    state.gpr[4] = TEST_MEM_SIZE + 1U;
    porpoise_libporpoise_card_probe_ex_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(native_call_count == 0U);
    CHECK(memory.read_count == 0U);
    CHECK(memory.write_count == 0U);

    test_initialize(&memory, &host, &state);
    state.gpr[4] = UINT32_C(0xFFFFFFFD);
    porpoise_libporpoise_card_probe_ex_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_ADDRESS_OVERFLOW);
    CHECK(native_call_count == 0U);
    CHECK(memory.read_count == 0U);
    CHECK(memory.write_count == 0U);

    test_initialize(&memory, &host, &state);
    state.gpr[4] = TEST_MMIO_POINTER;
    porpoise_libporpoise_card_probe_ex_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_MMIO);
    CHECK(native_call_count == 0U);
    CHECK(memory.read_count == 1U);
    CHECK(memory.write_count == 0U);

    test_initialize(&memory, &host, &state);
    state.gpr[4] = TEST_MEMORY_BASE + TEST_MEMORY_SIZE;
    porpoise_libporpoise_card_probe_ex_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
    CHECK(native_call_count == 0U);
    CHECK(memory.read_count == 1U);
    CHECK(memory.write_count == 0U);

    test_initialize(&memory, &host, &state);
    test_write_be32(&memory, TEST_MEM_SIZE, UINT32_C(0xCAFEBABE));
    state.gpr[5] = TEST_MEMORY_BASE + TEST_MEMORY_SIZE;
    porpoise_libporpoise_card_probe_ex_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
    CHECK(native_call_count == 0U);
    CHECK(memory.read_count == 2U);
    CHECK(memory.write_count == 0U);
    CHECK(test_read_be32(&memory, TEST_MEM_SIZE) == UINT32_C(0xCAFEBABE));

    test_initialize(&memory, &host, &state);
    host.write_bytes = NULL;
    porpoise_libporpoise_card_probe_ex_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_MISSING_HOST_CALLBACK);
    CHECK(native_call_count == 0U);
    CHECK(memory.read_count == 0U);
    CHECK(memory.write_count == 0U);
}

static void test_write_preflight_faults_before_native_call(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    test_initialize(&memory, &host, &state);
    test_write_be32(&memory, TEST_MEM_SIZE, 1U);
    test_write_be32(&memory, TEST_SECTOR_SIZE, 2U);
    native_write_outputs = 1;
    native_memory_size = 3;
    native_sector_size = 4;
    memory.fail_write_call = 1U;

    porpoise_libporpoise_card_probe_ex_adapter(&state);

    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    CHECK(native_call_count == 0U);
    CHECK(memory.write_count == 1U);
    CHECK(state.gpr[3] == 1U);
    CHECK(test_read_be32(&memory, TEST_MEM_SIZE) == 1U);
    CHECK(test_read_be32(&memory, TEST_SECTOR_SIZE) == 2U);

    porpoise_libporpoise_card_probe_ex_adapter(&state);
    CHECK(native_call_count == 0U);
    CHECK(memory.write_count == 1U);

    test_initialize(&memory, &host, &state);
    test_write_be32(&memory, TEST_MEM_SIZE, 5U);
    test_write_be32(&memory, TEST_SECTOR_SIZE, 6U);
    native_write_outputs = 1;
    native_memory_size = 7;
    native_sector_size = 8;
    memory.fail_write_call = 2U;

    porpoise_libporpoise_card_probe_ex_adapter(&state);

    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    CHECK(native_call_count == 0U);
    CHECK(memory.write_count == 2U);
    CHECK(test_read_be32(&memory, TEST_MEM_SIZE) == 5U);
    CHECK(test_read_be32(&memory, TEST_SECTOR_SIZE) == 6U);
}

int main(void)
{
    test_success_marshals_big_endian_outputs();
    test_no_card_preserves_preloaded_outputs();
    test_nullable_outputs();
    test_prevalidation_faults_before_native_call();
    test_write_preflight_faults_before_native_call();
    return 0;
}
