#ifndef LIBPORPOISE_HOST_THREAD_CARRIER_H
#define LIBPORPOISE_HOST_THREAD_CARRIER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIBPORPOISE_HOST_THREAD_CARRIER_API_VERSION 1U

typedef struct LibPorpoiseHostThreadCarrier
    LibPorpoiseHostThreadCarrier;

typedef void (*LibPorpoiseHostThreadCarrierEntryV1)(void *context);

typedef enum LibPorpoiseHostThreadCarrierResultV1 {
    LIBPORPOISE_HOST_THREAD_CARRIER_OK = 0,
    LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_ARGUMENT,
    LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_STATE,
    LIBPORPOISE_HOST_THREAD_CARRIER_OUT_OF_MEMORY,
    LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE,
    LIBPORPOISE_HOST_THREAD_CARRIER_TIMED_OUT
} LibPorpoiseHostThreadCarrierResultV1;

typedef struct LibPorpoiseHostThreadCarrierConfigV1 {
    uint32_t struct_size;
    LibPorpoiseHostThreadCarrierEntryV1 entry;
    void *entry_context;
    int32_t priority;
    const char *name;
} LibPorpoiseHostThreadCarrierConfigV1;

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierCreatePausedV1(
    const LibPorpoiseHostThreadCarrierConfigV1 *config,
    LibPorpoiseHostThreadCarrier **carrier_out);

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierResumeV1(
    LibPorpoiseHostThreadCarrier *carrier,
    int32_t *previous_suspend_count_out);

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierSuspendCurrentV1(
    LibPorpoiseHostThreadCarrier *carrier,
    int32_t *previous_suspend_count_out);

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierRequestStopV1(
    LibPorpoiseHostThreadCarrier *carrier);

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierJoinV1(
    LibPorpoiseHostThreadCarrier *carrier,
    uint32_t timeout_milliseconds);

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierDestroyV1(
    LibPorpoiseHostThreadCarrier *carrier);

#ifdef __cplusplus
}
#endif

#endif
