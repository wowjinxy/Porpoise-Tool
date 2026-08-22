#ifndef PORPOISE_BUILD_H
#define PORPOISE_BUILD_H

#include "porpoise/common.h"
#include "porpoise/recovery_title_host.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PORPOISE_BUILD_MANIFEST_SCHEMA_VERSION 3U
#define PORPOISE_BUILD_VERSION_CAPACITY 128U
#define PORPOISE_BUILD_ID_CAPACITY 64U
#define PORPOISE_BUILD_STATUS_FILE_ENV "PORPOISE_STATUS_FILE"
#define PORPOISE_BUILD_STATUS_MAGIC "PORPOISE_STATUS_V1"
#define PORPOISE_REJECT_APPROXIMATIONS_ENV "PORPOISE_REJECT_APPROXIMATIONS"

typedef enum PorpoiseBuildPhase {
    PORPOISE_BUILD_PHASE_PREFLIGHT = 0,
    PORPOISE_BUILD_PHASE_BIND_DEPENDENCIES,
    PORPOISE_BUILD_PHASE_CONFIGURE,
    PORPOISE_BUILD_PHASE_COMPILE,
    PORPOISE_BUILD_PHASE_STAGE_RUNTIME,
    PORPOISE_BUILD_PHASE_RUN
} PorpoiseBuildPhase;

typedef void (*PorpoiseBuildProgressCallback)(
    void *user_data,
    PorpoiseBuildPhase phase,
    size_t completed,
    size_t total,
    const char *detail);

typedef void (*PorpoiseBuildLogCallback)(
    void *user_data,
    PorpoiseBuildPhase phase,
    bool standard_error,
    const char *text,
    size_t length);

typedef bool (*PorpoiseBuildCancellationCallback)(void *user_data);

typedef struct PorpoiseBuildCallbacks {
    PorpoiseBuildProgressCallback progress;
    PorpoiseBuildLogCallback log;
    PorpoiseBuildCancellationCallback cancelled;
    void *user_data;
} PorpoiseBuildCallbacks;

typedef struct PorpoiseBuildEnvironmentEntry {
    const char *name;
    const char *value;
} PorpoiseBuildEnvironmentEntry;

/*
 * Machine-local inputs for configuring a generated Porpoise target. None of
 * these paths are written into a recovery project. The build cache is placed
 * beside project_file under
 * .porpoise-build/<portable target key>/<configuration digest>. Legacy IDs
 * that are not safe path components use a stable SHA-256 key.
 */
typedef struct PorpoiseBuildRequest {
    const char *project_file;
    const char *target_id;
    const char *generated_directory;
    const char *libporpoise_directory;
    /*
     * Preferred shared-core path: validate and materialize this reviewed host
     * directly into the managed build stage. Both pointers must be supplied.
     */
    const PorpoiseRecoveryTarget *recovery_target;
    const PorpoiseTranslationPlan *plan;
    /* Advanced fallback for an already materialized immutable host. */
    const char *title_host_directory;

    const char *meson_executable;
    const char *c_compiler;
    const char *cpp_compiler;
    const char *objdump_executable;
    const char *build_type;
    const char *generated_plan_digest;

    const char *run_working_directory;
    const char *dvd_root;
    const char *trace_file;
    size_t frame_limit;
    /* Test/acceptance gate: dynamically reached approximations fault even
     * when JSONL tracing is disabled. Ordinary Build/Run leaves this false. */
    bool reject_approximations;

    const char *const *runtime_search_directories;
    size_t runtime_search_directory_count;
    const char *const *run_arguments;
    size_t run_argument_count;
    const PorpoiseBuildEnvironmentEntry *environment;
    size_t environment_count;

    bool allow_copy_fallback;
    bool force_reconfigure;
    PorpoiseBuildCallbacks callbacks;
} PorpoiseBuildRequest;

typedef struct PorpoiseBuildPreflight {
    char meson_version[PORPOISE_BUILD_VERSION_CAPACITY];
    char compiler_family[PORPOISE_BUILD_VERSION_CAPACITY];
    char compiler_version[PORPOISE_BUILD_VERSION_CAPACITY];
    char compiler_target[PORPOISE_BUILD_VERSION_CAPACITY];
    char meson_path[PORPOISE_PATH_CAPACITY];
    char c_compiler_path[PORPOISE_PATH_CAPACITY];
    char cpp_compiler_path[PORPOISE_PATH_CAPACITY];
} PorpoiseBuildPreflight;

typedef struct PorpoiseBuildResult {
    char configuration_digest[PORPOISE_BUILD_ID_CAPACITY + 1U];
    char generated_output_identity[PORPOISE_BUILD_ID_CAPACITY + 1U];
    char libporpoise_identity[PORPOISE_BUILD_ID_CAPACITY + 1U];
    char sdl2_dependency_identity[PORPOISE_BUILD_ID_CAPACITY + 1U];
    char title_host_identity[PORPOISE_BUILD_ID_CAPACITY + 1U];
    char cache_directory[PORPOISE_PATH_CAPACITY];
    char source_directory[PORPOISE_PATH_CAPACITY];
    char build_directory[PORPOISE_PATH_CAPACITY];
    char executable_path[PORPOISE_PATH_CAPACITY];
    char manifest_path[PORPOISE_PATH_CAPACITY];
    char status_file_path[PORPOISE_PATH_CAPACITY];
    char guest_status_message[PORPOISE_MESSAGE_CAPACITY];
    PorpoiseBuildPreflight preflight;
    size_t runtime_file_count;
    int process_exit_code;
    uint32_t guest_pc;
    bool cache_reused;
    bool configured;
    bool compiled;
    bool executable_available;
    bool guest_status_reported;
    bool guest_faulted;
} PorpoiseBuildResult;

void porpoise_build_request_init(PorpoiseBuildRequest *request);
void porpoise_build_result_init(PorpoiseBuildResult *result);
const char *porpoise_build_phase_name(PorpoiseBuildPhase phase);

int porpoise_build_preflight(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildPreflight *result,
    PorpoiseDiagnostics *diagnostics);

int porpoise_project_build(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildResult *result,
    PorpoiseDiagnostics *diagnostics);

int porpoise_project_run(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildResult *build,
    PorpoiseDiagnostics *diagnostics);

#ifdef __cplusplus
}
#endif

#endif
