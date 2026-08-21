#include "porpoise/operation.h"

#include <string.h>

void porpoise_operation_callbacks_init(
    PorpoiseOperationCallbacks *callbacks) {
    if (callbacks != NULL) memset(callbacks, 0, sizeof(*callbacks));
}

void porpoise_operation_progress(
    const PorpoiseOperationCallbacks *callbacks,
    PorpoiseOperationPhase phase,
    size_t completed,
    size_t total,
    const char *detail) {
    if (callbacks != NULL && callbacks->progress != NULL) {
        callbacks->progress(
            callbacks->user_data,
            phase,
            completed,
            total,
            detail == NULL ? "" : detail);
    }
}

bool porpoise_operation_cancelled(
    const PorpoiseOperationCallbacks *callbacks) {
    return callbacks != NULL && callbacks->cancelled != NULL &&
           callbacks->cancelled(callbacks->user_data);
}

const char *porpoise_operation_phase_name(PorpoiseOperationPhase phase) {
    switch (phase) {
        case PORPOISE_PHASE_LOAD: return "load";
        case PORPOISE_PHASE_IMPORT: return "import";
        case PORPOISE_PHASE_SYMBOLS: return "symbols";
        case PORPOISE_PHASE_SIGNATURES: return "signatures";
        case PORPOISE_PHASE_PLAN: return "plan";
        case PORPOISE_PHASE_VALIDATE: return "validate";
        case PORPOISE_PHASE_GENERATE: return "generate";
        case PORPOISE_PHASE_PUBLISH: return "publish";
        default: return "unknown";
    }
}
