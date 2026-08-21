#include "porpoise_libporpoise_builtins_private.h"

#include <porpoise/stub.h>

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

#define VALID_GUEST_BASE UINT32_C(0x80010000)
#define SECOND_GUEST_BASE UINT32_C(0x80020000)
#define VALID_FIFO_SIZE UINT32_C(0x00010000)
#define TEST_PC UINT32_C(0x803C879C)

static void prepare_call(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host,
    uint32_t guest_base,
    uint32_t size)
{
    porpoise_state_init(state, host);
    state->pc = TEST_PC;
    state->gpr[3] = guest_base;
    state->gpr[4] = size;
}

static void check_failure(
    const PorpoisePpcState *state,
    PorpoiseFault fault,
    uint32_t fault_address,
    uint32_t original_r3)
{
    CHECK(state->status == PORPOISE_EXECUTION_FAULTED);
    CHECK(state->fault == fault);
    CHECK(state->fault_address == fault_address);
    CHECK(state->gpr[3] == original_r3);
}

static void check_pre_call_validation(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state,
    uint32_t guest_base,
    uint32_t size,
    PorpoiseFault fault,
    uint32_t fault_address)
{
    prepare_call(state, host, guest_base, size);
    porpoise_libporpoise_gx_init_adapter(state);
    check_failure(state, fault, fault_address, guest_base);
    CHECK(PorpoiseStubGXInitCallCount() == 0U);
}

static void init_host(PorpoiseHostAdapter *host)
{
    memset(host, 0, sizeof(*host));
    CHECK(porpoise_libporpoise_adapter_init(host) == PORPOISE_HOST_OK);
}

static void test_active_context_and_validation(void)
{
    PorpoiseHostAdapter host;
    PorpoiseHostAdapter fake_host;
    PorpoisePpcState state;
    void *expected_base;
    void *decoded_fifo;
    uint32_t token;
    unsigned int releases_before;

    PorpoiseStubGXInitReset();
    porpoise_libporpoise_gx_init_adapter(NULL);
    CHECK(PorpoiseStubGXInitCallCount() == 0U);

    prepare_call(&state, NULL, VALID_GUEST_BASE, VALID_FIFO_SIZE);
    porpoise_libporpoise_gx_init_adapter(&state);
    check_failure(
        &state,
        PORPOISE_FAULT_NO_HOST_ADAPTER,
        TEST_PC,
        VALID_GUEST_BASE);
    CHECK(PorpoiseStubGXInitCallCount() == 0U);

    memset(&fake_host, 0, sizeof(fake_host));
    prepare_call(&state, &fake_host, VALID_GUEST_BASE, VALID_FIFO_SIZE);
    porpoise_libporpoise_gx_init_adapter(&state);
    check_failure(
        &state,
        PORPOISE_FAULT_INVALID_STATE,
        TEST_PC,
        VALID_GUEST_BASE);
    CHECK(PorpoiseStubGXInitCallCount() == 0U);

    prepare_call(&state, NULL, VALID_GUEST_BASE, VALID_FIFO_SIZE);
    state.status = PORPOISE_EXECUTION_RETURNED;
    porpoise_libporpoise_gx_init_adapter(&state);
    CHECK(state.status == PORPOISE_EXECUTION_RETURNED);
    CHECK(state.gpr[3] == VALID_GUEST_BASE);
    CHECK(PorpoiseStubGXInitCallCount() == 0U);

    init_host(&host);
    check_pre_call_validation(
        &host,
        &state,
        0U,
        VALID_FIFO_SIZE,
        PORPOISE_FAULT_INVALID_POINTER,
        0U);
    check_pre_call_validation(
        &host,
        &state,
        VALID_GUEST_BASE + UINT32_C(4),
        VALID_FIFO_SIZE,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        VALID_GUEST_BASE + UINT32_C(4));
    check_pre_call_validation(
        &host,
        &state,
        VALID_GUEST_BASE,
        UINT32_C(0x0000FFE0),
        PORPOISE_FAULT_INVALID_ARGUMENT,
        UINT32_C(0x0000FFE0));
    check_pre_call_validation(
        &host,
        &state,
        VALID_GUEST_BASE,
        VALID_FIFO_SIZE + UINT32_C(1),
        PORPOISE_FAULT_INVALID_ARGUMENT,
        VALID_FIFO_SIZE + UINT32_C(1));
    check_pre_call_validation(
        &host,
        &state,
        UINT32_C(0xFFFF8000),
        VALID_FIFO_SIZE,
        PORPOISE_FAULT_ADDRESS_OVERFLOW,
        UINT32_C(0xFFFF8000));
    check_pre_call_validation(
        &host,
        &state,
        UINT32_C(0xCC000000),
        VALID_FIFO_SIZE,
        PORPOISE_FAULT_UNSUPPORTED_MMIO,
        UINT32_C(0xCC000000));
    check_pre_call_validation(
        &host,
        &state,
        UINT32_C(0xB0000000),
        VALID_FIFO_SIZE,
        PORPOISE_FAULT_INVALID_POINTER,
        UINT32_C(0xB0000000));
    check_pre_call_validation(
        &host,
        &state,
        UINT32_C(0x817F8000),
        VALID_FIFO_SIZE,
        PORPOISE_FAULT_UNMAPPED_ADDRESS,
        UINT32_C(0x817F8000));

    PorpoiseStubSetDecodeBias(1U);
    check_pre_call_validation(
        &host,
        &state,
        VALID_GUEST_BASE,
        VALID_FIFO_SIZE,
        PORPOISE_FAULT_INVALID_POINTER,
        VALID_GUEST_BASE);
    PorpoiseStubSetDecodeBias(0U);

    expected_base = NULL;
    CHECK(host.decode_pointer(
              host.context, VALID_GUEST_BASE, &expected_base) ==
          PORPOISE_HOST_OK);
    CHECK(expected_base != NULL);
    prepare_call(&state, &host, VALID_GUEST_BASE, VALID_FIFO_SIZE);
    porpoise_libporpoise_gx_init_adapter(&state);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubGXInitCallCount() == 1U);
    CHECK(PorpoiseStubGXInitLastBase() == expected_base);
    CHECK(PorpoiseStubGXInitLastSize() == VALID_FIFO_SIZE);
    token = state.gpr[3];
    CHECK(token == PorpoiseStubTokenAddress());
    CHECK(token != VALID_GUEST_BASE);
    decoded_fifo = NULL;
    CHECK(host.decode_pointer(host.context, token, &decoded_fifo) ==
          PORPOISE_HOST_OK);
    CHECK(decoded_fifo == PorpoiseStubNativePointer());

    prepare_call(&state, &host, SECOND_GUEST_BASE, VALID_FIFO_SIZE);
    porpoise_libporpoise_gx_init_adapter(&state);
    check_failure(
        &state,
        PORPOISE_FAULT_INVALID_STATE,
        SECOND_GUEST_BASE,
        SECOND_GUEST_BASE);
    CHECK(PorpoiseStubGXInitCallCount() == 1U);

    releases_before = PorpoiseStubTokenReleaseCount();
    porpoise_libporpoise_adapter_shutdown(&host);
    CHECK(PorpoiseStubTokenReleaseCount() == releases_before + 1U);

    /* Native GX owns process-global state and exposes no teardown. A fresh
     * adapter must not turn shutdown into permission for a second GXInit. */
    init_host(&host);
    prepare_call(&state, &host, VALID_GUEST_BASE, VALID_FIFO_SIZE);
    porpoise_libporpoise_gx_init_adapter(&state);
    check_failure(
        &state,
        PORPOISE_FAULT_INVALID_STATE,
        VALID_GUEST_BASE,
        VALID_GUEST_BASE);
    CHECK(PorpoiseStubGXInitCallCount() == 1U);
    porpoise_libporpoise_adapter_shutdown(&host);
}

static void test_poisoned_native_failure(
    int result_kind,
    PorpoiseFault expected_fault)
{
    PorpoiseHostAdapter host;
    PorpoisePpcState state;

    PorpoiseStubGXInitReset();
    PorpoiseStubGXInitSetResult(result_kind);
    init_host(&host);

    prepare_call(&state, &host, VALID_GUEST_BASE, VALID_FIFO_SIZE);
    porpoise_libporpoise_gx_init_adapter(&state);
    check_failure(
        &state,
        expected_fault,
        VALID_GUEST_BASE,
        VALID_GUEST_BASE);
    CHECK(PorpoiseStubGXInitCallCount() == 1U);

    prepare_call(&state, &host, SECOND_GUEST_BASE, VALID_FIFO_SIZE);
    porpoise_libporpoise_gx_init_adapter(&state);
    check_failure(
        &state,
        PORPOISE_FAULT_INVALID_STATE,
        SECOND_GUEST_BASE,
        SECOND_GUEST_BASE);
    CHECK(PorpoiseStubGXInitCallCount() == 1U);
    porpoise_libporpoise_adapter_shutdown(&host);
}

int main(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "success") == 0)) {
        test_active_context_and_validation();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "null") == 0) {
        test_poisoned_native_failure(
            PORPOISE_STUB_GX_INIT_NULL_RESULT,
            PORPOISE_FAULT_HOST_IO);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "unencodable") == 0) {
        test_poisoned_native_failure(
            PORPOISE_STUB_GX_INIT_UNENCODABLE_RESULT,
            PORPOISE_FAULT_INVALID_POINTER);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "mapped") == 0) {
        test_poisoned_native_failure(
            PORPOISE_STUB_GX_INIT_MAPPED_RESULT,
            PORPOISE_FAULT_INVALID_POINTER);
        return 0;
    }
    (void)fprintf(stderr, "unknown GXInit test mode\n");
    return 2;
}
