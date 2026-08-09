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
 * Release every opaque host-address token created by this adapter and clear
 * its callbacks. Call this once after the final lifted/ABI use. Console-memory
 * addresses are owned by libPorpoise and are never released here. Shutdown is
 * idempotent for the initialized object. Do not copy a live adapter value.
 */
void porpoise_libporpoise_adapter_shutdown(
    PorpoiseHostAdapter *adapter);

#ifdef __cplusplus
}
#endif

#endif
