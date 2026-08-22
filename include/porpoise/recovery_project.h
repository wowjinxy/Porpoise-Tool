#ifndef PORPOISE_RECOVERY_PROJECT_H
#define PORPOISE_RECOVERY_PROJECT_H

#include "porpoise/plan.h"
#include "porpoise/symbol_map.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PORPOISE_RECOVERY_PROJECT_SCHEMA_VERSION 2U
#define PORPOISE_RECOVERY_PROJECT_LEGACY_SCHEMA_VERSION 1U

/* Schema-v1 accepted every nonempty JSON string as a target id. Preserve that
 * contract while deriving a portable filesystem component whenever an id is
 * used below .porpoise-build. Simple portable IDs remain readable; other IDs
 * receive an opaque SHA-256 key. */
bool porpoise_recovery_target_id_is_valid(const char *id);
#define PORPOISE_RECOVERY_TARGET_CACHE_KEY_SIZE 72U
bool porpoise_recovery_target_cache_key(
    const char *id,
    char output[PORPOISE_RECOVERY_TARGET_CACHE_KEY_SIZE]);

#define PORPOISE_RECOVERY_TITLE_HOST_GPR_COUNT 32U
#define PORPOISE_RECOVERY_TITLE_HOST_STARTUP_FUNCTION_CAPACITY 8U
#define PORPOISE_RECOVERY_TITLE_HOST_INITIAL_WORD_CAPACITY 16U
#define PORPOISE_RECOVERY_TITLE_STARTUP_ESTABLISH_GUEST_MAIN_THREAD_AFTER \
    UINT32_C(0x00000001)
#define PORPOISE_RECOVERY_TITLE_STARTUP_KNOWN_FLAGS \
    PORPOISE_RECOVERY_TITLE_STARTUP_ESTABLISH_GUEST_MAIN_THREAD_AFTER

/*
 * Paths retain the spelling read from the project while also exposing the
 * project-relative, lexically resolved value used by the recovery pipeline.
 * Loading a project never requires a referenced path to exist.
 */
typedef struct PorpoiseRecoveryPath {
    char *value;
    char *resolved;
} PorpoiseRecoveryPath;

typedef enum PorpoiseRecoverySourceKind {
    PORPOISE_RECOVERY_SOURCE_ASSEMBLY = 0,
    PORPOISE_RECOVERY_SOURCE_MANAGED_ELF,
    PORPOISE_RECOVERY_SOURCE_DTK_PREPARED_ASSEMBLY
} PorpoiseRecoverySourceKind;

typedef struct PorpoiseRecoverySymbolSource {
    PorpoiseSymbolSourceKind kind;
    PorpoiseRecoveryPath path;
    PorpoiseRecoveryPath auxiliary_path;
    bool has_auxiliary_path;
    char *module;
    bool permissive;
} PorpoiseRecoverySymbolSource;

typedef struct PorpoiseRecoveryOverride {
    char *target;
    char *module;
    uint32_t address;
    uint32_t size;
    char *normalized_fingerprint;
    PorpoiseOverrideAction action;
    char *contract_name;
    bool acknowledge_conflict;
} PorpoiseRecoveryOverride;

typedef enum PorpoiseRecoveryAnnotationInterpretation {
    PORPOISE_RECOVERY_ANNOTATION_RAW_BYTES = 0,
    PORPOISE_RECOVERY_ANNOTATION_ZERO_FILL,
    PORPOISE_RECOVERY_ANNOTATION_ASCII,
    PORPOISE_RECOVERY_ANNOTATION_UTF8,
    PORPOISE_RECOVERY_ANNOTATION_SHIFT_JIS,
    PORPOISE_RECOVERY_ANNOTATION_UTF16,
    PORPOISE_RECOVERY_ANNOTATION_S8_ARRAY,
    PORPOISE_RECOVERY_ANNOTATION_U8_ARRAY,
    PORPOISE_RECOVERY_ANNOTATION_S16_ARRAY,
    PORPOISE_RECOVERY_ANNOTATION_U16_ARRAY,
    PORPOISE_RECOVERY_ANNOTATION_S32_ARRAY,
    PORPOISE_RECOVERY_ANNOTATION_U32_ARRAY,
    PORPOISE_RECOVERY_ANNOTATION_F32_ARRAY,
    PORPOISE_RECOVERY_ANNOTATION_F64_ARRAY,
    PORPOISE_RECOVERY_ANNOTATION_POINTER32_ARRAY
} PorpoiseRecoveryAnnotationInterpretation;

typedef struct PorpoiseRecoveryAnnotation {
    char *target;
    char *module;
    uint32_t address;
    uint32_t size;
    char *normalized_fingerprint;
    char *exact_bytes_sha256;
    PorpoiseRecoveryAnnotationInterpretation interpretation;
    uint32_t element_count;
    char *encoding;
} PorpoiseRecoveryAnnotation;

typedef struct PorpoiseRecoveryDependencyCacheEntry {
    PorpoiseRecoveryPath path;
    char *sha256;
    uint64_t size;
    uint64_t mtime_ns;
} PorpoiseRecoveryDependencyCacheEntry;

typedef struct PorpoiseRecoveryMatchCacheEntry {
    char *module;
    uint32_t address;
    uint32_t size;
    char *normalized_fingerprint;
    char *canonical_identity;
    char *contract_name;
} PorpoiseRecoveryMatchCacheEntry;

typedef struct PorpoiseRecoveryTargetCache {
    char *input_sha256;
    char *settings_sha256;
    char *dtk_version;
    PorpoiseRecoveryDependencyCacheEntry *dependencies;
    size_t dependency_count;
    PorpoiseRecoveryMatchCacheEntry *matches;
    size_t match_count;
} PorpoiseRecoveryTargetCache;

/*
 * Portable, reviewed bootstrap metadata for a direct lifted title entry.
 * Function fingerprints are relocation-aware canonical SHA-256 identities.
 * The provenance digests bind the review to the symbol/catalog evidence used
 * to derive it. A provenance digest is NULL exactly when that optional
 * evidence class was absent; inferred profiles capture every loaded evidence
 * class and Build/Run validation rejects appearance, disappearance, or a
 * semantic identity change. Machine-local runtime paths never belong in this
 * structure.
 */
typedef struct PorpoiseRecoveryTitleStartupFunction {
    char *module;
    uint32_t address;
    uint32_t size;
    char *normalized_fingerprint;
    uint32_t flags;
} PorpoiseRecoveryTitleStartupFunction;

typedef struct PorpoiseRecoveryTitleInitialWord {
    uint32_t address;
    uint32_t value;
} PorpoiseRecoveryTitleInitialWord;

typedef struct PorpoiseRecoveryTitleHostProfile {
    uint32_t entry_address;
    uint32_t gpr[PORPOISE_RECOVERY_TITLE_HOST_GPR_COUNT];
    uint32_t arena_lo;
    uint32_t arena_hi;
    PorpoiseRecoveryTitleStartupFunction startup_functions[
        PORPOISE_RECOVERY_TITLE_HOST_STARTUP_FUNCTION_CAPACITY];
    size_t startup_function_count;
    PorpoiseRecoveryTitleInitialWord initial_words[
        PORPOISE_RECOVERY_TITLE_HOST_INITIAL_WORD_CAPACITY];
    size_t initial_word_count;
    bool initialize_dvd;
    char *input_sha256;
    char *symbol_sources_sha256;
    char *sdk_catalogs_sha256;
} PorpoiseRecoveryTitleHostProfile;

typedef struct PorpoiseRecoveryTarget {
    char *id;
    bool enabled;
    PorpoiseRecoverySourceKind source_kind;
    PorpoiseRecoveryPath input;
    PorpoiseRecoveryPath output;
    char *entry;
    bool strict;
    PorpoiseSdkPolicy sdk_policy;

    PorpoiseRecoverySymbolSource *symbol_sources;
    size_t symbol_source_count;
    PorpoiseRecoveryPath skip_list;
    bool has_skip_list;

    PorpoiseRecoveryOverride *overrides;
    size_t override_count;
    PorpoiseRecoveryAnnotation *annotations;
    size_t annotation_count;
    PorpoiseRecoveryTargetCache cache;
    PorpoiseRecoveryTitleHostProfile title_host;
    bool has_title_host;
} PorpoiseRecoveryTarget;

typedef struct PorpoiseRecoveryProject {
    unsigned int schema_version;
    char *path;
    char *directory;
    PorpoiseRecoveryPath *sdk_catalogs;
    size_t sdk_catalog_count;
    PorpoiseRecoveryPath *abi_contracts;
    size_t abi_contract_count;
    PorpoiseRecoveryTarget *targets;
    size_t target_count;
} PorpoiseRecoveryProject;

void porpoise_recovery_project_init(PorpoiseRecoveryProject *project);
void porpoise_recovery_project_free(PorpoiseRecoveryProject *project);

/* Transactional: project is unchanged when parsing or validation fails. */
int porpoise_recovery_project_load(
    PorpoiseRecoveryProject *project,
    const char *path,
    PorpoiseDiagnostics *diagnostics);

/*
 * Write canonical schema-v2 JSON. A schema-v1 project loaded into memory is
 * migrated on save. Paths are rebased against path when both
 * locations share a volume/root; foreign or cross-volume paths stay absolute.
 * Serialization is completed in an adjacent file before an atomic filesystem
 * replacement, so a failed save leaves an existing project unchanged.
 */
int porpoise_recovery_project_save(
    const PorpoiseRecoveryProject *project,
    const char *path,
    PorpoiseDiagnostics *diagnostics);

const PorpoiseRecoveryTarget *porpoise_recovery_project_find_target(
    const PorpoiseRecoveryProject *project,
    const char *id);
PorpoiseRecoveryTarget *porpoise_recovery_project_find_target_mutable(
    PorpoiseRecoveryProject *project,
    const char *id);

const char *porpoise_recovery_source_kind_name(
    PorpoiseRecoverySourceKind kind);
bool porpoise_recovery_source_kind_from_name(
    const char *name,
    PorpoiseRecoverySourceKind *kind_out);
const char *porpoise_recovery_annotation_interpretation_name(
    PorpoiseRecoveryAnnotationInterpretation interpretation);
bool porpoise_recovery_annotation_interpretation_from_name(
    const char *name,
    PorpoiseRecoveryAnnotationInterpretation *interpretation_out);

#ifdef __cplusplus
}
#endif

#endif
