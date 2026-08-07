#ifndef PORPOISE_LOWER_H
#define PORPOISE_LOWER_H

#include "porpoise/abi.h"
#include "porpoise/program.h"
#include "porpoise/report.h"

#include <stdio.h>

typedef struct PorpoiseLoweringOptions {
    bool strict;
} PorpoiseLoweringOptions;

int porpoise_lower_function(
    FILE *output,
    const PorpoiseProgram *program,
    const PorpoiseSourceFile *source,
    const PorpoiseFunction *function,
    const PorpoiseAbiManifest *abi,
    const PorpoiseLoweringOptions *options,
    PorpoiseReport *report,
    PorpoiseDiagnostics *diagnostics);

#endif
