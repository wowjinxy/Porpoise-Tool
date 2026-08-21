#include "porpoise_libporpoise_builtins_private.h"

#include <dolphin/os/OSArena.h>
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

#define KAR_ARENA_LO UINT32_C(0x805F83A0)
#define KAR_ARENA_HI UINT32_C(0x81700000)

static void clear_fault(PorpoisePpcState *state)
{
    porpoise_state_clear_fault(state);
    state->status = PORPOISE_EXECUTION_RUNNING;
}

static void *decode_guest(
    PorpoiseHostAdapter *host,
    uint32_t guest_address)
{
    void *pointer = NULL;

    CHECK(host->decode_pointer(
              host->context, guest_address, &pointer) == PORPOISE_HOST_OK);
    CHECK(pointer != NULL);
    return pointer;
}

static void set_lo(PorpoisePpcState *state, uint32_t value)
{
    state->gpr[3] = value;
    porpoise_libporpoise_os_set_arena_lo_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
}

static void set_hi(PorpoisePpcState *state, uint32_t value)
{
    state->gpr[3] = value;
    porpoise_libporpoise_os_set_arena_hi_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
}

static uint32_t get_lo(PorpoisePpcState *state)
{
    porpoise_libporpoise_os_get_arena_lo_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    return state->gpr[3];
}

static uint32_t get_hi(PorpoisePpcState *state)
{
    porpoise_libporpoise_os_get_arena_hi_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    return state->gpr[3];
}

static void reset_bounds(PorpoisePpcState *state)
{
    set_lo(state, KAR_ARENA_LO);
    set_hi(state, KAR_ARENA_HI);
}

static void test_configuration_and_strict_getters(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    uint32_t token;
    void *default_lo;
    void *default_hi;

    porpoise_libporpoise_os_get_arena_lo_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(strstr(
              porpoise_state_fault_message(state),
              "were not configured") != NULL);
    clear_fault(state);

    default_lo = OSGetArenaLo();
    default_hi = OSGetArenaHi();
    CHECK(porpoise_libporpoise_configure_title_arena(
              host, KAR_ARENA_LO, KAR_ARENA_LO) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(porpoise_libporpoise_configure_title_arena(
              host, KAR_ARENA_HI, KAR_ARENA_LO) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(porpoise_libporpoise_configure_title_arena(
              host, UINT32_C(0xA0000000), UINT32_C(0xA0001000)) ==
          PORPOISE_HOST_UNMAPPED_ADDRESS);
    CHECK(porpoise_libporpoise_configure_title_arena(
              host, UINT32_C(0xCC000000), UINT32_C(0xCC001000)) ==
          PORPOISE_HOST_UNSUPPORTED_MMIO);
    CHECK(OSGetArenaLo() == default_lo);
    CHECK(OSGetArenaHi() == default_hi);

    PorpoiseStubRejectNextArenaHi();
    CHECK(porpoise_libporpoise_configure_title_arena(
              host, KAR_ARENA_LO, KAR_ARENA_HI) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(OSGetArenaLo() == default_lo);
    CHECK(OSGetArenaHi() == default_hi);
    porpoise_libporpoise_os_get_arena_hi_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_INVALID_STATE);
    clear_fault(state);

    CHECK(porpoise_libporpoise_configure_title_arena(
              host, KAR_ARENA_LO, KAR_ARENA_HI) == PORPOISE_HOST_OK);
    CHECK(porpoise_libporpoise_configure_title_arena(
              host, KAR_ARENA_LO, KAR_ARENA_HI) == PORPOISE_HOST_OK);
    CHECK(porpoise_libporpoise_configure_title_arena(
              host, KAR_ARENA_LO + 4U, KAR_ARENA_HI) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(porpoise_libporpoise_configure_title_arena(host, 0U, 0U) ==
          PORPOISE_HOST_INVALID_ARGUMENT);

    CHECK(get_lo(state) == KAR_ARENA_LO);
    CHECK(get_hi(state) == KAR_ARENA_HI);
    CHECK(OSGetArenaLo() == decode_guest(host, KAR_ARENA_LO));
    CHECK(OSGetArenaHi() == decode_guest(host, KAR_ARENA_HI));

    token = porpoise_encode_pointer(state, PorpoiseStubNativePointer());
    CHECK(!porpoise_state_has_fault(state));
    CHECK(token == PorpoiseStubTokenAddress());
    CHECK(get_lo(state) != token);
    CHECK(get_hi(state) != token);
}

static void test_kar_low_allocation_path(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    state->gpr[3] = UINT32_C(0x2000);
    state->gpr[4] = 4U;
    porpoise_libporpoise_os_alloc_from_arena_lo_adapter(state);

    CHECK(!porpoise_state_has_fault(state));
    CHECK(state->gpr[3] == UINT32_C(0x805F83A0));
    CHECK(get_lo(state) == UINT32_C(0x805FA3A0));
    CHECK(get_hi(state) == KAR_ARENA_HI);
    CHECK(OSGetArenaLo() ==
          decode_guest(host, UINT32_C(0x805FA3A0)));
    CHECK(porpoise_libporpoise_configure_title_arena(
              host, KAR_ARENA_LO, KAR_ARENA_HI) == PORPOISE_HOST_OK);
    CHECK(get_lo(state) == UINT32_C(0x805FA3A0));
}

static void test_guest_alignment_semantics(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    reset_bounds(state);
    set_lo(state, KAR_ARENA_LO + 3U);
    state->gpr[3] = 0U;
    state->gpr[4] = UINT32_C(0x20);
    porpoise_libporpoise_os_alloc_from_arena_lo_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(state->gpr[3] == UINT32_C(0x805F83C0));
    CHECK(get_lo(state) == UINT32_C(0x805F83C0));

    set_lo(state, KAR_ARENA_LO + 3U);
    state->gpr[3] = UINT32_C(0x21);
    state->gpr[4] = UINT32_C(0x20);
    porpoise_libporpoise_os_alloc_from_arena_lo_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(state->gpr[3] == UINT32_C(0x805F83C0));
    CHECK(get_lo(state) == UINT32_C(0x805F8400));
    CHECK(OSGetArenaLo() ==
          decode_guest(host, UINT32_C(0x805F8400)));

    reset_bounds(state);
    set_hi(state, KAR_ARENA_HI - UINT32_C(0x0D));
    state->gpr[3] = 0U;
    state->gpr[4] = UINT32_C(0x20);
    porpoise_libporpoise_os_alloc_from_arena_hi_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(state->gpr[3] == UINT32_C(0x816FFFE0));
    CHECK(get_hi(state) == UINT32_C(0x816FFFE0));

    set_hi(state, KAR_ARENA_HI - UINT32_C(0x0D));
    state->gpr[3] = UINT32_C(0x21);
    state->gpr[4] = UINT32_C(0x20);
    porpoise_libporpoise_os_alloc_from_arena_hi_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(state->gpr[3] == UINT32_C(0x816FFFA0));
    CHECK(get_hi(state) == UINT32_C(0x816FFFA0));
    CHECK(OSGetArenaHi() ==
          decode_guest(host, UINT32_C(0x816FFFA0)));
}

static void test_exhaustion_and_crossing_are_nonmutating(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    void *native_lo;
    void *native_hi;

    reset_bounds(state);
    set_hi(state, KAR_ARENA_LO + UINT32_C(0x100));
    state->gpr[3] = UINT32_C(0x100);
    state->gpr[4] = 1U;
    porpoise_libporpoise_os_alloc_from_arena_lo_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(state->gpr[3] == KAR_ARENA_LO);
    CHECK(get_lo(state) == get_hi(state));

    reset_bounds(state);
    set_hi(state, KAR_ARENA_LO + UINT32_C(0x100));
    state->gpr[3] = UINT32_C(0x100);
    state->gpr[4] = 1U;
    porpoise_libporpoise_os_alloc_from_arena_hi_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(state->gpr[3] == KAR_ARENA_LO);
    CHECK(get_lo(state) == get_hi(state));

    reset_bounds(state);
    set_hi(state, KAR_ARENA_LO + UINT32_C(0x100));
    native_lo = OSGetArenaLo();
    native_hi = OSGetArenaHi();

    state->gpr[3] = UINT32_C(0x101);
    state->gpr[4] = 1U;
    porpoise_libporpoise_os_alloc_from_arena_lo_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(state->gpr[3] == 0U);
    CHECK(OSGetArenaLo() == native_lo);
    CHECK(OSGetArenaHi() == native_hi);

    state->gpr[3] = UINT32_C(0x101);
    state->gpr[4] = 1U;
    porpoise_libporpoise_os_alloc_from_arena_hi_adapter(state);
    CHECK(!porpoise_state_has_fault(state));
    CHECK(state->gpr[3] == 0U);
    CHECK(OSGetArenaLo() == native_lo);
    CHECK(OSGetArenaHi() == native_hi);

    set_hi(state, KAR_ARENA_LO);
    CHECK(get_lo(state) == get_hi(state));
    CHECK(OSGetArenaLo() == OSGetArenaHi());

    state->gpr[3] = KAR_ARENA_LO + 4U;
    porpoise_libporpoise_os_set_arena_lo_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(OSGetArenaLo() == native_lo);
    CHECK(OSGetArenaHi() == native_lo);
    clear_fault(state);

    set_hi(state, KAR_ARENA_HI);
    set_lo(state, KAR_ARENA_HI);
    CHECK(get_lo(state) == get_hi(state));
    state->gpr[3] = KAR_ARENA_HI - 4U;
    porpoise_libporpoise_os_set_arena_hi_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(OSGetArenaLo() == decode_guest(host, KAR_ARENA_HI));
    CHECK(OSGetArenaHi() == decode_guest(host, KAR_ARENA_HI));
    clear_fault(state);
}

static void test_invalid_alignment_overflow_and_root_escape(
    PorpoisePpcState *state)
{
    void *native_lo;
    void *native_hi;

    reset_bounds(state);
    native_lo = OSGetArenaLo();
    native_hi = OSGetArenaHi();

    state->gpr[3] = 4U;
    state->gpr[4] = 0U;
    porpoise_libporpoise_os_alloc_from_arena_lo_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(OSGetArenaLo() == native_lo && OSGetArenaHi() == native_hi);
    clear_fault(state);

    state->gpr[3] = 4U;
    state->gpr[4] = 3U;
    porpoise_libporpoise_os_alloc_from_arena_hi_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(OSGetArenaLo() == native_lo && OSGetArenaHi() == native_hi);
    clear_fault(state);

    state->gpr[3] = 4U;
    state->gpr[4] = UINT32_C(0x80000000);
    porpoise_libporpoise_os_alloc_from_arena_lo_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_ADDRESS_OVERFLOW);
    CHECK(OSGetArenaLo() == native_lo && OSGetArenaHi() == native_hi);
    clear_fault(state);

    state->gpr[3] = UINT32_MAX;
    state->gpr[4] = 1U;
    porpoise_libporpoise_os_alloc_from_arena_hi_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_ADDRESS_OVERFLOW);
    CHECK(OSGetArenaLo() == native_lo && OSGetArenaHi() == native_hi);
    clear_fault(state);

    state->gpr[3] = KAR_ARENA_LO - 4U;
    porpoise_libporpoise_os_set_arena_lo_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(OSGetArenaLo() == native_lo && OSGetArenaHi() == native_hi);
    clear_fault(state);

    state->gpr[3] = KAR_ARENA_HI + 4U;
    porpoise_libporpoise_os_set_arena_hi_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_INVALID_ARGUMENT);
    CHECK(OSGetArenaLo() == native_lo && OSGetArenaHi() == native_hi);
    clear_fault(state);
}

static void test_native_divergence_faults_closed(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    void *expected_lo;

    reset_bounds(state);
    expected_lo = OSGetArenaLo();
    OSSetArenaLo(decode_guest(host, KAR_ARENA_LO + 4U));
    porpoise_libporpoise_os_get_arena_lo_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(strstr(
              porpoise_state_fault_message(state), "diverged") != NULL);

    OSSetArenaLo(expected_lo);
    clear_fault(state);
    CHECK(get_lo(state) == KAR_ARENA_LO);
}

static void test_native_rejection_rolls_back_mirror(
    PorpoiseHostAdapter *host,
    PorpoisePpcState *state)
{
    void *native_lo;
    void *native_hi;

    reset_bounds(state);
    native_lo = OSGetArenaLo();
    native_hi = OSGetArenaHi();
    PorpoiseStubRejectNextArenaLo();
    state->gpr[3] = KAR_ARENA_LO + UINT32_C(0x20);
    porpoise_libporpoise_os_set_arena_lo_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(strstr(
              porpoise_state_fault_message(state), "transaction") != NULL);
    CHECK(strstr(
              porpoise_state_fault_message(state), "rollback failed") == NULL);
    CHECK(OSGetArenaLo() == native_lo && OSGetArenaHi() == native_hi);
    clear_fault(state);
    CHECK(get_lo(state) == KAR_ARENA_LO);

    PorpoiseStubRejectNextArenaHi();
    state->gpr[3] = KAR_ARENA_HI - UINT32_C(0x20);
    porpoise_libporpoise_os_set_arena_hi_adapter(state);
    CHECK(state->fault == PORPOISE_FAULT_INVALID_STATE);
    CHECK(strstr(
              porpoise_state_fault_message(state), "transaction") != NULL);
    CHECK(strstr(
              porpoise_state_fault_message(state), "rollback failed") == NULL);
    CHECK(OSGetArenaLo() == native_lo && OSGetArenaHi() == native_hi);
    clear_fault(state);
    CHECK(get_hi(state) == KAR_ARENA_HI);
    CHECK(OSGetArenaLo() == decode_guest(host, KAR_ARENA_LO));
    CHECK(OSGetArenaHi() == decode_guest(host, KAR_ARENA_HI));
}

static void test_failed_initial_configuration_is_restored_on_shutdown(void)
{
    PorpoiseHostAdapter host;
    void *default_lo;
    void *default_hi;
    void *configured_lo;

    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    default_lo = OSGetArenaLo();
    default_hi = OSGetArenaHi();
    configured_lo = decode_guest(&host, KAR_ARENA_LO);

    /*
     * The forward low setter succeeds, the forward high setter rejects, and
     * the second low setter rejects the immediate rollback. The consumed
     * injections then allow shutdown to retry and restore both saved bounds.
     */
    PorpoiseStubRejectArenaLoOnCall(2U);
    PorpoiseStubRejectArenaHiOnCall(1U);
    CHECK(porpoise_libporpoise_configure_title_arena(
              &host, KAR_ARENA_LO, KAR_ARENA_HI) ==
          PORPOISE_HOST_IO_ERROR);
    CHECK(OSGetArenaLo() == configured_lo);
    CHECK(OSGetArenaHi() == default_hi);

    CHECK(porpoise_libporpoise_configure_title_arena(
              &host, KAR_ARENA_LO, KAR_ARENA_HI) ==
          PORPOISE_HOST_IO_ERROR);
    CHECK(porpoise_libporpoise_configure_title_arena(&host, 0U, 0U) ==
          PORPOISE_HOST_IO_ERROR);
    CHECK(OSGetArenaLo() == configured_lo);
    CHECK(OSGetArenaHi() == default_hi);

    porpoise_libporpoise_adapter_shutdown(&host);
    CHECK(OSGetArenaLo() == default_lo);
    CHECK(OSGetArenaHi() == default_hi);
}

int main(void)
{
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    void *default_native_lo;
    void *default_native_hi;

    PorpoiseStubSetSystemCallVectorMapped(1);
    test_failed_initial_configuration_is_restored_on_shutdown();
    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    default_native_lo = OSGetArenaLo();
    default_native_hi = OSGetArenaHi();
    porpoise_state_init(&state, &host);
    state.status = PORPOISE_EXECUTION_RUNNING;
    state.pc = UINT32_C(0x803D3C04);

    CHECK(porpoise_libporpoise_configure_title_arena(&host, 0U, 0U) ==
          PORPOISE_HOST_OK);
    test_configuration_and_strict_getters(&host, &state);
    test_kar_low_allocation_path(&host, &state);
    test_guest_alignment_semantics(&host, &state);
    test_exhaustion_and_crossing_are_nonmutating(&host, &state);
    test_invalid_alignment_overflow_and_root_escape(&state);
    test_native_divergence_faults_closed(&host, &state);
    test_native_rejection_rolls_back_mirror(&host, &state);

    porpoise_libporpoise_adapter_shutdown(&host);
    CHECK(OSGetArenaLo() == default_native_lo);
    CHECK(OSGetArenaHi() == default_native_hi);

    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    CHECK(porpoise_libporpoise_configure_title_arena(&host, 0U, 0U) ==
          PORPOISE_HOST_OK);
    CHECK(OSGetArenaLo() == default_native_lo);
    CHECK(OSGetArenaHi() == default_native_hi);
    porpoise_state_init(&state, &host);
    state.status = PORPOISE_EXECUTION_RUNNING;
    porpoise_libporpoise_os_get_arena_lo_adapter(&state);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_STATE);
    porpoise_libporpoise_adapter_shutdown(&host);
    return 0;
}
