#ifndef PORPOISE_PROJECT_H
#define PORPOISE_PROJECT_H

#include "porpoise/abi.h"
#include "porpoise/program.h"
#include "porpoise/report.h"

typedef struct PorpoiseProjectOptions {
    const char *output_path;
    const char *runtime_directory;
    const char *entry_symbol;
    bool force;
    bool strict;
} PorpoiseProjectOptions;

int porpoise_project_generate(
    const PorpoiseProgram *program,
    const PorpoiseAbiManifest *abi,
    const PorpoiseProjectOptions *options,
    PorpoiseReport *report,
    PorpoiseDiagnostics *diagnostics);

#endif
