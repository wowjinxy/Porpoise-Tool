#ifndef PORPOISE_RECOVERY_CACHE_H
#define PORPOISE_RECOVERY_CACHE_H

#include "porpoise/dtk_import.h"
#include "porpoise/recovery_project.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PorpoiseRecoveryCacheDependencyInput {
    /* Resolved path to a regular file used to build the session or plan. */
    const char *path;
} PorpoiseRecoveryCacheDependencyInput;

typedef struct PorpoiseRecoveryCacheInputs {
    /*
     * Resolved assembly/ELF path. Optional when dtk_metadata is supplied to
     * validation/rebuild, because validated DTK metadata binds the input.
     */
    const char *source_path;

    /* Canonical, caller-owned serialization of all non-file target settings. */
    const char *settings_identity;

    const PorpoiseRecoveryCacheDependencyInput *dependencies;
    size_t dependency_count;
    const PorpoiseOperationCallbacks *operation;
} PorpoiseRecoveryCacheInputs;

typedef enum PorpoiseRecoveryCacheState {
    PORPOISE_RECOVERY_CACHE_HIT = 0,
    PORPOISE_RECOVERY_CACHE_MISS,
    PORPOISE_RECOVERY_CACHE_STALE,
    PORPOISE_RECOVERY_CACHE_INVALID
} PorpoiseRecoveryCacheState;

typedef enum PorpoiseRecoveryCacheReason {
    PORPOISE_RECOVERY_CACHE_REASON_NONE = 0U,
    PORPOISE_RECOVERY_CACHE_REASON_ABSENT = 1U << 0,
    PORPOISE_RECOVERY_CACHE_REASON_INCOMPLETE = 1U << 1,
    PORPOISE_RECOVERY_CACHE_REASON_INPUT_CHANGED = 1U << 2,
    PORPOISE_RECOVERY_CACHE_REASON_INPUT_UNAVAILABLE = 1U << 3,
    PORPOISE_RECOVERY_CACHE_REASON_SETTINGS_CHANGED = 1U << 4,
    PORPOISE_RECOVERY_CACHE_REASON_DTK_CHANGED = 1U << 5,
    PORPOISE_RECOVERY_CACHE_REASON_DEPENDENCY_SET_CHANGED = 1U << 6,
    PORPOISE_RECOVERY_CACHE_REASON_DEPENDENCY_MISSING = 1U << 7,
    PORPOISE_RECOVERY_CACHE_REASON_DEPENDENCY_METADATA_CHANGED = 1U << 8,
    PORPOISE_RECOVERY_CACHE_REASON_DEPENDENCY_CONTENT_CHANGED = 1U << 9,
    PORPOISE_RECOVERY_CACHE_REASON_CONFLICTING_MATCH = 1U << 10
} PorpoiseRecoveryCacheReason;

typedef struct PorpoiseRecoveryCacheValidation {
    PorpoiseRecoveryCacheState state;
    unsigned int reason_flags;
    /* SIZE_MAX when no individual dependency caused the result. */
    size_t dependency_index;
    char dependency_path[PORPOISE_PATH_CAPACITY];
} PorpoiseRecoveryCacheValidation;

void porpoise_recovery_cache_inputs_init(
    PorpoiseRecoveryCacheInputs *inputs);
void porpoise_recovery_cache_validation_init(
    PorpoiseRecoveryCacheValidation *validation);

/* Free every owned field and return cache to the canonical absent state. */
void porpoise_recovery_target_cache_clear(
    PorpoiseRecoveryTargetCache *cache);

/*
 * Validate the non-authoritative cache against current source, settings, DTK
 * metadata, and dependency files. An absent or incomplete cache is a normal
 * cache miss and returns PORPOISE_EXIT_OK. Conflicting exact identities are
 * rejected as malformed cache data.
 */
int porpoise_recovery_target_cache_validate(
    const PorpoiseRecoveryTarget *target,
    const PorpoiseRecoveryCacheInputs *inputs,
    const PorpoiseDtkImportMetadata *dtk_metadata,
    PorpoiseRecoveryCacheValidation *validation,
    PorpoiseDiagnostics *diagnostics);

/*
 * Validate session/plan coherence, measure all current dependencies, retain
 * only exact SDK identities, and transactionally replace target->cache. The
 * previous cache is unchanged on cancellation or failure.
 */
int porpoise_recovery_target_cache_rebuild(
    PorpoiseRecoveryTarget *target,
    const PorpoiseRecoveryCacheInputs *inputs,
    const PorpoiseSession *session,
    const PorpoiseTranslationPlan *plan,
    const PorpoiseDtkImportMetadata *dtk_metadata,
    PorpoiseDiagnostics *diagnostics);

const char *porpoise_recovery_cache_state_name(
    PorpoiseRecoveryCacheState state);

#ifdef __cplusplus
}
#endif

#endif
