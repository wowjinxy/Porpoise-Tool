#include "porpoise_lifted.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_MEMORY_SIZE 128U

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

typedef struct TestMemory {
    uint8_t bytes[TEST_MEMORY_SIZE];
    size_t read_calls;
    size_t write_calls;
    uint32_t last_address;
    size_t last_size;
    int reject_reads;
    int reject_writes;
} TestMemory;

static PorpoiseHostResult test_read_bytes(
    void *context,
    uint32_t guest_address,
    void *destination,
    size_t size)
{
    TestMemory *memory = (TestMemory *)context;
    size_t address = (size_t)guest_address;

    memory->read_calls++;
    memory->last_address = guest_address;
    memory->last_size = size;
    if (memory->reject_reads) {
        return PORPOISE_HOST_IO_ERROR;
    }
    if (address > sizeof(memory->bytes) ||
        size > sizeof(memory->bytes) - address) {
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    }
    memcpy(destination, &memory->bytes[address], size);
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult test_write_bytes(
    void *context,
    uint32_t guest_address,
    const void *source,
    size_t size)
{
    TestMemory *memory = (TestMemory *)context;
    size_t address = (size_t)guest_address;

    memory->write_calls++;
    memory->last_address = guest_address;
    memory->last_size = size;
    if (memory->reject_writes) {
        return PORPOISE_HOST_IO_ERROR;
    }
    if (address > sizeof(memory->bytes) ||
        size > sizeof(memory->bytes) - address) {
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    }
    memcpy(&memory->bytes[address], source, size);
    return PORPOISE_HOST_OK;
}

static PorpoiseHostAdapter test_adapter(TestMemory *memory)
{
    PorpoiseHostAdapter adapter;

    memset(&adapter, 0, sizeof(adapter));
    adapter.context = memory;
    adapter.read_bytes = test_read_bytes;
    adapter.write_bytes = test_write_bytes;
    return adapter;
}

static void enable_psq(PorpoisePpcState *state)
{
    state->gpr[1] = UINT32_C(0x70);
    state->gpr[2] = UINT32_C(0x20);
    state->gpr[13] = UINT32_C(0x30);
    CHECK(porpoise_state_prepare_title_entry(state));
}

static uint32_t load_gqr(unsigned int type, unsigned int scale)
{
    return ((uint32_t)(type & 0x7U) << 16U) |
           ((uint32_t)(scale & 0x3FU) << 24U);
}

static uint32_t store_gqr(unsigned int type, unsigned int scale)
{
    return (uint32_t)(type & 0x7U) |
           ((uint32_t)(scale & 0x3FU) << 8U);
}

static void write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8U);
    bytes[1] = (uint8_t)value;
}

static void write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
}

static void set_double(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index,
    double value)
{
    porpoise_fpr_set_f64(state, register_index, lane_index, value);
    CHECK(state->fault == PORPOISE_FAULT_NONE);
}

static void check_double(
    const PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index,
    double expected)
{
    CHECK(porpoise_fpr_get_f64(state, register_index, lane_index) ==
          expected);
}

static void test_raw_float_transfers(void)
{
    TestMemory memory;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;

    memset(&memory, 0, sizeof(memory));
    adapter = test_adapter(&memory);
    porpoise_state_init(&state, &adapter);
    enable_psq(&state);
    state.gqr[1] = load_gqr(0U, 31U);
    write_u32(&memory.bytes[3], UINT32_C(0x7F812345));
    write_u32(&memory.bytes[7], UINT32_C(0x80000001));
    CHECK(porpoise_psq_load(&state, 2U, 3U, 0U, 1U, 0U));
    CHECK(memory.read_calls == 1U);
    CHECK(memory.last_address == 3U && memory.last_size == 8U);
    CHECK(state.fpr[2].lane_bits[0] ==
          porpoise_binary32_to_binary64_bits(UINT32_C(0x7F812345)));
    CHECK(state.fpr[2].lane_bits[1] ==
          porpoise_binary32_to_binary64_bits(UINT32_C(0x80000001)));

    state.gqr[1] = load_gqr(0U, 63U);
    write_u32(&memory.bytes[17], UINT32_C(0xBF800000));
    state.fpr[2].lane_bits[1] = UINT64_C(0xDEADBEEFCAFEBABE);
    CHECK(porpoise_psq_load(&state, 2U, 17U, 1U, 1U, 0U));
    CHECK(memory.read_calls == 2U);
    CHECK(memory.last_address == 17U && memory.last_size == 4U);
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0xBFF0000000000000));
    CHECK(state.fpr[2].lane_bits[1] == UINT64_C(0x3FF0000000000000));

    state.gqr[2] = store_gqr(0U, 63U);
    state.fpr[3].lane_bits[0] =
        porpoise_binary32_to_binary64_bits(UINT32_C(0x7FC12345));
    state.fpr[3].lane_bits[1] =
        porpoise_binary32_to_binary64_bits(UINT32_C(0x80000001));
    CHECK(porpoise_psq_store(&state, 3U, 29U, 0U, 2U, 0U));
    CHECK(memory.write_calls == 1U);
    CHECK(memory.last_address == 29U && memory.last_size == 8U);
    CHECK(memcmp(
              &memory.bytes[29],
              (const uint8_t[]){0x7FU, 0xC1U, 0x23U, 0x45U,
                                0x80U, 0x00U, 0x00U, 0x01U},
              8U) == 0);

    state.fpr[3].lane_bits[0] =
        porpoise_binary32_to_binary64_bits(UINT32_C(0xFF812345));
    memory.bytes[45] = 0xA5U;
    CHECK(porpoise_psq_store(&state, 3U, 41U, 1U, 2U, 0U));
    CHECK(memory.write_calls == 2U);
    CHECK(memory.last_address == 41U && memory.last_size == 4U);
    CHECK(memcmp(
              &memory.bytes[41],
              (const uint8_t[]){0xFFU, 0x81U, 0x23U, 0x45U},
              4U) == 0);
    CHECK(memory.bytes[45] == 0xA5U);
}

static void test_integer_loads(void)
{
    TestMemory memory;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;

    memset(&memory, 0, sizeof(memory));
    adapter = test_adapter(&memory);
    porpoise_state_init(&state, &adapter);
    enable_psq(&state);

    state.gqr[0] = load_gqr(4U, 0U);
    memory.bytes[1] = 2U;
    memory.bytes[2] = 255U;
    CHECK(porpoise_psq_load(&state, 1U, 1U, 0U, 0U, 0U));
    check_double(&state, 1U, 0U, 2.0);
    check_double(&state, 1U, 1U, 255.0);
    CHECK(memory.last_size == 2U);

    state.gqr[0] = load_gqr(4U, 31U);
    memory.bytes[5] = 128U;
    CHECK(porpoise_psq_load(&state, 1U, 5U, 1U, 0U, 0U));
    check_double(&state, 1U, 0U, 0.000000059604644775390625);
    check_double(&state, 1U, 1U, 1.0);
    CHECK(memory.last_size == 1U);

    state.gqr[1] = load_gqr(5U, 32U);
    write_u16(&memory.bytes[7], UINT16_C(0x0001));
    write_u16(&memory.bytes[9], UINT16_C(0xFFFF));
    CHECK(porpoise_psq_load(&state, 2U, 7U, 0U, 1U, 0U));
    check_double(&state, 2U, 0U, 4294967296.0);
    check_double(&state, 2U, 1U, 281470681743360.0);
    CHECK(memory.last_address == 7U && memory.last_size == 4U);

    state.gqr[1] = load_gqr(5U, 0U);
    write_u16(&memory.bytes[13], UINT16_C(0x1234));
    CHECK(porpoise_psq_load(&state, 2U, 13U, 1U, 1U, 0U));
    check_double(&state, 2U, 0U, 4660.0);
    check_double(&state, 2U, 1U, 1.0);

    state.gqr[2] = load_gqr(6U, 63U);
    memory.bytes[17] = 0x80U;
    memory.bytes[18] = 0x7FU;
    CHECK(porpoise_psq_load(&state, 3U, 17U, 0U, 2U, 0U));
    check_double(&state, 3U, 0U, -256.0);
    check_double(&state, 3U, 1U, 254.0);

    state.gqr[2] = load_gqr(6U, 0U);
    memory.bytes[21] = 0xFEU;
    CHECK(porpoise_psq_load(&state, 3U, 21U, 1U, 2U, 0U));
    check_double(&state, 3U, 0U, -2.0);
    check_double(&state, 3U, 1U, 1.0);

    state.gqr[3] = load_gqr(7U, 0U);
    write_u16(&memory.bytes[23], UINT16_C(0x8000));
    write_u16(&memory.bytes[25], UINT16_C(0x7FFF));
    CHECK(porpoise_psq_load(&state, 4U, 23U, 0U, 3U, 0U));
    check_double(&state, 4U, 0U, -32768.0);
    check_double(&state, 4U, 1U, 32767.0);

    state.gqr[3] = load_gqr(7U, 31U);
    write_u16(&memory.bytes[29], UINT16_C(0x0001));
    CHECK(porpoise_psq_load(&state, 4U, 29U, 1U, 3U, 0U));
    check_double(&state, 4U, 0U, 0.0000000004656612873077392578125);
    check_double(&state, 4U, 1U, 1.0);
    CHECK(memory.read_calls == 8U);
}

static void test_integer_stores(void)
{
    TestMemory memory;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;

    memset(&memory, 0, sizeof(memory));
    adapter = test_adapter(&memory);
    porpoise_state_init(&state, &adapter);
    enable_psq(&state);

    state.gqr[0] = store_gqr(4U, 0U);
    set_double(&state, 1U, 0U, 3.75);
    set_double(&state, 1U, 1U, -2.0);
    CHECK(porpoise_psq_store(&state, 1U, 1U, 0U, 0U, 0U));
    CHECK(memory.bytes[1] == 3U && memory.bytes[2] == 0U);
    CHECK(memory.last_address == 1U && memory.last_size == 2U);

    state.gqr[0] = store_gqr(4U, 31U);
    set_double(&state, 1U, 0U, 0.000000000931322574615478515625);
    memory.bytes[4] = 0xA5U;
    CHECK(porpoise_psq_store(&state, 1U, 3U, 1U, 0U, 0U));
    CHECK(memory.bytes[3] == 2U && memory.bytes[4] == 0xA5U);

    state.gqr[1] = store_gqr(5U, 0U);
    set_double(&state, 2U, 0U, 1.99);
    set_double(&state, 2U, 1U, 70000.0);
    CHECK(porpoise_psq_store(&state, 2U, 5U, 0U, 1U, 0U));
    CHECK(memcmp(
              &memory.bytes[5],
              (const uint8_t[]){0x00U, 0x01U, 0xFFU, 0xFFU},
              4U) == 0);

    state.gqr[1] = store_gqr(5U, 32U);
    set_double(&state, 2U, 0U, 12884901888.0);
    CHECK(porpoise_psq_store(&state, 2U, 11U, 1U, 1U, 0U));
    CHECK(memory.bytes[11] == 0U && memory.bytes[12] == 3U);

    state.gqr[2] = store_gqr(6U, 0U);
    set_double(&state, 3U, 0U, -12.75);
    set_double(&state, 3U, 1U, 200.0);
    CHECK(porpoise_psq_store(&state, 3U, 15U, 0U, 2U, 0U));
    CHECK(memory.bytes[15] == 0xF4U && memory.bytes[16] == 0x7FU);

    state.gqr[2] = store_gqr(6U, 63U);
    set_double(&state, 3U, 0U, -5.0);
    CHECK(porpoise_psq_store(&state, 3U, 19U, 1U, 2U, 0U));
    CHECK(memory.bytes[19] == 0xFEU);

    state.gqr[3] = store_gqr(7U, 0U);
    set_double(&state, 4U, 0U, -40000.0);
    set_double(&state, 4U, 1U, 40000.0);
    CHECK(porpoise_psq_store(&state, 4U, 21U, 0U, 3U, 0U));
    CHECK(memcmp(
              &memory.bytes[21],
              (const uint8_t[]){0x80U, 0x00U, 0x7FU, 0xFFU},
              4U) == 0);

    state.gqr[3] = store_gqr(7U, 0U);
    set_double(&state, 4U, 0U, 123.99);
    CHECK(porpoise_psq_store(&state, 4U, 27U, 1U, 3U, 0U));
    CHECK(memory.bytes[27] == 0U && memory.bytes[28] == 123U);
    CHECK(memory.write_calls == 8U);
}

static void test_nonfinite_store_policy(void)
{
    TestMemory memory;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;

    memset(&memory, 0, sizeof(memory));
    adapter = test_adapter(&memory);
    porpoise_state_init(&state, &adapter);
    enable_psq(&state);

    state.gqr[0] = store_gqr(4U, 0U);
    state.fpr[1].lane_bits[0] = UINT64_C(0xFFF8000000001234);
    state.fpr[1].lane_bits[1] = UINT64_C(0xFFF0000000000000);
    CHECK(porpoise_psq_store(&state, 1U, 3U, 0U, 0U, 0U));
    CHECK(memory.bytes[3] == 0xFFU);
    CHECK(memory.bytes[4] == 0x00U);

    state.gqr[1] = store_gqr(7U, 0U);
    state.fpr[2].lane_bits[0] = UINT64_C(0x7FF0000000000000);
    state.fpr[2].lane_bits[1] = UINT64_C(0xFFF0000000000000);
    CHECK(porpoise_psq_store(&state, 2U, 7U, 0U, 1U, 0U));
    CHECK(memcmp(
              &memory.bytes[7],
              (const uint8_t[]){0x7FU, 0xFFU, 0x80U, 0x00U},
              4U) == 0);

    state.gqr[2] = store_gqr(6U, 0U);
    state.fpr[3].lane_bits[0] = UINT64_C(0xFFF0000000001234);
    CHECK(porpoise_psq_store(&state, 3U, 13U, 1U, 2U, 0U));
    CHECK(memory.bytes[13] == 0x7FU);
}

static void test_mode_gating_and_exception_priority(void)
{
    TestMemory memory;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;
    size_t calls;

    memset(&memory, 0, sizeof(memory));
    adapter = test_adapter(&memory);
    porpoise_state_init(&state, &adapter);
    state.pc = UINT32_C(0x80001234);
    state.gqr[0] = load_gqr(4U, 0U);
    state.fpr[1].lane_bits[0] = UINT64_C(0x1111111111111111);
    state.fpr[1].lane_bits[1] = UINT64_C(0x2222222222222222);
    memory.bytes[3] = 7U;

    CHECK(!porpoise_psq_load(&state, 1U, 3U, 1U, 0U, 1U));
    CHECK(state.fault == PORPOISE_FAULT_ILLEGAL_INSTRUCTION);
    CHECK(state.fault_address == UINT32_C(0x80001234));
    CHECK(strstr(state.fault_message, "HID2[PSE]") != NULL);
    CHECK(memory.read_calls == 0U);
    CHECK(state.fpr[1].lane_bits[0] == UINT64_C(0x1111111111111111));
    CHECK(state.fpr[1].lane_bits[1] == UINT64_C(0x2222222222222222));

    porpoise_state_clear_fault(&state);
    state.hid2 = PORPOISE_HID2_PSE;
    CHECK(!porpoise_psq_load(&state, 1U, 3U, 1U, 0U, 1U));
    CHECK(state.fault == PORPOISE_FAULT_ILLEGAL_INSTRUCTION);
    CHECK(strstr(state.fault_message, "HID2[LSQE]") != NULL);
    CHECK(memory.read_calls == 0U);

    porpoise_state_clear_fault(&state);
    state.hid2 = PORPOISE_HID2_PSE | PORPOISE_HID2_LSQE;
    CHECK(!porpoise_psq_load(&state, 1U, 3U, 1U, 0U, 1U));
    CHECK(state.fault == PORPOISE_FAULT_FLOATING_POINT_UNAVAILABLE);
    CHECK(strcmp(
              porpoise_fault_string(state.fault),
              "floating-point unavailable") == 0);
    CHECK(memory.read_calls == 0U);

    porpoise_state_clear_fault(&state);
    state.hid2 = PORPOISE_HID2_PSE;
    state.msr = PORPOISE_MSR_FP;
    CHECK(porpoise_psq_load(&state, 1U, 3U, 1U, 0U, 0U));
    check_double(&state, 1U, 0U, 7.0);
    check_double(&state, 1U, 1U, 1.0);
    CHECK(memory.read_calls == 1U);

    state.gqr[0] = store_gqr(4U, 0U);
    state.hid2 = PORPOISE_HID2_LSQE;
    set_double(&state, 1U, 0U, 9.0);
    calls = memory.write_calls;
    CHECK(!porpoise_psq_store(&state, 1U, 5U, 1U, 0U, 1U));
    CHECK(state.fault == PORPOISE_FAULT_ILLEGAL_INSTRUCTION);
    CHECK(memory.write_calls == calls);
}

static void test_title_entry_mode_preparation(void)
{
    TestMemory memory;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;

    memset(&memory, 0, sizeof(memory));
    adapter = test_adapter(&memory);
    porpoise_state_init(&state, &adapter);
    CHECK(state.msr == 0U);
    CHECK(state.hid2 == 0U);

    state.msr = UINT32_C(0x00000001);
    state.hid2 = UINT32_C(0x10000000);
    state.pc = UINT32_C(0x80003100);
    state.gpr[1] = UINT32_C(0x70);
    state.gpr[2] = UINT32_C(0x20);
    state.gpr[13] = UINT32_C(0x30);
    state.gpr[3] = UINT32_C(0x12345678);
    CHECK(porpoise_state_prepare_title_entry(&state));

    CHECK(state.msr == (
        UINT32_C(0x00000001) | PORPOISE_MSR_EE | PORPOISE_MSR_FP));
    CHECK(state.hid2 == (
        UINT32_C(0x10000000) | PORPOISE_HID2_PSE | PORPOISE_HID2_LSQE));
    CHECK(state.pc == UINT32_C(0x80003100));
    CHECK(state.gpr[3] == UINT32_C(0x12345678));
    CHECK(state.gpr[1] == UINT32_C(0x70));
    CHECK(state.gpr[2] == UINT32_C(0x20));
    CHECK(state.gpr[13] == UINT32_C(0x30));
    CHECK(state.host == &adapter);

    CHECK(!porpoise_state_prepare_title_entry(NULL));
}

static void test_validation_and_atomic_faults(void)
{
    TestMemory memory;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;
    unsigned int type;
    size_t calls;
    uint8_t snapshot[TEST_MEMORY_SIZE];

    memset(&memory, 0xA5, sizeof(memory.bytes));
    memory.read_calls = 0U;
    memory.write_calls = 0U;
    memory.last_address = 0U;
    memory.last_size = 0U;
    memory.reject_reads = 0;
    memory.reject_writes = 0;
    adapter = test_adapter(&memory);
    porpoise_state_init(&state, &adapter);
    enable_psq(&state);

    for (type = 1U; type <= 3U; type++) {
        state.gqr[0] = load_gqr(type, 0U);
        state.fpr[1].lane_bits[0] = UINT64_C(0x1111111111111111);
        state.fpr[1].lane_bits[1] = UINT64_C(0x2222222222222222);
        calls = memory.read_calls;
        CHECK(!porpoise_psq_load(&state, 1U, 0U, 0U, 0U, 0U));
        CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
        CHECK(strstr(state.fault_message, "GQR load") != NULL);
        CHECK(memory.read_calls == calls);
        CHECK(state.fpr[1].lane_bits[0] ==
              UINT64_C(0x1111111111111111));
        CHECK(state.fpr[1].lane_bits[1] ==
              UINT64_C(0x2222222222222222));
        porpoise_state_clear_fault(&state);

        state.gqr[0] = store_gqr(type, 0U);
        memcpy(snapshot, memory.bytes, sizeof(snapshot));
        calls = memory.write_calls;
        CHECK(!porpoise_psq_store(&state, 1U, 0U, 0U, 0U, 0U));
        CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
        CHECK(strstr(state.fault_message, "GQR store") != NULL);
        CHECK(memory.write_calls == calls);
        CHECK(memcmp(snapshot, memory.bytes, sizeof(snapshot)) == 0);
        porpoise_state_clear_fault(&state);
    }

    calls = memory.read_calls;
    CHECK(!porpoise_psq_load(&state, 32U, 0U, 0U, 0U, 0U));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(memory.read_calls == calls);
    porpoise_state_clear_fault(&state);

    CHECK(!porpoise_psq_load(&state, 1U, 0U, 2U, 0U, 0U));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    porpoise_state_clear_fault(&state);
    CHECK(!porpoise_psq_store(&state, 1U, 0U, 0U, 8U, 0U));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    porpoise_state_clear_fault(&state);
    CHECK(!porpoise_psq_store(&state, 1U, 0U, 0U, 0U, 2U));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    porpoise_state_clear_fault(&state);

    state.gqr[0] = load_gqr(0U, 0U);
    state.fpr[1].lane_bits[0] = UINT64_C(0x3333333333333333);
    state.fpr[1].lane_bits[1] = UINT64_C(0x4444444444444444);
    calls = memory.read_calls;
    CHECK(!porpoise_psq_load(
        &state,
        1U,
        UINT32_MAX - 3U,
        0U,
        0U,
        0U));
    CHECK(state.fault == PORPOISE_FAULT_ADDRESS_OVERFLOW);
    CHECK(memory.read_calls == calls);
    CHECK(state.fpr[1].lane_bits[0] ==
          UINT64_C(0x3333333333333333));
    CHECK(state.fpr[1].lane_bits[1] ==
          UINT64_C(0x4444444444444444));
    porpoise_state_clear_fault(&state);

    state.gqr[0] = store_gqr(0U, 0U);
    memcpy(snapshot, memory.bytes, sizeof(snapshot));
    calls = memory.write_calls;
    CHECK(!porpoise_psq_store(
        &state,
        1U,
        UINT32_MAX - 3U,
        0U,
        0U,
        0U));
    CHECK(state.fault == PORPOISE_FAULT_ADDRESS_OVERFLOW);
    CHECK(memory.write_calls == calls);
    CHECK(memcmp(snapshot, memory.bytes, sizeof(snapshot)) == 0);
    porpoise_state_clear_fault(&state);

    state.gqr[0] = load_gqr(4U, 0U);
    state.fpr[1].lane_bits[0] = UINT64_C(0x5555555555555555);
    state.fpr[1].lane_bits[1] = UINT64_C(0x6666666666666666);
    memory.reject_reads = 1;
    calls = memory.read_calls;
    CHECK(!porpoise_psq_load(&state, 1U, 1U, 0U, 0U, 0U));
    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    CHECK(memory.read_calls == calls + 1U);
    CHECK(state.fpr[1].lane_bits[0] ==
          UINT64_C(0x5555555555555555));
    CHECK(state.fpr[1].lane_bits[1] ==
          UINT64_C(0x6666666666666666));
    memory.reject_reads = 0;
    porpoise_state_clear_fault(&state);

    state.gqr[0] = store_gqr(4U, 0U);
    memcpy(snapshot, memory.bytes, sizeof(snapshot));
    memory.reject_writes = 1;
    calls = memory.write_calls;
    CHECK(!porpoise_psq_store(&state, 1U, 1U, 0U, 0U, 0U));
    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    CHECK(memory.write_calls == calls + 1U);
    CHECK(memcmp(snapshot, memory.bytes, sizeof(snapshot)) == 0);
}

int main(void)
{
    test_raw_float_transfers();
    test_integer_loads();
    test_integer_stores();
    test_nonfinite_store_policy();
    test_title_entry_mode_preparation();
    test_mode_gating_and_exception_priority();
    test_validation_and_atomic_faults();
    return 0;
}
