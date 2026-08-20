#ifndef PORPOISE_SESSION_H
#define PORPOISE_SESSION_H

#include "porpoise/abi.h"
#include "porpoise/common.h"
#include "porpoise/program.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PorpoiseSession PorpoiseSession;

/*
 * input_path is required. Empty or NULL ABI and skip-list paths mean that the
 * corresponding optional input is not present.
 */
typedef struct PorpoiseSessionOpenOptions {
    const char *input_path;
    const char *abi_path;
    const char *skip_list_path;
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

#ifdef __cplusplus
}
#endif

#endif
