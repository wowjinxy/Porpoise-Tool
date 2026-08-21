#ifndef PORPOISE_OPERATION_H
#define PORPOISE_OPERATION_H

#include "porpoise/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PorpoiseOperationPhase {
    PORPOISE_PHASE_LOAD = 0,
    PORPOISE_PHASE_IMPORT,
    PORPOISE_PHASE_SYMBOLS,
    PORPOISE_PHASE_SIGNATURES,
    PORPOISE_PHASE_PLAN,
    PORPOISE_PHASE_VALIDATE,
    PORPOISE_PHASE_GENERATE,
    PORPOISE_PHASE_PUBLISH
} PorpoiseOperationPhase;

typedef void (*PorpoiseProgressCallback)(
    void *user_data,
    PorpoiseOperationPhase phase,
    size_t completed,
    size_t total,
    const char *detail);

typedef bool (*PorpoiseCancellationCallback)(void *user_data);

typedef struct PorpoiseOperationCallbacks {
    PorpoiseProgressCallback progress;
    PorpoiseCancellationCallback cancelled;
    void *user_data;
} PorpoiseOperationCallbacks;

void porpoise_operation_callbacks_init(
    PorpoiseOperationCallbacks *callbacks);
void porpoise_operation_progress(
    const PorpoiseOperationCallbacks *callbacks,
    PorpoiseOperationPhase phase,
    size_t completed,
    size_t total,
    const char *detail);
bool porpoise_operation_cancelled(
    const PorpoiseOperationCallbacks *callbacks);
const char *porpoise_operation_phase_name(PorpoiseOperationPhase phase);

#ifdef __cplusplus
}
#endif

#endif
