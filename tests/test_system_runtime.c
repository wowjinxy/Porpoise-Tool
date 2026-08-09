#include "porpoise_lifted.h"

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

typedef struct TestHost {
    uint64_t ticks;
    PorpoiseHostResult time_result;
    PorpoiseHostResult write_result;
    PorpoiseHostResult trap_result;
    PorpoiseHostResult system_call_result;
    size_t time_calls;
    size_t write_calls;
    size_t trap_calls;
    size_t system_call_calls;
    uint32_t write_address;
    size_t write_size;
    uint8_t write_bytes[32];
    PorpoisePpcState *event_state;
    uint32_t event_address;
    uint32_t trap_options;
    uint32_t trap_left;
    uint32_t trap_right;
    int set_event_status;
    PorpoiseExecutionStatus event_status;
    PorpoiseFault event_fault;
} TestHost;

static PorpoiseHostResult test_read_time_base(
    void *context,
    uint64_t *ticks_out)
{
    TestHost *host = (TestHost *)context;

    host->time_calls++;
    if (host->time_result != PORPOISE_HOST_OK) {
        return host->time_result;
    }
    if (ticks_out == NULL) {
        return PORPOISE_HOST_INVALID_POINTER;
    }
    *ticks_out = host->ticks;
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult test_write_bytes(
    void *context,
    uint32_t guest_address,
    const void *source,
    size_t size)
{
    TestHost *host = (TestHost *)context;

    host->write_calls++;
    host->write_address = guest_address;
    host->write_size = size;
    if (host->write_result != PORPOISE_HOST_OK) {
        return host->write_result;
    }
    if (source == NULL || size > sizeof(host->write_bytes)) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    memcpy(host->write_bytes, source, size);
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult test_trap(
    void *context,
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t trap_options,
    uint32_t left,
    uint32_t right)
{
    TestHost *host = (TestHost *)context;

    host->trap_calls++;
    host->event_state = state;
    host->event_address = instruction_address;
    host->trap_options = trap_options;
    host->trap_left = left;
    host->trap_right = right;
    if (host->set_event_status != 0) {
        state->status = host->event_status;
    }
    if (host->event_fault != PORPOISE_FAULT_NONE) {
        porpoise_state_set_fault(
            state,
            host->event_fault,
            instruction_address,
            "host callback fault");
    }
    return host->trap_result;
}

static PorpoiseHostResult test_system_call(
    void *context,
    PorpoisePpcState *state,
    uint32_t instruction_address)
{
    TestHost *host = (TestHost *)context;

    host->system_call_calls++;
    host->event_state = state;
    host->event_address = instruction_address;
    if (host->set_event_status != 0) {
        state->status = host->event_status;
    }
    if (host->event_fault != PORPOISE_FAULT_NONE) {
        porpoise_state_set_fault(
            state,
            host->event_fault,
            instruction_address,
            "host callback fault");
    }
    return host->system_call_result;
}

static PorpoiseHostAdapter test_adapter(TestHost *host)
{
    PorpoiseHostAdapter adapter;

    memset(&adapter, 0, sizeof(adapter));
    adapter.context = host;
    adapter.write_bytes = test_write_bytes;
    adapter.read_time_base = test_read_time_base;
    adapter.trap = test_trap;
    adapter.system_call = test_system_call;
    return adapter;
}

static void test_state_and_explicit_faults(void)
{
    TestHost host;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;
    uint32_t original_msr;

    memset(&host, 0, sizeof(host));
    adapter = test_adapter(&host);
    porpoise_state_init(&state, &adapter);

    CHECK(state.msr == 0U);
    CHECK(state.pvr == 0U);
    CHECK(state.sprg[3] == 0U);
    CHECK(state.segment_register[15] == 0U);
    CHECK(state.ibat_upper[7] == 0U);
    CHECK(state.ibat_lower[7] == 0U);
    CHECK(state.dbat_upper[7] == 0U);
    CHECK(state.dbat_lower[7] == 0U);
    CHECK(state.hid0 == 0U);
    CHECK(state.hid4 == 0U);
    CHECK(state.l2cr == 0U);
    CHECK(state.ictc == 0U);
    CHECK(state.wpar == 0U);
    CHECK(state.dma_upper == 0U);
    CHECK(state.dma_lower == 0U);
    CHECK(state.iabr == 0U);
    CHECK(state.dabr == 0U);
    CHECK(state.mmcr[1] == 0U);
    CHECK(state.pmc[3] == 0U);
    CHECK(state.sia == 0U);
    CHECK(state.sda == 0U);
    CHECK(state.thermal_management[2] == 0U);
    CHECK(state.opaque_spr[1023] == 0U);
    CHECK(state.time_base_bias == UINT64_C(0));
    CHECK(state.decrementer_valid == 0);

    state.pc = UINT32_C(0x80000FFC);
    CHECK(!porpoise_state_prepare_title_entry(&state));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(state.msr == 0U && state.hid2 == 0U);
    porpoise_state_clear_fault(&state);

    state.gpr[1] = UINT32_C(0x80001003);
    state.gpr[2] = UINT32_C(0x80002000);
    state.gpr[13] = UINT32_C(0x80003000);
    CHECK(!porpoise_state_prepare_title_entry(&state));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(state.msr == 0U && state.hid2 == 0U);
    porpoise_state_clear_fault(&state);

    state.msr = PORPOISE_MSR_PR | UINT32_C(0x1234);
    original_msr = state.msr;
    CHECK(!porpoise_write_msr(
        &state,
        UINT32_C(0x80001000),
        UINT32_C(0xCAFEBABE)));
    CHECK(state.msr == original_msr);
    CHECK(state.fault == PORPOISE_FAULT_PRIVILEGED_OPERATION);
    CHECK(state.fault_address == UINT32_C(0x80001000));
    CHECK(strcmp(
              porpoise_fault_string(state.fault),
              "privileged operation in problem state") == 0);

    porpoise_state_clear_fault(&state);
    state.msr = 0U;
    CHECK(porpoise_write_msr(
        &state,
        UINT32_C(0x80001004),
        UINT32_C(0xA5A50000)));
    CHECK(state.msr == UINT32_C(0xA5A50000));

    CHECK(!porpoise_illegal_instruction(
        &state,
        UINT32_C(0x80001008),
        "reserved system-register encoding"));
    CHECK(state.fault == PORPOISE_FAULT_ILLEGAL_INSTRUCTION);
    CHECK(state.fault_address == UINT32_C(0x80001008));
    CHECK(strcmp(
              porpoise_state_fault_message(&state),
              "reserved system-register encoding") == 0);

    porpoise_state_clear_fault(&state);
    state.status = PORPOISE_EXECUTION_FAULTED;
    CHECK(porpoise_state_has_fault(&state));
    CHECK(porpoise_state_should_stop(&state));
    CHECK(strcmp(
              porpoise_state_fault_message(&state),
              "invalid PPC state") == 0);
    porpoise_state_clear_fault(&state);
    state.status = PORPOISE_EXECUTION_RETURNED;
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(porpoise_state_should_stop(&state));

    state.status = (PorpoiseExecutionStatus)99;
    CHECK(porpoise_state_has_fault(&state));
    CHECK(porpoise_state_should_stop(&state));
    CHECK(strcmp(
              porpoise_state_fault_message(&state),
              "invalid PPC state") == 0);
}

static void test_time_base(void)
{
    TestHost host;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;
    uint64_t ticks;
    uint64_t expected;
    size_t calls_before;

    memset(&host, 0, sizeof(host));
    adapter = test_adapter(&host);
    porpoise_state_init(&state, &adapter);

    host.ticks = UINT64_C(0x1122334455667788);
    ticks = UINT64_C(0);
    CHECK(porpoise_time_base_read(
        &state,
        UINT32_C(0x80002000),
        &ticks));
    CHECK(ticks == host.ticks);
    CHECK(host.time_calls == 1U);

    CHECK(!porpoise_time_base_read(
        &state,
        UINT32_C(0x80002002),
        NULL));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(host.time_calls == 1U);
    porpoise_state_clear_fault(&state);

    CHECK(porpoise_time_base_write_lower(
        &state,
        UINT32_C(0x80002004),
        UINT32_C(0xAABBCCDD)));
    expected = UINT64_C(0x11223344AABBCCDD);
    CHECK(state.time_base_bias == expected - host.ticks);
    CHECK(porpoise_time_base_read(
        &state,
        UINT32_C(0x80002008),
        &ticks));
    CHECK(ticks == expected);

    host.ticks += UINT64_C(0x25);
    expected += UINT64_C(0x25);
    CHECK(porpoise_time_base_write_upper(
        &state,
        UINT32_C(0x8000200C),
        UINT32_C(0xDEADBEEF)));
    expected = (UINT64_C(0xDEADBEEF) << 32U) |
               (expected & UINT64_C(0xFFFFFFFF));
    CHECK(porpoise_time_base_read(
        &state,
        UINT32_C(0x80002010),
        &ticks));
    CHECK(ticks == expected);

    host.ticks = UINT64_MAX - UINT64_C(2);
    state.time_base_bias = UINT64_C(5);
    CHECK(porpoise_time_base_read(
        &state,
        UINT32_C(0x80002014),
        &ticks));
    CHECK(ticks == UINT64_C(2));

    state.msr = PORPOISE_MSR_PR;
    state.time_base_bias = UINT64_C(0x123456789ABCDEF0);
    calls_before = host.time_calls;
    CHECK(!porpoise_time_base_write_lower(
        &state,
        UINT32_C(0x80002018),
        UINT32_C(0x01020304)));
    CHECK(state.fault == PORPOISE_FAULT_PRIVILEGED_OPERATION);
    CHECK(state.time_base_bias == UINT64_C(0x123456789ABCDEF0));
    CHECK(host.time_calls == calls_before);

    porpoise_state_clear_fault(&state);
    state.msr = 0U;
    host.time_result = PORPOISE_HOST_IO_ERROR;
    ticks = UINT64_C(0xF00DF00DF00DF00D);
    CHECK(!porpoise_time_base_read(
        &state,
        UINT32_C(0x8000201C),
        &ticks));
    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    CHECK(ticks == UINT64_C(0xF00DF00DF00DF00D));

    porpoise_state_clear_fault(&state);
    adapter.read_time_base = NULL;
    CHECK(!porpoise_time_base_read(
        &state,
        UINT32_C(0x80002020),
        &ticks));
    CHECK(state.fault == PORPOISE_FAULT_MISSING_HOST_CALLBACK);
}

static void test_decrementer(void)
{
    TestHost host;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;
    uint32_t value;
    size_t calls_before;

    memset(&host, 0, sizeof(host));
    adapter = test_adapter(&host);
    porpoise_state_init(&state, &adapter);

    host.ticks = UINT64_C(100);
    CHECK(porpoise_decrementer_write(
        &state,
        UINT32_C(0x80003000),
        UINT32_C(10)));
    CHECK(state.decrementer_value == UINT32_C(10));
    CHECK(state.decrementer_anchor == UINT64_C(100));
    CHECK(state.decrementer_valid != 0);

    host.ticks = UINT64_C(104);
    value = UINT32_C(0);
    CHECK(porpoise_decrementer_read(
        &state,
        UINT32_C(0x80003004),
        &value));
    CHECK(value == UINT32_C(6));

    host.ticks = UINT64_C(111);
    CHECK(porpoise_decrementer_read(
        &state,
        UINT32_C(0x80003008),
        &value));
    CHECK(value == UINT32_MAX);

    host.ticks = UINT64_MAX - UINT64_C(2);
    CHECK(porpoise_decrementer_write(
        &state,
        UINT32_C(0x8000300C),
        UINT32_C(100)));
    host.ticks = UINT64_C(3);
    CHECK(porpoise_decrementer_read(
        &state,
        UINT32_C(0x80003010),
        &value));
    CHECK(value == UINT32_C(94));

    host.ticks = UINT64_C(1000);
    CHECK(porpoise_decrementer_write(
        &state,
        UINT32_C(0x80003014),
        UINT32_C(500)));
    host.ticks = UINT64_C(1010);
    CHECK(porpoise_time_base_write_upper(
        &state,
        UINT32_C(0x80003018),
        UINT32_C(0x87654321)));
    host.ticks = UINT64_C(1020);
    CHECK(porpoise_decrementer_read(
        &state,
        UINT32_C(0x8000301C),
        &value));
    CHECK(value == UINT32_C(480));

    state.msr = PORPOISE_MSR_PR;
    state.decrementer_value = UINT32_C(0x12345678);
    state.decrementer_anchor = UINT64_C(0x9988776655443322);
    state.decrementer_valid = 1;
    calls_before = host.time_calls;
    CHECK(!porpoise_decrementer_write(
        &state,
        UINT32_C(0x80003020),
        UINT32_C(0xABCDEF01)));
    CHECK(state.fault == PORPOISE_FAULT_PRIVILEGED_OPERATION);
    CHECK(state.decrementer_value == UINT32_C(0x12345678));
    CHECK(state.decrementer_anchor == UINT64_C(0x9988776655443322));
    CHECK(state.decrementer_valid == 1);
    CHECK(host.time_calls == calls_before);

    porpoise_state_clear_fault(&state);
    state.msr = 0U;
    host.time_result = PORPOISE_HOST_IO_ERROR;
    CHECK(!porpoise_decrementer_write(
        &state,
        UINT32_C(0x80003024),
        UINT32_C(0xABCDEF01)));
    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    CHECK(state.decrementer_value == UINT32_C(0x12345678));
    CHECK(state.decrementer_anchor == UINT64_C(0x9988776655443322));
}

static void test_cache_helpers(void)
{
    TestHost host;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;
    size_t index;
    size_t calls_before;

    memset(&host, 0, sizeof(host));
    memset(host.write_bytes, 0xA5, sizeof(host.write_bytes));
    adapter = test_adapter(&host);
    porpoise_state_init(&state, &adapter);

    CHECK(porpoise_cache_block_zero(&state, UINT32_C(0x53)));
    CHECK(host.write_calls == 1U);
    CHECK(host.write_address == UINT32_C(0x40));
    CHECK(host.write_size == 32U);
    for (index = 0U; index < sizeof(host.write_bytes); index++) {
        CHECK(host.write_bytes[index] == 0U);
    }

    CHECK(porpoise_cache_block_zero(&state, UINT32_MAX));
    CHECK(host.write_calls == 2U);
    CHECK(host.write_address == UINT32_C(0xFFFFFFE0));
    CHECK(host.write_size == 32U);

    calls_before = host.write_calls;
    state.msr = PORPOISE_MSR_PR;
    CHECK(!porpoise_data_cache_block_invalidate(
        &state,
        UINT32_C(0x80004000),
        UINT32_C(0x12345678)));
    CHECK(state.fault == PORPOISE_FAULT_PRIVILEGED_OPERATION);
    CHECK(host.write_calls == calls_before);

    porpoise_state_clear_fault(&state);
    state.msr = 0U;
    CHECK(porpoise_data_cache_block_invalidate(
        &state,
        UINT32_C(0x80004004),
        UINT32_C(0x12345678)));
    CHECK(host.write_calls == calls_before);

    host.write_result = PORPOISE_HOST_IO_ERROR;
    CHECK(!porpoise_cache_block_zero(&state, UINT32_C(0x60)));
    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    CHECK(host.write_calls == calls_before + 1U);
}

static void test_traps(void)
{
    TestHost host;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;

    memset(&host, 0, sizeof(host));
    adapter = test_adapter(&host);
    porpoise_state_init(&state, &adapter);

    CHECK(porpoise_trap_event(
        &state,
        UINT32_C(0x80005000),
        PORPOISE_TRAP_SIGNED_LESS,
        UINT32_C(1),
        UINT32_C(0)));
    CHECK(host.trap_calls == 0U);

    CHECK(porpoise_trap_event(
        &state,
        UINT32_C(0x80005004),
        PORPOISE_TRAP_SIGNED_LESS,
        UINT32_MAX,
        UINT32_C(0)));
    CHECK(host.trap_calls == 1U);
    CHECK(host.event_state == &state);
    CHECK(host.event_address == UINT32_C(0x80005004));
    CHECK(host.trap_options == PORPOISE_TRAP_SIGNED_LESS);
    CHECK(host.trap_left == UINT32_MAX);
    CHECK(host.trap_right == UINT32_C(0));

    CHECK(porpoise_trap_event(
        &state,
        UINT32_C(0x80005008),
        PORPOISE_TRAP_ALWAYS,
        UINT32_C(0x11111111),
        UINT32_C(0x22222222)));
    CHECK(host.trap_calls == 2U);

    host.set_event_status = 1;
    host.event_status = PORPOISE_EXECUTION_RETURNED;
    CHECK(!porpoise_trap_event(
        &state,
        UINT32_C(0x8000500A),
        PORPOISE_TRAP_ALWAYS,
        UINT32_C(1),
        UINT32_C(2)));
    CHECK(state.status == PORPOISE_EXECUTION_RETURNED);
    CHECK(state.fault == PORPOISE_FAULT_NONE);
    CHECK(host.trap_calls == 3U);
    host.set_event_status = 0;
    state.status = PORPOISE_EXECUTION_READY;

    CHECK(!porpoise_trap_event(
        &state,
        UINT32_C(0x8000500C),
        UINT32_C(0x20),
        UINT32_C(0),
        UINT32_C(0)));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(host.trap_calls == 3U);

    porpoise_state_clear_fault(&state);
    host.trap_result = PORPOISE_HOST_UNSUPPORTED_MMIO;
    host.event_fault = PORPOISE_FAULT_UNSUPPORTED_OPERATION;
    CHECK(!porpoise_trap_event(
        &state,
        UINT32_C(0x80005010),
        PORPOISE_TRAP_EQUAL,
        UINT32_C(7),
        UINT32_C(7)));
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
    CHECK(strcmp(
              porpoise_state_fault_message(&state),
              "host callback fault") == 0);

    porpoise_state_clear_fault(&state);
    host.event_fault = PORPOISE_FAULT_NONE;
    host.trap_result = PORPOISE_HOST_OK;
    adapter.trap = NULL;
    CHECK(!porpoise_trap_event(
        &state,
        UINT32_C(0x80005014),
        PORPOISE_TRAP_EQUAL,
        UINT32_C(8),
        UINT32_C(8)));
    CHECK(state.fault == PORPOISE_FAULT_MISSING_HOST_CALLBACK);
}

static void test_system_calls(void)
{
    TestHost host;
    PorpoiseHostAdapter adapter;
    PorpoisePpcState state;

    memset(&host, 0, sizeof(host));
    adapter = test_adapter(&host);
    porpoise_state_init(&state, &adapter);

    state.msr = PORPOISE_MSR_PR;
    CHECK(porpoise_system_call_event(
        &state,
        UINT32_C(0x80006000)));
    CHECK(host.system_call_calls == 1U);
    CHECK(host.event_state == &state);
    CHECK(host.event_address == UINT32_C(0x80006000));

    host.set_event_status = 1;
    host.event_status = PORPOISE_EXECUTION_FAULTED;
    CHECK(!porpoise_system_call_event(
        &state,
        UINT32_C(0x80006002)));
    CHECK(state.status == PORPOISE_EXECUTION_FAULTED);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(strstr(
              porpoise_state_fault_message(&state),
              "system-call callback") != NULL);

    porpoise_state_clear_fault(&state);
    host.event_status = PORPOISE_EXECUTION_RETURNED;
    CHECK(!porpoise_system_call_event(
        &state,
        UINT32_C(0x80006003)));
    CHECK(state.status == PORPOISE_EXECUTION_RETURNED);
    CHECK(state.fault == PORPOISE_FAULT_NONE);
    host.set_event_status = 0;
    state.status = PORPOISE_EXECUTION_READY;

    host.set_event_status = 1;
    host.event_status = (PorpoiseExecutionStatus)99;
    CHECK(!porpoise_system_call_event(
        &state,
        UINT32_C(0x80006003)));
    CHECK(state.status == PORPOISE_EXECUTION_FAULTED);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);

    porpoise_state_clear_fault(&state);
    host.set_event_status = 0;

    host.system_call_result = PORPOISE_HOST_IO_ERROR;
    CHECK(!porpoise_system_call_event(
        &state,
        UINT32_C(0x80006004)));
    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);

    porpoise_state_clear_fault(&state);
    adapter.system_call = NULL;
    CHECK(!porpoise_system_call_event(
        &state,
        UINT32_C(0x80006008)));
    CHECK(state.fault == PORPOISE_FAULT_MISSING_HOST_CALLBACK);
}

int main(void)
{
    test_state_and_explicit_faults();
    test_time_base();
    test_decrementer();
    test_cache_helpers();
    test_traps();
    test_system_calls();
    return 0;
}
