#ifndef PORPOISE_ANALYSIS_H
#define PORPOISE_ANALYSIS_H

#include "porpoise/abi.h"
#include "porpoise/program.h"

typedef struct PorpoiseAnalysis {
    const PorpoiseFunction *entry;
    size_t translated_function_count;
} PorpoiseAnalysis;

int porpoise_analyze_program(
    const PorpoiseProgram *program,
    const PorpoiseAbiManifest *abi,
    const char *requested_entry,
    PorpoiseAnalysis *analysis,
    PorpoiseDiagnostics *diagnostics);

#endif
