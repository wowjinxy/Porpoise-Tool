#include "porpoise/session.h"

#include <stdlib.h>
#include <string.h>

struct PorpoiseSession {
    PorpoiseProgram program;
    PorpoiseAbiManifest abi;
};

void porpoise_session_open_options_init(PorpoiseSessionOpenOptions *options) {
    if (options != NULL) memset(options, 0, sizeof(*options));
}

static int session_invalid_argument(
    PorpoiseDiagnostics *diagnostics,
    const char *message) {
    if (diagnostics != NULL) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "%s",
            message);
    }
    return PORPOISE_EXIT_INTERNAL;
}

int porpoise_session_open(
    const PorpoiseSessionOpenOptions *options,
    PorpoiseSession **session_out,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSession *session;
    int result;

    if (session_out == NULL || diagnostics == NULL) {
        return PORPOISE_EXIT_INTERNAL;
    }
    *session_out = NULL;
    if (options == NULL) {
        return session_invalid_argument(
            diagnostics, "session open options are required");
    }
    if (options->input_path == NULL || options->input_path[0] == '\0') {
        return session_invalid_argument(
            diagnostics, "session input path is required");
    }

    session = (PorpoiseSession *)calloc(1U, sizeof(*session));
    if (session == NULL) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            options->input_path,
            0U,
            0U,
            "out of memory while opening recovery session");
        return PORPOISE_EXIT_INTERNAL;
    }
    porpoise_program_init(&session->program);
    porpoise_abi_init(&session->abi);

    result = porpoise_program_load(
        &session->program, options->input_path, diagnostics);
    if (result == PORPOISE_EXIT_OK &&
        options->skip_list_path != NULL &&
        options->skip_list_path[0] != '\0') {
        result = porpoise_program_apply_skip_list(
            &session->program, options->skip_list_path, diagnostics);
    }
    if (result == PORPOISE_EXIT_OK &&
        options->abi_path != NULL &&
        options->abi_path[0] != '\0') {
        result = porpoise_abi_load(
            &session->abi, options->abi_path, diagnostics);
    }
    if (result != PORPOISE_EXIT_OK) {
        porpoise_session_close(session);
        return result;
    }

    *session_out = session;
    return PORPOISE_EXIT_OK;
}

void porpoise_session_close(PorpoiseSession *session) {
    if (session == NULL) return;
    porpoise_abi_free(&session->abi);
    porpoise_program_free(&session->program);
    free(session);
}

const PorpoiseProgram *porpoise_session_program(
    const PorpoiseSession *session) {
    return session == NULL ? NULL : &session->program;
}

const PorpoiseAbiManifest *porpoise_session_abi(
    const PorpoiseSession *session) {
    return session == NULL ? NULL : &session->abi;
}
