#ifndef PORPOISE_SESSION_H
#define PORPOISE_SESSION_H

#include "porpoise/abi.h"
#include "porpoise/common.h"
#include "porpoise/operation.h"
#include "porpoise/program.h"
#include "porpoise/sdk_catalog.h"
#include "porpoise/symbol_map.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PorpoiseSession PorpoiseSession;

typedef struct PorpoiseSessionSymbolSource {
    PorpoiseSymbolSourceKind kind;
    const char *path;
    /* DTK splits.txt; ignored for a CodeWarrior map. */
    const char *auxiliary_path;
    const char *module;
    /* When true, malformed individual records warn and are skipped. */
    bool permissive;
} PorpoiseSessionSymbolSource;

/*
 * input_path is required. Empty or NULL ABI and skip-list paths mean that the
 * corresponding optional input is not present.
 */
typedef struct PorpoiseSessionOpenOptions {
    const char *input_path;
    const char *abi_path;
    const char *skip_list_path;
    const PorpoiseSessionSymbolSource *symbol_sources;
    size_t symbol_source_count;
    const char *const *sdk_catalog_paths;
    size_t sdk_catalog_path_count;
    const PorpoiseOperationCallbacks *operation;
} PorpoiseSessionOpenOptions;

void porpoise_session_open_options_init(PorpoiseSessionOpenOptions *options);

/*
 * Open and fully validate an immutable recovery input session. On failure,
 * session_out is left NULL. A successful session owns its Program and ABI
 * manifest and must be released with porpoise_session_close().
 */
int porpoise_session_open(
    const PorpoiseSessionOpenOptions *options,
    PorpoiseSession **session_out,
    PorpoiseDiagnostics *diagnostics);
void porpoise_session_close(PorpoiseSession *session);

/* Returned objects borrow immutable storage owned by session. */
const PorpoiseProgram *porpoise_session_program(
    const PorpoiseSession *session);
const PorpoiseAbiManifest *porpoise_session_abi(
    const PorpoiseSession *session);
const PorpoiseSymbolCatalog *porpoise_session_symbols(
    const PorpoiseSession *session);
const PorpoiseSdkCatalog *porpoise_session_sdk_catalog(
    const PorpoiseSession *session);

#ifdef __cplusplus
}
#endif

#endif
