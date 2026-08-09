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

static void test_fpscr_helpers(void) {
    PorpoisePpcState state;

    porpoise_state_init(&state, NULL);
    porpoise_fpscr_raise_exceptions(&state, PORPOISE_FPSCR_VXSNAN);
    CHECK((state.fpscr & PORPOISE_FPSCR_FX) != 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_VX) != 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_VXSNAN) != 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_FEX) == 0U);

    /* FX changes only when a previously clear exception cause is raised. */
    state.fpscr &= ~PORPOISE_FPSCR_FX;
    porpoise_fpscr_raise_exceptions(&state, PORPOISE_FPSCR_VXSNAN);
    CHECK((state.fpscr & PORPOISE_FPSCR_FX) == 0U);
    state.fpscr |= PORPOISE_FPSCR_VE;
    porpoise_fpscr_recompute_summaries(&state);
    CHECK((state.fpscr & PORPOISE_FPSCR_FEX) != 0U);
    state.fpscr &= ~PORPOISE_FPSCR_VXSNAN;
    porpoise_fpscr_recompute_summaries(&state);
    CHECK((state.fpscr & PORPOISE_FPSCR_VX) == 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_FEX) == 0U);

    state.cr = UINT32_C(0x12345678);
    state.fpscr = PORPOISE_FPSCR_FX |
                  PORPOISE_FPSCR_VX |
                  PORPOISE_FPSCR_OX;
    porpoise_fpscr_update_cr1(&state);
    CHECK(porpoise_cr_get_field(&state, 0U) == 1U);
    CHECK(porpoise_cr_get_field(&state, 1U) == 0x0BU);
    CHECK(porpoise_cr_get_field(&state, 2U) == 3U);
}

static void test_float_comparisons(void) {
    const uint64_t positive_zero = UINT64_C(0x0000000000000000);
    const uint64_t negative_zero = UINT64_C(0x8000000000000000);
    const uint64_t positive_one = UINT64_C(0x3FF0000000000000);
    const uint64_t negative_one = UINT64_C(0xBFF0000000000000);
    const uint64_t positive_two = UINT64_C(0x4000000000000000);
    const uint64_t negative_two = UINT64_C(0xC000000000000000);
    const uint64_t positive_infinity = UINT64_C(0x7FF0000000000000);
    const uint64_t negative_infinity = UINT64_C(0xFFF0000000000000);
    const uint64_t quiet_nan = UINT64_C(0x7FF8000012345678);
    const uint64_t signaling_nan = UINT64_C(0x7FF0000012345678);
    PorpoisePpcState state;
    uint32_t original_cr;
    uint32_t original_fpscr;

    porpoise_state_init(&state, NULL);
    state.cr = UINT32_C(0x12345678);
    state.fpscr = UINT32_C(0x00010000);
    porpoise_fcmpu(&state, 6U, positive_one, positive_two);
    CHECK(porpoise_cr_get_field(&state, 6U) == PORPOISE_FPCC_LESS);
    CHECK((state.fpscr & PORPOISE_FPSCR_FPCC_MASK) ==
          ((uint32_t)PORPOISE_FPCC_LESS << 12U));
    CHECK((state.fpscr & UINT32_C(0x00010000)) != 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_EXCEPTION_CAUSE_MASK) == 0U);
    CHECK(porpoise_cr_get_field(&state, 0U) == 1U);

    porpoise_fcmpo(&state, 2U, negative_zero, positive_zero);
    CHECK(porpoise_cr_get_field(&state, 2U) == PORPOISE_FPCC_EQUAL);
    porpoise_fcmpu(&state, 2U, negative_two, negative_one);
    CHECK(porpoise_cr_get_field(&state, 2U) == PORPOISE_FPCC_LESS);
    porpoise_fcmpu(&state, 2U, positive_infinity, positive_two);
    CHECK(porpoise_cr_get_field(&state, 2U) == PORPOISE_FPCC_GREATER);
    porpoise_fcmpu(&state, 2U, negative_infinity, negative_two);
    CHECK(porpoise_cr_get_field(&state, 2U) == PORPOISE_FPCC_LESS);

    porpoise_state_init(&state, NULL);
    state.fpscr = PORPOISE_FPSCR_VE;
    porpoise_fcmpu(&state, 3U, quiet_nan, positive_one);
    CHECK(porpoise_cr_get_field(&state, 3U) == PORPOISE_FPCC_UNORDERED);
    CHECK((state.fpscr & PORPOISE_FPSCR_INVALID_CAUSE_MASK) == 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_FEX) == 0U);
    CHECK(!porpoise_state_has_fault(&state));

    porpoise_state_init(&state, NULL);
    porpoise_fcmpu(&state, 4U, signaling_nan, positive_one);
    CHECK(porpoise_cr_get_field(&state, 4U) == PORPOISE_FPCC_UNORDERED);
    CHECK((state.fpscr & PORPOISE_FPSCR_FX) != 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_VX) != 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_VXSNAN) != 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_VXVC) == 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_FEX) == 0U);
    CHECK(!porpoise_state_has_fault(&state));

    porpoise_state_init(&state, NULL);
    porpoise_fcmpo(&state, 5U, quiet_nan, positive_one);
    CHECK(porpoise_cr_get_field(&state, 5U) == PORPOISE_FPCC_UNORDERED);
    CHECK((state.fpscr & PORPOISE_FPSCR_VXVC) != 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_VXSNAN) == 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_VX) != 0U);

    porpoise_state_init(&state, NULL);
    porpoise_fcmpo(&state, 5U, signaling_nan, positive_one);
    CHECK((state.fpscr & PORPOISE_FPSCR_VXSNAN) != 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_VXVC) != 0U);
    CHECK(!porpoise_state_has_fault(&state));

    porpoise_state_init(&state, NULL);
    state.pc = UINT32_C(0x80001000);
    state.fpscr = PORPOISE_FPSCR_VE;
    porpoise_fcmpo(&state, 7U, quiet_nan, positive_one);
    CHECK(porpoise_cr_get_field(&state, 7U) == PORPOISE_FPCC_UNORDERED);
    CHECK((state.fpscr & PORPOISE_FPSCR_VXVC) != 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_VXSNAN) == 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_FEX) != 0U);
    CHECK(state.fault == PORPOISE_FAULT_FLOATING_POINT_EXCEPTION);
    CHECK(state.fault_address == UINT32_C(0x80001000));

    porpoise_state_init(&state, NULL);
    state.pc = UINT32_C(0x80001234);
    state.fpscr = PORPOISE_FPSCR_VE;
    porpoise_fcmpo(&state, 7U, signaling_nan, quiet_nan);
    CHECK(porpoise_cr_get_field(&state, 7U) == PORPOISE_FPCC_UNORDERED);
    CHECK((state.fpscr & PORPOISE_FPSCR_VXSNAN) != 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_VXVC) == 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_VX) != 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_FEX) != 0U);
    CHECK(state.fault == PORPOISE_FAULT_FLOATING_POINT_EXCEPTION);
    CHECK(state.status == PORPOISE_EXECUTION_FAULTED);
    CHECK(state.fault_address == UINT32_C(0x80001234));
    CHECK(strcmp(porpoise_state_fault_message(&state),
                 "enabled invalid operation during fcmpo") == 0);

    porpoise_state_init(&state, NULL);
    state.pc = UINT32_C(0x80005678);
    state.fpscr = PORPOISE_FPSCR_VE;
    porpoise_fcmpu(&state, 1U, signaling_nan, positive_one);
    CHECK(porpoise_cr_get_field(&state, 1U) == PORPOISE_FPCC_UNORDERED);
    CHECK((state.fpscr & PORPOISE_FPSCR_VXSNAN) != 0U);
    CHECK((state.fpscr & PORPOISE_FPSCR_FEX) != 0U);
    CHECK(state.fault == PORPOISE_FAULT_FLOATING_POINT_EXCEPTION);
    CHECK(state.fault_address == UINT32_C(0x80005678));

    porpoise_state_init(&state, NULL);
    state.pc = UINT32_C(0x80009ABC);
    state.cr = UINT32_C(0x89ABCDEF);
    state.fpscr = PORPOISE_FPSCR_VE | UINT32_C(0x00010000);
    original_cr = state.cr;
    original_fpscr = state.fpscr;
    porpoise_fcmpo(&state, 8U, signaling_nan, positive_one);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(state.fault_address == UINT32_C(0x80009ABC));
    CHECK(state.cr == original_cr);
    CHECK(state.fpscr == original_fpscr);
}

static void test_fsel(void) {
    const uint64_t nonnegative_bits = UINT64_C(0x7FF0000012345678);
    const uint64_t negative_bits = UINT64_C(0xFFF80000ABCDEF01);
    const uint64_t signaling_payload = UINT64_C(0x7FF000000000CAFE);

    CHECK(porpoise_fsel_bits(
              UINT64_C(0x3FF0000000000000),
              nonnegative_bits,
              negative_bits) == nonnegative_bits);
    CHECK(porpoise_fsel_bits(
              UINT64_C(0xBFF0000000000000),
              nonnegative_bits,
              negative_bits) == negative_bits);
    CHECK(porpoise_fsel_bits(
              UINT64_C(0x0000000000000000),
              nonnegative_bits,
              negative_bits) == nonnegative_bits);
    CHECK(porpoise_fsel_bits(
              UINT64_C(0x8000000000000000),
              nonnegative_bits,
              negative_bits) == nonnegative_bits);
    CHECK(porpoise_fsel_bits(
              UINT64_C(0x7FF0000000000000),
              nonnegative_bits,
              negative_bits) == nonnegative_bits);
    CHECK(porpoise_fsel_bits(
              UINT64_C(0xFFF0000000000000),
              nonnegative_bits,
              negative_bits) == negative_bits);
    CHECK(porpoise_fsel_bits(
              UINT64_C(0x7FF8000000000123),
              nonnegative_bits,
              negative_bits) == negative_bits);
    CHECK(porpoise_fsel_bits(
              UINT64_C(0xFFF0000000000123),
              nonnegative_bits,
              negative_bits) == negative_bits);
    CHECK(porpoise_fsel_bits(
              UINT64_C(0x3FF0000000000000),
              signaling_payload,
              negative_bits) == signaling_payload);
}

static void test_floating_format_conversions(void) {
    static const struct {
        uint32_t binary32_bits;
        uint64_t binary64_bits;
    } vectors[] = {
        {UINT32_C(0x00000000), UINT64_C(0x0000000000000000)},
        {UINT32_C(0x80000000), UINT64_C(0x8000000000000000)},
        {UINT32_C(0x00000001), UINT64_C(0x36A0000000000000)},
        {UINT32_C(0x80000001), UINT64_C(0xB6A0000000000000)},
        {UINT32_C(0x007FFFFF), UINT64_C(0x380FFFFFC0000000)},
        {UINT32_C(0x00800000), UINT64_C(0x3810000000000000)},
        {UINT32_C(0x3F800000), UINT64_C(0x3FF0000000000000)},
        {UINT32_C(0x7F7FFFFF), UINT64_C(0x47EFFFFFE0000000)},
        {UINT32_C(0x7F800000), UINT64_C(0x7FF0000000000000)},
        {UINT32_C(0xFF800000), UINT64_C(0xFFF0000000000000)},
        {UINT32_C(0x7FC12345), UINT64_C(0x7FF82468A0000000)},
        {UINT32_C(0xFF812345), UINT64_C(0xFFF02468A0000000)},
    };
    size_t index;

    for (index = 0U; index < sizeof(vectors) / sizeof(vectors[0]); index++) {
        CHECK(porpoise_binary32_to_binary64_bits(
                  vectors[index].binary32_bits) ==
              vectors[index].binary64_bits);
        CHECK(porpoise_binary64_to_binary32_bits(
                  vectors[index].binary64_bits) ==
              vectors[index].binary32_bits);
    }

    /*
     * stfs does not define IEEE rounding for an arbitrary double: its source
     * must already be single-representable. The deterministic D.7 model keeps
     * retained bits and discards lower bits for these out-of-contract values.
     */
    CHECK(porpoise_binary64_to_binary32_bits(
              UINT64_C(0x3FF0000010000000)) == UINT32_C(0x3F800000));
    CHECK(porpoise_binary64_to_binary32_bits(
              UINT64_C(0x3FF000001FFFFFFF)) == UINT32_C(0x3F800000));
    CHECK(porpoise_binary64_to_binary32_bits(
              UINT64_C(0x3FF0000020000000)) == UINT32_C(0x3F800001));
    CHECK(porpoise_binary64_to_binary32_bits(
              UINT64_C(0x47F0000000000000)) == UINT32_C(0x7F800000));
    CHECK(porpoise_binary64_to_binary32_bits(
              UINT64_C(0x3690000000000000)) == UINT32_C(0x34800000));
}

static void test_raw_fpr_memory(void) {
    TestMemory memory;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;
    uint32_t original_fpscr;

    memset(&memory, 0, sizeof(memory));
    adapter.context = &memory;
    adapter.read_bytes = read_bytes;
    adapter.write_bytes = write_bytes;
    adapter.decode_pointer = decode_pointer;
    adapter.encode_pointer = encode_pointer;
    porpoise_state_init(&state, &adapter);
    state.fpscr = PORPOISE_FPSCR_VE | PORPOISE_FPSCR_XX;
    original_fpscr = state.fpscr;

    memory.bytes[1] = 0x7FU;
    memory.bytes[2] = 0xC1U;
    memory.bytes[3] = 0x23U;
    memory.bytes[4] = 0x45U;
    state.fpr[2].lane_bits[1] = UINT64_C(0x1122334455667788);
    CHECK(porpoise_fpr_load_binary32(&state, 2U, 0U, 1U));
    CHECK(state.fpr[2].lane_bits[0] == UINT64_C(0x7FF82468A0000000));
    CHECK(state.fpr[2].lane_bits[1] == UINT64_C(0x1122334455667788));
    CHECK(state.fpscr == original_fpscr);
    CHECK(porpoise_fpr_store_binary32(&state, 2U, 0U, 5U));
    CHECK(memory.bytes[5] == 0x7FU && memory.bytes[6] == 0xC1U);
    CHECK(memory.bytes[7] == 0x23U && memory.bytes[8] == 0x45U);

    memory.bytes[9] = 0xFFU;
    memory.bytes[10] = 0x81U;
    memory.bytes[11] = 0x23U;
    memory.bytes[12] = 0x45U;
    CHECK(porpoise_fpr_load_binary32(&state, 2U, 1U, 9U));
    CHECK(state.fpr[2].lane_bits[1] == UINT64_C(0xFFF02468A0000000));
    CHECK(state.fpscr == original_fpscr);
    CHECK(porpoise_fpr_store_binary32(&state, 2U, 1U, 13U));
    CHECK(memory.bytes[13] == 0xFFU && memory.bytes[14] == 0x81U);
    CHECK(memory.bytes[15] == 0x23U && memory.bytes[16] == 0x45U);

    memory.bytes[17] = 0x7FU;
    memory.bytes[18] = 0xF0U;
    memory.bytes[19] = 0x00U;
    memory.bytes[20] = 0x00U;
    memory.bytes[21] = 0x00U;
    memory.bytes[22] = 0x00U;
    memory.bytes[23] = 0xCAU;
    memory.bytes[24] = 0xFEU;
    CHECK(porpoise_fpr_load_binary64(&state, 3U, 0U, 17U));
    CHECK(state.fpr[3].lane_bits[0] == UINT64_C(0x7FF000000000CAFE));
    CHECK(porpoise_fpr_store_binary64(&state, 3U, 0U, 25U));
    CHECK(memcmp(&memory.bytes[17], &memory.bytes[25], 8U) == 0);
    CHECK(state.fpscr == original_fpscr);

    state.fpr[4].lane_bits[0] = UINT64_C(0xDEADBEEFCAFEBABE);
    CHECK(!porpoise_fpr_load_binary32(&state, 4U, 0U, 61U));
    CHECK(state.fpr[4].lane_bits[0] == UINT64_C(0xDEADBEEFCAFEBABE));
    CHECK(state.fault == PORPOISE_FAULT_UNMAPPED_ADDRESS);
    porpoise_state_clear_fault(&state);

    memory.bytes[36] = 0xA5U;
    memory.bytes[37] = 0xA5U;
    memory.bytes[38] = 0xA5U;
    memory.bytes[39] = 0xA5U;
    CHECK(!porpoise_fpr_store_binary32(&state, 32U, 0U, 36U));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(memory.bytes[36] == 0xA5U && memory.bytes[39] == 0xA5U);
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
    test_fpscr_helpers();
    test_float_comparisons();
    test_fsel();
    test_floating_format_conversions();
    test_raw_fpr_memory();
    porpoise_state_init(&state, &adapter);
    CHECK(state.status == PORPOISE_EXECUTION_READY);
    CHECK(state.fpscr == 0U);

    porpoise_fpr_set_bits(
        &state,
        1U,
        1U,
        UINT64_C(0x7FF8000012345678));
    porpoise_fpr_set_f64(&state, 1U, 0U, 1.5);
    CHECK(porpoise_fpr_get_f64(&state, 1U, 0U) == 1.5);
    CHECK(porpoise_fpr_get_bits(&state, 1U, 1U) ==
          UINT64_C(0x7FF8000012345678));
    porpoise_fpr_set_bits(
        &state,
        1U,
        0U,
        UINT64_C(0x8000000000000000));
    CHECK(porpoise_fpr_get_bits(&state, 1U, 0U) ==
          UINT64_C(0x8000000000000000));
    porpoise_fpr_set_bits(&state, 32U, 0U, 0U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    porpoise_state_clear_fault(&state);

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
