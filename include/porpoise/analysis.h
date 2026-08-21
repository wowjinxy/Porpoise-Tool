#ifndef PORPOISE_ANALYSIS_H
#define PORPOISE_ANALYSIS_H

#include "porpoise/abi.h"
#include "porpoise/program.h"

typedef struct PorpoiseImportBinding {
    const PorpoiseAbiFunction *import;
    const PorpoiseFunction *owner;
    const PorpoiseAddressAlias *alias;
    uint32_t guest_address;
} PorpoiseImportBinding;

typedef struct PorpoiseAnalysis {
    const PorpoiseFunction *entry;
    size_t translated_function_count;
    PorpoiseImportBinding *import_bindings;
    size_t import_binding_count;
} PorpoiseAnalysis;

/*
 * Bindings own only their array. Their import, owner, and alias pointers borrow
 * immutable storage from the ABI manifest and program, which must remain alive
 * while the analysis is consumed.
 */
void porpoise_analysis_init(PorpoiseAnalysis *analysis);
void porpoise_analysis_free(PorpoiseAnalysis *analysis);

/*
 * analysis must be initialized before its first use. A successful call
 * replaces its previous contents; a failed call leaves them unchanged.
 */
int porpoise_analyze_program(
    const PorpoiseProgram *program,
    const PorpoiseAbiManifest *abi,
    const char *requested_entry,
    PorpoiseAnalysis *analysis,
    PorpoiseDiagnostics *diagnostics);

#endif
