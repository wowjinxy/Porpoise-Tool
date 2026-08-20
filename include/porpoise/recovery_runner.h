#ifndef PORPOISE_RECOVERY_RUNNER_H
#define PORPOISE_RECOVERY_RUNNER_H

#include "porpoise/dtk_import.h"
#include "porpoise/project.h"
#include "porpoise/recovery_project.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PorpoiseRecoveryRunOptions {
    /* No selectors means every enabled target. Explicit selectors may name a
     * disabled target and are processed in selector order. */
    const char *const *target_ids;
    size_t target_id_count;
    bool analyze_only;
    bool force;
    const char *report_path;
    const char *runtime_directory;
    const char *dtk_path;
    const PorpoiseOperationCallbacks *operation;
} PorpoiseRecoveryRunOptions;

typedef struct PorpoiseRecoveryRunTarget {
    const PorpoiseRecoveryTarget *target;
    char assembly_path[PORPOISE_PATH_CAPACITY];
    PorpoiseDtkImportResult import_result;
    PorpoiseSession *session;
    PorpoiseTranslationPlan *plan;
    PorpoiseReport report;
    PorpoiseStagedProject *staged;
    bool generated;
    bool published;
} PorpoiseRecoveryRunTarget;

typedef struct PorpoiseRecoveryRunResult {
    PorpoiseRecoveryRunTarget *targets;
    size_t target_count;
} PorpoiseRecoveryRunResult;

void porpoise_recovery_run_options_init(
    PorpoiseRecoveryRunOptions *options);
void porpoise_recovery_run_result_init(
    PorpoiseRecoveryRunResult *result);
void porpoise_recovery_run_result_free(
    PorpoiseRecoveryRunResult *result);

/*
 * Load/import, plan, and validate every selected target before generation.
 * Generation stages every target first and publishes the complete batch only
 * after all stages succeed. The result retains sessions and plans so a GUI can
 * inspect evidence or replan without reparsing.
 */
int porpoise_recovery_project_run(
    PorpoiseRecoveryProject *project,
    const PorpoiseRecoveryRunOptions *options,
    PorpoiseRecoveryRunResult *result,
    PorpoiseDiagnostics *diagnostics);

/* Write the schema-v3 aggregate plan/generation report atomically. */
int porpoise_recovery_run_write_report(
    const PorpoiseRecoveryProject *project,
    const PorpoiseRecoveryRunResult *result,
    const char *path,
    const PorpoiseDiagnostics *diagnostics);

#ifdef __cplusplus
}
#endif

#endif
