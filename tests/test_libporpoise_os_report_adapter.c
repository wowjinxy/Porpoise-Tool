#include "porpoise_libporpoise_builtins_private.h"

#include <dolphin/os.h>

#include <stdarg.h>
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
#define TEST_STACK (TEST_MEMORY_BASE + UINT32_C(0x00001000))
#define TEST_FORMAT (TEST_MEMORY_BASE + UINT32_C(0x00002000))
#define TEST_STRING (TEST_MEMORY_BASE + UINT32_C(0x00004000))

enum {
    TEST_MEMORY_SIZE = 0x20000,
    TEST_CAPTURE_CAPACITY = 20000,
    TEST_REPORT_FORMAT_LIMIT = 4096,
    TEST_REPORT_STRING_LIMIT = 4096
};

typedef struct TestMemory {
    uint8_t bytes[TEST_MEMORY_SIZE];
} TestMemory;

static char captured_report[TEST_CAPTURE_CAPACITY];
static unsigned int captured_report_count;

void OSReport(const char *format, ...)
{
    va_list arguments;
    const char *message;
    int result;

    CHECK(format != NULL);
    CHECK(strcmp(format, "%s") == 0);
    va_start(arguments, format);
    message = va_arg(arguments, const char *);
    va_end(arguments);
    CHECK(message != NULL);
    result = snprintf(
        captured_report, sizeof(captured_report), "%s", message);
    CHECK(result >= 0);
    CHECK((size_t)result < sizeof(captured_report));
    captured_report_count++;
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
    if (!test_translate(guest_address, size, &offset)) {
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    }
    memcpy(destination, memory->bytes + offset, size);
    return PORPOISE_HOST_OK;
}

static void test_write_bytes(
    TestMemory *memory,
    uint32_t guest_address,
    const void *source,
    size_t size)
{
    size_t offset;

    CHECK(memory != NULL);
    CHECK(source != NULL);
    CHECK(test_translate(guest_address, size, &offset));
    memcpy(memory->bytes + offset, source, size);
}

static void test_write_string(
    TestMemory *memory,
    uint32_t guest_address,
    const char *value)
{
    test_write_bytes(memory, guest_address, value, strlen(value) + 1U);
}

static void test_write_be32(
    TestMemory *memory,
    uint32_t guest_address,
    uint32_t value)
{
    uint8_t bytes[4];

    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
    test_write_bytes(memory, guest_address, bytes, sizeof(bytes));
}

static void test_write_be64(
    TestMemory *memory,
    uint32_t guest_address,
    uint64_t value)
{
    uint8_t bytes[8];
    unsigned int index;

    for (index = 0U; index < 8U; index++) {
        bytes[index] = (uint8_t)(value >> (56U - index * 8U));
    }
    test_write_bytes(memory, guest_address, bytes, sizeof(bytes));
}

static void test_write_f64(
    TestMemory *memory,
    uint32_t guest_address,
    double value)
{
    uint64_t bits;

    memcpy(&bits, &value, sizeof(bits));
    test_write_be64(memory, guest_address, bits);
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
    porpoise_state_init(state, host);
    state->status = PORPOISE_EXECUTION_RUNNING;
    state->pc = UINT32_C(0x803D4CE8);
    state->gpr[1] = TEST_STACK;
    state->gpr[3] = TEST_FORMAT;
    captured_report[0] = '\0';
    captured_report_count = 0U;
}

static void test_basic_scalars_and_gpr_overflow(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    test_initialize(&memory, &host, &state);
    test_write_string(
        &memory,
        TEST_FORMAT,
        "literal %% %c %s %p %d %u %x %X %o");
    test_write_string(&memory, TEST_STRING, "guest");
    state.gpr[4] = (uint32_t)'A';
    state.gpr[5] = TEST_STRING;
    state.gpr[6] = UINT32_C(0x8123ABCD);
    state.gpr[7] = UINT32_C(0xFFFFFFD6);
    state.gpr[8] = 42U;
    state.gpr[9] = UINT32_C(0xBEEF);
    state.gpr[10] = UINT32_C(0xCAFE);
    test_write_be32(&memory, TEST_STACK + 8U, 493U);

    porpoise_libporpoise_os_report_adapter(&state);

    CHECK(!porpoise_state_has_fault(&state));
    CHECK(captured_report_count == 1U);
    CHECK(strcmp(
              captured_report,
              "literal % A guest 0x8123abcd -42 42 beef CAFE 755") == 0);
}

static void test_integer_widths_and_eabi_alignment(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    test_initialize(&memory, &host, &state);
    test_write_string(
        &memory,
        TEST_FORMAT,
        "%lld|%u|%u|%u|%u|%u|%u|%u|%llX|%u");
    state.gpr[4] = UINT32_C(0xDEADBEEF);
    state.gpr[5] = UINT32_MAX;
    state.gpr[6] = UINT32_C(0xFFFFFFFE);
    state.gpr[7] = 7U;
    state.gpr[8] = 8U;
    state.gpr[9] = 9U;
    state.gpr[10] = 10U;
    test_write_be32(&memory, TEST_STACK + 8U, 11U);
    test_write_be32(&memory, TEST_STACK + 12U, 12U);
    test_write_be32(&memory, TEST_STACK + 16U, 13U);
    test_write_be32(&memory, TEST_STACK + 20U, UINT32_C(0xBAD0BAD0));
    test_write_be64(
        &memory,
        TEST_STACK + 24U,
        UINT64_C(0x1122334455667788));
    test_write_be32(&memory, TEST_STACK + 32U, 14U);

    porpoise_libporpoise_os_report_adapter(&state);

    CHECK(!porpoise_state_has_fault(&state));
    CHECK(strcmp(
              captured_report,
              "-2|7|8|9|10|11|12|13|1122334455667788|14") == 0);

    test_initialize(&memory, &host, &state);
    test_write_string(
        &memory,
        TEST_FORMAT,
        "%hhd|%hd|%li|%jd|%zd|%td|%llu");
    state.gpr[4] = UINT32_C(0xFFFFFFFF);
    state.gpr[5] = UINT32_C(0xFFFF8000);
    state.gpr[6] = UINT32_C(0xFFFFFFFE);
    state.gpr[7] = UINT32_MAX;
    state.gpr[8] = UINT32_C(0xFFFFFFFD);
    state.gpr[9] = UINT32_C(0xFFFFFFFC);
    state.gpr[10] = UINT32_C(0xFFFFFFFB);
    test_write_be64(&memory, TEST_STACK + 8U, UINT64_MAX);

    porpoise_libporpoise_os_report_adapter(&state);

    CHECK(!porpoise_state_has_fault(&state));
    CHECK(strcmp(captured_report, "-1|-32768|-2|-3|-4|-5|18446744073709551615") == 0);
}

static void test_gpr64_skips_unpaired_r10_and_spills(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    test_initialize(&memory, &host, &state);
    test_write_string(
        &memory,
        TEST_FORMAT,
        "%u|%u|%u|%u|%u|%u|%llx|%u");
    state.gpr[4] = 4U;
    state.gpr[5] = 5U;
    state.gpr[6] = 6U;
    state.gpr[7] = 7U;
    state.gpr[8] = 8U;
    state.gpr[9] = 9U;
    state.gpr[10] = UINT32_C(0xDEADBEEF);
    test_write_be64(
        &memory,
        TEST_STACK + 8U,
        UINT64_C(0x1122334455667788));
    test_write_be32(&memory, TEST_STACK + 16U, 10U);

    porpoise_libporpoise_os_report_adapter(&state);

    CHECK(!porpoise_state_has_fault(&state));
    CHECK(strcmp(
              captured_report,
              "4|5|6|7|8|9|1122334455667788|10") == 0);
}

static void test_mixed_gpr_fpr_spills_share_stack_cursor(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    unsigned int index;

    test_initialize(&memory, &host, &state);
    test_write_string(
        &memory,
        TEST_FORMAT,
        "%u|%u|%u|%u|%u|%u|%u|"
        "%g|%g|%g|%g|%g|%g|%g|%g|"
        "%u|%g|%u|%g");
    for (index = 0U; index < 7U; index++) {
        state.gpr[4U + index] = 10U + index;
    }
    for (index = 1U; index <= 8U; index++) {
        porpoise_fpr_set_f64(&state, index, 0U, (double)index);
    }
    test_write_be32(&memory, TEST_STACK + 8U, 21U);
    test_write_f64(&memory, TEST_STACK + 16U, 9.5);
    test_write_be32(&memory, TEST_STACK + 24U, 22U);
    test_write_f64(&memory, TEST_STACK + 32U, 10.5);
    porpoise_cr_set_bit(&state, 6U, 1);

    porpoise_libporpoise_os_report_adapter(&state);

    CHECK(!porpoise_state_has_fault(&state));
    CHECK(strcmp(
              captured_report,
              "10|11|12|13|14|15|16|"
              "1|2|3|4|5|6|7|8|21|9.5|22|10.5") == 0);
}

static void test_dynamic_fields_and_independent_fpr_cursor(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    test_initialize(&memory, &host, &state);
    test_write_string(
        &memory,
        TEST_FORMAT,
        "%*.*f|%u|%hhd|%hu");
    state.gpr[4] = 8U;
    state.gpr[5] = 2U;
    state.gpr[6] = 77U;
    state.gpr[7] = UINT32_C(0xFE);
    state.gpr[8] = UINT32_C(0x12345);
    porpoise_fpr_set_f64(&state, 1U, 0U, 1.25);
    porpoise_cr_set_bit(&state, 6U, 1);

    porpoise_libporpoise_os_report_adapter(&state);

    CHECK(!porpoise_state_has_fault(&state));
    CHECK(strcmp(captured_report, "    1.25|77|-2|9029") == 0);
}

static void test_float_families_and_fpr_overflow(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    unsigned int index;
    unsigned int separator_count = 0U;
    size_t output_index;

    test_initialize(&memory, &host, &state);
    test_write_string(
        &memory,
        TEST_FORMAT,
        "%g|%G|%f|%F|%e|%E|%a|%A|%g");
    for (index = 1U; index <= 8U; index++) {
        porpoise_fpr_set_f64(&state, index, 0U, (double)index);
    }
    test_write_f64(&memory, TEST_STACK + 8U, 9.0);
    porpoise_cr_set_bit(&state, 6U, 1);

    porpoise_libporpoise_os_report_adapter(&state);

    CHECK(!porpoise_state_has_fault(&state));
    CHECK(captured_report_count == 1U);
    for (output_index = 0U; captured_report[output_index] != '\0'; output_index++) {
        if (captured_report[output_index] == '|') separator_count++;
    }
    CHECK(separator_count == 8U);
    CHECK(strstr(captured_report, "3.000000") != NULL);
    CHECK(strstr(captured_report, "4.000000") != NULL);
    CHECK(strstr(captured_report, "e+") != NULL);
    CHECK(strstr(captured_report, "E+") != NULL);
    CHECK(strstr(captured_report, "0x") != NULL);
    CHECK(strstr(captured_report, "0X") != NULL);
    CHECK(output_index >= 2U);
    CHECK(strcmp(captured_report + output_index - 2U, "|9") == 0);
}

static void test_floating_arguments_require_cr6(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    test_initialize(&memory, &host, &state);
    test_write_string(&memory, TEST_FORMAT, "%f");
    porpoise_fpr_set_f64(&state, 1U, 0U, 1.0);

    porpoise_libporpoise_os_report_adapter(&state);

    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(strstr(porpoise_state_fault_message(&state), "CR bit 6") != NULL);
    CHECK(captured_report_count == 0U);
}

static void test_bounded_guest_strings(void)
{
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    uint32_t end_string =
        TEST_MEMORY_BASE + TEST_MEMORY_SIZE - UINT32_C(3);

    test_initialize(&memory, &host, &state);
    memset(
        memory.bytes + (TEST_FORMAT - TEST_MEMORY_BASE),
        'A',
        TEST_REPORT_FORMAT_LIMIT);
    porpoise_libporpoise_os_report_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(strstr(porpoise_state_fault_message(&state), "format") != NULL);
    CHECK(captured_report_count == 0U);

    test_initialize(&memory, &host, &state);
    test_write_string(&memory, TEST_FORMAT, "%.3s");
    test_write_bytes(&memory, end_string, "abc", 3U);
    state.gpr[4] = end_string;
    porpoise_libporpoise_os_report_adapter(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(strcmp(captured_report, "abc") == 0);

    test_initialize(&memory, &host, &state);
    test_write_string(&memory, TEST_FORMAT, "%s");
    memset(
        memory.bytes + (TEST_STRING - TEST_MEMORY_BASE),
        'B',
        TEST_REPORT_STRING_LIMIT);
    state.gpr[4] = TEST_STRING;
    porpoise_libporpoise_os_report_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(strstr(porpoise_state_fault_message(&state), "%s") != NULL);
    CHECK(captured_report_count == 0U);

    test_initialize(&memory, &host, &state);
    test_write_string(&memory, TEST_FORMAT, "%s");
    state.gpr[4] = 0U;
    porpoise_libporpoise_os_report_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_POINTER);
    CHECK(captured_report_count == 0U);

    test_initialize(&memory, &host, &state);
    test_write_string(&memory, TEST_FORMAT, "%.0s");
    state.gpr[4] = 0U;
    porpoise_libporpoise_os_report_adapter(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(strcmp(captured_report, "") == 0);
}

static void test_unsupported_formats_fault_stickily(void)
{
    static const char *const unsupported[] = {
        "%n",
        "%1$d",
        "%ls",
        "%lc",
        "%Lf",
        "%q"
    };
    TestMemory memory;
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    size_t index;

    for (index = 0U;
         index < sizeof(unsupported) / sizeof(unsupported[0]);
         index++) {
        test_initialize(&memory, &host, &state);
        test_write_string(&memory, TEST_FORMAT, unsupported[index]);
        state.gpr[4] = TEST_STRING;
        porpoise_fpr_set_f64(&state, 1U, 0U, 1.0);
        porpoise_cr_set_bit(&state, 6U, 1);
        porpoise_libporpoise_os_report_adapter(&state);
        CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
        CHECK(state.status == PORPOISE_EXECUTION_FAULTED);
        CHECK(porpoise_state_fault_message(&state)[0] != '\0');
        CHECK(captured_report_count == 0U);
    }

    test_initialize(&memory, &host, &state);
    test_write_string(&memory, TEST_FORMAT, "%n");
    porpoise_libporpoise_os_report_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
    {
        char first_fault[PORPOISE_FAULT_MESSAGE_CAPACITY];

        (void)snprintf(
            first_fault,
            sizeof(first_fault),
            "%s",
            porpoise_state_fault_message(&state));
        test_write_string(&memory, TEST_FORMAT, "must not print");
        porpoise_libporpoise_os_report_adapter(&state);
        CHECK(strcmp(first_fault, porpoise_state_fault_message(&state)) == 0);
        CHECK(captured_report_count == 0U);
    }

    test_initialize(&memory, &host, &state);
    test_write_string(&memory, TEST_FORMAT, "%1025d");
    porpoise_libporpoise_os_report_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(captured_report_count == 0U);

    test_initialize(&memory, &host, &state);
    test_write_string(&memory, TEST_FORMAT, "%*d");
    state.gpr[4] = 1025U;
    porpoise_libporpoise_os_report_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(captured_report_count == 0U);

    test_initialize(&memory, &host, &state);
    test_write_string(&memory, TEST_FORMAT, "A%cB");
    state.gpr[4] = 0U;
    porpoise_libporpoise_os_report_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
    CHECK(strstr(porpoise_state_fault_message(&state), "NUL %c") != NULL);
    CHECK(captured_report_count == 0U);
}

int main(void)
{
    test_basic_scalars_and_gpr_overflow();
    test_integer_widths_and_eabi_alignment();
    test_gpr64_skips_unpaired_r10_and_spills();
    test_mixed_gpr_fpr_spills_share_stack_cursor();
    test_dynamic_fields_and_independent_fpr_cursor();
    test_float_families_and_fpr_overflow();
    test_floating_arguments_require_cr6();
    test_bounded_guest_strings();
    test_unsupported_formats_fault_stickily();
    return 0;
}
