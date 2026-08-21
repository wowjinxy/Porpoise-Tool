#ifndef PORPOISE_RECOVERY_PROJECT_H
#define PORPOISE_RECOVERY_PROJECT_H

#include "porpoise/plan.h"
#include "porpoise/symbol_map.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PORPOISE_RECOVERY_PROJECT_SCHEMA_VERSION 1U

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
 * Write canonical schema-v1 JSON. Paths are rebased against path when both
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
