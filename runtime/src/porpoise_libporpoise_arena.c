#include "porpoise_libporpoise_builtins_private.h"
#include "porpoise_libporpoise_private.h"

#include <stdint.h>

static int porpoise_arena_alignment_is_valid(uint32_t alignment)
{
    return alignment != 0U &&
           (alignment & (alignment - UINT32_C(1))) == 0U;
}

static void porpoise_arena_fault(
    PorpoisePpcState *state,
    PorpoiseFault fault,
    uint32_t address,
    const char *message)
{
    porpoise_state_set_fault(state, fault, address, message);
}

static void porpoise_arena_get(
    PorpoisePpcState *state,
    int high)
{
    PorpoiseLibporpoiseArenaSnapshot arena;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_arena_snapshot(state, &arena)) {
        return;
    }
    state->gpr[3] = high ? arena.hi : arena.lo;
}

void porpoise_libporpoise_os_get_arena_lo_adapter(
    PorpoisePpcState *state)
{
    porpoise_arena_get(state, 0);
}

void porpoise_libporpoise_os_get_arena_hi_adapter(
    PorpoisePpcState *state)
{
    porpoise_arena_get(state, 1);
}

static void porpoise_arena_set(
    PorpoisePpcState *state,
    int high)
{
    PorpoiseLibporpoiseArenaSnapshot arena;
    uint32_t value;
    uint32_t new_lo;
    uint32_t new_hi;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    value = state->gpr[3];
    if (!porpoise_libporpoise_arena_snapshot(state, &arena)) {
        return;
    }
    new_lo = high ? arena.lo : value;
    new_hi = high ? value : arena.hi;
    if (new_lo < arena.configured_base ||
        new_hi > arena.configured_limit || new_lo > new_hi) {
        porpoise_arena_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            value,
            high
                ? "OSSetArenaHi crosses or leaves the configured guest arena"
                : "OSSetArenaLo crosses or leaves the configured guest arena");
        return;
    }
    (void)porpoise_libporpoise_arena_commit(
        state, &arena, new_lo, new_hi);
}

void porpoise_libporpoise_os_set_arena_lo_adapter(
    PorpoisePpcState *state)
{
    porpoise_arena_set(state, 0);
}

void porpoise_libporpoise_os_set_arena_hi_adapter(
    PorpoisePpcState *state)
{
    porpoise_arena_set(state, 1);
}

static void porpoise_arena_alloc_lo(PorpoisePpcState *state)
{
    PorpoiseLibporpoiseArenaSnapshot arena;
    uint32_t size;
    uint32_t alignment;
    uint64_t mask;
    uint64_t aligned_start;
    uint64_t unaligned_end;
    uint64_t aligned_end;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    size = state->gpr[3];
    alignment = state->gpr[4];
    if (!porpoise_arena_alignment_is_valid(alignment)) {
        porpoise_arena_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            alignment,
            "OSAllocFromArenaLo alignment must be a nonzero power of two");
        return;
    }
    if (!porpoise_libporpoise_arena_snapshot(state, &arena)) {
        return;
    }

    mask = (uint64_t)alignment - UINT64_C(1);
    aligned_start = ((uint64_t)arena.lo + mask) & ~mask;
    if (aligned_start > UINT32_MAX) {
        porpoise_arena_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            arena.lo,
            "OSAllocFromArenaLo start alignment overflows 32 bits");
        return;
    }
    unaligned_end = aligned_start + (uint64_t)size;
    if (unaligned_end > UINT32_MAX) {
        porpoise_arena_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            (uint32_t)aligned_start,
            "OSAllocFromArenaLo size overflows the 32-bit guest address space");
        return;
    }
    aligned_end = (unaligned_end + mask) & ~mask;
    if (aligned_end > UINT32_MAX) {
        porpoise_arena_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            (uint32_t)unaligned_end,
            "OSAllocFromArenaLo end alignment overflows 32 bits");
        return;
    }
    if (aligned_start > arena.hi || aligned_end > arena.hi) {
        state->gpr[3] = 0U;
        return;
    }
    if (!porpoise_libporpoise_arena_commit(
            state, &arena, (uint32_t)aligned_end, arena.hi)) {
        return;
    }
    state->gpr[3] = (uint32_t)aligned_start;
}

static void porpoise_arena_alloc_hi(PorpoisePpcState *state)
{
    PorpoiseLibporpoiseArenaSnapshot arena;
    uint32_t size;
    uint32_t alignment;
    uint32_t mask;
    uint32_t aligned_old_hi;
    uint32_t new_hi;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    size = state->gpr[3];
    alignment = state->gpr[4];
    if (!porpoise_arena_alignment_is_valid(alignment)) {
        porpoise_arena_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            alignment,
            "OSAllocFromArenaHi alignment must be a nonzero power of two");
        return;
    }
    if (!porpoise_libporpoise_arena_snapshot(state, &arena)) {
        return;
    }

    mask = alignment - UINT32_C(1);
    aligned_old_hi = arena.hi & ~mask;
    if (size > aligned_old_hi) {
        porpoise_arena_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            arena.hi,
            "OSAllocFromArenaHi size underflows the 32-bit guest address space");
        return;
    }
    new_hi = (aligned_old_hi - size) & ~mask;
    if (new_hi < arena.lo) {
        state->gpr[3] = 0U;
        return;
    }
    if (!porpoise_libporpoise_arena_commit(
            state, &arena, arena.lo, new_hi)) {
        return;
    }
    state->gpr[3] = new_hi;
}

void porpoise_libporpoise_os_alloc_from_arena_lo_adapter(
    PorpoisePpcState *state)
{
    porpoise_arena_alloc_lo(state);
}

void porpoise_libporpoise_os_alloc_from_arena_hi_adapter(
    PorpoisePpcState *state)
{
    porpoise_arena_alloc_hi(state);
}
