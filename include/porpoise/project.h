#ifndef PORPOISE_PROJECT_H
#define PORPOISE_PROJECT_H

#include "porpoise/plan.h"
#include "porpoise/report.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PorpoiseProjectOptions {
    const char *output_path;
    const char *runtime_directory;
    const char *entry_symbol;
    bool force;
    bool strict;
    const PorpoiseOperationCallbacks *operation;
} PorpoiseProjectOptions;

/*
 * A fully generated project which has not yet replaced its destination.
 * Staged projects make it possible to validate and generate every target in a
 * project file before publishing any of them.
 */
typedef struct PorpoiseStagedProject PorpoiseStagedProject;

void porpoise_project_options_init(PorpoiseProjectOptions *options);

/*
 * Generate from a validated immutable translation plan. The plan controls all
 * function dispositions and the selected entry point; entry_symbol remains in
 * PorpoiseProjectOptions only for the legacy compatibility entry point below.
 */
int porpoise_project_generate_plan(
    const PorpoiseTranslationPlan *plan,
    const PorpoiseProjectOptions *options,
    PorpoiseReport *report,
    PorpoiseDiagnostics *diagnostics);

int porpoise_project_stage_plan(
    const PorpoiseTranslationPlan *plan,
    const PorpoiseProjectOptions *options,
    PorpoiseReport *report,
    PorpoiseStagedProject **staged_out,
    PorpoiseDiagnostics *diagnostics);

const char *porpoise_staged_project_output_path(
    const PorpoiseStagedProject *staged);
const char *porpoise_staged_project_stage_path(
    const PorpoiseStagedProject *staged);

/* Publish one staged project with the same rollback guarantees as the legacy
 * generator. The staged object remains caller-owned and must be freed. */
int porpoise_project_publish_staged(
    PorpoiseStagedProject *staged,
    PorpoiseDiagnostics *diagnostics);

/*
 * Publish a complete batch transactionally. Existing destinations are moved
 * to recoverable siblings first and a journal is maintained until every new
 * output is in place. Any failure restores the previous batch as a unit.
 */
int porpoise_project_publish_batch(
    PorpoiseStagedProject *const *staged,
    size_t staged_count,
    PorpoiseDiagnostics *diagnostics);

/* Removes an unpublished staging tree. Published destinations are untouched. */
void porpoise_staged_project_free(PorpoiseStagedProject *staged);

/* Compatibility wrapper for callers that still own Program and ABI objects. */
int porpoise_project_generate(
    const PorpoiseProgram *program,
    const PorpoiseAbiManifest *abi,
    const PorpoiseProjectOptions *options,
    PorpoiseReport *report,
    PorpoiseDiagnostics *diagnostics);

#ifdef __cplusplus
}
#endif

#endif
