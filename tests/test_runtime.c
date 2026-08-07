#include "porpoise_lifted.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) abort(); } while (0)

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
    CHECK(state.status == PORPOISE_EXECUTION_READY);

    porpoise_store_u32(&state, 1U, UINT32_C(0x12345678));
    CHECK(memory.bytes[1] == 0x12U && memory.bytes[2] == 0x34U);
    CHECK(memory.bytes[3] == 0x56U && memory.bytes[4] == 0x78U);
    CHECK(porpoise_load_u32(&state, 1U) == UINT32_C(0x12345678));

    porpoise_store_u16(&state, 3U, UINT16_C(0xABCD));
    CHECK(porpoise_load_u16(&state, 3U) == UINT16_C(0xABCD));
    porpoise_store_f32(&state, 5U, 1.5F);
    CHECK(porpoise_load_f32(&state, 5U) == 1.5F);

    state.gpr[29] = UINT32_C(0x11223344);
    state.gpr[30] = UINT32_C(0x55667788);
    state.gpr[31] = UINT32_C(0x99AABBCC);
    CHECK(porpoise_store_multiple_words(&state, 16U, 29U));
    CHECK(memory.bytes[16] == 0x11U && memory.bytes[19] == 0x44U);
    CHECK(memory.bytes[20] == 0x55U && memory.bytes[23] == 0x88U);
    CHECK(memory.bytes[24] == 0x99U && memory.bytes[27] == 0xCCU);
    state.gpr[29] = 0U;
    state.gpr[30] = 0U;
    state.gpr[31] = 0U;
    CHECK(porpoise_load_multiple_words(&state, 16U, 29U));
    CHECK(state.gpr[29] == UINT32_C(0x11223344));
    CHECK(state.gpr[30] == UINT32_C(0x55667788));
    CHECK(state.gpr[31] == UINT32_C(0x99AABBCC));
    state.gpr[30] = UINT32_C(0xDEADBEEF);
    state.gpr[31] = UINT32_C(0xCAFEBABE);
    CHECK(!porpoise_load_multiple_words(&state, 60U, 30U));
    CHECK(state.gpr[30] == UINT32_C(0xDEADBEEF));
    CHECK(state.gpr[31] == UINT32_C(0xCAFEBABE));
    porpoise_state_clear_fault(&state);
    memory.bytes[60] = 0xA5U;
    memory.bytes[61] = 0xA5U;
    memory.bytes[62] = 0xA5U;
    memory.bytes[63] = 0xA5U;
    CHECK(!porpoise_store_multiple_words(&state, 60U, 30U));
    CHECK(memory.bytes[60] == 0xA5U && memory.bytes[63] == 0xA5U);
    porpoise_state_clear_fault(&state);
    CHECK(!porpoise_load_multiple_words(&state, UINT32_MAX - 3U, 30U));
    CHECK(state.fault == PORPOISE_FAULT_ADDRESS_OVERFLOW);
    CHECK(state.gpr[30] == UINT32_C(0xDEADBEEF));
    CHECK(state.gpr[31] == UINT32_C(0xCAFEBABE));
    porpoise_state_clear_fault(&state);
    CHECK(!porpoise_load_multiple_words(&state, 0U, 32U));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    porpoise_state_clear_fault(&state);

    CHECK(porpoise_shift_left32(1U, 31U) == UINT32_C(0x80000000));
    CHECK(porpoise_shift_left32(1U, 32U) == 0U);
    CHECK(porpoise_shift_right32(UINT32_C(0x80000000), 32U) == 0U);
    CHECK(porpoise_shift_left32(1U, 64U) == 1U);
    CHECK(porpoise_sign_extend8(UINT32_C(0x7F)) == UINT32_C(0x7F));
    CHECK(porpoise_sign_extend8(UINT32_C(0x80)) == UINT32_C(0xFFFFFF80));
    CHECK(porpoise_sign_extend16(UINT32_C(0x7FFF)) == UINT32_C(0x7FFF));
    CHECK(porpoise_sign_extend16(UINT32_C(0x8000)) == UINT32_C(0xFFFF8000));
    CHECK(porpoise_count_leading_zeros32(0U) == 32U);
    CHECK(porpoise_count_leading_zeros32(1U) == 31U);
    CHECK(porpoise_count_leading_zeros32(UINT32_C(0x00F00000)) == 8U);
    CHECK(porpoise_add_with_carry32(&state, UINT32_MAX, 0U, 1U) == 0U);
    CHECK((state.xer & UINT32_C(0x20000000)) != 0U);
    CHECK(porpoise_add_with_carry32(&state, 1U, 2U, 0U) == 3U);
    CHECK((state.xer & UINT32_C(0x20000000)) == 0U);
    CHECK(porpoise_rotate_left32(UINT32_C(0x12345678), 0U) == UINT32_C(0x12345678));
    CHECK(porpoise_rotate_left32(UINT32_C(0x12345678), 32U) == UINT32_C(0x12345678));
    CHECK(porpoise_mask32(0U, 0U) == UINT32_C(0x80000000));
    CHECK(porpoise_mask32(28U, 3U) == UINT32_C(0xF000000F));
    CHECK(porpoise_arithmetic_shift_right32(&state, UINT32_C(0x80000001), 0U) == UINT32_C(0x80000001));
    CHECK(porpoise_arithmetic_shift_right32(&state, UINT32_C(0x80000001), 31U) == UINT32_MAX);
    CHECK((state.xer & UINT32_C(0x20000000)) != 0U);
    CHECK(porpoise_arithmetic_shift_right32(&state, UINT32_C(0x80000000), 32U) == UINT32_MAX);
    CHECK((state.xer & UINT32_C(0x20000000)) != 0U);
    CHECK(porpoise_arithmetic_shift_right32(&state, UINT32_C(0x7FFFFFFF), 63U) == 0U);
    CHECK((state.xer & UINT32_C(0x20000000)) == 0U);

    porpoise_compare_signed(&state, 0U, UINT32_MAX, 0U);
    CHECK(porpoise_cr_get_field(&state, 0U) == 8U);
    porpoise_compare_signed(&state, 0U, UINT32_C(0x80000000), UINT32_MAX);
    CHECK(porpoise_cr_get_field(&state, 0U) == 8U);
    porpoise_compare_signed(&state, 0U, UINT32_C(0x7FFFFFFF), UINT32_C(0x80000000));
    CHECK(porpoise_cr_get_field(&state, 0U) == 4U);
    porpoise_set_cr0_result(&state, UINT32_C(0x80000000));
    CHECK(porpoise_cr_get_field(&state, 0U) == 8U);
    porpoise_compare_unsigned(&state, 1U, UINT32_MAX, 0U);
    CHECK(porpoise_cr_get_field(&state, 1U) == 4U);

    CHECK(porpoise_decode_pointer(&state, 7U) == &memory.bytes[7]);
    CHECK(porpoise_encode_pointer(&state, &memory.bytes[9]) == 9U);
    CHECK(porpoise_decode_pointer(&state, 0U) == NULL);
    CHECK(porpoise_encode_pointer(&state, NULL) == 0U);
    CHECK(!porpoise_state_has_fault(&state));

    porpoise_store_u16(&state, UINT32_MAX, 1U);
    CHECK(state.fault == PORPOISE_FAULT_ADDRESS_OVERFLOW);
    CHECK(state.status == PORPOISE_EXECUTION_FAULTED);
    porpoise_state_clear_fault(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(state.status == PORPOISE_EXECUTION_READY);
    return 0;
}
