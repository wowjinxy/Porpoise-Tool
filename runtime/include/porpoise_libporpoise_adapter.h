#ifndef PORPOISE_LIBPORPOISE_ADAPTER_H
#define PORPOISE_LIBPORPOISE_ADAPTER_H

#include "porpoise_lifted.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize libPorpoise once and populate adapter with the callbacks used by
 * lifted code. This function is intended to run on the host startup thread,
 * before any lifted function is called. Exactly one adapter instance may be
 * live at a time; initializing another instance or reinitializing the live
 * instance returns PORPOISE_HOST_INVALID_ARGUMENT without changing it.
 */
PorpoiseHostResult porpoise_libporpoise_adapter_init(
    PorpoiseHostAdapter *adapter);

/*
 * Initialize for a generated title. When initialize_dvd is nonzero, the
 * optional DVD root is applied before OSInit and native DVDInit builds the
 * host FST immediately afterward. A process may initialize native DVD only
 * once; subsequent configured adapters must request the identical root.
 */
PorpoiseHostResult porpoise_libporpoise_adapter_init_for_title(
    PorpoiseHostAdapter *adapter,
    const char *dvd_root_directory,
    int initialize_dvd);

/*
 * Bind the generated address dispatcher to the active adapter. The raw
 * dispatcher remains private to the adapter; host callbacks are routed
 * through porpoise_libporpoise_run_guest so all guest execution is serialized
 * by libPorpoise's single-core scheduler lock. Binding is one-time, except
 * that repeating the exact same binding is an idempotent success.
 */
PorpoiseHostResult porpoise_libporpoise_bind_guest_dispatch(
    PorpoiseHostAdapter *adapter,
    PorpoiseHostCallGuestFn dispatcher);

/* Generated exports use thread-local PPC state. Register their binder with
 * the address dispatcher as one transaction so a host-thread carrier can
 * install its private state before it enters lifted code. */
typedef void (*PorpoiseBindExportStateFn)(PorpoisePpcState *state);

PorpoiseHostResult porpoise_libporpoise_bind_guest_runtime(
    PorpoiseHostAdapter *adapter,
    PorpoiseHostCallGuestFn dispatcher,
    PorpoiseBindExportStateFn export_state_binder);

/* Validate and bind the canonical guest main thread produced by the title's
 * lifted thread initializer. This never derives a guest identity from a
 * native OSThread pointer. */
PorpoiseHostResult porpoise_libporpoise_bind_guest_main_thread(
    PorpoisePpcState *state);

/*
 * Run one generated guest dispatch through the active libPorpoise adapter.
 * Nested calls are safe: libPorpoise's interrupt lock is recursive for the
 * owning host thread, and every invocation restores the prior lock state.
 */
int porpoise_libporpoise_run_guest(
    PorpoisePpcState *state,
    uint32_t guest_function_address);

/*
 * Apply optional title-linked arena bounds after OSInit. Passing two zero
 * bounds retains libPorpoise's defaults but does not create a guest-address
 * mirror; any other pair must name one valid, contiguous guest-memory span
 * with an exclusive upper bound. Repeating the exact configured root is
 * idempotent; changing it during one adapter lifetime is rejected.
 */
PorpoiseHostResult porpoise_libporpoise_configure_title_arena(
    const PorpoiseHostAdapter *adapter,
    uint32_t guest_arena_lo,
    uint32_t guest_arena_hi);

/*
 * Release every opaque host-address token created by this adapter and clear
 * its callbacks. Call this once after the final lifted/ABI use. Any configured
 * native arena bounds are restored to their pre-title values and verified.
 * Console-memory addresses are owned by libPorpoise and are never released
 * here. Shutdown is idempotent for the initialized object. Do not copy a live
 * adapter value.
 */
void porpoise_libporpoise_adapter_shutdown(
    PorpoiseHostAdapter *adapter);

#ifdef __cplusplus
}
#endif

#endif
