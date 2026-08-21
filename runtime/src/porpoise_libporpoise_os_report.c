#include "porpoise_libporpoise_builtins_private.h"

#include <dolphin/os.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    PORPOISE_REPORT_MAX_FORMAT_BYTES = 4096,
    PORPOISE_REPORT_MAX_STRING_BYTES = 4096,
    PORPOISE_REPORT_MAX_FIELD_VALUE = 1024,
    PORPOISE_REPORT_OUTPUT_CAPACITY = 16384,
    PORPOISE_REPORT_PIECE_CAPACITY = 8192,
    PORPOISE_REPORT_NATIVE_FORMAT_CAPACITY = 64
};

enum PorpoiseReportFlags {
    PORPOISE_REPORT_FLAG_LEFT = 1U << 0,
    PORPOISE_REPORT_FLAG_PLUS = 1U << 1,
    PORPOISE_REPORT_FLAG_SPACE = 1U << 2,
    PORPOISE_REPORT_FLAG_ALTERNATE = 1U << 3,
    PORPOISE_REPORT_FLAG_ZERO = 1U << 4
};

typedef enum PorpoiseReportLength {
    PORPOISE_REPORT_LENGTH_NONE = 0,
    PORPOISE_REPORT_LENGTH_HH,
    PORPOISE_REPORT_LENGTH_H,
    PORPOISE_REPORT_LENGTH_L,
    PORPOISE_REPORT_LENGTH_LL,
    PORPOISE_REPORT_LENGTH_J,
    PORPOISE_REPORT_LENGTH_Z,
    PORPOISE_REPORT_LENGTH_T,
    PORPOISE_REPORT_LENGTH_CAPITAL_L
} PorpoiseReportLength;

typedef struct PorpoiseReportConversion {
    unsigned int flags;
    int width_present;
    uint32_t width;
    int precision_present;
    uint32_t precision;
    PorpoiseReportLength length;
    char specifier;
} PorpoiseReportConversion;

typedef struct PorpoiseReportArguments {
    unsigned int gpr_slot;
    unsigned int fpr_slot;
    uint64_t stack_address;
    int stack_initialized;
} PorpoiseReportArguments;

static void porpoise_report_fault(
    PorpoisePpcState *state,
    PorpoiseFault fault,
    uint32_t guest_address,
    const char *message)
{
    porpoise_state_set_fault(state, fault, guest_address, message);
}

static int porpoise_report_read_guest_byte(
    PorpoisePpcState *state,
    uint32_t guest_address,
    size_t offset,
    uint8_t *value_out,
    const char *overflow_message)
{
    if (offset > (size_t)(UINT32_MAX - guest_address)) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            guest_address,
            overflow_message);
        return 0;
    }
    *value_out = porpoise_load_u8(
        state, guest_address + (uint32_t)offset);
    return !porpoise_state_has_fault(state);
}

static int porpoise_report_read_guest_string(
    PorpoisePpcState *state,
    uint32_t guest_address,
    char *destination,
    size_t limit,
    int allow_unterminated,
    const char *overflow_message,
    const char *limit_message)
{
    size_t index;

    if (limit == 0U) {
        destination[0] = '\0';
        return 1;
    }
    if (guest_address == 0U) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_address,
            "OSReport guest string pointer is null");
        return 0;
    }
    for (index = 0U; index < limit; index++) {
        uint8_t value;

        if (!porpoise_report_read_guest_byte(
                state,
                guest_address,
                index,
                &value,
                overflow_message)) {
            return 0;
        }
        destination[index] = (char)value;
        if (value == 0U) {
            return 1;
        }
    }
    destination[limit] = '\0';
    if (allow_unterminated) {
        return 1;
    }
    porpoise_report_fault(
        state,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        guest_address,
        limit_message);
    return 0;
}

static int porpoise_report_stack_argument(
    PorpoisePpcState *state,
    PorpoiseReportArguments *arguments,
    uint32_t alignment,
    uint32_t size,
    uint32_t *guest_address_out)
{
    uint64_t aligned;

    if (!arguments->stack_initialized) {
        arguments->stack_address = (uint64_t)state->gpr[1] + UINT64_C(8);
        arguments->stack_initialized = 1;
    }
    aligned = (arguments->stack_address + (uint64_t)(alignment - 1U)) &
              ~(uint64_t)(alignment - 1U);
    if (aligned > UINT32_MAX ||
        (uint64_t)size > UINT64_C(0x100000000) - aligned) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            state->gpr[1],
            "OSReport overflow argument crosses the 32-bit guest address boundary");
        return 0;
    }
    *guest_address_out = (uint32_t)aligned;
    arguments->stack_address = aligned + size;
    return 1;
}

static int porpoise_report_next_gpr32(
    PorpoisePpcState *state,
    PorpoiseReportArguments *arguments,
    uint32_t *value_out)
{
    uint32_t guest_address;

    if (arguments->gpr_slot < 8U) {
        *value_out = state->gpr[3U + arguments->gpr_slot];
        arguments->gpr_slot++;
        return 1;
    }
    arguments->gpr_slot = 8U;
    if (!porpoise_report_stack_argument(
            state, arguments, 4U, 4U, &guest_address)) {
        return 0;
    }
    *value_out = porpoise_load_u32(state, guest_address);
    return !porpoise_state_has_fault(state);
}

static int porpoise_report_next_gpr64(
    PorpoisePpcState *state,
    PorpoiseReportArguments *arguments,
    uint64_t *value_out)
{
    uint32_t guest_address;

    if ((arguments->gpr_slot & 1U) != 0U) {
        arguments->gpr_slot++;
    }
    if (arguments->gpr_slot < 7U) {
        unsigned int register_index = 3U + arguments->gpr_slot;

        *value_out = ((uint64_t)state->gpr[register_index] << 32U) |
                     (uint64_t)state->gpr[register_index + 1U];
        arguments->gpr_slot += 2U;
        return 1;
    }
    arguments->gpr_slot = 8U;
    if (!porpoise_report_stack_argument(
            state, arguments, 8U, 8U, &guest_address)) {
        return 0;
    }
    *value_out = porpoise_load_u64(state, guest_address);
    return !porpoise_state_has_fault(state);
}

static int porpoise_report_next_f64(
    PorpoisePpcState *state,
    PorpoiseReportArguments *arguments,
    double *value_out)
{
    uint64_t bits;
    uint32_t guest_address;

    if (!porpoise_cr_get_bit(state, 6U)) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "OSReport floating argument requires PPC EABI CR bit 6");
        return 0;
    }
    if (arguments->fpr_slot < 8U) {
        bits = porpoise_fpr_get_bits(
            state, 1U + arguments->fpr_slot, 0U);
        arguments->fpr_slot++;
    } else {
        if (!porpoise_report_stack_argument(
                state, arguments, 8U, 8U, &guest_address)) {
            return 0;
        }
        bits = porpoise_load_u64(state, guest_address);
        if (porpoise_state_has_fault(state)) {
            return 0;
        }
    }
    memcpy(value_out, &bits, sizeof(*value_out));
    return 1;
}

static int porpoise_report_parse_decimal(
    PorpoisePpcState *state,
    const char *format,
    size_t *index,
    int *present_out,
    uint32_t *value_out,
    const char *limit_message)
{
    uint32_t value = 0U;
    int overflow = 0;
    int present = 0;

    while (format[*index] >= '0' && format[*index] <= '9') {
        uint32_t digit = (uint32_t)(format[*index] - '0');

        present = 1;
        if (value > (UINT32_MAX - digit) / 10U) {
            overflow = 1;
        } else if (!overflow) {
            value = value * 10U + digit;
        }
        (*index)++;
    }
    if (present && format[*index] == '$') {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            state->gpr[3] + (uint32_t)*index,
            "OSReport positional arguments are unsupported");
        return 0;
    }
    if (overflow || value > PORPOISE_REPORT_MAX_FIELD_VALUE) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->gpr[3] + (uint32_t)*index,
            limit_message);
        return 0;
    }
    *present_out = present;
    *value_out = value;
    return 1;
}

static int64_t porpoise_report_sign_extend(
    uint64_t value,
    unsigned int bit_count)
{
    uint64_t mask;
    uint64_t sign;
    uint64_t magnitude;

    if (bit_count == 64U) {
        mask = UINT64_MAX;
        sign = UINT64_C(1) << 63U;
    } else {
        mask = (UINT64_C(1) << bit_count) - UINT64_C(1);
        sign = UINT64_C(1) << (bit_count - 1U);
    }
    value &= mask;
    if ((value & sign) == 0U) {
        return (int64_t)value;
    }
    magnitude = ((~value) & mask) + UINT64_C(1);
    if (bit_count == 64U && magnitude == (UINT64_C(1) << 63U)) {
        return INT64_MIN;
    }
    return -(int64_t)magnitude;
}

static int porpoise_report_dynamic_field(
    PorpoisePpcState *state,
    PorpoiseReportArguments *arguments,
    int is_width,
    PorpoiseReportConversion *conversion)
{
    uint32_t raw;
    int64_t signed_value;

    if (!porpoise_report_next_gpr32(state, arguments, &raw)) {
        return 0;
    }
    signed_value = porpoise_report_sign_extend(raw, 32U);
    if (is_width && signed_value < 0) {
        if (signed_value == INT32_MIN) {
            porpoise_report_fault(
                state,
                PORPOISE_FAULT_INVALID_ARGUMENT,
                state->pc,
                "OSReport dynamic width cannot be INT32_MIN");
            return 0;
        }
        conversion->flags |= PORPOISE_REPORT_FLAG_LEFT;
        signed_value = -signed_value;
    }
    if (!is_width && signed_value < 0) {
        conversion->precision_present = 0;
        conversion->precision = 0U;
        return 1;
    }
    if ((uint64_t)signed_value > PORPOISE_REPORT_MAX_FIELD_VALUE) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            is_width
                ? "OSReport dynamic width exceeds the deterministic limit"
                : "OSReport dynamic precision exceeds the deterministic limit");
        return 0;
    }
    if (is_width) {
        conversion->width_present = 1;
        conversion->width = (uint32_t)signed_value;
    } else {
        conversion->precision_present = 1;
        conversion->precision = (uint32_t)signed_value;
    }
    return 1;
}

static int porpoise_report_parse_conversion(
    PorpoisePpcState *state,
    const char *format,
    size_t *index,
    PorpoiseReportArguments *arguments,
    PorpoiseReportConversion *conversion)
{
    memset(conversion, 0, sizeof(*conversion));

    for (;;) {
        char flag = format[*index];

        if (flag == '-') conversion->flags |= PORPOISE_REPORT_FLAG_LEFT;
        else if (flag == '+') conversion->flags |= PORPOISE_REPORT_FLAG_PLUS;
        else if (flag == ' ') conversion->flags |= PORPOISE_REPORT_FLAG_SPACE;
        else if (flag == '#') conversion->flags |= PORPOISE_REPORT_FLAG_ALTERNATE;
        else if (flag == '0') conversion->flags |= PORPOISE_REPORT_FLAG_ZERO;
        else break;
        (*index)++;
    }

    if (format[*index] == '*') {
        (*index)++;
        if (format[*index] == '$' ||
            (format[*index] >= '0' && format[*index] <= '9')) {
            porpoise_report_fault(
                state,
                PORPOISE_FAULT_UNSUPPORTED_OPERATION,
                state->gpr[3] + (uint32_t)*index,
                "OSReport positional arguments are unsupported");
            return 0;
        }
        if (!porpoise_report_dynamic_field(
                state, arguments, 1, conversion)) {
            return 0;
        }
    } else if (!porpoise_report_parse_decimal(
                   state,
                   format,
                   index,
                   &conversion->width_present,
                   &conversion->width,
                   "OSReport field width exceeds the deterministic limit")) {
        return 0;
    }

    if (format[*index] == '.') {
        (*index)++;
        conversion->precision_present = 1;
        conversion->precision = 0U;
        if (format[*index] == '*') {
            (*index)++;
            if (format[*index] == '$' ||
                (format[*index] >= '0' && format[*index] <= '9')) {
                porpoise_report_fault(
                    state,
                    PORPOISE_FAULT_UNSUPPORTED_OPERATION,
                    state->gpr[3] + (uint32_t)*index,
                    "OSReport positional arguments are unsupported");
                return 0;
            }
            if (!porpoise_report_dynamic_field(
                    state, arguments, 0, conversion)) {
                return 0;
            }
        } else if (!porpoise_report_parse_decimal(
                       state,
                       format,
                       index,
                       &conversion->precision_present,
                       &conversion->precision,
                       "OSReport precision exceeds the deterministic limit")) {
            return 0;
        } else if (!conversion->precision_present) {
            conversion->precision_present = 1;
            conversion->precision = 0U;
        }
    }

    if (format[*index] == 'h' && format[*index + 1U] == 'h') {
        conversion->length = PORPOISE_REPORT_LENGTH_HH;
        *index += 2U;
    } else if (format[*index] == 'h') {
        conversion->length = PORPOISE_REPORT_LENGTH_H;
        (*index)++;
    } else if (format[*index] == 'l' && format[*index + 1U] == 'l') {
        conversion->length = PORPOISE_REPORT_LENGTH_LL;
        *index += 2U;
    } else if (format[*index] == 'l') {
        conversion->length = PORPOISE_REPORT_LENGTH_L;
        (*index)++;
    } else if (format[*index] == 'j') {
        conversion->length = PORPOISE_REPORT_LENGTH_J;
        (*index)++;
    } else if (format[*index] == 'z') {
        conversion->length = PORPOISE_REPORT_LENGTH_Z;
        (*index)++;
    } else if (format[*index] == 't') {
        conversion->length = PORPOISE_REPORT_LENGTH_T;
        (*index)++;
    } else if (format[*index] == 'L') {
        conversion->length = PORPOISE_REPORT_LENGTH_CAPITAL_L;
        (*index)++;
    }

    conversion->specifier = format[*index];
    if (conversion->specifier == '\0') {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->gpr[3] + (uint32_t)*index,
            "OSReport format ends inside a conversion");
        return 0;
    }
    (*index)++;
    return 1;
}

static int porpoise_report_append_character(
    char *destination,
    size_t capacity,
    size_t *length,
    char value)
{
    if (*length + 1U >= capacity) {
        return 0;
    }
    destination[*length] = value;
    (*length)++;
    destination[*length] = '\0';
    return 1;
}

static int porpoise_report_append_bytes(
    PorpoisePpcState *state,
    char *destination,
    size_t capacity,
    size_t *length,
    const char *source,
    size_t source_length)
{
    if (source_length >= capacity || *length >= capacity - source_length) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->gpr[3],
            "OSReport output exceeds the deterministic limit");
        return 0;
    }
    memcpy(destination + *length, source, source_length);
    *length += source_length;
    destination[*length] = '\0';
    return 1;
}

static int porpoise_report_append_decimal(
    char *destination,
    size_t capacity,
    size_t *length,
    uint32_t value)
{
    char reverse_digits[10];
    size_t digit_count = 0U;

    do {
        reverse_digits[digit_count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    while (digit_count != 0U) {
        if (!porpoise_report_append_character(
                destination,
                capacity,
                length,
                reverse_digits[--digit_count])) {
            return 0;
        }
    }
    return 1;
}

static int porpoise_report_build_native_format(
    PorpoisePpcState *state,
    const PorpoiseReportConversion *conversion,
    int use_long_long,
    char specifier,
    char *destination,
    size_t capacity)
{
    size_t length = 0U;

    destination[0] = '\0';
    if (!porpoise_report_append_character(
            destination, capacity, &length, '%')) {
        goto internal_error;
    }
    if ((conversion->flags & PORPOISE_REPORT_FLAG_LEFT) != 0U &&
        !porpoise_report_append_character(
            destination, capacity, &length, '-')) {
        goto internal_error;
    }
    if ((conversion->flags & PORPOISE_REPORT_FLAG_PLUS) != 0U &&
        !porpoise_report_append_character(
            destination, capacity, &length, '+')) {
        goto internal_error;
    }
    if ((conversion->flags & PORPOISE_REPORT_FLAG_SPACE) != 0U &&
        !porpoise_report_append_character(
            destination, capacity, &length, ' ')) {
        goto internal_error;
    }
    if ((conversion->flags & PORPOISE_REPORT_FLAG_ALTERNATE) != 0U &&
        !porpoise_report_append_character(
            destination, capacity, &length, '#')) {
        goto internal_error;
    }
    if ((conversion->flags & PORPOISE_REPORT_FLAG_ZERO) != 0U &&
        !porpoise_report_append_character(
            destination, capacity, &length, '0')) {
        goto internal_error;
    }
    if (conversion->width_present &&
        !porpoise_report_append_decimal(
            destination, capacity, &length, conversion->width)) {
        goto internal_error;
    }
    if (conversion->precision_present) {
        if (!porpoise_report_append_character(
                destination, capacity, &length, '.') ||
            !porpoise_report_append_decimal(
                destination,
                capacity,
                &length,
                conversion->precision)) {
            goto internal_error;
        }
    }
    if (use_long_long) {
        if (!porpoise_report_append_character(
                destination, capacity, &length, 'l') ||
            !porpoise_report_append_character(
                destination, capacity, &length, 'l')) {
            goto internal_error;
        }
    }
    if (!porpoise_report_append_character(
            destination, capacity, &length, specifier)) {
        goto internal_error;
    }
    return 1;

internal_error:
    porpoise_report_fault(
        state,
        PORPOISE_FAULT_INVALID_STATE,
        state->pc,
        "OSReport native format buffer is unexpectedly too small");
    return 0;
}

static int porpoise_report_accept_snprintf(
    PorpoisePpcState *state,
    char *output,
    size_t *output_length,
    const char *piece,
    int result)
{
    if (result < 0) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_HOST_IO,
            state->pc,
            "OSReport host formatting failed");
        return 0;
    }
    if ((size_t)result >= PORPOISE_REPORT_PIECE_CAPACITY) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->gpr[3],
            "OSReport conversion exceeds the deterministic limit");
        return 0;
    }
    return porpoise_report_append_bytes(
        state,
        output,
        PORPOISE_REPORT_OUTPUT_CAPACITY,
        output_length,
        piece,
        (size_t)result);
}

static int porpoise_report_reject_flags(
    PorpoisePpcState *state,
    const PorpoiseReportConversion *conversion,
    unsigned int allowed_flags,
    const char *message)
{
    if ((conversion->flags & ~allowed_flags) == 0U) {
        return 1;
    }
    porpoise_report_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        state->gpr[3],
        message);
    return 0;
}

static int porpoise_report_format_integer(
    PorpoisePpcState *state,
    PorpoiseReportArguments *arguments,
    const PorpoiseReportConversion *conversion,
    char *output,
    size_t *output_length)
{
    char native_format[PORPOISE_REPORT_NATIVE_FORMAT_CAPACITY];
    char piece[PORPOISE_REPORT_PIECE_CAPACITY];
    uint64_t raw;
    unsigned int bit_count;
    int is_signed;
    unsigned int allowed_flags;
    int result;

    if (conversion->length == PORPOISE_REPORT_LENGTH_CAPITAL_L) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            state->gpr[3],
            "OSReport integer conversion has an unsupported length modifier");
        return 0;
    }
    bit_count = (conversion->length == PORPOISE_REPORT_LENGTH_LL ||
                 conversion->length == PORPOISE_REPORT_LENGTH_J)
                    ? 64U
                    : 32U;
    if (bit_count == 64U) {
        if (!porpoise_report_next_gpr64(state, arguments, &raw)) return 0;
    } else {
        uint32_t raw32;

        if (!porpoise_report_next_gpr32(state, arguments, &raw32)) return 0;
        raw = raw32;
        if (conversion->length == PORPOISE_REPORT_LENGTH_HH) bit_count = 8U;
        else if (conversion->length == PORPOISE_REPORT_LENGTH_H) bit_count = 16U;
    }

    is_signed = conversion->specifier == 'd' ||
                conversion->specifier == 'i';
    if (is_signed) {
        allowed_flags = PORPOISE_REPORT_FLAG_LEFT |
                        PORPOISE_REPORT_FLAG_PLUS |
                        PORPOISE_REPORT_FLAG_SPACE |
                        PORPOISE_REPORT_FLAG_ZERO;
    } else if (conversion->specifier == 'u') {
        allowed_flags = PORPOISE_REPORT_FLAG_LEFT |
                        PORPOISE_REPORT_FLAG_ZERO;
    } else {
        allowed_flags = PORPOISE_REPORT_FLAG_LEFT |
                        PORPOISE_REPORT_FLAG_ALTERNATE |
                        PORPOISE_REPORT_FLAG_ZERO;
    }
    if (!porpoise_report_reject_flags(
            state,
            conversion,
            allowed_flags,
            "OSReport integer conversion uses flags undefined for its specifier")) {
        return 0;
    }
    if (!porpoise_report_build_native_format(
            state,
            conversion,
            1,
            conversion->specifier,
            native_format,
            sizeof(native_format))) {
        return 0;
    }
    if (is_signed) {
        long long value = (long long)porpoise_report_sign_extend(raw, bit_count);

        result = snprintf(piece, sizeof(piece), native_format, value);
    } else {
        uint64_t mask = bit_count == 64U
                            ? UINT64_MAX
                            : (UINT64_C(1) << bit_count) - UINT64_C(1);
        unsigned long long value = (unsigned long long)(raw & mask);

        result = snprintf(piece, sizeof(piece), native_format, value);
    }
    return porpoise_report_accept_snprintf(
        state, output, output_length, piece, result);
}

static int porpoise_report_format_character(
    PorpoisePpcState *state,
    PorpoiseReportArguments *arguments,
    const PorpoiseReportConversion *conversion,
    char *output,
    size_t *output_length)
{
    char native_format[PORPOISE_REPORT_NATIVE_FORMAT_CAPACITY];
    char piece[PORPOISE_REPORT_PIECE_CAPACITY];
    uint32_t raw;
    int result;

    if (conversion->length != PORPOISE_REPORT_LENGTH_NONE) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            state->gpr[3],
            "OSReport wide or length-modified character conversion is unsupported");
        return 0;
    }
    if (conversion->precision_present ||
        !porpoise_report_reject_flags(
            state,
            conversion,
            PORPOISE_REPORT_FLAG_LEFT,
            "OSReport character conversion uses unsupported flags")) {
        if (!porpoise_state_has_fault(state)) {
            porpoise_report_fault(
                state,
                PORPOISE_FAULT_UNSUPPORTED_OPERATION,
                state->gpr[3],
                "OSReport character precision is unsupported");
        }
        return 0;
    }
    if (!porpoise_report_next_gpr32(state, arguments, &raw) ||
        !porpoise_report_build_native_format(
            state,
            conversion,
            0,
            'c',
            native_format,
            sizeof(native_format))) {
        return 0;
    }
    if ((raw & UINT32_C(0xFF)) == 0U) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            state->gpr[3],
            "OSReport NUL %c output is unsupported");
        return 0;
    }
    result = snprintf(piece, sizeof(piece), native_format, (int)raw);
    return porpoise_report_accept_snprintf(
        state, output, output_length, piece, result);
}

static int porpoise_report_format_string(
    PorpoisePpcState *state,
    PorpoiseReportArguments *arguments,
    const PorpoiseReportConversion *conversion,
    char *output,
    size_t *output_length)
{
    char native_format[PORPOISE_REPORT_NATIVE_FORMAT_CAPACITY];
    char guest_string[PORPOISE_REPORT_MAX_STRING_BYTES + 1U];
    char piece[PORPOISE_REPORT_PIECE_CAPACITY];
    uint32_t guest_address;
    size_t read_limit;
    int result;

    if (conversion->length != PORPOISE_REPORT_LENGTH_NONE) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            state->gpr[3],
            "OSReport wide or length-modified string conversion is unsupported");
        return 0;
    }
    if (!porpoise_report_reject_flags(
            state,
            conversion,
            PORPOISE_REPORT_FLAG_LEFT,
            "OSReport string conversion uses unsupported flags")) {
        return 0;
    }
    if (!porpoise_report_next_gpr32(
            state, arguments, &guest_address)) {
        return 0;
    }
    read_limit = conversion->precision_present
                     ? (size_t)conversion->precision
                     : (size_t)PORPOISE_REPORT_MAX_STRING_BYTES;
    if (!porpoise_report_read_guest_string(
            state,
            guest_address,
            guest_string,
            read_limit,
            conversion->precision_present,
            "OSReport %s argument crosses the 32-bit guest address boundary",
            "OSReport %s argument is not terminated within the deterministic limit") ||
        !porpoise_report_build_native_format(
            state,
            conversion,
            0,
            's',
            native_format,
            sizeof(native_format))) {
        return 0;
    }
    result = snprintf(piece, sizeof(piece), native_format, guest_string);
    return porpoise_report_accept_snprintf(
        state, output, output_length, piece, result);
}

static int porpoise_report_format_pointer(
    PorpoisePpcState *state,
    PorpoiseReportArguments *arguments,
    const PorpoiseReportConversion *conversion,
    char *output,
    size_t *output_length)
{
    char native_format[PORPOISE_REPORT_NATIVE_FORMAT_CAPACITY];
    char pointer_text[11];
    char piece[PORPOISE_REPORT_PIECE_CAPACITY];
    uint32_t guest_address;
    int result;

    if (conversion->length != PORPOISE_REPORT_LENGTH_NONE ||
        conversion->precision_present) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            state->gpr[3],
            "OSReport pointer precision or length modifiers are unsupported");
        return 0;
    }
    if (!porpoise_report_reject_flags(
            state,
            conversion,
            PORPOISE_REPORT_FLAG_LEFT,
            "OSReport pointer conversion uses unsupported flags") ||
        !porpoise_report_next_gpr32(
            state, arguments, &guest_address)) {
        return 0;
    }
    result = snprintf(
        pointer_text,
        sizeof(pointer_text),
        "0x%08llx",
        (unsigned long long)guest_address);
    if (result != 10) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_HOST_IO,
            state->pc,
            "OSReport could not render a guest pointer");
        return 0;
    }
    if (!porpoise_report_build_native_format(
            state,
            conversion,
            0,
            's',
            native_format,
            sizeof(native_format))) {
        return 0;
    }
    result = snprintf(piece, sizeof(piece), native_format, pointer_text);
    return porpoise_report_accept_snprintf(
        state, output, output_length, piece, result);
}

static int porpoise_report_format_float(
    PorpoisePpcState *state,
    PorpoiseReportArguments *arguments,
    const PorpoiseReportConversion *conversion,
    char *output,
    size_t *output_length)
{
    char native_format[PORPOISE_REPORT_NATIVE_FORMAT_CAPACITY];
    char piece[PORPOISE_REPORT_PIECE_CAPACITY];
    double value;
    int result;

    if (conversion->length != PORPOISE_REPORT_LENGTH_NONE &&
        conversion->length != PORPOISE_REPORT_LENGTH_L) {
        porpoise_report_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            state->gpr[3],
            conversion->length == PORPOISE_REPORT_LENGTH_CAPITAL_L
                ? "OSReport long-double arguments are unsupported"
                : "OSReport floating conversion has an unsupported length modifier");
        return 0;
    }
    if (!porpoise_report_next_f64(state, arguments, &value) ||
        !porpoise_report_build_native_format(
            state,
            conversion,
            0,
            conversion->specifier,
            native_format,
            sizeof(native_format))) {
        return 0;
    }
    result = snprintf(piece, sizeof(piece), native_format, value);
    return porpoise_report_accept_snprintf(
        state, output, output_length, piece, result);
}

static int porpoise_report_format_conversion(
    PorpoisePpcState *state,
    PorpoiseReportArguments *arguments,
    const PorpoiseReportConversion *conversion,
    char *output,
    size_t *output_length)
{
    switch (conversion->specifier) {
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X':
        case 'o':
            return porpoise_report_format_integer(
                state, arguments, conversion, output, output_length);
        case 'c':
            return porpoise_report_format_character(
                state, arguments, conversion, output, output_length);
        case 's':
            return porpoise_report_format_string(
                state, arguments, conversion, output, output_length);
        case 'p':
            return porpoise_report_format_pointer(
                state, arguments, conversion, output, output_length);
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            return porpoise_report_format_float(
                state, arguments, conversion, output, output_length);
        case 'n':
            porpoise_report_fault(
                state,
                PORPOISE_FAULT_UNSUPPORTED_OPERATION,
                state->gpr[3],
                "OSReport %n writes are forbidden");
            return 0;
        default:
            porpoise_report_fault(
                state,
                PORPOISE_FAULT_UNSUPPORTED_OPERATION,
                state->gpr[3],
                "OSReport format specifier is unsupported");
            return 0;
    }
}

void porpoise_libporpoise_os_report_adapter(PorpoisePpcState *state)
{
    char format[PORPOISE_REPORT_MAX_FORMAT_BYTES + 1U];
    char output[PORPOISE_REPORT_OUTPUT_CAPACITY];
    PorpoiseReportArguments arguments;
    size_t format_index = 0U;
    size_t output_length = 0U;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_report_read_guest_string(
            state,
            state->gpr[3],
            format,
            PORPOISE_REPORT_MAX_FORMAT_BYTES,
            0,
            "OSReport format crosses the 32-bit guest address boundary",
            "OSReport format is not terminated within the deterministic limit")) {
        return;
    }

    memset(&arguments, 0, sizeof(arguments));
    arguments.gpr_slot = 1U;
    output[0] = '\0';

    while (format[format_index] != '\0') {
        PorpoiseReportConversion conversion;

        if (format[format_index] != '%') {
            if (!porpoise_report_append_character(
                    output,
                    sizeof(output),
                    &output_length,
                    format[format_index])) {
                porpoise_report_fault(
                    state,
                    PORPOISE_FAULT_INVALID_ARGUMENT,
                    state->gpr[3],
                    "OSReport output exceeds the deterministic limit");
                return;
            }
            format_index++;
            continue;
        }

        format_index++;
        if (format[format_index] == '%') {
            if (!porpoise_report_append_character(
                    output, sizeof(output), &output_length, '%')) {
                porpoise_report_fault(
                    state,
                    PORPOISE_FAULT_INVALID_ARGUMENT,
                    state->gpr[3],
                    "OSReport output exceeds the deterministic limit");
                return;
            }
            format_index++;
            continue;
        }
        if (!porpoise_report_parse_conversion(
                state,
                format,
                &format_index,
                &arguments,
                &conversion) ||
            !porpoise_report_format_conversion(
                state,
                &arguments,
                &conversion,
                output,
                &output_length)) {
            return;
        }
    }

    /* The native format is constant and the only native vararg is a bounded,
     * sanitized host string. Guest format bytes never reach host varargs. */
    OSReport("%s", output);
}
