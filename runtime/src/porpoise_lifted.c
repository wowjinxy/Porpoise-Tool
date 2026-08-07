#include "porpoise_lifted.h"

#include <stdio.h>
#include <string.h>

static PorpoiseFault porpoise_fault_from_host_result(
    PorpoiseHostResult result)
{
    switch (result) {
        case PORPOISE_HOST_OK:
            return PORPOISE_FAULT_NONE;
        case PORPOISE_HOST_INVALID_ARGUMENT:
            return PORPOISE_FAULT_INVALID_ARGUMENT;
        case PORPOISE_HOST_INVALID_POINTER:
            return PORPOISE_FAULT_INVALID_POINTER;
        case PORPOISE_HOST_UNMAPPED_ADDRESS:
            return PORPOISE_FAULT_UNMAPPED_ADDRESS;
        case PORPOISE_HOST_UNSUPPORTED_MMIO:
            return PORPOISE_FAULT_UNSUPPORTED_MMIO;
        case PORPOISE_HOST_ADDRESS_OVERFLOW:
            return PORPOISE_FAULT_ADDRESS_OVERFLOW;
        case PORPOISE_HOST_IO_ERROR:
            return PORPOISE_FAULT_HOST_IO;
        default:
            return PORPOISE_FAULT_HOST_IO;
    }
}

const char *porpoise_fault_string(PorpoiseFault fault)
{
    switch (fault) {
        case PORPOISE_FAULT_NONE:
            return "no fault";
        case PORPOISE_FAULT_INVALID_STATE:
            return "invalid PPC state";
        case PORPOISE_FAULT_NO_HOST_ADAPTER:
            return "no host adapter";
        case PORPOISE_FAULT_MISSING_HOST_CALLBACK:
            return "missing host callback";
        case PORPOISE_FAULT_INVALID_ARGUMENT:
            return "invalid argument";
        case PORPOISE_FAULT_INVALID_POINTER:
            return "invalid pointer";
        case PORPOISE_FAULT_UNMAPPED_ADDRESS:
            return "unmapped guest address";
        case PORPOISE_FAULT_UNSUPPORTED_MMIO:
            return "unsupported MMIO access";
        case PORPOISE_FAULT_ADDRESS_OVERFLOW:
            return "guest address overflow";
        case PORPOISE_FAULT_HOST_IO:
            return "host I/O failure";
        case PORPOISE_FAULT_UNSUPPORTED_OPERATION:
            return "unsupported operation";
        default:
            return "unknown fault";
    }
}

const char *porpoise_host_result_string(PorpoiseHostResult result)
{
    switch (result) {
        case PORPOISE_HOST_OK:
            return "success";
        case PORPOISE_HOST_INVALID_ARGUMENT:
            return "host adapter rejected an argument";
        case PORPOISE_HOST_INVALID_POINTER:
            return "host adapter rejected a null or invalid pointer";
        case PORPOISE_HOST_UNMAPPED_ADDRESS:
            return "guest address is not mapped";
        case PORPOISE_HOST_UNSUPPORTED_MMIO:
            return "guest address refers to unsupported MMIO";
        case PORPOISE_HOST_ADDRESS_OVERFLOW:
            return "guest address range overflows 32 bits";
        case PORPOISE_HOST_IO_ERROR:
            return "host adapter I/O failed";
        default:
            return "unknown host adapter result";
    }
}

void porpoise_state_init(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host)
{
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->host = host;
}

void porpoise_state_clear_fault(PorpoisePpcState *state)
{
    if (state == NULL) {
        return;
    }

    state->fault = PORPOISE_FAULT_NONE;
    if (state->status == PORPOISE_EXECUTION_FAULTED) {
        state->status = PORPOISE_EXECUTION_READY;
    }
    state->fault_address = 0U;
    state->fault_message[0] = '\0';
}

void porpoise_state_set_fault(
    PorpoisePpcState *state,
    PorpoiseFault fault,
    uint32_t guest_address,
    const char *message)
{
    const char *fault_message;

    if (state == NULL || fault == PORPOISE_FAULT_NONE ||
        state->fault != PORPOISE_FAULT_NONE) {
        return;
    }

    fault_message = message != NULL ? message : porpoise_fault_string(fault);
    state->fault = fault;
    state->status = PORPOISE_EXECUTION_FAULTED;
    state->fault_address = guest_address;
    (void)snprintf(
        state->fault_message,
        sizeof(state->fault_message),
        "%s",
        fault_message);
}

int porpoise_state_has_fault(const PorpoisePpcState *state)
{
    return state == NULL || state->fault != PORPOISE_FAULT_NONE;
}

const char *porpoise_state_fault_message(const PorpoisePpcState *state)
{
    if (state == NULL) {
        return porpoise_fault_string(PORPOISE_FAULT_INVALID_STATE);
    }
    if (state->fault_message[0] != '\0') {
        return state->fault_message;
    }
    return porpoise_fault_string(state->fault);
}

uint8_t porpoise_cr_get_field(
    const PorpoisePpcState *state,
    unsigned int field_index)
{
    unsigned int shift;

    if (state == NULL || field_index >= 8U) {
        return 0U;
    }

    shift = (7U - field_index) * 4U;
    return (uint8_t)((state->cr >> shift) & 0x0FU);
}

void porpoise_cr_set_field(
    PorpoisePpcState *state,
    unsigned int field_index,
    uint8_t value)
{
    unsigned int shift;
    uint32_t mask;

    if (state == NULL) {
        return;
    }
    if (field_index >= 8U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "CR field index is outside 0..7");
        return;
    }

    shift = (7U - field_index) * 4U;
    mask = UINT32_C(0xF) << shift;
    state->cr = (state->cr & ~mask) |
                (((uint32_t)value & UINT32_C(0xF)) << shift);
}

int porpoise_cr_get_bit(
    const PorpoisePpcState *state,
    unsigned int bit_index)
{
    unsigned int shift;

    if (state == NULL || bit_index >= 32U) {
        return 0;
    }

    shift = 31U - bit_index;
    return (int)((state->cr >> shift) & UINT32_C(1));
}

void porpoise_cr_set_bit(
    PorpoisePpcState *state,
    unsigned int bit_index,
    int value)
{
    unsigned int shift;
    uint32_t mask;

    if (state == NULL) {
        return;
    }
    if (bit_index >= 32U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "CR bit index is outside 0..31");
        return;
    }

    shift = 31U - bit_index;
    mask = UINT32_C(1) << shift;
    if (value != 0) {
        state->cr |= mask;
    } else {
        state->cr &= ~mask;
    }
}

uint32_t porpoise_shift_left32(uint32_t value, uint32_t shift_source)
{
    unsigned int shift = (unsigned int)(shift_source & UINT32_C(0x3F));
    return shift < 32U ? value << shift : 0U;
}

uint32_t porpoise_shift_right32(uint32_t value, uint32_t shift_source)
{
    unsigned int shift = (unsigned int)(shift_source & UINT32_C(0x3F));
    return shift < 32U ? value >> shift : 0U;
}

uint32_t porpoise_arithmetic_shift_right32(
    PorpoisePpcState *state,
    uint32_t value,
    unsigned int shift)
{
    uint32_t result;
    uint32_t discarded;
    const uint32_t carry_mask = UINT32_C(0x20000000);

    shift &= 31U;
    if (shift == 0U) {
        result = value;
        discarded = 0U;
    } else {
        uint32_t sign_fill = (value & UINT32_C(0x80000000)) != 0U
                                 ? UINT32_MAX << (32U - shift)
                                 : 0U;
        result = (value >> shift) | sign_fill;
        discarded = value & (UINT32_MAX >> (32U - shift));
    }
    if (state != NULL) {
        if ((value & UINT32_C(0x80000000)) != 0U && discarded != 0U) {
            state->xer |= carry_mask;
        } else {
            state->xer &= ~carry_mask;
        }
    }
    return result;
}

uint32_t porpoise_rotate_left32(uint32_t value, unsigned int shift)
{
    shift &= 31U;
    return shift == 0U ? value : (value << shift) | (value >> (32U - shift));
}

uint32_t porpoise_mask32(unsigned int mask_begin, unsigned int mask_end)
{
    uint32_t mask = 0U;
    unsigned int bit = mask_begin & 31U;
    mask_end &= 31U;
    for (;;) {
        mask |= UINT32_C(1) << (31U - bit);
        if (bit == mask_end) break;
        bit = (bit + 1U) & 31U;
    }
    return mask;
}

static uint8_t porpoise_condition_field(
    const PorpoisePpcState *state,
    int relation)
{
    uint8_t value = relation < 0 ? 8U : relation > 0 ? 4U : 2U;
    if (state != NULL && (state->xer & UINT32_C(0x80000000)) != 0U) value |= 1U;
    return value;
}

void porpoise_set_cr0_result(PorpoisePpcState *state, uint32_t value)
{
    int relation = (int32_t)value < 0 ? -1 : value != 0U ? 1 : 0;
    porpoise_cr_set_field(state, 0U, porpoise_condition_field(state, relation));
}

void porpoise_compare_signed(
    PorpoisePpcState *state,
    unsigned int field_index,
    uint32_t left,
    uint32_t right)
{
    int32_t signed_left = (int32_t)left;
    int32_t signed_right = (int32_t)right;
    int relation = signed_left < signed_right ? -1 : signed_left > signed_right ? 1 : 0;
    porpoise_cr_set_field(state, field_index, porpoise_condition_field(state, relation));
}

void porpoise_compare_unsigned(
    PorpoisePpcState *state,
    unsigned int field_index,
    uint32_t left,
    uint32_t right)
{
    int relation = left < right ? -1 : left > right ? 1 : 0;
    porpoise_cr_set_field(state, field_index, porpoise_condition_field(state, relation));
}

static int porpoise_validate_span(
    PorpoisePpcState *state,
    uint32_t guest_address,
    size_t size)
{
    if (state == NULL) {
        return 0;
    }
    if (state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    if (size == 0U) {
        return 1;
    }

    if (size - 1U > (size_t)(UINT32_MAX - guest_address)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            guest_address,
            "memory access crosses the 32-bit guest address boundary");
        return 0;
    }
    if (state->host == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_NO_HOST_ADAPTER,
            guest_address,
            NULL);
        return 0;
    }

    return 1;
}

static int porpoise_read_bytes(
    PorpoisePpcState *state,
    uint32_t guest_address,
    void *destination,
    size_t size)
{
    PorpoiseHostResult result;

    if (!porpoise_validate_span(state, guest_address, size)) {
        return 0;
    }
    if (state->host->read_bytes == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            guest_address,
            "host adapter has no read callback");
        return 0;
    }

    result = state->host->read_bytes(
        state->host->context,
        guest_address,
        destination,
        size);
    if (result != PORPOISE_HOST_OK) {
        porpoise_state_set_fault(
            state,
            porpoise_fault_from_host_result(result),
            guest_address,
            porpoise_host_result_string(result));
        return 0;
    }

    return 1;
}

static int porpoise_write_bytes(
    PorpoisePpcState *state,
    uint32_t guest_address,
    const void *source,
    size_t size)
{
    PorpoiseHostResult result;

    if (!porpoise_validate_span(state, guest_address, size)) {
        return 0;
    }
    if (state->host->write_bytes == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            guest_address,
            "host adapter has no write callback");
        return 0;
    }

    result = state->host->write_bytes(
        state->host->context,
        guest_address,
        source,
        size);
    if (result != PORPOISE_HOST_OK) {
        porpoise_state_set_fault(
            state,
            porpoise_fault_from_host_result(result),
            guest_address,
            porpoise_host_result_string(result));
        return 0;
    }

    return 1;
}

uint8_t porpoise_load_u8(PorpoisePpcState *state, uint32_t guest_address)
{
    uint8_t bytes[1] = {0U};

    (void)porpoise_read_bytes(state, guest_address, bytes, sizeof(bytes));
    return bytes[0];
}

uint16_t porpoise_load_u16(PorpoisePpcState *state, uint32_t guest_address)
{
    uint8_t bytes[2] = {0U, 0U};

    if (!porpoise_read_bytes(state, guest_address, bytes, sizeof(bytes))) {
        return 0U;
    }
    return (uint16_t)(((uint16_t)bytes[0] << 8U) |
                      (uint16_t)bytes[1]);
}

uint32_t porpoise_load_u32(PorpoisePpcState *state, uint32_t guest_address)
{
    uint8_t bytes[4] = {0U, 0U, 0U, 0U};

    if (!porpoise_read_bytes(state, guest_address, bytes, sizeof(bytes))) {
        return 0U;
    }
    return ((uint32_t)bytes[0] << 24U) |
           ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) |
           (uint32_t)bytes[3];
}

uint64_t porpoise_load_u64(PorpoisePpcState *state, uint32_t guest_address)
{
    uint8_t bytes[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

    if (!porpoise_read_bytes(state, guest_address, bytes, sizeof(bytes))) {
        return 0U;
    }
    return ((uint64_t)bytes[0] << 56U) |
           ((uint64_t)bytes[1] << 48U) |
           ((uint64_t)bytes[2] << 40U) |
           ((uint64_t)bytes[3] << 32U) |
           ((uint64_t)bytes[4] << 24U) |
           ((uint64_t)bytes[5] << 16U) |
           ((uint64_t)bytes[6] << 8U) |
           (uint64_t)bytes[7];
}

float porpoise_load_f32(PorpoisePpcState *state, uint32_t guest_address)
{
    uint32_t bits;
    float value;

    bits = porpoise_load_u32(state, guest_address);
    value = 0.0F;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

double porpoise_load_f64(PorpoisePpcState *state, uint32_t guest_address)
{
    uint64_t bits;
    double value;

    bits = porpoise_load_u64(state, guest_address);
    value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void porpoise_store_u8(
    PorpoisePpcState *state,
    uint32_t guest_address,
    uint8_t value)
{
    uint8_t bytes[1];

    bytes[0] = value;
    (void)porpoise_write_bytes(state, guest_address, bytes, sizeof(bytes));
}

void porpoise_store_u16(
    PorpoisePpcState *state,
    uint32_t guest_address,
    uint16_t value)
{
    uint8_t bytes[2];

    bytes[0] = (uint8_t)(value >> 8U);
    bytes[1] = (uint8_t)value;
    (void)porpoise_write_bytes(state, guest_address, bytes, sizeof(bytes));
}

void porpoise_store_u32(
    PorpoisePpcState *state,
    uint32_t guest_address,
    uint32_t value)
{
    uint8_t bytes[4];

    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
    (void)porpoise_write_bytes(state, guest_address, bytes, sizeof(bytes));
}

void porpoise_store_u64(
    PorpoisePpcState *state,
    uint32_t guest_address,
    uint64_t value)
{
    uint8_t bytes[8];

    bytes[0] = (uint8_t)(value >> 56U);
    bytes[1] = (uint8_t)(value >> 48U);
    bytes[2] = (uint8_t)(value >> 40U);
    bytes[3] = (uint8_t)(value >> 32U);
    bytes[4] = (uint8_t)(value >> 24U);
    bytes[5] = (uint8_t)(value >> 16U);
    bytes[6] = (uint8_t)(value >> 8U);
    bytes[7] = (uint8_t)value;
    (void)porpoise_write_bytes(state, guest_address, bytes, sizeof(bytes));
}

void porpoise_store_f32(
    PorpoisePpcState *state,
    uint32_t guest_address,
    float value)
{
    uint32_t bits;

    bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    porpoise_store_u32(state, guest_address, bits);
}

void porpoise_store_f64(
    PorpoisePpcState *state,
    uint32_t guest_address,
    double value)
{
    uint64_t bits;

    bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    porpoise_store_u64(state, guest_address, bits);
}

void *porpoise_decode_pointer(
    PorpoisePpcState *state,
    uint32_t guest_address)
{
    PorpoiseHostResult result;
    void *pointer;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return NULL;
    }
    if (guest_address == 0U) {
        return NULL;
    }
    if (state->host == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_NO_HOST_ADAPTER,
            guest_address,
            NULL);
        return NULL;
    }
    if (state->host->decode_pointer == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            guest_address,
            "host adapter has no pointer decoder");
        return NULL;
    }

    pointer = NULL;
    result = state->host->decode_pointer(
        state->host->context,
        guest_address,
        &pointer);
    if (result != PORPOISE_HOST_OK || pointer == NULL) {
        if (result == PORPOISE_HOST_OK) {
            result = PORPOISE_HOST_INVALID_POINTER;
        }
        porpoise_state_set_fault(
            state,
            porpoise_fault_from_host_result(result),
            guest_address,
            porpoise_host_result_string(result));
        return NULL;
    }

    return pointer;
}

uint32_t porpoise_encode_pointer(
    PorpoisePpcState *state,
    const void *pointer)
{
    PorpoiseHostResult result;
    uint32_t guest_address;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0U;
    }
    if (pointer == NULL) {
        return 0U;
    }
    if (state->host == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_NO_HOST_ADAPTER,
            state->pc,
            NULL);
        return 0U;
    }
    if (state->host->encode_pointer == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            state->pc,
            "host adapter has no pointer encoder");
        return 0U;
    }

    guest_address = 0U;
    result = state->host->encode_pointer(
        state->host->context,
        pointer,
        &guest_address);
    if (result != PORPOISE_HOST_OK || guest_address == 0U) {
        if (result == PORPOISE_HOST_OK) {
            result = PORPOISE_HOST_INVALID_POINTER;
        }
        porpoise_state_set_fault(
            state,
            porpoise_fault_from_host_result(result),
            state->pc,
            porpoise_host_result_string(result));
        return 0U;
    }

    return guest_address;
}
