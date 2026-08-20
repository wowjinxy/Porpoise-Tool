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
} PorpoiseProjectOptions;

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
