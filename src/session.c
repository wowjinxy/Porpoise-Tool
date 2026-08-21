#include "porpoise/session.h"

#include <stdlib.h>
#include <string.h>

struct PorpoiseSession {
    PorpoiseProgram program;
    PorpoiseAbiManifest abi;
    PorpoiseSymbolCatalog symbols;
    PorpoiseSdkCatalog sdk_catalog;
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

static int session_cancelled(
    const PorpoiseSessionOpenOptions *options,
    PorpoiseDiagnostics *diagnostics) {
    porpoise_diagnostics_add(
        diagnostics,
        PORPOISE_SEVERITY_INFO,
        options->input_path,
        0U,
        0U,
        "session loading was cancelled");
    return PORPOISE_EXIT_CANCELLED;
}

int porpoise_session_open(
    const PorpoiseSessionOpenOptions *options,
    PorpoiseSession **session_out,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSession *session;
    size_t source_index;
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
    if ((options->symbol_source_count != 0U &&
         options->symbol_sources == NULL) ||
        (options->sdk_catalog_path_count != 0U &&
         options->sdk_catalog_paths == NULL) ||
        (options->abi_path_count != 0U &&
         options->abi_paths == NULL)) {
        return session_invalid_argument(
            diagnostics, "session optional source arrays are inconsistent");
    }
    if (porpoise_operation_cancelled(options->operation)) {
        return session_cancelled(options, diagnostics);
    }
    porpoise_operation_progress(
        options->operation,
        PORPOISE_PHASE_LOAD,
        0U,
        3U,
        options->input_path);

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
    porpoise_symbol_catalog_init(&session->symbols);
    porpoise_sdk_catalog_init(&session->sdk_catalog);

    result = porpoise_program_load(
        &session->program, options->input_path, diagnostics);
    if (result == PORPOISE_EXIT_OK) {
        porpoise_operation_progress(
            options->operation,
            PORPOISE_PHASE_LOAD,
            1U,
            3U,
            options->input_path);
        if (porpoise_operation_cancelled(options->operation)) {
            result = session_cancelled(options, diagnostics);
        }
    }
    if (result == PORPOISE_EXIT_OK) {
        result = porpoise_sdk_catalog_load_builtin(
            &session->sdk_catalog, diagnostics);
    }
    for (source_index = 0U;
         result == PORPOISE_EXIT_OK &&
         source_index < options->symbol_source_count;
         source_index++) {
        const PorpoiseSessionSymbolSource *source =
            &options->symbol_sources[source_index];
        PorpoiseSymbolMapLoadOptions map_options;
        if (source->path == NULL || source->path[0] == '\0') {
            result = session_invalid_argument(
                diagnostics, "symbol source path is required");
            break;
        }
        if (porpoise_operation_cancelled(options->operation)) {
            result = session_cancelled(options, diagnostics);
            break;
        }
        porpoise_symbol_map_load_options_init(&map_options);
        map_options.module = source->module;
        map_options.strict = !source->permissive;
        porpoise_operation_progress(
            options->operation,
            PORPOISE_PHASE_SYMBOLS,
            source_index,
            options->symbol_source_count,
            source->path);
        if (source->kind == PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP) {
            result = porpoise_symbol_catalog_load_codewarrior(
                &session->symbols,
                source->path,
                &map_options,
                diagnostics);
        } else if (source->kind == PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS) {
            result = porpoise_symbol_catalog_load_dtk(
                &session->symbols,
                source->path,
                source->auxiliary_path,
                &map_options,
                diagnostics);
        } else {
            result = session_invalid_argument(
                diagnostics, "symbol source kind is invalid");
        }
    }
    if (result == PORPOISE_EXIT_OK && options->symbol_source_count != 0U) {
        porpoise_operation_progress(
            options->operation,
            PORPOISE_PHASE_SYMBOLS,
            options->symbol_source_count,
            options->symbol_source_count,
            "symbol sources loaded");
    }
    for (source_index = 0U;
         result == PORPOISE_EXIT_OK &&
         source_index < options->sdk_catalog_path_count;
         source_index++) {
        const char *path = options->sdk_catalog_paths[source_index];
        if (path == NULL || path[0] == '\0') {
            result = session_invalid_argument(
                diagnostics, "SDK catalog path is required");
            break;
        }
        if (porpoise_operation_cancelled(options->operation)) {
            result = session_cancelled(options, diagnostics);
            break;
        }
        result = porpoise_sdk_catalog_load_json(
            &session->sdk_catalog, path, diagnostics);
    }
    if (result == PORPOISE_EXIT_OK &&
        options->skip_list_path != NULL &&
        options->skip_list_path[0] != '\0') {
        result = porpoise_program_apply_skip_list(
            &session->program, options->skip_list_path, diagnostics);
    }
    if (result == PORPOISE_EXIT_OK) {
        porpoise_operation_progress(
            options->operation,
            PORPOISE_PHASE_LOAD,
            2U,
            3U,
            options->skip_list_path);
        if (porpoise_operation_cancelled(options->operation)) {
            result = session_cancelled(options, diagnostics);
        }
    }
    if (result == PORPOISE_EXIT_OK &&
        options->abi_path != NULL &&
        options->abi_path[0] != '\0') {
        result = porpoise_abi_load_additive(
            &session->abi, options->abi_path, diagnostics);
    }
    for (source_index = 0U;
         result == PORPOISE_EXIT_OK &&
         source_index < options->abi_path_count;
         source_index++) {
        const char *path = options->abi_paths[source_index];
        if (path == NULL || path[0] == '\0') {
            result = session_invalid_argument(
                diagnostics, "ABI contract path is required");
            break;
        }
        if (porpoise_operation_cancelled(options->operation)) {
            result = session_cancelled(options, diagnostics);
            break;
        }
        result = porpoise_abi_load_additive(
            &session->abi, path, diagnostics);
    }
    if (result == PORPOISE_EXIT_OK) {
        porpoise_operation_progress(
            options->operation,
            PORPOISE_PHASE_LOAD,
            3U,
            3U,
            options->abi_path_count != 0U
                ? options->abi_paths[options->abi_path_count - 1U]
                : options->abi_path);
        if (porpoise_operation_cancelled(options->operation)) {
            result = session_cancelled(options, diagnostics);
        }
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
    porpoise_sdk_catalog_free(&session->sdk_catalog);
    porpoise_symbol_catalog_free(&session->symbols);
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

const PorpoiseSymbolCatalog *porpoise_session_symbols(
    const PorpoiseSession *session) {
    return session == NULL ? NULL : &session->symbols;
}

const PorpoiseSdkCatalog *porpoise_session_sdk_catalog(
    const PorpoiseSession *session) {
    return session == NULL ? NULL : &session->sdk_catalog;
}
