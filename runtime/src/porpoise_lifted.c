#include "porpoise_lifted.h"
#include "porpoise_ppc_fp.h"

#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* These helpers inspect and temporarily change the host floating-point
 * environment.  Optimized builds must preserve that observable state: in
 * particular, the exact-domain checks depend on FE_INEXACT rather than merely
 * on the rounded host result. */
#if defined(__clang__)
#pragma STDC FENV_ACCESS ON
#elif defined(__GNUC__)
#pragma GCC optimize ("rounding-math")
#endif

static int porpoise_finish_host_event(
    PorpoisePpcState *state,
    PorpoiseExecutionStatus status_before,
    uint32_t instruction_address,
    const char *event_name);

#if !defined(__CPPCHECK__) && \
    (FLT_RADIX != 2 || FLT_MANT_DIG != 24 || FLT_MAX_EXP != 128 || \
     DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024)
#error "Porpoise lifted runtime requires IEEE-754 binary32 and binary64"
#endif

typedef char PorpoiseFloatMustBe32Bits[sizeof(float) == 4U ? 1 : -1];
typedef char PorpoiseDoubleMustBe64Bits[sizeof(double) == 8U ? 1 : -1];

static FILE *porpoise_trace_output(PorpoisePpcState *state)
{
    return state != NULL ? (FILE *)state->trace_file : NULL;
}

static void porpoise_trace_json_string(FILE *output, const char *value)
{
    const unsigned char *cursor =
        (const unsigned char *)(value != NULL ? value : "");

    fputc('"', output);
    while (*cursor != '\0') {
        switch (*cursor) {
            case '"': fputs("\\\"", output); break;
            case '\\': fputs("\\\\", output); break;
            case '\b': fputs("\\b", output); break;
            case '\f': fputs("\\f", output); break;
            case '\n': fputs("\\n", output); break;
            case '\r': fputs("\\r", output); break;
            case '\t': fputs("\\t", output); break;
            default:
                if (*cursor < 0x20U) {
                    fprintf(output, "\\u%04X", (unsigned int)*cursor);
                } else {
                    fputc((int)*cursor, output);
                }
                break;
        }
        cursor++;
    }
    fputc('"', output);
}

static void porpoise_trace_write_stack(
    FILE *output,
    const PorpoisePpcState *state)
{
    uint32_t index;

    fputs(",\"stack\":[", output);
    for (index = 0U; index < state->trace_call_depth; index++) {
        if (index != 0U) fputc(',', output);
        fprintf(
            output,
            "\"0x%08lX\"",
            (unsigned long)state->trace_call_stack[index]);
    }
    fputc(']', output);
    if (state->trace_call_overflow != 0U) {
        fprintf(
            output,
            ",\"stack_overflow\":%lu",
            (unsigned long)state->trace_call_overflow);
    }
}

static FILE *porpoise_trace_begin_event(
    PorpoisePpcState *state,
    const char *event)
{
    FILE *output = porpoise_trace_output(state);
    const char *function_name = "";
    uint32_t function_address = 0U;

    if (output == NULL) return NULL;
    if (state->trace_call_depth != 0U) {
        uint32_t current = state->trace_call_depth - 1U;
        function_address = state->trace_call_stack[current];
        function_name = state->trace_function_stack[current];
    }
    state->trace_sequence++;
    fprintf(
        output,
        "{\"sequence\":%llu,\"event\":",
        (unsigned long long)state->trace_sequence);
    porpoise_trace_json_string(output, event);
    fprintf(output, ",\"pc\":\"0x%08lX\"", (unsigned long)state->pc);
    fputs(",\"function\":", output);
    porpoise_trace_json_string(output, function_name);
    fprintf(
        output,
        ",\"function_address\":\"0x%08lX\"",
        (unsigned long)function_address);
    return output;
}

static void porpoise_trace_finish_event(
    FILE *output,
    const PorpoisePpcState *state)
{
    porpoise_trace_write_stack(output, state);
    fputs("}\n", output);
    fflush(output);
}

int porpoise_trace_configure_from_environment(PorpoisePpcState *state)
{
    const char *trace_path;
    const char *frame_limit;
    const char *reject_approximations;
    char *end = NULL;
    unsigned long parsed_limit;

    if (state == NULL || state->trace_file != NULL) return 0;
    trace_path = getenv("PORPOISE_TRACE");
    frame_limit = getenv("PORPOISE_FRAME_LIMIT");
    reject_approximations = getenv("PORPOISE_REJECT_APPROXIMATIONS");
    if (trace_path != NULL && trace_path[0] != '\0') {
        FILE *output = fopen(trace_path, "wb");
        if (output == NULL) return 0;
        state->trace_file = output;
    }
    if (frame_limit != NULL && frame_limit[0] != '\0') {
        errno = 0;
        parsed_limit = strtoul(frame_limit, &end, 10);
        if (errno != 0 || end == frame_limit || *end != '\0' ||
            parsed_limit == 0UL || parsed_limit > (unsigned long)UINT32_MAX) {
            porpoise_trace_close(state);
            return 0;
        }
        state->trace_frame_limit = (uint32_t)parsed_limit;
    }
    if (reject_approximations != NULL &&
        reject_approximations[0] != '\0') {
        if (strcmp(reject_approximations, "1") == 0) {
            state->reject_approximations = 1;
        } else if (strcmp(reject_approximations, "0") != 0) {
            porpoise_trace_close(state);
            return 0;
        }
    }
    return 1;
}

void porpoise_trace_close(PorpoisePpcState *state)
{
    FILE *output;

    if (state == NULL) return;
    output = porpoise_trace_output(state);
    if (output != NULL) fclose(output);
    state->trace_file = NULL;
    state->trace_call_depth = 0U;
    state->trace_call_overflow = 0U;
    memset(
        state->trace_function_stack,
        0,
        sizeof(state->trace_function_stack));
}

void porpoise_trace_call_enter(
    PorpoisePpcState *state,
    uint32_t guest_address,
    const char *dispatch_kind,
    const char *function_name)
{
    FILE *output;

    if (porpoise_trace_output(state) == NULL) return;
    if (state->trace_call_depth < PORPOISE_TRACE_STACK_CAPACITY) {
        state->trace_call_stack[state->trace_call_depth] = guest_address;
        state->trace_function_stack[state->trace_call_depth] = function_name;
        state->trace_call_depth++;
    } else if (state->trace_call_overflow != UINT32_MAX) {
        state->trace_call_overflow++;
    }
    output = porpoise_trace_begin_event(state, "call");
    if (output == NULL) return;
    fputs(",\"phase\":\"enter\",\"address\":", output);
    fprintf(output, "\"0x%08lX\",\"kind\":", (unsigned long)guest_address);
    porpoise_trace_json_string(output, dispatch_kind);
    porpoise_trace_finish_event(output, state);
}

void porpoise_trace_call_exit(
    PorpoisePpcState *state,
    uint32_t guest_address,
    const char *dispatch_kind,
    const char *function_name)
{
    FILE *output = porpoise_trace_begin_event(state, "call");
    (void)function_name;

    if (output == NULL) return;
    fputs(",\"phase\":\"exit\",\"address\":", output);
    fprintf(output, "\"0x%08lX\",\"kind\":", (unsigned long)guest_address);
    porpoise_trace_json_string(output, dispatch_kind);
    porpoise_trace_finish_event(output, state);
    if (state->trace_call_overflow != 0U) {
        state->trace_call_overflow--;
    } else if (state->trace_call_depth != 0U) {
        state->trace_call_depth--;
        state->trace_function_stack[state->trace_call_depth] = NULL;
    }
}

void porpoise_trace_approximate(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    const char *mnemonic)
{
    FILE *output;

    if (state == NULL) return;
    if (state->approximation_count == 0U) {
        state->first_approximation_address = instruction_address;
        (void)snprintf(
            state->first_approximation_mnemonic,
            sizeof(state->first_approximation_mnemonic),
            "%s",
            mnemonic != NULL ? mnemonic : "");
    }
    if (state->approximation_count != UINT64_MAX) {
        state->approximation_count++;
    }

    output = porpoise_trace_begin_event(state, "approximate");
    if (output != NULL) {
        fprintf(
            output,
            ",\"address\":\"0x%08lX\",\"mnemonic\":",
            (unsigned long)instruction_address);
        porpoise_trace_json_string(output, mnemonic);
        porpoise_trace_finish_event(output, state);
    }

    if (state->reject_approximations != 0 &&
        state->fault == PORPOISE_FAULT_NONE) {
        char message[PORPOISE_FAULT_MESSAGE_CAPACITY];

        (void)snprintf(
            message,
            sizeof(message),
            "unreviewed approximate instruction reached: %s",
            mnemonic != NULL && mnemonic[0] != '\0' ? mnemonic : "unknown");
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            instruction_address,
            message);
    }
}

static void porpoise_trace_frame_internal(
    PorpoisePpcState *state,
    uint32_t guest_frame_buffer,
    int content_observed,
    uint64_t content_hash,
    int content_varied)
{
    FILE *output;

    /* A retrace with no selected guest XFB is not a presented frame.  In
     * particular it must never satisfy the test-only frame limit. */
    if (state == NULL || porpoise_state_should_stop(state) ||
        guest_frame_buffer == 0U) return;
    if (state->trace_frame_count != UINT32_MAX) state->trace_frame_count++;
    output = porpoise_trace_begin_event(state, "frame");
    if (output != NULL) {
        fprintf(
            output,
            ",\"frame\":%lu,\"frame_buffer\":\"0x%08lX\"",
            (unsigned long)state->trace_frame_count,
            (unsigned long)guest_frame_buffer);
        if (content_observed != 0) {
            fprintf(
                output,
                ",\"content_hash\":\"0x%016llX\",\"content_varied\":%s",
                (unsigned long long)content_hash,
                content_varied != 0 ? "true" : "false");
        }
        porpoise_trace_finish_event(output, state);
    }
    if (state->trace_frame_limit != 0U &&
        state->trace_frame_count >= state->trace_frame_limit) {
        state->status = PORPOISE_EXECUTION_RETURNED;
    }
}

void porpoise_trace_frame(
    PorpoisePpcState *state,
    uint32_t guest_frame_buffer)
{
    porpoise_trace_frame_internal(
        state, guest_frame_buffer, 0, UINT64_C(0), 0);
}

void porpoise_trace_frame_observed(
    PorpoisePpcState *state,
    uint32_t guest_frame_buffer,
    uint64_t content_hash,
    int content_varied)
{
    porpoise_trace_frame_internal(
        state,
        guest_frame_buffer,
        1,
        content_hash,
        content_varied != 0);
}

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
        case PORPOISE_FAULT_FLOATING_POINT_EXCEPTION:
            return "enabled floating-point exception";
        case PORPOISE_FAULT_FLOATING_POINT_UNAVAILABLE:
            return "floating-point unavailable";
        case PORPOISE_FAULT_PRIVILEGED_OPERATION:
            return "privileged operation in problem state";
        case PORPOISE_FAULT_ILLEGAL_INSTRUCTION:
            return "illegal instruction";
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

int porpoise_state_prepare_title_entry(PorpoisePpcState *state)
{
    if (porpoise_state_should_stop(state)) {
        return 0;
    }
    if (state->gpr[1] == 0U || state->gpr[2] == 0U ||
        state->gpr[13] == 0U || (state->gpr[1] & UINT32_C(7)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "title host did not provide aligned r1 and nonzero r2/r13");
        return 0;
    }

    /* DolphinMain begins after native OSInit, where external interrupts are
     * enabled. The lifted title deliberately does not execute console __start. */
    state->msr |= PORPOISE_MSR_EE | PORPOISE_MSR_FP;
    state->hid2 |= PORPOISE_HID2_PSE | PORPOISE_HID2_LSQE;
    return 1;
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
    if (!state->trace_fault_emitted) {
        FILE *output = porpoise_trace_begin_event(state, "fault");
        state->trace_fault_emitted = 1;
        if (output != NULL) {
            fprintf(
                output,
                ",\"fault_address\":\"0x%08lX\",\"fault\":",
                (unsigned long)guest_address);
            porpoise_trace_json_string(output, porpoise_fault_string(fault));
            fputs(",\"message\":", output);
            porpoise_trace_json_string(output, fault_message);
            porpoise_trace_finish_event(output, state);
        }
    }
}

static int porpoise_execution_status_is_valid(
    PorpoiseExecutionStatus status)
{
    return status == PORPOISE_EXECUTION_READY ||
           status == PORPOISE_EXECUTION_RUNNING ||
           status == PORPOISE_EXECUTION_RETURNED ||
           status == PORPOISE_EXECUTION_FAULTED;
}

int porpoise_state_has_fault(const PorpoisePpcState *state)
{
    return state == NULL || state->fault != PORPOISE_FAULT_NONE ||
           state->status == PORPOISE_EXECUTION_FAULTED ||
           !porpoise_execution_status_is_valid(state->status);
}

int porpoise_state_should_stop(const PorpoisePpcState *state)
{
    return porpoise_state_has_fault(state) ||
           state->status == PORPOISE_EXECUTION_RETURNED;
}

int porpoise_guest_lr_returns_to_caller(const PorpoisePpcState *state)
{
    if (state == NULL) return 0;
    /* A directly embedded lifted function has no generated dispatcher frame;
     * its C caller is the only available continuation. Generated execution
     * always has a provenance entry and therefore takes the checked path. */
    if (state->lifted_call_depth == 0U) return 1;
    return state->lifted_call_depth <= PORPOISE_LIFTED_CALL_STACK_CAPACITY &&
           state->lr == state->lifted_return_stack[state->lifted_call_depth - 1U];
}

const char *porpoise_state_fault_message(const PorpoisePpcState *state)
{
    if (state == NULL) {
        return porpoise_fault_string(PORPOISE_FAULT_INVALID_STATE);
    }
    if (state->fault_message[0] != '\0') {
        return state->fault_message;
    }
    if ((state->status == PORPOISE_EXECUTION_FAULTED ||
         !porpoise_execution_status_is_valid(state->status)) &&
        state->fault == PORPOISE_FAULT_NONE) {
        return porpoise_fault_string(PORPOISE_FAULT_INVALID_STATE);
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

uint32_t porpoise_sign_extend8(uint32_t value)
{
    value &= UINT32_C(0xFF);
    return (value & UINT32_C(0x80)) != 0U
               ? value | UINT32_C(0xFFFFFF00)
               : value;
}

uint32_t porpoise_sign_extend16(uint32_t value)
{
    value &= UINT32_C(0xFFFF);
    return (value & UINT32_C(0x8000)) != 0U
               ? value | UINT32_C(0xFFFF0000)
               : value;
}

uint32_t porpoise_count_leading_zeros32(uint32_t value)
{
    uint32_t count = 0U;

    if (value == 0U) {
        return 32U;
    }
    while ((value & UINT32_C(0x80000000)) == 0U) {
        value <<= 1U;
        count++;
    }
    return count;
}

uint32_t porpoise_add_with_carry32(
    PorpoisePpcState *state,
    uint32_t left,
    uint32_t right,
    uint32_t carry_in)
{
    const uint32_t carry_mask = UINT32_C(0x20000000);
    uint64_t sum = (uint64_t)left + (uint64_t)right +
                   (uint64_t)(carry_in & UINT32_C(1));

    if (state != NULL) {
        if (sum > UINT32_MAX) {
            state->xer |= carry_mask;
        } else {
            state->xer &= ~carry_mask;
        }
    }
    return (uint32_t)sum;
}

uint32_t porpoise_arithmetic_shift_right32(
    PorpoisePpcState *state,
    uint32_t value,
    unsigned int shift)
{
    uint32_t result;
    uint32_t discarded;
    const uint32_t carry_mask = UINT32_C(0x20000000);

    shift &= 63U;
    if (shift == 0U) {
        result = value;
        discarded = 0U;
    } else if (shift >= 32U) {
        result = (value & UINT32_C(0x80000000)) != 0U ? UINT32_MAX : 0U;
        discarded = value;
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
    int relation = (value & UINT32_C(0x80000000)) != 0U
                       ? -1
                       : value != 0U ? 1 : 0;
    porpoise_cr_set_field(state, 0U, porpoise_condition_field(state, relation));
}

void porpoise_compare_signed(
    PorpoisePpcState *state,
    unsigned int field_index,
    uint32_t left,
    uint32_t right)
{
    uint32_t ordered_left = left ^ UINT32_C(0x80000000);
    uint32_t ordered_right = right ^ UINT32_C(0x80000000);
    int relation = ordered_left < ordered_right
                       ? -1
                       : ordered_left > ordered_right ? 1 : 0;
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

void porpoise_fpscr_recompute_summaries(PorpoisePpcState *state)
{
    uint32_t fpscr;
    int enabled_exception;

    if (state == NULL) {
        return;
    }

    fpscr = state->fpscr;
    if ((fpscr & PORPOISE_FPSCR_INVALID_CAUSE_MASK) != 0U) {
        fpscr |= PORPOISE_FPSCR_VX;
    } else {
        fpscr &= ~PORPOISE_FPSCR_VX;
    }

    enabled_exception =
        (((fpscr & PORPOISE_FPSCR_VX) != 0U) &&
         ((fpscr & PORPOISE_FPSCR_VE) != 0U)) ||
        (((fpscr & PORPOISE_FPSCR_OX) != 0U) &&
         ((fpscr & PORPOISE_FPSCR_OE) != 0U)) ||
        (((fpscr & PORPOISE_FPSCR_UX) != 0U) &&
         ((fpscr & PORPOISE_FPSCR_UE) != 0U)) ||
        (((fpscr & PORPOISE_FPSCR_ZX) != 0U) &&
         ((fpscr & PORPOISE_FPSCR_ZE) != 0U)) ||
        (((fpscr & PORPOISE_FPSCR_XX) != 0U) &&
         ((fpscr & PORPOISE_FPSCR_XE) != 0U));
    if (enabled_exception) {
        fpscr |= PORPOISE_FPSCR_FEX;
    } else {
        fpscr &= ~PORPOISE_FPSCR_FEX;
    }

    state->fpscr = fpscr;
}

void porpoise_fpscr_raise_exceptions(
    PorpoisePpcState *state,
    uint32_t exception_causes)
{
    uint32_t causes;

    if (state == NULL) {
        return;
    }

    causes = exception_causes & PORPOISE_FPSCR_EXCEPTION_CAUSE_MASK;
    if (causes == 0U) {
        return;
    }
    if ((causes & ~state->fpscr) != 0U) {
        state->fpscr |= PORPOISE_FPSCR_FX;
    }
    state->fpscr |= causes;
    porpoise_fpscr_recompute_summaries(state);
}

void porpoise_fpscr_update_cr1(PorpoisePpcState *state)
{
    if (state == NULL) {
        return;
    }
    porpoise_cr_set_field(
        state,
        1U,
        (uint8_t)((state->fpscr >> 28U) & UINT32_C(0xF)));
}

#define PORPOISE_F64_SIGN_MASK UINT64_C(0x8000000000000000)
#define PORPOISE_F64_EXPONENT_MASK UINT64_C(0x7FF0000000000000)
#define PORPOISE_F64_FRACTION_MASK UINT64_C(0x000FFFFFFFFFFFFF)
#define PORPOISE_F64_QUIET_MASK UINT64_C(0x0008000000000000)
#define PORPOISE_F32_SIGN_MASK UINT32_C(0x80000000)
#define PORPOISE_F32_EXPONENT_MASK UINT32_C(0x7F800000)
#define PORPOISE_F32_FRACTION_MASK UINT32_C(0x007FFFFF)
#define PORPOISE_F32_QUIET_MASK UINT32_C(0x00400000)

#define PORPOISE_FPRF_QNAN 0x11U
#define PORPOISE_FPRF_NEGATIVE_INFINITY 0x09U
#define PORPOISE_FPRF_NEGATIVE_NORMAL 0x08U
#define PORPOISE_FPRF_NEGATIVE_DENORMAL 0x18U
#define PORPOISE_FPRF_NEGATIVE_ZERO 0x12U
#define PORPOISE_FPRF_POSITIVE_ZERO 0x02U
#define PORPOISE_FPRF_POSITIVE_DENORMAL 0x14U
#define PORPOISE_FPRF_POSITIVE_NORMAL 0x04U
#define PORPOISE_FPRF_POSITIVE_INFINITY 0x05U

uint64_t porpoise_binary32_to_binary64_bits(uint32_t binary32_bits)
{
    uint64_t sign = (uint64_t)(binary32_bits & PORPOISE_F32_SIGN_MASK)
                    << 32U;
    unsigned int exponent =
        (unsigned int)((binary32_bits >> 23U) & UINT32_C(0xFF));
    uint32_t fraction = binary32_bits & PORPOISE_F32_FRACTION_MASK;

    if (exponent > 0U && exponent < 255U) {
        uint64_t double_exponent = (uint64_t)(exponent + 896U) << 52U;
        return sign | double_exponent | ((uint64_t)fraction << 29U);
    }
    if (exponent == 0U && fraction != 0U) {
        unsigned int highest_bit = 0U;
        uint32_t remaining = fraction;
        uint64_t significand;

        while ((remaining >>= 1U) != 0U) {
            highest_bit++;
        }
        significand = (uint64_t)fraction << (52U - highest_bit);
        return sign |
               ((uint64_t)(highest_bit + 874U) << 52U) |
               (significand & PORPOISE_F64_FRACTION_MASK);
    }

    return sign |
           (exponent == 255U ? PORPOISE_F64_EXPONENT_MASK : 0U) |
           ((uint64_t)fraction << 29U);
}

uint32_t porpoise_binary64_to_binary32_bits(uint64_t binary64_bits)
{
    uint64_t magnitude = binary64_bits & ~PORPOISE_F64_SIGN_MASK;
    unsigned int exponent =
        (unsigned int)((binary64_bits >> 52U) & UINT64_C(0x7FF));
    uint32_t selected_bits =
        (uint32_t)((binary64_bits >> 32U) & UINT64_C(0xC0000000)) |
        (uint32_t)((binary64_bits >> 29U) & UINT64_C(0x3FFFFFFF));

    if (exponent > 896U || magnitude == 0U) {
        return selected_bits;
    }
    if (exponent >= 874U) {
        uint64_t denormalized =
            UINT64_C(0x80000000) |
            ((binary64_bits & PORPOISE_F64_FRACTION_MASK) >> 21U);
        unsigned int shift = 905U - exponent;
        uint32_t sign = (uint32_t)(binary64_bits >> 32U) &
                        PORPOISE_F32_SIGN_MASK;
        return sign | (uint32_t)(denormalized >> shift);
    }

    /* Appendix D.7 calls this source range undefined. Keep it deterministic. */
    return selected_bits;
}

static int porpoise_f64_is_nan(uint64_t bits)
{
    return (bits & PORPOISE_F64_EXPONENT_MASK) ==
               PORPOISE_F64_EXPONENT_MASK &&
           (bits & PORPOISE_F64_FRACTION_MASK) != 0U;
}

static int porpoise_f64_is_signaling_nan(uint64_t bits)
{
    return porpoise_f64_is_nan(bits) &&
           (bits & PORPOISE_F64_QUIET_MASK) == 0U;
}

static uint8_t porpoise_f64_compare(uint64_t left_bits, uint64_t right_bits)
{
    uint64_t left_magnitude = left_bits & ~PORPOISE_F64_SIGN_MASK;
    uint64_t right_magnitude = right_bits & ~PORPOISE_F64_SIGN_MASK;
    uint64_t left_key;
    uint64_t right_key;

    if (porpoise_f64_is_nan(left_bits) ||
        porpoise_f64_is_nan(right_bits)) {
        return PORPOISE_FPCC_UNORDERED;
    }
    if (left_magnitude == 0U && right_magnitude == 0U) {
        return PORPOISE_FPCC_EQUAL;
    }

    left_key = (left_bits & PORPOISE_F64_SIGN_MASK) != 0U
                   ? ~left_bits
                   : left_bits ^ PORPOISE_F64_SIGN_MASK;
    right_key = (right_bits & PORPOISE_F64_SIGN_MASK) != 0U
                    ? ~right_bits
                    : right_bits ^ PORPOISE_F64_SIGN_MASK;
    if (left_key < right_key) {
        return PORPOISE_FPCC_LESS;
    }
    if (left_key > right_key) {
        return PORPOISE_FPCC_GREATER;
    }
    return PORPOISE_FPCC_EQUAL;
}

static void porpoise_float_compare(
    PorpoisePpcState *state,
    unsigned int field_index,
    uint64_t left_bits,
    uint64_t right_bits,
    int ordered)
{
    int left_nan;
    int right_nan;
    int signaling_nan;
    int invalid_operation;
    uint32_t causes = 0U;
    uint8_t result;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return;
    }
    if (field_index >= 8U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "floating-point comparison CR field is outside 0..7");
        return;
    }

    left_nan = porpoise_f64_is_nan(left_bits);
    right_nan = porpoise_f64_is_nan(right_bits);
    signaling_nan = porpoise_f64_is_signaling_nan(left_bits) ||
                    porpoise_f64_is_signaling_nan(right_bits);
    invalid_operation = signaling_nan ||
                        (ordered && (left_nan || right_nan));

    if (signaling_nan) {
        causes |= PORPOISE_FPSCR_VXSNAN;
    }
    if (ordered && (left_nan || right_nan)) {
        if (!signaling_nan ||
            (state->fpscr & PORPOISE_FPSCR_VE) == 0U) {
            causes |= PORPOISE_FPSCR_VXVC;
        }
    }
    if (causes != 0U) {
        porpoise_fpscr_raise_exceptions(state, causes);
    }

    result = porpoise_f64_compare(left_bits, right_bits);
    state->fpscr =
        (state->fpscr & ~PORPOISE_FPSCR_FPCC_MASK) |
        ((uint32_t)result << 12U);
    porpoise_cr_set_field(state, field_index, result);

    /*
     * The lifted runtime has no guest program-exception vector. Stop after
     * applying the architecturally visible compare state instead of silently
     * continuing through an enabled invalid-operation exception.
     */
    if (invalid_operation &&
        (state->fpscr & PORPOISE_FPSCR_VE) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_FLOATING_POINT_EXCEPTION,
            state->pc,
            ordered
                ? "enabled invalid operation during fcmpo"
                : "enabled invalid operation during fcmpu");
    }
}

void porpoise_fcmpo(
    PorpoisePpcState *state,
    unsigned int field_index,
    uint64_t left_bits,
    uint64_t right_bits)
{
    porpoise_float_compare(
        state,
        field_index,
        left_bits,
        right_bits,
        1);
}

void porpoise_fcmpu(
    PorpoisePpcState *state,
    unsigned int field_index,
    uint64_t left_bits,
    uint64_t right_bits)
{
    porpoise_float_compare(
        state,
        field_index,
        left_bits,
        right_bits,
        0);
}

uint64_t porpoise_fsel_bits(
    uint64_t condition_bits,
    uint64_t nonnegative_bits,
    uint64_t negative_bits)
{
    uint64_t magnitude = condition_bits & ~PORPOISE_F64_SIGN_MASK;

    if (porpoise_f64_is_nan(condition_bits)) {
        return negative_bits;
    }
    if (magnitude == 0U ||
        (condition_bits & PORPOISE_F64_SIGN_MASK) == 0U) {
        return nonnegative_bits;
    }
    return negative_bits;
}

static int porpoise_validate_fpr_lane(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index)
{
    if (state == NULL) {
        return 0;
    }
    if (register_index >= 32U || lane_index >= 2U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "FPR register or lane index is out of range");
        return 0;
    }
    return 1;
}

uint64_t porpoise_fpr_get_bits(
    const PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index)
{
    if (state == NULL || register_index >= 32U || lane_index >= 2U) {
        return 0U;
    }
    return state->fpr[register_index].lane_bits[lane_index];
}

void porpoise_fpr_set_bits(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index,
    uint64_t bits)
{
    if (!porpoise_validate_fpr_lane(state, register_index, lane_index)) {
        return;
    }
    state->fpr[register_index].lane_bits[lane_index] = bits;
}

double porpoise_fpr_get_f64(
    const PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index)
{
    uint64_t bits = porpoise_fpr_get_bits(state, register_index, lane_index);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void porpoise_fpr_set_f64(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index,
    double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    porpoise_fpr_set_bits(state, register_index, lane_index, bits);
}

typedef struct PorpoiseSingleRoundResult {
    uint32_t bits;
    int inexact;
    int incremented;
    int overflow;
    int underflow;
} PorpoiseSingleRoundResult;

typedef struct PorpoiseHostFmaResult {
    uint64_t bits;
    int inexact;
    int overflow;
    int underflow;
} PorpoiseHostFmaResult;

static void porpoise_fpscr_set_fprf(
    PorpoisePpcState *state,
    uint8_t fprf)
{
    state->fpscr =
        (state->fpscr & ~PORPOISE_FPSCR_FPRF_MASK) |
        ((uint32_t)(fprf & 0x1FU) << 12U);
}

static void porpoise_fpscr_clear_rounding(PorpoisePpcState *state)
{
    state->fpscr &= ~(PORPOISE_FPSCR_FR | PORPOISE_FPSCR_FI);
}

static void porpoise_fpscr_set_rounding(
    PorpoisePpcState *state,
    int inexact,
    int incremented,
    uint32_t additional_causes)
{
    uint32_t causes = additional_causes;

    porpoise_fpscr_clear_rounding(state);
    if (incremented) {
        state->fpscr |= PORPOISE_FPSCR_FR;
    }
    if (inexact) {
        state->fpscr |= PORPOISE_FPSCR_FI;
        causes |= PORPOISE_FPSCR_XX;
    }
    if (causes != 0U) {
        porpoise_fpscr_raise_exceptions(state, causes);
    }
}

static uint8_t porpoise_fprf_from_binary32(uint32_t bits)
{
    uint32_t magnitude = bits & ~PORPOISE_F32_SIGN_MASK;
    int negative = (bits & PORPOISE_F32_SIGN_MASK) != 0U;

    if ((bits & PORPOISE_F32_EXPONENT_MASK) ==
        PORPOISE_F32_EXPONENT_MASK) {
        if ((bits & PORPOISE_F32_FRACTION_MASK) != 0U) {
            return PORPOISE_FPRF_QNAN;
        }
        return negative
                   ? PORPOISE_FPRF_NEGATIVE_INFINITY
                   : PORPOISE_FPRF_POSITIVE_INFINITY;
    }
    if (magnitude == 0U) {
        return negative
                   ? PORPOISE_FPRF_NEGATIVE_ZERO
                   : PORPOISE_FPRF_POSITIVE_ZERO;
    }
    if ((bits & PORPOISE_F32_EXPONENT_MASK) == 0U) {
        return negative
                   ? PORPOISE_FPRF_NEGATIVE_DENORMAL
                   : PORPOISE_FPRF_POSITIVE_DENORMAL;
    }
    return negative
               ? PORPOISE_FPRF_NEGATIVE_NORMAL
               : PORPOISE_FPRF_POSITIVE_NORMAL;
}

static uint8_t porpoise_fprf_from_binary64(uint64_t bits)
{
    uint64_t magnitude = bits & ~PORPOISE_F64_SIGN_MASK;
    int negative = (bits & PORPOISE_F64_SIGN_MASK) != 0U;

    if ((bits & PORPOISE_F64_EXPONENT_MASK) ==
        PORPOISE_F64_EXPONENT_MASK) {
        if ((bits & PORPOISE_F64_FRACTION_MASK) != 0U) {
            return PORPOISE_FPRF_QNAN;
        }
        return negative
                   ? PORPOISE_FPRF_NEGATIVE_INFINITY
                   : PORPOISE_FPRF_POSITIVE_INFINITY;
    }
    if (magnitude == 0U) {
        return negative
                   ? PORPOISE_FPRF_NEGATIVE_ZERO
                   : PORPOISE_FPRF_POSITIVE_ZERO;
    }
    if ((bits & PORPOISE_F64_EXPONENT_MASK) == 0U) {
        return negative
                   ? PORPOISE_FPRF_NEGATIVE_DENORMAL
                   : PORPOISE_FPRF_POSITIVE_DENORMAL;
    }
    return negative
               ? PORPOISE_FPRF_NEGATIVE_NORMAL
               : PORPOISE_FPRF_POSITIVE_NORMAL;
}

static uint64_t porpoise_normalize_f64_significand(
    uint64_t bits,
    int *exponent)
{
    unsigned int exponent_field =
        (unsigned int)((bits >> 52U) & UINT64_C(0x7FF));
    uint64_t fraction = bits & PORPOISE_F64_FRACTION_MASK;

    if (exponent_field != 0U) {
        *exponent = (int)exponent_field - 1023;
        return UINT64_C(0x0010000000000000) | fraction;
    }

    {
        unsigned int highest_bit = 0U;
        uint64_t remaining = fraction;

        while ((remaining >>= 1U) != 0U) {
            highest_bit++;
        }
        *exponent = -1074 + (int)highest_bit;
        return fraction << (52U - highest_bit);
    }
}

static uint64_t porpoise_round_right(
    uint64_t value,
    unsigned int shift,
    int negative,
    unsigned int rounding_mode,
    int *inexact,
    int *incremented)
{
    uint64_t retained;
    uint64_t remainder;
    int increment = 0;

    if (shift == 0U) {
        *inexact = 0;
        *incremented = 0;
        return value;
    }
    if (shift < 64U) {
        uint64_t mask = (UINT64_C(1) << shift) - UINT64_C(1);
        retained = value >> shift;
        remainder = value & mask;
    } else {
        retained = 0U;
        remainder = value;
    }

    *inexact = remainder != 0U;
    if (*inexact) {
        if (rounding_mode == 0U && shift < 64U) {
            uint64_t halfway = UINT64_C(1) << (shift - 1U);
            increment = remainder > halfway ||
                        (remainder == halfway &&
                         (retained & UINT64_C(1)) != 0U);
        } else if (rounding_mode == 2U && !negative) {
            increment = 1;
        } else if (rounding_mode == 3U && negative) {
            increment = 1;
        }
    }
    *incremented = increment;
    return retained + (uint64_t)increment;
}

static uint32_t porpoise_binary32_overflow_result(
    int negative,
    unsigned int rounding_mode)
{
    uint32_t sign = negative ? PORPOISE_F32_SIGN_MASK : 0U;
    int to_infinity = rounding_mode == 0U ||
                      (rounding_mode == 2U && !negative) ||
                      (rounding_mode == 3U && negative);

    return sign |
           (to_infinity
                ? PORPOISE_F32_EXPONENT_MASK
                : UINT32_C(0x7F7FFFFF));
}

static PorpoiseSingleRoundResult porpoise_round_finite_to_binary32(
    uint64_t bits,
    unsigned int rounding_mode)
{
    PorpoiseSingleRoundResult result;
    uint32_t sign = (bits & PORPOISE_F64_SIGN_MASK) != 0U
                        ? PORPOISE_F32_SIGN_MASK
                        : 0U;
    int negative = sign != 0U;
    int exponent;
    uint64_t significand;
    uint64_t rounded;

    memset(&result, 0, sizeof(result));
    significand = porpoise_normalize_f64_significand(bits, &exponent);

    if (exponent >= -126) {
        rounded = porpoise_round_right(
            significand,
            29U,
            negative,
            rounding_mode,
            &result.inexact,
            &result.incremented);
        if (rounded == UINT64_C(0x01000000)) {
            rounded >>= 1U;
            exponent++;
        }
        if (exponent > 127) {
            result.bits = porpoise_binary32_overflow_result(
                negative,
                rounding_mode);
            result.inexact = 1;
            /* FR is architecturally undefined for disabled overflow. */
            result.incremented = 0;
            result.overflow = 1;
            return result;
        }
        result.bits = sign |
                      ((uint32_t)(exponent + 127) << 23U) |
                      ((uint32_t)rounded & PORPOISE_F32_FRACTION_MASK);
        return result;
    }

    {
        unsigned int shift = (unsigned int)(-exponent - 97);
        rounded = porpoise_round_right(
            significand,
            shift,
            negative,
            rounding_mode,
            &result.inexact,
            &result.incremented);
        result.underflow = result.inexact;
        if (rounded >= UINT64_C(0x00800000)) {
            result.bits = sign | UINT32_C(0x00800000);
        } else {
            result.bits = sign | (uint32_t)rounded;
        }
    }
    return result;
}

static uint64_t porpoise_adjusted_single_result(
    uint64_t bits,
    unsigned int rounding_mode,
    int exponent_adjustment,
    int *inexact,
    int *incremented)
{
    uint64_t sign = bits & PORPOISE_F64_SIGN_MASK;
    uint64_t significand;
    uint64_t rounded;
    int exponent;

    significand = porpoise_normalize_f64_significand(bits, &exponent);
    rounded = porpoise_round_right(
        significand,
        29U,
        sign != 0U,
        rounding_mode,
        inexact,
        incremented);
    if (rounded == UINT64_C(0x01000000)) {
        rounded >>= 1U;
        exponent++;
    }
    exponent += exponent_adjustment;
    return sign |
           ((uint64_t)(unsigned int)(exponent + 1023) << 52U) |
           ((rounded & UINT64_C(0x007FFFFF)) << 29U);
}

static int porpoise_f64_is_infinity(uint64_t bits)
{
    return (bits & ~PORPOISE_F64_SIGN_MASK) ==
           PORPOISE_F64_EXPONENT_MASK;
}

static int porpoise_f64_is_zero(uint64_t bits)
{
    return (bits & ~PORPOISE_F64_SIGN_MASK) == 0U;
}

static int porpoise_f64_is_denormal(uint64_t bits)
{
    return (bits & PORPOISE_F64_EXPONENT_MASK) == 0U &&
           (bits & PORPOISE_F64_FRACTION_MASK) != 0U;
}

static uint64_t porpoise_f64_quiet_nan(uint64_t bits)
{
    return bits | PORPOISE_F64_QUIET_MASK;
}

static double porpoise_double_from_bits(uint64_t bits)
{
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint64_t porpoise_double_to_bits(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int porpoise_host_rounding_mode(unsigned int rounding_mode)
{
    switch (rounding_mode) {
        case 1U:
            return FE_TOWARDZERO;
        case 2U:
            return FE_UPWARD;
        case 3U:
            return FE_DOWNWARD;
        default:
            return FE_TONEAREST;
    }
}

static PorpoiseHostFmaResult porpoise_host_fma(
    double multiplicand,
    double multiplier,
    double addend,
    unsigned int rounding_mode)
{
    PorpoiseHostFmaResult result;
    fenv_t saved_environment;
    int environment_saved;
    int exceptions = 0;
    volatile double left = multiplicand;
    volatile double right = multiplier;
    volatile double add = addend;
    double value;

    memset(&result, 0, sizeof(result));
    environment_saved = feholdexcept(&saved_environment) == 0;
    if (environment_saved) {
        (void)fesetround(porpoise_host_rounding_mode(rounding_mode));
    }
    value = fma(left, right, add);
    if (environment_saved) {
        exceptions = fetestexcept(FE_INEXACT | FE_OVERFLOW | FE_UNDERFLOW);
        (void)fesetenv(&saved_environment);
    }

    result.bits = porpoise_double_to_bits(value);
    result.inexact = (exceptions & FE_INEXACT) != 0;
    result.overflow = (exceptions & FE_OVERFLOW) != 0;
    result.underflow = (exceptions & FE_UNDERFLOW) != 0;
    return result;
}

static uint64_t porpoise_force_25_bit_multiplier(uint64_t bits)
{
    uint64_t exponent = bits & PORPOISE_F64_EXPONENT_MASK;

    if (exponent == 0U || exponent == PORPOISE_F64_EXPONENT_MASK) {
        return bits;
    }
    return (bits & UINT64_C(0xFFFFFFFFF8000000)) +
           (bits & UINT64_C(0x0000000008000000));
}

static uint64_t porpoise_flush_fma_operand(
    uint64_t bits,
    PorpoiseFpPrecision precision,
    int non_ieee)
{
    uint64_t magnitude;
    unsigned int exponent;

    if (!non_ieee || porpoise_f64_is_nan(bits) ||
        porpoise_f64_is_infinity(bits)) {
        return bits;
    }
    magnitude = bits & ~PORPOISE_F64_SIGN_MASK;
    if (magnitude == 0U) {
        return bits;
    }
    exponent = (unsigned int)(
        (bits & PORPOISE_F64_EXPONENT_MASK) >> 52U);
    if ((precision == PORPOISE_FP_PRECISION_DOUBLE && exponent == 0U) ||
        (precision == PORPOISE_FP_PRECISION_SINGLE && exponent < 897U)) {
        return bits & PORPOISE_F64_SIGN_MASK;
    }
    return bits;
}

static int porpoise_scalar_fp_finish(
    PorpoisePpcState *state,
    int record)
{
    if (record) {
        porpoise_fpscr_update_cr1(state);
    }
    return state->fault == PORPOISE_FAULT_NONE;
}

static int porpoise_scalar_fp_invalid_fault(
    PorpoisePpcState *state,
    int record,
    const char *message)
{
    if (record) {
        porpoise_fpscr_update_cr1(state);
    }
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_FLOATING_POINT_EXCEPTION,
        state->pc,
        message);
    return 0;
}

typedef struct PorpoiseRsqrtEstimateSegment {
    uint32_t intercept;
    int32_t slope;
} PorpoiseRsqrtEstimateSegment;

/*
 * Gekko's reciprocal-square-root estimate is a piecewise-linear bit
 * transform. These coefficients are hardware-test reference data for its 32
 * segments. Keeping the implementation integer-only makes the result
 * independent of the host floating-point environment.
 */
static const PorpoiseRsqrtEstimateSegment PORPOISE_RSQRT_SEGMENTS[32] = {
    {UINT32_C(0x1A7E800), -INT32_C(0x568)},
    {UINT32_C(0x17CB800), -INT32_C(0x4F3)},
    {UINT32_C(0x1552800), -INT32_C(0x48D)},
    {UINT32_C(0x130C000), -INT32_C(0x435)},
    {UINT32_C(0x10F2000), -INT32_C(0x3E7)},
    {UINT32_C(0x0EFF000), -INT32_C(0x3A2)},
    {UINT32_C(0x0D2E000), -INT32_C(0x365)},
    {UINT32_C(0x0B7C000), -INT32_C(0x32E)},
    {UINT32_C(0x09E5000), -INT32_C(0x2FC)},
    {UINT32_C(0x0867000), -INT32_C(0x2D0)},
    {UINT32_C(0x06FF000), -INT32_C(0x2A8)},
    {UINT32_C(0x05AB800), -INT32_C(0x283)},
    {UINT32_C(0x046A000), -INT32_C(0x261)},
    {UINT32_C(0x0339800), -INT32_C(0x243)},
    {UINT32_C(0x0218800), -INT32_C(0x226)},
    {UINT32_C(0x0105800), -INT32_C(0x20B)},
    {UINT32_C(0x3FFA000), -INT32_C(0x7A4)},
    {UINT32_C(0x3C29000), -INT32_C(0x700)},
    {UINT32_C(0x38AA000), -INT32_C(0x670)},
    {UINT32_C(0x3572000), -INT32_C(0x5F2)},
    {UINT32_C(0x3279000), -INT32_C(0x584)},
    {UINT32_C(0x2FB7000), -INT32_C(0x524)},
    {UINT32_C(0x2D26000), -INT32_C(0x4CC)},
    {UINT32_C(0x2AC0000), -INT32_C(0x47E)},
    {UINT32_C(0x2881000), -INT32_C(0x43A)},
    {UINT32_C(0x2665000), -INT32_C(0x3FA)},
    {UINT32_C(0x2468000), -INT32_C(0x3C2)},
    {UINT32_C(0x2287000), -INT32_C(0x38E)},
    {UINT32_C(0x20C1000), -INT32_C(0x35E)},
    {UINT32_C(0x1F12000), -INT32_C(0x332)},
    {UINT32_C(0x1D79000), -INT32_C(0x30A)},
    {UINT32_C(0x1BF4000), -INT32_C(0x2E6)},
};

static uint64_t porpoise_gecko_frsqrte_finite_bits(uint64_t source_bits)
{
    uint64_t fraction = source_bits & PORPOISE_F64_FRACTION_MASK;
    int64_t exponent = (int64_t)(source_bits & PORPOISE_F64_EXPONENT_MASK);
    uint64_t exponent_parity;
    int64_t mapped_exponent;
    uint32_t lookup;
    uint32_t segment_index;
    uint32_t segment_offset;
    const PorpoiseRsqrtEstimateSegment *segment;
    int64_t estimate;

    if (exponent == 0) {
        do {
            exponent -= INT64_C(0x0010000000000000);
            fraction <<= 1U;
        } while ((fraction & UINT64_C(0x0010000000000000)) == 0U);
        fraction &= PORPOISE_F64_FRACTION_MASK;
        exponent += INT64_C(0x0010000000000000);
    }

    exponent_parity =
        (uint64_t)exponent & UINT64_C(0x0010000000000000);
    mapped_exponent =
        INT64_C(0x3FF0000000000000) -
        ((exponent - INT64_C(0x3FE0000000000000)) / INT64_C(2));
    lookup = (uint32_t)((exponent_parity | fraction) >> 37U);
    segment_index = lookup >> 11U;
    segment_offset = lookup & UINT32_C(0x7FF);
    segment = &PORPOISE_RSQRT_SEGMENTS[segment_index];
    estimate = (int64_t)segment->intercept +
               (int64_t)segment->slope * (int64_t)segment_offset;

    return ((uint64_t)mapped_exponent & PORPOISE_F64_EXPONENT_MASK) |
           ((uint64_t)estimate << 26U);
}

int porpoise_frsqrte(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int source_register,
    int record)
{
    uint64_t source_bits;
    uint64_t result_bits;
    uint64_t magnitude;
    uint64_t exponent;
    uint64_t fraction;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    if (!porpoise_validate_fpr_lane(state, destination_register, 0U) ||
        !porpoise_validate_fpr_lane(state, source_register, 0U)) {
        return 0;
    }

    source_bits = state->fpr[source_register].lane_bits[0];
    magnitude = source_bits & ~PORPOISE_F64_SIGN_MASK;
    exponent = source_bits & PORPOISE_F64_EXPONENT_MASK;
    fraction = source_bits & PORPOISE_F64_FRACTION_MASK;

    if (magnitude == 0U) {
        porpoise_fpscr_clear_rounding(state);
        porpoise_fpscr_raise_exceptions(state, PORPOISE_FPSCR_ZX);
        if ((state->fpscr & PORPOISE_FPSCR_ZE) != 0U) {
            return porpoise_scalar_fp_invalid_fault(
                state,
                record,
                "enabled zero-divide exception during frsqrte");
        }
        result_bits = (source_bits & PORPOISE_F64_SIGN_MASK) |
                      PORPOISE_F64_EXPONENT_MASK;
    } else if (exponent == PORPOISE_F64_EXPONENT_MASK && fraction != 0U) {
        int signaling = porpoise_f64_is_signaling_nan(source_bits);

        porpoise_fpscr_clear_rounding(state);
        if (signaling) {
            porpoise_fpscr_raise_exceptions(
                state,
                PORPOISE_FPSCR_VXSNAN);
            if ((state->fpscr & PORPOISE_FPSCR_VE) != 0U) {
                return porpoise_scalar_fp_invalid_fault(
                    state,
                    record,
                    "enabled invalid operation during frsqrte");
            }
        }
        result_bits = porpoise_f64_quiet_nan(source_bits);
    } else if ((source_bits & PORPOISE_F64_SIGN_MASK) != 0U) {
        porpoise_fpscr_clear_rounding(state);
        porpoise_fpscr_raise_exceptions(state, PORPOISE_FPSCR_VXSQRT);
        if ((state->fpscr & PORPOISE_FPSCR_VE) != 0U) {
            return porpoise_scalar_fp_invalid_fault(
                state,
                record,
                "enabled invalid square-root operation during frsqrte");
        }
        result_bits = UINT64_C(0x7FF8000000000000);
    } else if (exponent == PORPOISE_F64_EXPONENT_MASK) {
        porpoise_fpscr_clear_rounding(state);
        result_bits = 0U;
    } else {
        result_bits = porpoise_gecko_frsqrte_finite_bits(source_bits);
    }

    state->fpr[destination_register].lane_bits[0] = result_bits;
    porpoise_fpscr_set_fprf(
        state,
        porpoise_fprf_from_binary64(result_bits));
    return porpoise_scalar_fp_finish(state, record);
}

int porpoise_frsp(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int source_register,
    int record)
{
    uint64_t source_bits;
    uint64_t result_bits;
    unsigned int rounding_mode;
    int exponent;
    int inexact;
    int incremented;
    PorpoiseSingleRoundResult rounded;
    uint32_t causes;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    if (!porpoise_validate_fpr_lane(state, destination_register, 0U) ||
        !porpoise_validate_fpr_lane(state, source_register, 0U)) {
        return 0;
    }

    source_bits = state->fpr[source_register].lane_bits[0];
    rounding_mode = state->fpscr & PORPOISE_FPSCR_RN_MASK;

    if (porpoise_f64_is_nan(source_bits)) {
        int signaling = porpoise_f64_is_signaling_nan(source_bits);

        porpoise_fpscr_clear_rounding(state);
        if (signaling) {
            porpoise_fpscr_raise_exceptions(
                state,
                PORPOISE_FPSCR_VXSNAN);
            if ((state->fpscr & PORPOISE_FPSCR_VE) != 0U) {
                return porpoise_scalar_fp_invalid_fault(
                    state,
                    record,
                    "enabled invalid operation during frsp");
            }
        }
        result_bits = porpoise_f64_quiet_nan(source_bits) &
                      UINT64_C(0xFFFFFFFFE0000000);
        state->fpr[destination_register].lane_bits[0] = result_bits;
        state->fpr[destination_register].lane_bits[1] = result_bits;
        porpoise_fpscr_set_fprf(state, PORPOISE_FPRF_QNAN);
        return porpoise_scalar_fp_finish(state, record);
    }

    if (porpoise_f64_is_zero(source_bits) ||
        porpoise_f64_is_infinity(source_bits)) {
        porpoise_fpscr_clear_rounding(state);
        state->fpr[destination_register].lane_bits[0] = source_bits;
        state->fpr[destination_register].lane_bits[1] = source_bits;
        porpoise_fpscr_set_fprf(
            state,
            porpoise_fprf_from_binary64(source_bits));
        return porpoise_scalar_fp_finish(state, record);
    }

    if ((state->fpscr & PORPOISE_FPSCR_NI) != 0U &&
        porpoise_f64_is_denormal(source_bits)) {
        result_bits = source_bits & PORPOISE_F64_SIGN_MASK;
        porpoise_fpscr_clear_rounding(state);
        state->fpr[destination_register].lane_bits[0] = result_bits;
        state->fpr[destination_register].lane_bits[1] = result_bits;
        porpoise_fpscr_set_fprf(
            state,
            porpoise_fprf_from_binary64(result_bits));
        return porpoise_scalar_fp_finish(state, record);
    }

    (void)porpoise_normalize_f64_significand(source_bits, &exponent);

    if ((state->fpscr & PORPOISE_FPSCR_NI) == 0U &&
        exponent < -126 &&
        (state->fpscr & PORPOISE_FPSCR_UE) != 0U) {
        result_bits = porpoise_adjusted_single_result(
            source_bits,
            rounding_mode,
            192,
            &inexact,
            &incremented);
        state->fpr[destination_register].lane_bits[0] = result_bits;
        state->fpr[destination_register].lane_bits[1] = result_bits;
        porpoise_fpscr_set_rounding(
            state,
            inexact,
            incremented,
            PORPOISE_FPSCR_UX);
        porpoise_fpscr_set_fprf(
            state,
            porpoise_fprf_from_binary64(result_bits));
        return porpoise_scalar_fp_finish(state, record);
    }

    rounded = porpoise_round_finite_to_binary32(
        source_bits,
        rounding_mode);
    if ((state->fpscr & PORPOISE_FPSCR_NI) != 0U &&
        (rounded.bits & PORPOISE_F32_EXPONENT_MASK) == 0U &&
        (rounded.bits & PORPOISE_F32_FRACTION_MASK) != 0U) {
        rounded.bits &= PORPOISE_F32_SIGN_MASK;
        rounded.inexact = 1;
        rounded.incremented = 0;
        rounded.underflow = 1;
    }

    if (rounded.overflow &&
        (state->fpscr & PORPOISE_FPSCR_OE) != 0U) {
        result_bits = porpoise_adjusted_single_result(
            source_bits,
            rounding_mode,
            -192,
            &inexact,
            &incremented);
        state->fpr[destination_register].lane_bits[0] = result_bits;
        state->fpr[destination_register].lane_bits[1] = result_bits;
        porpoise_fpscr_set_rounding(
            state,
            inexact,
            incremented,
            PORPOISE_FPSCR_OX);
        porpoise_fpscr_set_fprf(
            state,
            porpoise_fprf_from_binary64(result_bits));
        return porpoise_scalar_fp_finish(state, record);
    }

    result_bits = porpoise_binary32_to_binary64_bits(rounded.bits);
    state->fpr[destination_register].lane_bits[0] = result_bits;
    state->fpr[destination_register].lane_bits[1] = result_bits;
    causes = (rounded.overflow ? PORPOISE_FPSCR_OX : 0U) |
             (rounded.underflow ? PORPOISE_FPSCR_UX : 0U);
    porpoise_fpscr_set_rounding(
        state,
        rounded.inexact,
        rounded.incremented,
        causes);
    porpoise_fpscr_set_fprf(
        state,
        porpoise_fprf_from_binary32(rounded.bits));

    return porpoise_scalar_fp_finish(state, record);
}

static int porpoise_fctiw_with_rounding(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int source_register,
    int record,
    unsigned int rounding_mode,
    const char *instruction_name)
{
    uint64_t source_bits;
    uint64_t magnitude_bits;
    uint64_t result_bits;
    uint32_t result_word = 0U;
    uint32_t causes = 0U;
    unsigned int exponent_field;
    int negative;
    int invalid = 0;
    int inexact = 0;
    int incremented = 0;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    if (!porpoise_validate_fpr_lane(state, destination_register, 0U) ||
        !porpoise_validate_fpr_lane(state, source_register, 0U)) {
        return 0;
    }

    source_bits = state->fpr[source_register].lane_bits[0];
    magnitude_bits = source_bits & ~PORPOISE_F64_SIGN_MASK;
    negative = (source_bits & PORPOISE_F64_SIGN_MASK) != 0U;
    exponent_field = (unsigned int)(
        (source_bits & PORPOISE_F64_EXPONENT_MASK) >> 52U);

    if (porpoise_f64_is_nan(source_bits)) {
        if (porpoise_f64_is_signaling_nan(source_bits)) {
            causes |= PORPOISE_FPSCR_VXSNAN;
        }
        causes |= PORPOISE_FPSCR_VXCVI;
        result_word = UINT32_C(0x80000000);
        invalid = 1;
    } else if (porpoise_f64_is_infinity(source_bits)) {
        causes |= PORPOISE_FPSCR_VXCVI;
        result_word = negative
                          ? UINT32_C(0x80000000)
                          : UINT32_C(0x7FFFFFFF);
        invalid = 1;
    } else if (magnitude_bits == 0U ||
               ((state->fpscr & PORPOISE_FPSCR_NI) != 0U &&
                porpoise_f64_is_denormal(source_bits))) {
        result_word = 0U;
    } else {
        uint64_t significand;
        uint64_t integer_magnitude = 0U;
        int exponent = -1022;

        if (exponent_field != 0U) {
            exponent = (int)exponent_field - 1023;
            significand = UINT64_C(0x0010000000000000) |
                          (source_bits & PORPOISE_F64_FRACTION_MASK);
        } else {
            significand = source_bits & PORPOISE_F64_FRACTION_MASK;
        }

        if (exponent > 31) {
            invalid = 1;
        } else {
            unsigned int shift = (unsigned int)(52 - exponent);

            integer_magnitude = porpoise_round_right(
                significand,
                shift,
                negative,
                rounding_mode,
                &inexact,
                &incremented);
            if ((!negative && integer_magnitude > UINT64_C(0x7FFFFFFF)) ||
                (negative && integer_magnitude > UINT64_C(0x80000000))) {
                invalid = 1;
            }
        }

        if (invalid) {
            causes |= PORPOISE_FPSCR_VXCVI;
            result_word = negative
                              ? UINT32_C(0x80000000)
                              : UINT32_C(0x7FFFFFFF);
        } else if (negative) {
            result_word = UINT32_C(0) - (uint32_t)integer_magnitude;
        } else {
            result_word = (uint32_t)integer_magnitude;
        }
    }

    if (invalid) {
        porpoise_fpscr_clear_rounding(state);
        porpoise_fpscr_raise_exceptions(state, causes);
        if ((state->fpscr & PORPOISE_FPSCR_VE) != 0U) {
            return porpoise_scalar_fp_invalid_fault(
                state,
                record,
                instruction_name);
        }
    } else {
        porpoise_fpscr_set_rounding(state, inexact, incremented, 0U);
    }

    result_bits = UINT64_C(0xFFF8000000000000) | result_word;
    if (result_word == 0U && negative) {
        result_bits |= UINT64_C(0x0000000100000000);
    }
    state->fpr[destination_register].lane_bits[0] = result_bits;
    return porpoise_scalar_fp_finish(state, record);
}

int porpoise_fctiw(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int source_register,
    int record)
{
    unsigned int rounding_mode = state != NULL
                                     ? state->fpscr & PORPOISE_FPSCR_RN_MASK
                                     : 0U;

    return porpoise_fctiw_with_rounding(
        state,
        destination_register,
        source_register,
        record,
        rounding_mode,
        "enabled invalid operation during fctiw");
}

int porpoise_fctiwz(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int source_register,
    int record)
{
    return porpoise_fctiw_with_rounding(
        state,
        destination_register,
        source_register,
        record,
        1U,
        "enabled invalid operation during fctiwz");
}

int porpoise_mffs(
    PorpoisePpcState *state,
    unsigned int destination_register,
    int record)
{
    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    if (!porpoise_validate_fpr_lane(state, destination_register, 0U)) {
        return 0;
    }

    /* The high word is undefined; use the stable Gekko-compatible pattern. */
    state->fpr[destination_register].lane_bits[0] =
        UINT64_C(0xFFF8000000000000) | state->fpscr;
    return porpoise_scalar_fp_finish(state, record);
}

int porpoise_mtfsf(
    PorpoisePpcState *state,
    unsigned int field_mask,
    unsigned int source_register,
    int record)
{
    uint32_t selected = 0U;
    uint32_t source;
    unsigned int field;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    if (field_mask > 0xFFU) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "mtfsf field mask is outside 0..255");
        return 0;
    }
    if (!porpoise_validate_fpr_lane(state, source_register, 0U)) {
        return 0;
    }

    for (field = 0U; field < 8U; field++) {
        if ((field_mask & (0x80U >> field)) != 0U) {
            selected |= UINT32_C(0xF0000000) >> (field * 4U);
        }
    }
    source = (uint32_t)state->fpr[source_register].lane_bits[0];
    state->fpscr = (state->fpscr & ~selected) | (source & selected);
    porpoise_fpscr_recompute_summaries(state);
    return porpoise_scalar_fp_finish(state, record);
}

int porpoise_mtfsb1(
    PorpoisePpcState *state,
    unsigned int bit_index,
    int record)
{
    uint32_t bit;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    if (bit_index >= 32U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "mtfsb1 bit index is outside 0..31");
        return 0;
    }

    bit = UINT32_C(0x80000000) >> bit_index;
    if (bit_index != 1U && bit_index != 2U) {
        if ((bit & PORPOISE_FPSCR_EXCEPTION_CAUSE_MASK) != 0U) {
            porpoise_fpscr_raise_exceptions(state, bit);
        } else {
            state->fpscr |= bit;
        }
    }
    porpoise_fpscr_recompute_summaries(state);
    return porpoise_scalar_fp_finish(state, record);
}

static void porpoise_write_fma_result(
    PorpoisePpcState *state,
    unsigned int destination_register,
    PorpoiseFpPrecision precision,
    uint64_t bits)
{
    state->fpr[destination_register].lane_bits[0] = bits;
    if (precision == PORPOISE_FP_PRECISION_SINGLE) {
        state->fpr[destination_register].lane_bits[1] = bits;
    }
}

int porpoise_fp_fma(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int multiplicand_register,
    unsigned int multiplier_register,
    unsigned int addend_register,
    PorpoiseFpFmaOperation operation,
    PorpoiseFpPrecision precision,
    int record)
{
    uint64_t a_bits;
    uint64_t b_bits;
    uint64_t c_bits;
    uint64_t result_bits;
    uint64_t selected_nan = 0U;
    uint32_t causes = 0U;
    unsigned int rounding_mode;
    int subtract;
    int negate;
    int a_nan;
    int b_nan;
    int c_nan;
    int product_infinity;
    int invalid_infinities;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    if (operation < PORPOISE_FP_FMA_MADD ||
        operation > PORPOISE_FP_FMA_NMSUB ||
        (precision != PORPOISE_FP_PRECISION_DOUBLE &&
         precision != PORPOISE_FP_PRECISION_SINGLE)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "invalid scalar fused multiply-add operation or precision");
        return 0;
    }
    if (!porpoise_validate_fpr_lane(state, destination_register, 0U) ||
        !porpoise_validate_fpr_lane(state, multiplicand_register, 0U) ||
        !porpoise_validate_fpr_lane(state, multiplier_register, 0U) ||
        !porpoise_validate_fpr_lane(state, addend_register, 0U)) {
        return 0;
    }

    rounding_mode = state->fpscr & PORPOISE_FPSCR_RN_MASK;
    subtract = operation == PORPOISE_FP_FMA_MSUB ||
               operation == PORPOISE_FP_FMA_NMSUB;
    negate = operation == PORPOISE_FP_FMA_NMADD ||
             operation == PORPOISE_FP_FMA_NMSUB;

    a_bits = porpoise_flush_fma_operand(
        state->fpr[multiplicand_register].lane_bits[0],
        precision,
        (state->fpscr & PORPOISE_FPSCR_NI) != 0U);
    c_bits = porpoise_flush_fma_operand(
        state->fpr[multiplier_register].lane_bits[0],
        precision,
        (state->fpscr & PORPOISE_FPSCR_NI) != 0U);
    b_bits = porpoise_flush_fma_operand(
        state->fpr[addend_register].lane_bits[0],
        precision,
        (state->fpscr & PORPOISE_FPSCR_NI) != 0U);

    a_nan = porpoise_f64_is_nan(a_bits);
    b_nan = porpoise_f64_is_nan(b_bits);
    c_nan = porpoise_f64_is_nan(c_bits);
    if (porpoise_f64_is_signaling_nan(a_bits) ||
        porpoise_f64_is_signaling_nan(b_bits) ||
        porpoise_f64_is_signaling_nan(c_bits)) {
        causes |= PORPOISE_FPSCR_VXSNAN;
    }
    if ((porpoise_f64_is_zero(a_bits) &&
         porpoise_f64_is_infinity(c_bits)) ||
        (porpoise_f64_is_infinity(a_bits) &&
         porpoise_f64_is_zero(c_bits))) {
        causes |= PORPOISE_FPSCR_VXIMZ;
    }

    product_infinity =
        !a_nan && !c_nan &&
        ((porpoise_f64_is_infinity(a_bits) &&
          !porpoise_f64_is_zero(c_bits)) ||
         (porpoise_f64_is_infinity(c_bits) &&
          !porpoise_f64_is_zero(a_bits)));
    invalid_infinities =
        product_infinity && porpoise_f64_is_infinity(b_bits) &&
        ((((a_bits ^ c_bits) & PORPOISE_F64_SIGN_MASK) != 0U) !=
         (((b_bits & PORPOISE_F64_SIGN_MASK) != 0U) ^ subtract));
    if (invalid_infinities) {
        causes |= PORPOISE_FPSCR_VXISI;
    }

    if (a_nan) {
        selected_nan = a_bits;
    } else if (b_nan) {
        selected_nan = b_bits;
    } else if (c_nan) {
        selected_nan = c_bits;
    }

    if (selected_nan != 0U || causes != 0U) {
        porpoise_fpscr_clear_rounding(state);
        if (causes != 0U) {
            porpoise_fpscr_raise_exceptions(state, causes);
        }
        if (causes != 0U &&
            (state->fpscr & PORPOISE_FPSCR_VE) != 0U) {
            return porpoise_scalar_fp_invalid_fault(
                state,
                record,
                "enabled invalid operation during fused multiply-add");
        }
        result_bits = selected_nan != 0U
                          ? porpoise_f64_quiet_nan(selected_nan)
                          : UINT64_C(0x7FF8000000000000);
        if (precision == PORPOISE_FP_PRECISION_SINGLE) {
            result_bits &= UINT64_C(0xFFFFFFFFE0000000);
        }
        porpoise_write_fma_result(
            state,
            destination_register,
            precision,
            result_bits);
        porpoise_fpscr_set_fprf(state, PORPOISE_FPRF_QNAN);
        return porpoise_scalar_fp_finish(state, record);
    }

    if (product_infinity || porpoise_f64_is_infinity(b_bits)) {
        int negative;

        if (product_infinity) {
            negative = ((a_bits ^ c_bits) &
                        PORPOISE_F64_SIGN_MASK) != 0U;
        } else {
            negative = ((b_bits & PORPOISE_F64_SIGN_MASK) != 0U) ^
                       subtract;
        }
        negative ^= negate;
        result_bits = PORPOISE_F64_EXPONENT_MASK |
                      (negative ? PORPOISE_F64_SIGN_MASK : 0U);
        porpoise_fpscr_clear_rounding(state);
        porpoise_write_fma_result(
            state,
            destination_register,
            precision,
            result_bits);
        porpoise_fpscr_set_fprf(
            state,
            porpoise_fprf_from_binary64(result_bits));
        return porpoise_scalar_fp_finish(state, record);
    }

    {
        PorpoiseHostFmaResult host_result;
        uint64_t host_c_bits = precision == PORPOISE_FP_PRECISION_SINGLE
                                   ? porpoise_force_25_bit_multiplier(c_bits)
                                   : c_bits;
        double a = porpoise_double_from_bits(a_bits);
        double c = porpoise_double_from_bits(host_c_bits);
        double b = porpoise_double_from_bits(
            subtract ? b_bits ^ PORPOISE_F64_SIGN_MASK : b_bits);

        host_result = porpoise_host_fma(a, c, b, rounding_mode);
        if (precision == PORPOISE_FP_PRECISION_DOUBLE) {
            result_bits = host_result.bits;
            if ((state->fpscr & PORPOISE_FPSCR_NI) != 0U &&
                porpoise_f64_is_denormal(result_bits)) {
                result_bits &= PORPOISE_F64_SIGN_MASK;
                host_result.inexact = 1;
                host_result.underflow = 1;
            }
            if (negate && !porpoise_f64_is_nan(result_bits)) {
                result_bits ^= PORPOISE_F64_SIGN_MASK;
            }
            causes =
                (host_result.overflow ? PORPOISE_FPSCR_OX : 0U) |
                (host_result.underflow ? PORPOISE_FPSCR_UX : 0U);
            porpoise_write_fma_result(
                state,
                destination_register,
                precision,
                result_bits);
            porpoise_fpscr_set_rounding(
                state,
                host_result.inexact,
                0,
                causes);
            porpoise_fpscr_set_fprf(
                state,
                porpoise_fprf_from_binary64(result_bits));
        } else {
            PorpoiseSingleRoundResult single_result;
            uint32_t single_bits;

            memset(&single_result, 0, sizeof(single_result));
            if (porpoise_f64_is_infinity(host_result.bits)) {
                single_result.bits = porpoise_binary32_overflow_result(
                    (host_result.bits & PORPOISE_F64_SIGN_MASK) != 0U,
                    rounding_mode);
                single_result.inexact = 1;
                single_result.overflow = 1;
            } else if (porpoise_f64_is_zero(host_result.bits)) {
                single_result.bits =
                    (host_result.bits & PORPOISE_F64_SIGN_MASK) != 0U
                        ? PORPOISE_F32_SIGN_MASK
                        : 0U;
                single_result.inexact = host_result.inexact;
                single_result.underflow = host_result.underflow;
            } else if (porpoise_f64_is_nan(host_result.bits)) {
                single_result.bits = UINT32_C(0x7FC00000);
            } else {
                single_result = porpoise_round_finite_to_binary32(
                    host_result.bits,
                    rounding_mode);
                single_result.inexact =
                    single_result.inexact || host_result.inexact;
                single_result.overflow =
                    single_result.overflow || host_result.overflow;
                single_result.underflow =
                    single_result.underflow || host_result.underflow;
            }
            if ((state->fpscr & PORPOISE_FPSCR_NI) != 0U &&
                (single_result.bits & PORPOISE_F32_EXPONENT_MASK) == 0U &&
                (single_result.bits & PORPOISE_F32_FRACTION_MASK) != 0U) {
                single_result.bits &= PORPOISE_F32_SIGN_MASK;
                single_result.inexact = 1;
                single_result.incremented = 0;
                single_result.underflow = 1;
            }
            single_bits = single_result.bits;
            if (negate &&
                (single_bits & PORPOISE_F32_EXPONENT_MASK) !=
                    PORPOISE_F32_EXPONENT_MASK) {
                single_bits ^= PORPOISE_F32_SIGN_MASK;
            } else if (negate &&
                       (single_bits & PORPOISE_F32_FRACTION_MASK) == 0U) {
                single_bits ^= PORPOISE_F32_SIGN_MASK;
            }
            result_bits = porpoise_binary32_to_binary64_bits(single_bits);
            causes =
                (single_result.overflow ? PORPOISE_FPSCR_OX : 0U) |
                (single_result.underflow ? PORPOISE_FPSCR_UX : 0U);
            porpoise_write_fma_result(
                state,
                destination_register,
                precision,
                result_bits);
            porpoise_fpscr_set_rounding(
                state,
                single_result.inexact,
                single_result.incremented,
                causes);
            porpoise_fpscr_set_fprf(
                state,
                porpoise_fprf_from_binary32(single_bits));
        }
    }

    return porpoise_scalar_fp_finish(state, record);
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

int porpoise_store_bytes(
    PorpoisePpcState *state,
    uint32_t guest_address,
    const uint8_t *source,
    size_t size)
{
    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    if (size == 0U) {
        return 1;
    }
    if (source == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_address,
            "raw guest-memory source is NULL");
        return 0;
    }
    return porpoise_write_bytes(state, guest_address, source, size);
}

int porpoise_zero_bytes(
    PorpoisePpcState *state,
    uint32_t guest_address,
    size_t size)
{
    static const uint8_t zeros[4096] = {0};
    size_t remaining = size;
    uint32_t cursor = guest_address;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
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
            "guest-memory zero fill crosses the 32-bit address boundary");
        return 0;
    }
    while (remaining != 0U) {
        size_t amount = remaining < sizeof(zeros) ? remaining : sizeof(zeros);
        if (!porpoise_write_bytes(state, cursor, zeros, amount)) {
            return 0;
        }
        cursor += (uint32_t)amount;
        remaining -= amount;
    }
    return 1;
}

int porpoise_require_supervisor(
    PorpoisePpcState *state,
    uint32_t instruction_address)
{
    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    if ((state->msr & PORPOISE_MSR_PR) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_PRIVILEGED_OPERATION,
            instruction_address,
            NULL);
        return 0;
    }
    return 1;
}

int porpoise_write_msr(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t value)
{
    uint32_t previous_value;

    if (!porpoise_require_supervisor(state, instruction_address)) {
        return 0;
    }
    previous_value = state->msr;
    state->msr = value;
    if ((previous_value & PORPOISE_MSR_EE) == 0U &&
        (value & PORPOISE_MSR_EE) != 0U) {
        return porpoise_poll_host_events(state, instruction_address);
    }
    return 1;
}

int porpoise_msr_transition_is_exact(
    const PorpoisePpcState *state,
    uint32_t value)
{
    const uint32_t modeled_mask =
        PORPOISE_MSR_EE | PORPOISE_MSR_PR | PORPOISE_MSR_FP |
        PORPOISE_MSR_RI;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE ||
        (state->msr & PORPOISE_MSR_PR) != 0U) {
        return 0;
    }
    return ((state->msr ^ value) & ~modeled_mask) == 0U;
}

int porpoise_poll_host_events(
    PorpoisePpcState *state,
    uint32_t instruction_address)
{
    PorpoiseHostResult result;
    PorpoiseExecutionStatus status_before;

    if (porpoise_state_should_stop(state)) {
        return 0;
    }
    if (state->host == NULL || state->host->poll_events == NULL) {
        return 1;
    }
    if (state->host_event_delivery_depth != 0U) {
        return 1;
    }

    status_before = state->status;
    state->host_event_delivery_depth = 1U;
    result = state->host->poll_events(state->host->context, state);
    state->host_event_delivery_depth = 0U;
    if (result != PORPOISE_HOST_OK) {
        if (state->fault == PORPOISE_FAULT_NONE) {
            porpoise_state_set_fault(
                state,
                porpoise_fault_from_host_result(result),
                instruction_address,
                porpoise_host_result_string(result));
        }
        return 0;
    }
    return porpoise_finish_host_event(
        state,
        status_before,
        instruction_address,
        "host event poll");
}

static int porpoise_read_raw_time_base(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint64_t *ticks_out)
{
    PorpoiseHostResult result;
    uint64_t ticks;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    if (ticks_out == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            instruction_address,
            "time-base destination is null");
        return 0;
    }
    if (state->host == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_NO_HOST_ADAPTER,
            instruction_address,
            NULL);
        return 0;
    }
    if (state->host->read_time_base == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            instruction_address,
            "host adapter has no time-base callback");
        return 0;
    }

    ticks = UINT64_C(0);
    result = state->host->read_time_base(
        state->host->context,
        &ticks);
    if (result != PORPOISE_HOST_OK) {
        porpoise_state_set_fault(
            state,
            porpoise_fault_from_host_result(result),
            instruction_address,
            porpoise_host_result_string(result));
        return 0;
    }

    *ticks_out = ticks;
    return 1;
}

int porpoise_time_base_read(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint64_t *ticks_out)
{
    uint64_t raw_ticks;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    if (ticks_out == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            instruction_address,
            "time-base destination is null");
        return 0;
    }
    if (!porpoise_read_raw_time_base(
            state,
            instruction_address,
            &raw_ticks)) {
        return 0;
    }
    *ticks_out = raw_ticks + state->time_base_bias;
    return 1;
}

static int porpoise_time_base_write_half(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t value,
    int write_upper)
{
    uint64_t raw_ticks;
    uint64_t guest_ticks;
    uint64_t replacement;

    if (!porpoise_require_supervisor(state, instruction_address)) {
        return 0;
    }
    if (!porpoise_read_raw_time_base(
            state,
            instruction_address,
            &raw_ticks)) {
        return 0;
    }

    guest_ticks = raw_ticks + state->time_base_bias;
    if (write_upper != 0) {
        replacement = ((uint64_t)value << 32U) |
                      (guest_ticks & UINT64_C(0xFFFFFFFF));
    } else {
        replacement = (guest_ticks & UINT64_C(0xFFFFFFFF00000000)) |
                      (uint64_t)value;
    }

    state->time_base_bias = replacement - raw_ticks;
    return 1;
}

int porpoise_time_base_write_lower(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t value)
{
    return porpoise_time_base_write_half(
        state,
        instruction_address,
        value,
        0);
}

int porpoise_time_base_write_upper(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t value)
{
    return porpoise_time_base_write_half(
        state,
        instruction_address,
        value,
        1);
}

int porpoise_decrementer_read(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t *value_out)
{
    uint64_t raw_ticks;
    uint32_t elapsed;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    if (value_out == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            instruction_address,
            "decrementer destination is null");
        return 0;
    }
    if (!porpoise_require_supervisor(state, instruction_address)) {
        return 0;
    }
    if (!porpoise_read_raw_time_base(
            state,
            instruction_address,
            &raw_ticks)) {
        return 0;
    }

    if (state->decrementer_valid == 0) {
        state->decrementer_anchor = raw_ticks;
        state->decrementer_valid = 1;
    }
    elapsed = (uint32_t)(raw_ticks - state->decrementer_anchor);
    *value_out = state->decrementer_value - elapsed;
    return 1;
}

int porpoise_decrementer_write(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t value)
{
    uint64_t raw_ticks;

    if (!porpoise_require_supervisor(state, instruction_address)) {
        return 0;
    }
    if (!porpoise_read_raw_time_base(
            state,
            instruction_address,
            &raw_ticks)) {
        return 0;
    }

    state->decrementer_value = value;
    state->decrementer_anchor = raw_ticks;
    state->decrementer_valid = 1;
    return 1;
}

int porpoise_cache_block_zero(
    PorpoisePpcState *state,
    uint32_t effective_address)
{
    static const uint8_t zero_block[32] = {0U};
    uint32_t block_address = effective_address & UINT32_C(0xFFFFFFE0);

    return porpoise_write_bytes(
        state,
        block_address,
        zero_block,
        sizeof(zero_block));
}

int porpoise_data_cache_block_invalidate(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t effective_address)
{
    (void)effective_address;
    return porpoise_require_supervisor(state, instruction_address);
}

static int porpoise_trap_condition(
    uint32_t trap_options,
    uint32_t left,
    uint32_t right)
{
    uint32_t signed_left = left ^ UINT32_C(0x80000000);
    uint32_t signed_right = right ^ UINT32_C(0x80000000);

    return (((trap_options & PORPOISE_TRAP_SIGNED_LESS) != 0U &&
             signed_left < signed_right) ||
            ((trap_options & PORPOISE_TRAP_SIGNED_GREATER) != 0U &&
             signed_left > signed_right) ||
            ((trap_options & PORPOISE_TRAP_EQUAL) != 0U &&
             left == right) ||
            ((trap_options & PORPOISE_TRAP_UNSIGNED_LESS) != 0U &&
             left < right) ||
            ((trap_options & PORPOISE_TRAP_UNSIGNED_GREATER) != 0U &&
             left > right));
}

static int porpoise_finish_host_event(
    PorpoisePpcState *state,
    PorpoiseExecutionStatus status_before,
    uint32_t instruction_address,
    const char *event_name)
{
    char message[96];

    if (state->fault != PORPOISE_FAULT_NONE) {
        state->status = PORPOISE_EXECUTION_FAULTED;
        return 0;
    }
    if (state->status == status_before) {
        return 1;
    }
    if (state->status == PORPOISE_EXECUTION_RETURNED) {
        return 0;
    }

    (void)snprintf(
        message,
        sizeof(message),
        "%s callback produced an invalid execution status",
        event_name);
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_INVALID_STATE,
        instruction_address,
        message);
    return 0;
}

int porpoise_trap_event(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t trap_options,
    uint32_t left,
    uint32_t right)
{
    PorpoiseHostResult result;
    PorpoiseExecutionStatus status_before;

    if (porpoise_state_should_stop(state)) {
        return 0;
    }
    if ((trap_options & ~UINT32_C(0x1F)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            instruction_address,
            "trap option is outside 0..31");
        return 0;
    }
    if (!porpoise_trap_condition(trap_options, left, right)) {
        return 1;
    }
    if (state->host == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_NO_HOST_ADAPTER,
            instruction_address,
            NULL);
        return 0;
    }
    if (state->host->trap == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            instruction_address,
            "host adapter has no trap callback");
        return 0;
    }

    status_before = state->status;
    result = state->host->trap(
        state->host->context,
        state,
        instruction_address,
        trap_options,
        left,
        right);
    if (result != PORPOISE_HOST_OK) {
        if (state->fault == PORPOISE_FAULT_NONE) {
            porpoise_state_set_fault(
                state,
                porpoise_fault_from_host_result(result),
                instruction_address,
                porpoise_host_result_string(result));
        }
        return 0;
    }
    return porpoise_finish_host_event(
        state,
        status_before,
        instruction_address,
        "trap");
}

int porpoise_system_call_event(
    PorpoisePpcState *state,
    uint32_t instruction_address)
{
    PorpoiseHostResult result;
    PorpoiseExecutionStatus status_before;

    if (porpoise_state_should_stop(state)) {
        return 0;
    }
    if (state->host == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_NO_HOST_ADAPTER,
            instruction_address,
            NULL);
        return 0;
    }
    if (state->host->system_call == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            instruction_address,
            "host adapter has no system-call callback");
        return 0;
    }

    status_before = state->status;
    result = state->host->system_call(
        state->host->context,
        state,
        instruction_address);
    if (result != PORPOISE_HOST_OK) {
        if (state->fault == PORPOISE_FAULT_NONE) {
            porpoise_state_set_fault(
                state,
                porpoise_fault_from_host_result(result),
                instruction_address,
                porpoise_host_result_string(result));
        }
        return 0;
    }
    return porpoise_finish_host_event(
        state,
        status_before,
        instruction_address,
        "system-call");
}

int porpoise_illegal_instruction(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    const char *message)
{
    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_ILLEGAL_INSTRUCTION,
        instruction_address,
        message);
    return 0;
}

typedef enum PorpoisePsqType {
    PORPOISE_PSQ_TYPE_FLOAT32 = 0,
    PORPOISE_PSQ_TYPE_UNSIGNED8 = 4,
    PORPOISE_PSQ_TYPE_UNSIGNED16 = 5,
    PORPOISE_PSQ_TYPE_SIGNED8 = 6,
    PORPOISE_PSQ_TYPE_SIGNED16 = 7
} PorpoisePsqType;

static int porpoise_validate_psq_arguments(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int w,
    unsigned int gqr_index,
    unsigned int requires_lsqe)
{
    if (state == NULL || state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    if (register_index >= 32U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "PSQ FPR index is outside 0..31");
        return 0;
    }
    if (w > 1U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "PSQ W value is outside 0..1");
        return 0;
    }
    if (gqr_index >= 8U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "PSQ GQR index is outside 0..7");
        return 0;
    }
    if (requires_lsqe > 1U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "PSQ instruction-form selector is outside 0..1");
        return 0;
    }
    return 1;
}

static int porpoise_psq_preflight(
    PorpoisePpcState *state,
    unsigned int requires_lsqe)
{
    if ((state->hid2 & PORPOISE_HID2_PSE) == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_ILLEGAL_INSTRUCTION,
            state->pc,
            "paired-single execution is disabled by HID2[PSE]");
        return 0;
    }
    if (requires_lsqe != 0U &&
        (state->hid2 & PORPOISE_HID2_LSQE) == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_ILLEGAL_INSTRUCTION,
            state->pc,
            "non-indexed quantized load/store is disabled by HID2[LSQE]");
        return 0;
    }
    if ((state->msr & PORPOISE_MSR_FP) == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_FLOATING_POINT_UNAVAILABLE,
            state->pc,
            "paired-single instruction executed with MSR[FP] cleared");
        return 0;
    }
    return 1;
}

static int porpoise_psq_element_size(
    PorpoisePpcState *state,
    unsigned int type,
    int is_store,
    size_t *size_out)
{
    switch (type) {
        case PORPOISE_PSQ_TYPE_FLOAT32:
            *size_out = 4U;
            return 1;
        case PORPOISE_PSQ_TYPE_UNSIGNED8:
        case PORPOISE_PSQ_TYPE_SIGNED8:
            *size_out = 1U;
            return 1;
        case PORPOISE_PSQ_TYPE_UNSIGNED16:
        case PORPOISE_PSQ_TYPE_SIGNED16:
            *size_out = 2U;
            return 1;
        default:
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_UNSUPPORTED_OPERATION,
                state->pc,
                is_store
                    ? "reserved GQR store quantization type"
                    : "reserved GQR load quantization type");
            return 0;
    }
}

static int porpoise_psq_load_scale(unsigned int raw_scale)
{
    return raw_scale <= 31U
               ? -(int)raw_scale
               : 64 - (int)raw_scale;
}

static int porpoise_psq_store_scale(unsigned int raw_scale)
{
    return raw_scale <= 31U
               ? (int)raw_scale
               : (int)raw_scale - 64;
}

static int porpoise_fpr_lane_is_widened_binary32(uint64_t bits)
{
    uint32_t narrowed = porpoise_binary64_to_binary32_bits(bits);

    return porpoise_binary32_to_binary64_bits(narrowed) == bits;
}

typedef struct PorpoiseExactBinary32Result {
    uint64_t widened_bits;
    uint32_t binary32_bits;
    int inexact;
    int incremented;
    int overflow;
    int underflow;
} PorpoiseExactBinary32Result;

static int porpoise_fp_exact_controls_are_active(
    const PorpoisePpcState *state,
    int paired)
{
    const uint32_t unsupported_controls =
        PORPOISE_FPSCR_VE | PORPOISE_FPSCR_OE |
        PORPOISE_FPSCR_UE | PORPOISE_FPSCR_ZE |
        PORPOISE_FPSCR_XE | PORPOISE_FPSCR_NI |
        PORPOISE_FPSCR_RN_MASK;

    return state != NULL && state->fault == PORPOISE_FAULT_NONE &&
           (state->msr & PORPOISE_MSR_FP) != 0U &&
           (!paired || (state->hid2 & PORPOISE_HID2_PSE) != 0U) &&
           (state->fpscr & unsupported_controls) == 0U;
}

static int porpoise_fpr_lane_is_ordinary_binary32(uint64_t bits)
{
    uint32_t narrowed;
    uint32_t exponent;

    if (!porpoise_fpr_lane_is_widened_binary32(bits)) {
        return 0;
    }
    narrowed = porpoise_binary64_to_binary32_bits(bits);
    exponent = narrowed & PORPOISE_F32_EXPONENT_MASK;
    if (exponent == PORPOISE_F32_EXPONENT_MASK) {
        return 0;
    }
    return exponent != 0U ||
           (narrowed & PORPOISE_F32_FRACTION_MASK) == 0U;
}

static int porpoise_fpr_lane_is_finite_normal_or_zero(uint64_t bits)
{
    uint64_t exponent = bits & PORPOISE_F64_EXPONENT_MASK;

    return exponent != PORPOISE_F64_EXPONENT_MASK &&
           (exponent != 0U ||
            (bits & PORPOISE_F64_FRACTION_MASK) == 0U);
}

static int porpoise_host_binary64_operation_is_exact(
    uint64_t left_bits,
    uint64_t right_bits,
    PorpoiseFpBinaryOperation operation,
    uint64_t *result_bits_out)
{
    fenv_t saved_environment;
    volatile double left = porpoise_double_from_bits(left_bits);
    volatile double right = porpoise_double_from_bits(right_bits);
    volatile double result;
    int exceptions;

    if (result_bits_out == NULL ||
        operation < PORPOISE_FP_BINARY_ADD ||
        operation > PORPOISE_FP_BINARY_DIVIDE ||
        feholdexcept(&saved_environment) != 0) {
        return 0;
    }
    if (fesetround(FE_TONEAREST) != 0) {
        (void)fesetenv(&saved_environment);
        return 0;
    }

    switch (operation) {
        case PORPOISE_FP_BINARY_ADD:
            result = left + right;
            break;
        case PORPOISE_FP_BINARY_SUBTRACT:
            result = left - right;
            break;
        case PORPOISE_FP_BINARY_MULTIPLY:
            result = left * right;
            break;
        case PORPOISE_FP_BINARY_DIVIDE:
            result = left / right;
            break;
        default:
            (void)fesetenv(&saved_environment);
            return 0;
    }

    exceptions = fetestexcept(
        FE_INEXACT | FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW);
    *result_bits_out = porpoise_double_to_bits((double)result);
    (void)fesetenv(&saved_environment);
    return exceptions == 0;
}

static int porpoise_exact_binary32_result(
    uint64_t result_bits,
    PorpoiseExactBinary32Result *result_out)
{
    PorpoiseSingleRoundResult rounded;
    if (result_out == NULL || porpoise_f64_is_nan(result_bits) ||
        porpoise_f64_is_infinity(result_bits)) {
        return 0;
    }
    memset(result_out, 0, sizeof(*result_out));
    if (porpoise_f64_is_zero(result_bits)) {
        result_out->binary32_bits =
            (result_bits & PORPOISE_F64_SIGN_MASK) != 0U
                ? PORPOISE_F32_SIGN_MASK
                : 0U;
        result_out->widened_bits = porpoise_binary32_to_binary64_bits(
            result_out->binary32_bits);
        return 1;
    }

    rounded = porpoise_round_finite_to_binary32(result_bits, 0U);
    result_out->binary32_bits = rounded.bits;
    result_out->widened_bits = porpoise_binary32_to_binary64_bits(
        rounded.bits);
    result_out->inexact = rounded.inexact;
    result_out->incremented = rounded.incremented;
    result_out->overflow = rounded.overflow;
    result_out->underflow = rounded.underflow;
    return 1;
}

static int porpoise_evaluate_exact_binary32_operation(
    uint64_t left_bits,
    uint64_t right_bits,
    PorpoiseFpBinaryOperation operation,
    PorpoiseExactBinary32Result *result_out)
{
    uint64_t intermediate_bits;

    if (!porpoise_fpr_lane_is_ordinary_binary32(left_bits) ||
        !porpoise_fpr_lane_is_ordinary_binary32(right_bits) ||
        !porpoise_host_binary64_operation_is_exact(
            left_bits,
            right_bits,
            operation,
            &intermediate_bits)) {
        return 0;
    }
    return porpoise_exact_binary32_result(intermediate_bits, result_out);
}

/* Divide two widened, ordinary binary32 values and round the exact rational
 * quotient directly to binary32.  This avoids using a rounded binary64
 * quotient as an intermediate (which is not sufficient evidence for fdivs).
 *
 * The currently claimed domain intentionally excludes exceptional and
 * subnormal results.  Those paths retain the existing approximation/fault
 * behavior until their Gekko-specific exception semantics are modeled. */
static int porpoise_evaluate_exact_binary32_division(
    uint64_t numerator_bits,
    uint64_t denominator_bits,
    PorpoiseExactBinary32Result *result_out)
{
    uint32_t numerator;
    uint32_t denominator;
    uint32_t numerator_magnitude;
    uint32_t denominator_magnitude;
    uint32_t numerator_significand;
    uint32_t denominator_significand;
    uint32_t quotient;
    uint32_t remainder;
    uint32_t sign;
    uint64_t scaled_numerator;
    int result_exponent;
    int incremented;

    if (result_out == NULL ||
        !porpoise_fpr_lane_is_ordinary_binary32(numerator_bits) ||
        !porpoise_fpr_lane_is_ordinary_binary32(denominator_bits)) {
        return 0;
    }
    numerator = porpoise_binary64_to_binary32_bits(numerator_bits);
    denominator = porpoise_binary64_to_binary32_bits(denominator_bits);
    numerator_magnitude = numerator & ~PORPOISE_F32_SIGN_MASK;
    denominator_magnitude = denominator & ~PORPOISE_F32_SIGN_MASK;
    if (denominator_magnitude == 0U) {
        return 0;
    }

    memset(result_out, 0, sizeof(*result_out));
    sign = (numerator ^ denominator) & PORPOISE_F32_SIGN_MASK;
    if (numerator_magnitude == 0U) {
        result_out->binary32_bits = sign;
        result_out->widened_bits = porpoise_binary32_to_binary64_bits(sign);
        return 1;
    }

    numerator_significand =
        UINT32_C(0x00800000) |
        (numerator & PORPOISE_F32_FRACTION_MASK);
    denominator_significand =
        UINT32_C(0x00800000) |
        (denominator & PORPOISE_F32_FRACTION_MASK);
    result_exponent =
        (int)((numerator & PORPOISE_F32_EXPONENT_MASK) >> 23U) -
        (int)((denominator & PORPOISE_F32_EXPONENT_MASK) >> 23U);
    if (numerator_significand < denominator_significand) {
        numerator_significand <<= 1U;
        result_exponent--;
    }

    /* The normalized significand ratio is in [1, 2).  Its 24-bit quotient
     * and full rounding remainder both fit comfortably in uint64_t. */
    scaled_numerator = (uint64_t)numerator_significand << 23U;
    quotient = (uint32_t)(scaled_numerator / denominator_significand);
    remainder = (uint32_t)(scaled_numerator % denominator_significand);
    incremented =
        (uint64_t)remainder * UINT64_C(2) > denominator_significand ||
        ((uint64_t)remainder * UINT64_C(2) == denominator_significand &&
         (quotient & 1U) != 0U);
    if (incremented) {
        quotient++;
        if (quotient == UINT32_C(0x01000000)) {
            quotient >>= 1U;
            result_exponent++;
        }
    }

    /* Stay deliberately conservative at underflow and overflow boundaries. */
    if (result_exponent < -126 || result_exponent > 127) {
        return 0;
    }
    result_out->binary32_bits =
        sign |
        ((uint32_t)(result_exponent + 127) << 23U) |
        (quotient & PORPOISE_F32_FRACTION_MASK);
    result_out->widened_bits = porpoise_binary32_to_binary64_bits(
        result_out->binary32_bits);
    result_out->inexact = remainder != 0U;
    result_out->incremented = incremented;
    return 1;
}

static int porpoise_evaluate_exact_scalar_single_operation(
    uint64_t left_bits,
    uint64_t right_bits,
    PorpoiseFpBinaryOperation operation,
    PorpoiseExactBinary32Result *result_out)
{
    uint64_t intermediate_bits;

    if (operation == PORPOISE_FP_BINARY_DIVIDE &&
        porpoise_evaluate_exact_binary32_division(
            left_bits,
            right_bits,
            result_out)) {
        return 1;
    }
    if (operation == PORPOISE_FP_BINARY_MULTIPLY) {
        uint64_t forced_multiplier =
            porpoise_force_25_bit_multiplier(right_bits);

        /* fmuls encodes its second source as frC.  Gekko applies Force25 to
         * that operand before multiplication.  A host binary64 product is a
         * valid exact intermediate only when fenv confirms no rounding. */
        if (!porpoise_fpr_lane_is_finite_normal_or_zero(left_bits) ||
            !porpoise_fpr_lane_is_finite_normal_or_zero(right_bits) ||
            !porpoise_host_binary64_operation_is_exact(
                left_bits,
                forced_multiplier,
                operation,
                &intermediate_bits)) {
            return 0;
        }
        return porpoise_exact_binary32_result(
            intermediate_bits,
            result_out);
    }
    if (!porpoise_fpr_lane_is_finite_normal_or_zero(left_bits) ||
        !porpoise_fpr_lane_is_finite_normal_or_zero(right_bits) ||
        !porpoise_host_binary64_operation_is_exact(
            left_bits,
            right_bits,
            operation,
            &intermediate_bits)) {
        return 0;
    }
    return porpoise_exact_binary32_result(intermediate_bits, result_out);
}

static int porpoise_evaluate_exact_binary32_fma(
    uint64_t multiplicand_bits,
    uint64_t multiplier_bits,
    uint64_t addend_bits,
    PorpoiseFpFmaOperation operation,
    PorpoiseExactBinary32Result *result_out)
{
    PorpoiseHostFmaResult host_result;
    uint64_t forced_multiplier;
    double addend;
    int subtract;
    int negate;

    if (operation < PORPOISE_FP_FMA_MADD ||
        operation > PORPOISE_FP_FMA_NMSUB ||
        !porpoise_fpr_lane_is_ordinary_binary32(multiplicand_bits) ||
        !porpoise_fpr_lane_is_ordinary_binary32(multiplier_bits) ||
        !porpoise_fpr_lane_is_ordinary_binary32(addend_bits)) {
        return 0;
    }
    forced_multiplier = porpoise_force_25_bit_multiplier(multiplier_bits);
    if (forced_multiplier != multiplier_bits) {
        return 0;
    }

    subtract = operation == PORPOISE_FP_FMA_MSUB ||
               operation == PORPOISE_FP_FMA_NMSUB;
    negate = operation == PORPOISE_FP_FMA_NMADD ||
             operation == PORPOISE_FP_FMA_NMSUB;
    addend = porpoise_double_from_bits(
        subtract ? addend_bits ^ PORPOISE_F64_SIGN_MASK : addend_bits);
    host_result = porpoise_host_fma(
        porpoise_double_from_bits(multiplicand_bits),
        porpoise_double_from_bits(forced_multiplier),
        addend,
        0U);
    if (host_result.inexact || host_result.overflow || host_result.underflow ||
        !porpoise_exact_binary32_result(host_result.bits, result_out)) {
        return 0;
    }
    if (negate) {
        result_out->binary32_bits ^= PORPOISE_F32_SIGN_MASK;
        result_out->widened_bits ^= PORPOISE_F64_SIGN_MASK;
    }
    return 1;
}

static void porpoise_commit_exact_paired_result(
    PorpoisePpcState *state,
    unsigned int destination_register,
    const PorpoiseExactBinary32Result *lane_zero,
    const PorpoiseExactBinary32Result *lane_one,
    unsigned int fprf_lane,
    int record)
{
    uint32_t causes =
        (lane_zero->overflow || lane_one->overflow
             ? PORPOISE_FPSCR_OX
             : 0U) |
        (lane_zero->underflow || lane_one->underflow
             ? PORPOISE_FPSCR_UX
             : 0U);

    state->fpr[destination_register].lane_bits[0] = lane_zero->widened_bits;
    state->fpr[destination_register].lane_bits[1] = lane_one->widened_bits;
    /* Gekko exception/status bits are set by either paired computation. */
    porpoise_fpscr_set_rounding(
        state,
        lane_zero->inexact || lane_one->inexact,
        lane_zero->incremented || lane_one->incremented,
        causes);
    porpoise_fpscr_set_fprf(
        state,
        porpoise_fprf_from_binary32(
            fprf_lane == 0U
                ? lane_zero->binary32_bits
                : lane_one->binary32_bits));
    if (record) {
        porpoise_fpscr_update_cr1(state);
    }
}

static uint32_t porpoise_ppc_fp_invalid_to_fpscr(uint32_t causes)
{
    uint32_t fpscr_causes = 0U;

    if ((causes & PORPOISE_PPC_FP_INVALID_SNAN) != 0U) {
        fpscr_causes |= PORPOISE_FPSCR_VXSNAN;
    }
    if ((causes &
         PORPOISE_PPC_FP_INVALID_INFINITY_MINUS_INFINITY) != 0U) {
        fpscr_causes |= PORPOISE_FPSCR_VXISI;
    }
    if ((causes &
         PORPOISE_PPC_FP_INVALID_INFINITY_TIMES_ZERO) != 0U) {
        fpscr_causes |= PORPOISE_FPSCR_VXIMZ;
    }
    return fpscr_causes;
}

static void porpoise_ppc_fp_fault_enabled_exception(
    PorpoisePpcState *state,
    uint32_t operation_causes)
{
    uint32_t enabled_causes = 0U;

    if ((operation_causes & PORPOISE_FPSCR_OX) != 0U &&
        (state->fpscr & PORPOISE_FPSCR_OE) != 0U) {
        enabled_causes |= PORPOISE_FPSCR_OX;
    }
    if ((operation_causes & PORPOISE_FPSCR_UX) != 0U &&
        (state->fpscr & PORPOISE_FPSCR_UE) != 0U) {
        enabled_causes |= PORPOISE_FPSCR_UX;
    }
    if ((operation_causes & PORPOISE_FPSCR_XX) != 0U &&
        (state->fpscr & PORPOISE_FPSCR_XE) != 0U) {
        enabled_causes |= PORPOISE_FPSCR_XX;
    }
    if (enabled_causes != 0U &&
        (state->msr & (PORPOISE_MSR_FE0 | PORPOISE_MSR_FE1)) != 0U) {
        /* Gekko treats every enabled floating-point exception as precise
         * whenever either FE bit is set.  This runtime has no guest program-
         * exception vector yet, so stop after committing the architected
         * overflow/underflow-adjusted or inexact result. */
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_FLOATING_POINT_EXCEPTION,
            state->pc,
            "paired-single exception requires a guest program-exception vector");
    }
}

static int porpoise_commit_exact_ppc_paired_result(
    PorpoisePpcState *state,
    unsigned int destination_register,
    const PorpoisePpcFp32Result results[2],
    int record)
{
    uint32_t invalid_causes =
        porpoise_ppc_fp_invalid_to_fpscr(results[0].invalid_causes) |
        porpoise_ppc_fp_invalid_to_fpscr(results[1].invalid_causes);
    uint32_t additional_causes = invalid_causes;
    uint64_t result_bits[2];
    int inexact = 0;
    int incremented = 0;
    unsigned int lane;

    if (invalid_causes != 0U) {
        porpoise_fpscr_clear_rounding(state);
        porpoise_fpscr_raise_exceptions(state, invalid_causes);
        if ((state->fpscr & PORPOISE_FPSCR_VE) != 0U) {
            if (record) {
                porpoise_fpscr_update_cr1(state);
            }
            if ((state->msr & (PORPOISE_MSR_FE0 | PORPOISE_MSR_FE1)) != 0U) {
                /* The lifted runtime cannot dispatch the guest program-
                 * exception vector. Stop precisely only when the guest's
                 * floating-exception mode requests that exception. */
                porpoise_state_set_fault(
                    state,
                    PORPOISE_FAULT_FLOATING_POINT_EXCEPTION,
                    state->pc,
                    "paired-single exception requires a guest program-exception vector");
            }
            return 1;
        }
    }

    for (lane = 0U; lane < 2U; lane++) {
        int use_adjusted_overflow =
            results[lane].overflow &&
            (state->fpscr & PORPOISE_FPSCR_OE) != 0U &&
            results[lane].adjusted_result_valid;
        int use_adjusted_underflow =
            results[lane].tiny_before_rounding &&
            (state->fpscr & PORPOISE_FPSCR_UE) != 0U &&
            results[lane].adjusted_result_valid;
        int use_adjusted =
            use_adjusted_overflow || use_adjusted_underflow;

        result_bits[lane] = use_adjusted
                                ? results[lane].adjusted_bits
                                : porpoise_binary32_to_binary64_bits(
                                      results[lane].bits);
        inexact |= use_adjusted
                       ? results[lane].adjusted_inexact
                       : results[lane].inexact;
        incremented |= use_adjusted
                           ? results[lane].adjusted_incremented
                           : results[lane].incremented;
        if (results[lane].overflow) {
            additional_causes |= PORPOISE_FPSCR_OX;
        }
        if (results[lane].underflow ||
            (results[lane].tiny_before_rounding &&
             (state->fpscr & PORPOISE_FPSCR_UE) != 0U)) {
            additional_causes |= PORPOISE_FPSCR_UX;
        }
    }

    state->fpr[destination_register].lane_bits[0] = result_bits[0];
    state->fpr[destination_register].lane_bits[1] = result_bits[1];
    porpoise_fpscr_set_rounding(
        state,
        inexact,
        incremented,
        additional_causes);
    if ((state->fpscr & PORPOISE_FPSCR_OE) != 0U &&
        results[0].overflow && results[0].adjusted_result_valid) {
        porpoise_fpscr_set_fprf(
            state,
            porpoise_fprf_from_binary64(result_bits[0]));
    } else if ((state->fpscr & PORPOISE_FPSCR_UE) != 0U &&
               results[0].tiny_before_rounding &&
               results[0].adjusted_result_valid) {
        porpoise_fpscr_set_fprf(
            state,
            porpoise_fprf_from_binary64(result_bits[0]));
    } else {
        porpoise_fpscr_set_fprf(
            state,
            porpoise_fprf_from_binary32(results[0].bits));
    }
    if (record) {
        porpoise_fpscr_update_cr1(state);
    }
    porpoise_ppc_fp_fault_enabled_exception(
        state,
        additional_causes |
            (inexact ? PORPOISE_FPSCR_XX : 0U));
    return 1;
}

static int porpoise_widened_lane_to_binary32(
    uint64_t widened_bits,
    uint32_t *binary32_bits_out)
{
    if (binary32_bits_out == NULL ||
        !porpoise_fpr_lane_is_widened_binary32(widened_bits)) {
        return 0;
    }
    *binary32_bits_out = porpoise_binary64_to_binary32_bits(widened_bits);
    return 1;
}

int porpoise_fp_binary_single_try_exact(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int left_register,
    unsigned int right_register,
    PorpoiseFpBinaryOperation operation,
    int record)
{
    PorpoiseExactBinary32Result result;

    if (!porpoise_fp_exact_controls_are_active(state, 0) ||
        destination_register >= 32U || left_register >= 32U ||
        right_register >= 32U ||
        !porpoise_evaluate_exact_scalar_single_operation(
            state->fpr[left_register].lane_bits[0],
            state->fpr[right_register].lane_bits[0],
            operation,
            &result)) {
        return 0;
    }

    state->fpr[destination_register].lane_bits[0] = result.widened_bits;
    state->fpr[destination_register].lane_bits[1] = result.widened_bits;
    porpoise_fpscr_set_rounding(
        state,
        result.inexact,
        result.incremented,
        (result.overflow ? PORPOISE_FPSCR_OX : 0U) |
            (result.underflow ? PORPOISE_FPSCR_UX : 0U));
    porpoise_fpscr_set_fprf(
        state,
        porpoise_fprf_from_binary32(result.binary32_bits));
    if (record) {
        porpoise_fpscr_update_cr1(state);
    }
    return 1;
}

int porpoise_fp_fma_execution_is_exact(
    const PorpoisePpcState *state,
    unsigned int multiplicand_register,
    unsigned int multiplier_register,
    unsigned int addend_register,
    PorpoiseFpFmaOperation operation,
    PorpoiseFpPrecision precision)
{
    PorpoiseExactBinary32Result result;

    return precision == PORPOISE_FP_PRECISION_SINGLE &&
           porpoise_fp_exact_controls_are_active(state, 0) &&
           multiplicand_register < 32U && multiplier_register < 32U &&
           addend_register < 32U &&
           porpoise_evaluate_exact_binary32_fma(
               state->fpr[multiplicand_register].lane_bits[0],
               state->fpr[multiplier_register].lane_bits[0],
               state->fpr[addend_register].lane_bits[0],
               operation,
               &result);
}

int porpoise_ps_binary_try_exact(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int left_register,
    unsigned int right_register,
    PorpoiseFpBinaryOperation operation,
    int record)
{
    PorpoiseExactBinary32Result results[2];
    uint64_t right_bits[2];
    unsigned int lane;

    if (!porpoise_fp_exact_controls_are_active(state, 1) ||
        destination_register >= 32U || left_register >= 32U ||
        right_register >= 32U) {
        return 0;
    }
    right_bits[0] = state->fpr[right_register].lane_bits[0];
    right_bits[1] = state->fpr[right_register].lane_bits[1];
    if (operation == PORPOISE_FP_BINARY_MULTIPLY) {
        right_bits[0] = porpoise_force_25_bit_multiplier(right_bits[0]);
        right_bits[1] = porpoise_force_25_bit_multiplier(right_bits[1]);
        if (right_bits[0] != state->fpr[right_register].lane_bits[0] ||
            right_bits[1] != state->fpr[right_register].lane_bits[1]) {
            return 0;
        }
    }
    for (lane = 0U; lane < 2U; lane++) {
        if (!porpoise_evaluate_exact_binary32_operation(
                state->fpr[left_register].lane_bits[lane],
                right_bits[lane],
                operation,
                &results[lane])) {
            return 0;
        }
    }
    porpoise_commit_exact_paired_result(
        state,
        destination_register,
        &results[0],
        &results[1],
        0U,
        record);
    return 1;
}

int porpoise_ps_fma_try_exact(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int multiplicand_register,
    unsigned int multiplier_register,
    unsigned int addend_register,
    PorpoiseFpFmaOperation operation,
    int scalar_lane,
    int record)
{
    PorpoiseExactBinary32Result results[2];
    unsigned int lane;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE ||
        destination_register >= 32U || multiplicand_register >= 32U ||
        multiplier_register >= 32U || addend_register >= 32U ||
        scalar_lane < -1 || scalar_lane > 1) {
        return 0;
    }

    if (operation == PORPOISE_FP_FMA_MADD) {
        PorpoisePpcFp32Result ppc_results[2];
        uint32_t multiplicands[2];
        uint32_t multipliers[2];
        uint32_t addends[2];
        unsigned int rounding_mode =
            state->fpscr & PORPOISE_FPSCR_RN_MASK;
        int non_ieee_mode =
            (state->fpscr & PORPOISE_FPSCR_NI) != 0U;

        if (!porpoise_psq_preflight(state, 0U)) {
            return 0;
        }
        for (lane = 0U; lane < 2U; lane++) {
            unsigned int multiplier_lane = scalar_lane < 0
                                               ? lane
                                               : (unsigned int)scalar_lane;
            if (!porpoise_widened_lane_to_binary32(
                    state->fpr[multiplicand_register].lane_bits[lane],
                    &multiplicands[lane]) ||
                !porpoise_widened_lane_to_binary32(
                    state->fpr[multiplier_register]
                        .lane_bits[multiplier_lane],
                    &multipliers[lane]) ||
                !porpoise_widened_lane_to_binary32(
                    state->fpr[addend_register].lane_bits[lane],
                    &addends[lane])) {
                return 0;
            }
        }
        for (lane = 0U; lane < 2U; lane++) {
            if (!porpoise_ppc_fp32_madd(
                    multiplicands[lane],
                    multipliers[lane],
                    addends[lane],
                    rounding_mode,
                    non_ieee_mode,
                    &ppc_results[lane])) {
                return 0;
            }
        }
        return porpoise_commit_exact_ppc_paired_result(
            state,
            destination_register,
            ppc_results,
            record);
    }

    if (!porpoise_fp_exact_controls_are_active(state, 1)) {
        return 0;
    }
    for (lane = 0U; lane < 2U; lane++) {
        unsigned int multiplier_lane = scalar_lane < 0
                                           ? lane
                                           : (unsigned int)scalar_lane;
        if (!porpoise_evaluate_exact_binary32_fma(
                state->fpr[multiplicand_register].lane_bits[lane],
                state->fpr[multiplier_register].lane_bits[multiplier_lane],
                state->fpr[addend_register].lane_bits[lane],
                operation,
                &results[lane])) {
            return 0;
        }
    }
    porpoise_commit_exact_paired_result(
        state,
        destination_register,
        &results[0],
        &results[1],
        0U,
        record);
    return 1;
}

int porpoise_ps_scalar_multiply_try_exact(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int multiplicand_register,
    unsigned int scalar_register,
    unsigned int scalar_lane,
    int record)
{
    PorpoisePpcFp32Result results[2];
    uint32_t multiplicands[2];
    uint32_t scalar_bits;
    unsigned int rounding_mode;
    int non_ieee_mode;
    unsigned int lane;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE ||
        destination_register >= 32U || multiplicand_register >= 32U ||
        scalar_register >= 32U || scalar_lane > 1U) {
        return 0;
    }
    if (!porpoise_psq_preflight(state, 0U) ||
        !porpoise_widened_lane_to_binary32(
            state->fpr[scalar_register].lane_bits[scalar_lane],
            &scalar_bits)) {
        return 0;
    }
    for (lane = 0U; lane < 2U; lane++) {
        if (!porpoise_widened_lane_to_binary32(
                state->fpr[multiplicand_register].lane_bits[lane],
                &multiplicands[lane])) {
            return 0;
        }
    }
    rounding_mode = state->fpscr & PORPOISE_FPSCR_RN_MASK;
    non_ieee_mode = (state->fpscr & PORPOISE_FPSCR_NI) != 0U;
    for (lane = 0U; lane < 2U; lane++) {
        if (!porpoise_ppc_fp32_mul(
                multiplicands[lane],
                scalar_bits,
                rounding_mode,
                non_ieee_mode,
                &results[lane])) {
            return 0;
        }
    }
    return porpoise_commit_exact_ppc_paired_result(
        state,
        destination_register,
        results,
        record);
}

static int porpoise_ps_defined_domain_failure(PorpoisePpcState *state)
{
    if (state != NULL && state->fault == PORPOISE_FAULT_NONE) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            state->pc,
            "paired-single arithmetic received mixed/non-binary32 FPR contents");
    }
    return 0;
}

int porpoise_ps_muls_scalar(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int multiplicand_register,
    unsigned int scalar_register,
    unsigned int scalar_lane,
    int record)
{
    if (!porpoise_ps_scalar_multiply_try_exact(
            state,
            destination_register,
            multiplicand_register,
            scalar_register,
            scalar_lane,
            record)) {
        return porpoise_ps_defined_domain_failure(state);
    }
    return state->fault == PORPOISE_FAULT_NONE;
}

int porpoise_ps_madds_scalar(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int multiplicand_register,
    unsigned int scalar_register,
    unsigned int addend_register,
    unsigned int scalar_lane,
    int record)
{
    if (scalar_lane > 1U) {
        if (state != NULL && state->fault == PORPOISE_FAULT_NONE) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_INVALID_ARGUMENT,
                state->pc,
                "paired-single scalar lane is outside 0..1");
        }
        return 0;
    }
    if (!porpoise_ps_fma_try_exact(
            state,
            destination_register,
            multiplicand_register,
            scalar_register,
            addend_register,
            PORPOISE_FP_FMA_MADD,
            (int)scalar_lane,
            record)) {
        return porpoise_ps_defined_domain_failure(state);
    }
    return state->fault == PORPOISE_FAULT_NONE;
}

int porpoise_ps_sum_try_exact(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int left_register,
    unsigned int passthrough_register,
    unsigned int right_register,
    unsigned int sum_lane,
    int record)
{
    PorpoiseExactBinary32Result sum;
    PorpoiseExactBinary32Result passthrough;
    PorpoiseExactBinary32Result results[2];
    unsigned int passthrough_lane;
    uint64_t passthrough_bits;

    if (!porpoise_fp_exact_controls_are_active(state, 1) ||
        destination_register >= 32U || left_register >= 32U ||
        passthrough_register >= 32U || right_register >= 32U ||
        sum_lane > 1U ||
        !porpoise_evaluate_exact_binary32_operation(
            state->fpr[left_register].lane_bits[0],
            state->fpr[right_register].lane_bits[1],
            PORPOISE_FP_BINARY_ADD,
            &sum)) {
        return 0;
    }
    passthrough_lane = sum_lane == 0U ? 1U : 0U;
    passthrough_bits =
        state->fpr[passthrough_register].lane_bits[passthrough_lane];
    if (!porpoise_fpr_lane_is_ordinary_binary32(passthrough_bits)) {
        return 0;
    }
    passthrough.binary32_bits = porpoise_binary64_to_binary32_bits(
        passthrough_bits);
    passthrough.widened_bits = passthrough_bits;
    passthrough.inexact = 0;
    passthrough.incremented = 0;
    passthrough.overflow = 0;
    passthrough.underflow = 0;
    results[sum_lane] = sum;
    results[passthrough_lane] = passthrough;
    porpoise_commit_exact_paired_result(
        state,
        destination_register,
        &results[0],
        &results[1],
        sum_lane,
        record);
    return 1;
}

int porpoise_psq_transfer_is_exact(
    const PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int w,
    unsigned int gqr_index,
    int store)
{
    unsigned int type;

    if (state == NULL || register_index >= 32U || w > 1U ||
        gqr_index >= 8U || (store != 0 && store != 1)) {
        return 0;
    }

    type = store != 0
               ? state->gqr[gqr_index] & 0x7U
               : (state->gqr[gqr_index] >> 16U) & 0x7U;
    if (type != PORPOISE_PSQ_TYPE_FLOAT32) {
        return 0;
    }
    if (store == 0) {
        return 1;
    }
    if (!porpoise_fpr_lane_is_widened_binary32(
            state->fpr[register_index].lane_bits[0])) {
        return 0;
    }
    return w != 0U || porpoise_fpr_lane_is_widened_binary32(
                          state->fpr[register_index].lane_bits[1]);
}

static uint16_t porpoise_read_big_endian16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8U) |
                      (uint16_t)bytes[1]);
}

static uint32_t porpoise_read_big_endian32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24U) |
           ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) |
           (uint32_t)bytes[3];
}

static void porpoise_write_big_endian16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8U);
    bytes[1] = (uint8_t)value;
}

static void porpoise_write_big_endian32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
}

static int32_t porpoise_psq_read_integer(
    const uint8_t *bytes,
    unsigned int type)
{
    if (type == PORPOISE_PSQ_TYPE_UNSIGNED8) {
        return (int32_t)bytes[0];
    }
    if (type == PORPOISE_PSQ_TYPE_SIGNED8) {
        return bytes[0] < 0x80U
                   ? (int32_t)bytes[0]
                   : (int32_t)bytes[0] - 256;
    }

    {
        uint16_t value = porpoise_read_big_endian16(bytes);

        if (type == PORPOISE_PSQ_TYPE_UNSIGNED16 || value < 0x8000U) {
            return (int32_t)value;
        }
        return (int32_t)value - 65536;
    }
}

static uint32_t porpoise_psq_scaled_integer_to_binary32(
    int32_t value,
    int scale_exponent)
{
    uint32_t magnitude;
    uint32_t significand;
    uint32_t sign = 0U;
    unsigned int highest_bit;
    int exponent;

    if (value == 0) {
        return 0U;
    }
    if (value < 0) {
        sign = PORPOISE_F32_SIGN_MASK;
        magnitude = UINT32_C(0) - (uint32_t)value;
    } else {
        magnitude = (uint32_t)value;
    }

    highest_bit = 31U - porpoise_count_leading_zeros32(magnitude);
    exponent = (int)highest_bit + scale_exponent;
    significand = magnitude << (23U - highest_bit);
    return sign |
           ((uint32_t)(exponent + 127) << 23U) |
           (significand & PORPOISE_F32_FRACTION_MASK);
}

static uint32_t porpoise_psq_truncated_magnitude(
    uint64_t bits,
    int scale_exponent,
    uint32_t limit)
{
    unsigned int exponent_field = (unsigned int)(
        (bits & PORPOISE_F64_EXPONENT_MASK) >> 52U);
    uint64_t significand;
    int binary_shift;

    if (exponent_field == 0U) {
        significand = bits & PORPOISE_F64_FRACTION_MASK;
        binary_shift = -1074 + scale_exponent;
    } else {
        significand = UINT64_C(0x0010000000000000) |
                      (bits & PORPOISE_F64_FRACTION_MASK);
        binary_shift = (int)exponent_field - 1023 - 52 +
                       scale_exponent;
    }

    if (significand == 0U) {
        return 0U;
    }
    if (binary_shift >= 0) {
        if (binary_shift >= 32 ||
            significand > ((uint64_t)limit >>
                           (unsigned int)binary_shift)) {
            return limit;
        }
        return (uint32_t)(significand <<
                          (unsigned int)binary_shift);
    }
    if (binary_shift <= -64) {
        return 0U;
    }

    significand >>= (unsigned int)(-binary_shift);
    return significand > (uint64_t)limit
               ? limit
               : (uint32_t)significand;
}

static uint16_t porpoise_psq_quantize_integer(
    uint64_t bits,
    int scale_exponent,
    unsigned int type)
{
    int is_signed = type == PORPOISE_PSQ_TYPE_SIGNED8 ||
                    type == PORPOISE_PSQ_TYPE_SIGNED16;
    unsigned int width =
        type == PORPOISE_PSQ_TYPE_UNSIGNED8 ||
                type == PORPOISE_PSQ_TYPE_SIGNED8
            ? 8U
            : 16U;
    uint32_t modulus = UINT32_C(1) << width;
    uint32_t positive_limit = is_signed
                                  ? (modulus >> 1U) - 1U
                                  : modulus - 1U;
    uint32_t negative_limit = modulus >> 1U;
    int negative = (bits & PORPOISE_F64_SIGN_MASK) != 0U;
    uint32_t magnitude;

    if (porpoise_f64_is_nan(bits)) {
        return (uint16_t)positive_limit;
    }
    if (porpoise_f64_is_infinity(bits)) {
        if (!negative) {
            return (uint16_t)positive_limit;
        }
        return is_signed ? (uint16_t)negative_limit : 0U;
    }
    if (negative && !is_signed) {
        return 0U;
    }

    magnitude = porpoise_psq_truncated_magnitude(
        bits,
        scale_exponent,
        negative ? negative_limit : positive_limit);
    if (!negative || magnitude == 0U) {
        return (uint16_t)magnitude;
    }
    return (uint16_t)((modulus - magnitude) & (modulus - 1U));
}

int porpoise_psq_load(
    PorpoisePpcState *state,
    unsigned int destination_register,
    uint32_t guest_address,
    unsigned int w,
    unsigned int gqr_index,
    unsigned int requires_lsqe)
{
    uint8_t bytes[8] = {0U};
    uint64_t result[2] = {
        UINT64_C(0),
        UINT64_C(0x3FF0000000000000),
    };
    uint32_t gqr;
    unsigned int type;
    unsigned int raw_scale;
    size_t element_size;
    size_t element_count;
    size_t index;

    if (!porpoise_validate_psq_arguments(
            state,
            destination_register,
            w,
            gqr_index,
            requires_lsqe) ||
        !porpoise_psq_preflight(state, requires_lsqe)) {
        return 0;
    }

    gqr = state->gqr[gqr_index];
    type = (gqr >> 16U) & 0x7U;
    raw_scale = (gqr >> 24U) & 0x3FU;
    if (!porpoise_psq_element_size(
            state,
            type,
            0,
            &element_size)) {
        return 0;
    }
    element_count = w != 0U ? 1U : 2U;
    if (!porpoise_read_bytes(
            state,
            guest_address,
            bytes,
            element_size * element_count)) {
        return 0;
    }

    for (index = 0U; index < element_count; index++) {
        const uint8_t *element = &bytes[index * element_size];
        uint32_t binary32_bits;

        if (type == PORPOISE_PSQ_TYPE_FLOAT32) {
            binary32_bits = porpoise_read_big_endian32(element);
        } else {
            int32_t integer_value = porpoise_psq_read_integer(
                element,
                type);
            binary32_bits = porpoise_psq_scaled_integer_to_binary32(
                integer_value,
                porpoise_psq_load_scale(raw_scale));
        }
        result[index] = porpoise_binary32_to_binary64_bits(
            binary32_bits);
    }

    state->fpr[destination_register].lane_bits[0] = result[0];
    state->fpr[destination_register].lane_bits[1] = result[1];
    return 1;
}

int porpoise_psq_store(
    PorpoisePpcState *state,
    unsigned int source_register,
    uint32_t guest_address,
    unsigned int w,
    unsigned int gqr_index,
    unsigned int requires_lsqe)
{
    uint8_t bytes[8] = {0U};
    uint32_t gqr;
    unsigned int type;
    unsigned int raw_scale;
    size_t element_size;
    size_t element_count;
    size_t index;

    if (!porpoise_validate_psq_arguments(
            state,
            source_register,
            w,
            gqr_index,
            requires_lsqe) ||
        !porpoise_psq_preflight(state, requires_lsqe)) {
        return 0;
    }

    gqr = state->gqr[gqr_index];
    type = gqr & 0x7U;
    raw_scale = (gqr >> 8U) & 0x3FU;
    if (!porpoise_psq_element_size(
            state,
            type,
            1,
            &element_size)) {
        return 0;
    }
    element_count = w != 0U ? 1U : 2U;

    for (index = 0U; index < element_count; index++) {
        uint8_t *element = &bytes[index * element_size];
        uint64_t source_bits =
            state->fpr[source_register].lane_bits[index];

        if (type == PORPOISE_PSQ_TYPE_FLOAT32) {
            porpoise_write_big_endian32(
                element,
                porpoise_binary64_to_binary32_bits(source_bits));
        } else {
            uint16_t quantized = porpoise_psq_quantize_integer(
                source_bits,
                porpoise_psq_store_scale(raw_scale),
                type);

            if (element_size == 1U) {
                element[0] = (uint8_t)quantized;
            } else {
                porpoise_write_big_endian16(element, quantized);
            }
        }
    }

    return porpoise_write_bytes(
        state,
        guest_address,
        bytes,
        element_size * element_count);
}

int porpoise_load_multiple_words(
    PorpoisePpcState *state,
    uint32_t guest_address,
    unsigned int first_register)
{
    uint8_t bytes[32U * 4U];
    size_t register_count;
    size_t index;

    if (state == NULL) {
        return 0;
    }
    if (first_register >= 32U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "first multiple-load register is outside 0..31");
        return 0;
    }
    register_count = 32U - first_register;
    if (!porpoise_read_bytes(
            state,
            guest_address,
            bytes,
            register_count * 4U)) {
        return 0;
    }
    for (index = 0U; index < register_count; index++) {
        const uint8_t *word = &bytes[index * 4U];
        state->gpr[first_register + index] =
            ((uint32_t)word[0] << 24U) |
            ((uint32_t)word[1] << 16U) |
            ((uint32_t)word[2] << 8U) |
            (uint32_t)word[3];
    }
    return 1;
}

int porpoise_store_multiple_words(
    PorpoisePpcState *state,
    uint32_t guest_address,
    unsigned int first_register)
{
    uint8_t bytes[32U * 4U] = {0U};
    size_t register_count;
    size_t index;

    if (state == NULL) {
        return 0;
    }
    if (first_register >= 32U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "first multiple-store register is outside 0..31");
        return 0;
    }
    register_count = 32U - first_register;
    for (index = 0U; index < register_count; index++) {
        uint32_t value = state->gpr[first_register + index];
        uint8_t *word = &bytes[index * 4U];
        word[0] = (uint8_t)(value >> 24U);
        word[1] = (uint8_t)(value >> 16U);
        word[2] = (uint8_t)(value >> 8U);
        word[3] = (uint8_t)value;
    }
    return porpoise_write_bytes(
        state,
        guest_address,
        bytes,
        register_count * 4U);
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

void porpoise_store_gx_fifo_u8(
    PorpoisePpcState *state,
    uint8_t value)
{
    const uint32_t guest_address = UINT32_C(0xCC008000);
    PorpoiseHostResult result;

    if (!porpoise_validate_span(state, guest_address, sizeof(value))) {
        return;
    }
    if (state->host->write_gx_fifo_u8 == NULL) {
        porpoise_store_u8(state, guest_address, value);
        return;
    }
    result = state->host->write_gx_fifo_u8(state->host->context, value);
    if (result != PORPOISE_HOST_OK) {
        porpoise_state_set_fault(
            state,
            porpoise_fault_from_host_result(result),
            guest_address,
            porpoise_host_result_string(result));
    }
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

int porpoise_fpr_load_binary32(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index,
    uint32_t guest_address)
{
    uint32_t binary32_bits;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE ||
        !porpoise_validate_fpr_lane(state, register_index, lane_index)) {
        return 0;
    }
    binary32_bits = porpoise_load_u32(state, guest_address);
    if (state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    state->fpr[register_index].lane_bits[lane_index] =
        porpoise_binary32_to_binary64_bits(binary32_bits);
    return 1;
}

int porpoise_fpr_store_binary32(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index,
    uint32_t guest_address)
{
    uint32_t binary32_bits;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE ||
        !porpoise_validate_fpr_lane(state, register_index, lane_index)) {
        return 0;
    }
    binary32_bits = porpoise_binary64_to_binary32_bits(
        state->fpr[register_index].lane_bits[lane_index]);
    porpoise_store_u32(state, guest_address, binary32_bits);
    return state->fault == PORPOISE_FAULT_NONE;
}

int porpoise_fpr_load_binary64(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index,
    uint32_t guest_address)
{
    uint64_t binary64_bits;

    if (state == NULL || state->fault != PORPOISE_FAULT_NONE ||
        !porpoise_validate_fpr_lane(state, register_index, lane_index)) {
        return 0;
    }
    binary64_bits = porpoise_load_u64(state, guest_address);
    if (state->fault != PORPOISE_FAULT_NONE) {
        return 0;
    }
    state->fpr[register_index].lane_bits[lane_index] = binary64_bits;
    return 1;
}

int porpoise_fpr_store_binary64(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index,
    uint32_t guest_address)
{
    if (state == NULL || state->fault != PORPOISE_FAULT_NONE ||
        !porpoise_validate_fpr_lane(state, register_index, lane_index)) {
        return 0;
    }
    porpoise_store_u64(
        state,
        guest_address,
        state->fpr[register_index].lane_bits[lane_index]);
    return state->fault == PORPOISE_FAULT_NONE;
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
