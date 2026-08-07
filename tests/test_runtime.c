#include "porpoise_lifted.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct TestMemory {
    uint8_t bytes[64];
} TestMemory;

/* cppcheck-suppress constParameterCallback -- signature is fixed by the host ABI. */
static PorpoiseHostResult read_bytes(
    void *context,
    uint32_t address,
    void *destination,
    size_t size) {
    const TestMemory *memory = (const TestMemory *)context;
    if (address > sizeof(memory->bytes) || size > sizeof(memory->bytes) - address)
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    memcpy(destination, &memory->bytes[address], size);
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult write_bytes(
    void *context,
    uint32_t address,
    const void *source,
    size_t size) {
    TestMemory *memory = (TestMemory *)context;
    if (address > sizeof(memory->bytes) || size > sizeof(memory->bytes) - address)
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    memcpy(&memory->bytes[address], source, size);
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult decode_pointer(void *context, uint32_t address, void **result) {
    TestMemory *memory = (TestMemory *)context;
    if (address >= sizeof(memory->bytes)) return PORPOISE_HOST_UNMAPPED_ADDRESS;
    *result = &memory->bytes[address];
    return PORPOISE_HOST_OK;
}

/* cppcheck-suppress constParameterCallback -- signature is fixed by the host ABI. */
static PorpoiseHostResult encode_pointer(void *context, const void *pointer, uint32_t *result) {
    const TestMemory *memory = (const TestMemory *)context;
    const uint8_t *value = (const uint8_t *)pointer;
    if (value < memory->bytes || value >= memory->bytes + sizeof(memory->bytes))
        return PORPOISE_HOST_INVALID_POINTER;
    *result = (uint32_t)(value - memory->bytes);
    return PORPOISE_HOST_OK;
}

int main(void) {
    TestMemory memory;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;
    memset(&memory, 0, sizeof(memory));
    adapter.context = &memory;
    adapter.read_bytes = read_bytes;
    adapter.write_bytes = write_bytes;
    adapter.decode_pointer = decode_pointer;
    adapter.encode_pointer = encode_pointer;
    porpoise_state_init(&state, &adapter);
    assert(state.status == PORPOISE_EXECUTION_READY);

    porpoise_store_u32(&state, 1U, UINT32_C(0x12345678));
    assert(memory.bytes[1] == 0x12U && memory.bytes[2] == 0x34U);
    assert(memory.bytes[3] == 0x56U && memory.bytes[4] == 0x78U);
    assert(porpoise_load_u32(&state, 1U) == UINT32_C(0x12345678));

    porpoise_store_u16(&state, 3U, UINT16_C(0xABCD));
    assert(porpoise_load_u16(&state, 3U) == UINT16_C(0xABCD));
    porpoise_store_f32(&state, 5U, 1.5F);
    assert(porpoise_load_f32(&state, 5U) == 1.5F);

    assert(porpoise_shift_left32(1U, 31U) == UINT32_C(0x80000000));
    assert(porpoise_shift_left32(1U, 32U) == 0U);
    assert(porpoise_shift_right32(UINT32_C(0x80000000), 32U) == 0U);
    assert(porpoise_shift_left32(1U, 64U) == 1U);
    assert(porpoise_rotate_left32(UINT32_C(0x12345678), 0U) == UINT32_C(0x12345678));
    assert(porpoise_rotate_left32(UINT32_C(0x12345678), 32U) == UINT32_C(0x12345678));
    assert(porpoise_mask32(0U, 0U) == UINT32_C(0x80000000));
    assert(porpoise_mask32(28U, 3U) == UINT32_C(0xF000000F));
    assert(porpoise_arithmetic_shift_right32(&state, UINT32_C(0x80000001), 0U) == UINT32_C(0x80000001));
    assert(porpoise_arithmetic_shift_right32(&state, UINT32_C(0x80000001), 31U) == UINT32_MAX);
    assert((state.xer & UINT32_C(0x20000000)) != 0U);

    porpoise_compare_signed(&state, 0U, UINT32_MAX, 0U);
    assert(porpoise_cr_get_field(&state, 0U) == 8U);
    porpoise_compare_unsigned(&state, 1U, UINT32_MAX, 0U);
    assert(porpoise_cr_get_field(&state, 1U) == 4U);

    assert(porpoise_decode_pointer(&state, 7U) == &memory.bytes[7]);
    assert(porpoise_encode_pointer(&state, &memory.bytes[9]) == 9U);
    assert(porpoise_decode_pointer(&state, 0U) == NULL);
    assert(porpoise_encode_pointer(&state, NULL) == 0U);
    assert(!porpoise_state_has_fault(&state));

    porpoise_store_u16(&state, UINT32_MAX, 1U);
    assert(state.fault == PORPOISE_FAULT_ADDRESS_OVERFLOW);
    assert(state.status == PORPOISE_EXECUTION_FAULTED);
    porpoise_state_clear_fault(&state);
    assert(!porpoise_state_has_fault(&state));
    assert(state.status == PORPOISE_EXECUTION_READY);
    return 0;
}
