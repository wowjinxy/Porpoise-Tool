#ifndef PORPOISE_DTK_IMPORT_H
#define PORPOISE_DTK_IMPORT_H

#include "porpoise/common.h"
#include "porpoise/operation.h"
#include "porpoise/sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PORPOISE_DTK_IMPORT_METADATA_SCHEMA_VERSION 1U
#define PORPOISE_DTK_IMPORT_METADATA_FILE ".porpoise-dtk-cache-v1"
#define PORPOISE_DTK_VERSION_CAPACITY 128U

typedef enum PorpoiseDtkSourceKind {
    PORPOISE_DTK_SOURCE_MANAGED_ELF = 0,
    PORPOISE_DTK_SOURCE_PREPARED_ASM
} PorpoiseDtkSourceKind;

typedef struct PorpoiseDtkProcessResult {
    int exit_code;
    char *standard_output;
    char *standard_error;
} PorpoiseDtkProcessResult;

/*
 * Run an argv vector without a command shell. The callback owns no arguments.
 * It must return a Porpoise exit code and allocate any captured output with
 * malloc-compatible storage. A successful launch returns PORPOISE_EXIT_OK even
 * when the child exit_code is nonzero.
 */
typedef int (*PorpoiseDtkRunCallback)(
    void *user_data,
    const char *const *argv,
    const char *working_directory,
    const PorpoiseOperationCallbacks *operation,
    PorpoiseDtkProcessResult *result,
    PorpoiseDiagnostics *diagnostics);

typedef struct PorpoiseDtkImportOptions {
    PorpoiseDtkSourceKind source_kind;
    const char *input_path;

    /* Required for MANAGED_ELF and ignored for PREPARED_ASM. */
    const char *cache_path;
    const char *dtk_path;

    /* Exact canonical settings text supplied by the project layer. */
    const char *settings_identity;

    unsigned int minimum_dtk_major;
    unsigned int minimum_dtk_minor;
    unsigned int minimum_dtk_patch;
    bool allow_cache_reuse;
    bool prepared_require_link_order;

    PorpoiseDtkRunCallback runner;
    void *runner_user_data;
    const PorpoiseOperationCallbacks *operation;
} PorpoiseDtkImportOptions;

typedef struct PorpoiseDtkImportMetadata {
    uint32_t schema_version;
    PorpoiseDtkSourceKind source_kind;
    char dtk_version[PORPOISE_DTK_VERSION_CAPACITY];
    char input_sha256[PORPOISE_SHA256_HEX_SIZE];
    char tool_sha256[PORPOISE_SHA256_HEX_SIZE];
    char settings_sha256[PORPOISE_SHA256_HEX_SIZE];
    char dependency_sha256[PORPOISE_SHA256_HEX_SIZE];
    char content_sha256[PORPOISE_SHA256_HEX_SIZE];
    size_t asm_file_count;
    size_t function_count;
    size_t annotation_count;
} PorpoiseDtkImportMetadata;

typedef struct PorpoiseDtkImportResult {
    char validated_path[PORPOISE_PATH_CAPACITY];
    bool cache_hit;
    PorpoiseDtkImportMetadata metadata;
} PorpoiseDtkImportResult;

void porpoise_dtk_process_result_init(PorpoiseDtkProcessResult *result);
void porpoise_dtk_process_result_free(PorpoiseDtkProcessResult *result);
void porpoise_dtk_import_options_init(PorpoiseDtkImportOptions *options);
void porpoise_dtk_import_result_init(PorpoiseDtkImportResult *result);

/* Portable shell-free runner used when options.runner is NULL. */
int porpoise_dtk_run_process_default(
    void *user_data,
    const char *const *argv,
    const char *working_directory,
    const PorpoiseOperationCallbacks *operation,
    PorpoiseDtkProcessResult *result,
    PorpoiseDiagnostics *diagnostics);

/*
 * MANAGED_ELF validates DTK, imports into a fresh sibling staging directory,
 * validates and hashes the result, then publishes the cache atomically.
 * PREPARED_ASM performs the same safe-tree and assembly validation in place.
 */
int porpoise_dtk_import_run(
    const PorpoiseDtkImportOptions *options,
    PorpoiseDtkImportResult *result,
    PorpoiseDiagnostics *diagnostics);

int porpoise_dtk_validate_prepared(
    const char *path,
    bool require_link_order,
    const char *settings_identity,
    const PorpoiseOperationCallbacks *operation,
    PorpoiseDtkImportResult *result,
    PorpoiseDiagnostics *diagnostics);

#ifdef __cplusplus
}
#endif

#endif
