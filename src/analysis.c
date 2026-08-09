#include "porpoise/analysis.h"
#include "porpoise/util.h"

#include <stdlib.h>
#include <string.h>

enum {
    PORPOISE_GENERATED_SYMBOL_CAPACITY = PORPOISE_NAME_CAPACITY + 32
};

static const char *abi_callable_name(const PorpoiseAbiFunction *function) {
    if (function->kind == PORPOISE_ABI_IMPORT && function->adapter != NULL) {
        return function->adapter;
    }
    return function->wrapper;
}

static const char *abi_callable_role(const PorpoiseAbiFunction *function) {
    if (function->kind == PORPOISE_ABI_EXPORT) {
        return "export wrapper";
    }
    return function->adapter != NULL ? "import adapter" : "import wrapper";
}

static bool import_bridge_name(const PorpoiseAbiFunction *function,
                               char *name,
                               size_t capacity) {
    char sanitized[PORPOISE_NAME_CAPACITY];

    porpoise_sanitize_identifier(
        function->symbol,
        sanitized,
        sizeof(sanitized));
    return porpoise_format(
        name,
        capacity,
        "porpoise_import_%s",
        sanitized);
}

static bool lifted_function_name(const PorpoiseFunction *function,
                                 char *name,
                                 size_t capacity) {
    return porpoise_format(
        name,
        capacity,
        "porpoise_lifted_%s",
        function->c_name);
}

static bool abi_callable_name_is_reserved(const char *name) {
    static const char *const reserved_names[] = {
        /* Entry points and public typedef names that do not use our prefixes. */
        "main",
        "DolphinMain",
        "PorpoisePpcState",
        "PorpoiseHostAdapter",
        "PorpoiseFpr",
        "PorpoiseHostResult",
        "PorpoiseFault",
        "PorpoiseExecutionStatus",
        "PorpoiseFpFmaOperation",
        "PorpoiseFpPrecision",
        "PorpoiseTitleHostResultV1",
        "PorpoiseHostPrepareTitleEntryV1",
        "PorpoiseHostReadBytesFn",
        "PorpoiseHostWriteBytesFn",
        "PorpoiseHostDecodePointerFn",
        "PorpoiseHostEncodePointerFn",
        "PorpoiseHostReadTimeBaseFn",
        "PorpoiseHostTrapFn",
        "PorpoiseHostSystemCallFn",
        "PorpoiseLiftedFunction"
    };
    size_t index;

    /* Own these prefixes so future public helpers/constants cannot drift. */
    if (strncmp(name, "porpoise_", 9U) == 0 ||
        strncmp(name, "PORPOISE_", 9U) == 0) {
        return true;
    }

    for (index = 0U;
         index < sizeof(reserved_names) / sizeof(reserved_names[0]);
         index++) {
        if (strcmp(name, reserved_names[index]) == 0) {
            return true;
        }
    }
    return false;
}

static int validate_abi_namespace(const PorpoiseProgram *program,
                                  const PorpoiseAbiManifest *abi,
                                  PorpoiseDiagnostics *diagnostics) {
    size_t function_index;
    bool valid = true;

    for (function_index = 0U;
         function_index < abi->function_count;
         function_index++) {
        const PorpoiseAbiFunction *function =
            &abi->functions[function_index];
        const char *callable = abi_callable_name(function);
        const char *role = abi_callable_role(function);
        size_t previous;
        size_t generated_index;
        size_t file_index;

        if (function->kind == PORPOISE_ABI_IMPORT) {
            char bridge[PORPOISE_GENERATED_SYMBOL_CAPACITY];
            const PorpoiseAddressAlias *declared_alias =
                porpoise_program_find_declared_alias(
                    program,
                    function->symbol,
                    NULL);

            if (porpoise_program_find_function(
                    program,
                    function->symbol) != NULL) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    NULL,
                    0U,
                    0U,
                    "ABI import %s conflicts with a translated function",
                    function->symbol);
                valid = false;
            }
            if (declared_alias != NULL &&
                !declared_alias->is_function_name) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    declared_alias->source_path,
                    declared_alias->source_line,
                    declared_alias->address,
                    "ABI import %s conflicts with an ordinary input address alias",
                    function->symbol);
                valid = false;
            }
            if (!import_bridge_name(function, bridge, sizeof(bridge))) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    NULL,
                    0U,
                    0U,
                    "cannot construct the generated bridge name for ABI import %s",
                    function->symbol);
                return PORPOISE_EXIT_INTERNAL;
            }
            for (previous = 0U; previous < function_index; previous++) {
                const PorpoiseAbiFunction *candidate =
                    &abi->functions[previous];
                char candidate_bridge[PORPOISE_GENERATED_SYMBOL_CAPACITY];

                if (candidate->kind != PORPOISE_ABI_IMPORT) {
                    continue;
                }
                if (!import_bridge_name(
                        candidate,
                        candidate_bridge,
                        sizeof(candidate_bridge))) {
                    porpoise_diagnostics_add(
                        diagnostics,
                        PORPOISE_SEVERITY_ERROR,
                        NULL,
                        0U,
                        0U,
                        "cannot construct the generated bridge name for ABI import %s",
                        candidate->symbol);
                    return PORPOISE_EXIT_INTERNAL;
                }
                if (strcmp(bridge, candidate_bridge) == 0) {
                    porpoise_diagnostics_add(
                        diagnostics,
                        PORPOISE_SEVERITY_ERROR,
                        NULL,
                        0U,
                        0U,
                        "ABI imports %s and %s collide as generated bridge %s",
                        candidate->symbol,
                        function->symbol,
                        bridge);
                    valid = false;
                }
            }
        }

        if (callable == NULL) {
            porpoise_diagnostics_add(
                diagnostics,
                PORPOISE_SEVERITY_ERROR,
                NULL,
                0U,
                0U,
                "ABI %s for %s has no callable C identifier",
                role,
                function->symbol);
            valid = false;
            continue;
        }

        if (abi_callable_name_is_reserved(callable)) {
            porpoise_diagnostics_add(
                diagnostics,
                PORPOISE_SEVERITY_ERROR,
                NULL,
                0U,
                0U,
                "ABI %s %s for %s is reserved by the generated project or runtime",
                role,
                callable,
                function->symbol);
            valid = false;
        }

        for (previous = 0U; previous < function_index; previous++) {
            const PorpoiseAbiFunction *candidate = &abi->functions[previous];
            const char *candidate_callable = abi_callable_name(candidate);

            if (candidate_callable != NULL &&
                strcmp(callable, candidate_callable) == 0) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    NULL,
                    0U,
                    0U,
                    "ABI %s for %s and ABI %s for %s both use C identifier %s",
                    abi_callable_role(candidate),
                    candidate->symbol,
                    role,
                    function->symbol,
                    callable);
                valid = false;
            }
        }

        for (generated_index = 0U;
             generated_index < abi->function_count;
             generated_index++) {
            const PorpoiseAbiFunction *generated =
                &abi->functions[generated_index];
            char bridge[PORPOISE_GENERATED_SYMBOL_CAPACITY];

            if (generated->kind != PORPOISE_ABI_IMPORT) {
                continue;
            }
            if (!import_bridge_name(generated, bridge, sizeof(bridge))) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    NULL,
                    0U,
                    0U,
                    "cannot construct the generated bridge name for ABI import %s",
                    generated->symbol);
                return PORPOISE_EXIT_INTERNAL;
            }
            if (strcmp(callable, bridge) == 0) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    NULL,
                    0U,
                    0U,
                    "ABI %s %s for %s collides with generated import bridge %s",
                    role,
                    callable,
                    function->symbol,
                    bridge);
                valid = false;
            }
        }

        for (file_index = 0U;
             file_index < program->file_count;
             file_index++) {
            const PorpoiseSourceFile *file = &program->files[file_index];
            size_t lifted_index;

            for (lifted_index = 0U;
                 lifted_index < file->function_count;
                 lifted_index++) {
                const PorpoiseFunction *lifted =
                    &file->functions[lifted_index];
                char lifted_name[PORPOISE_GENERATED_SYMBOL_CAPACITY];

                if (lifted->skipped) {
                    continue;
                }
                if (!lifted_function_name(
                        lifted,
                        lifted_name,
                        sizeof(lifted_name))) {
                    porpoise_diagnostics_add(
                        diagnostics,
                        PORPOISE_SEVERITY_ERROR,
                        file->relative_path,
                        0U,
                        lifted->start_address,
                        "cannot construct the generated name for lifted function %s",
                        lifted->name);
                    return PORPOISE_EXIT_INTERNAL;
                }
                if (strcmp(callable, lifted_name) == 0) {
                    porpoise_diagnostics_add(
                        diagnostics,
                        PORPOISE_SEVERITY_ERROR,
                        file->relative_path,
                        0U,
                        lifted->start_address,
                        "ABI %s %s for %s collides with lifted function %s",
                        role,
                        callable,
                        function->symbol,
                        lifted_name);
                    valid = false;
                }
            }
        }
    }

    return valid ? PORPOISE_EXIT_OK : PORPOISE_EXIT_USAGE;
}

static bool function_has_global_input_name(
    const PorpoiseFunction *function,
    const char *name) {
    size_t alias_index;
    if (function->is_global && strcmp(function->name, name) == 0) return true;
    for (alias_index = 0U;
         alias_index < function->alias_count;
         alias_index++) {
        const PorpoiseAddressAlias *alias = &function->aliases[alias_index];
        if (alias->is_function_name && alias->is_global &&
            strcmp(alias->name, name) == 0) {
            return true;
        }
    }
    return false;
}

void porpoise_analysis_init(PorpoiseAnalysis *analysis) {
    if (analysis != NULL) memset(analysis, 0, sizeof(*analysis));
}

void porpoise_analysis_free(PorpoiseAnalysis *analysis) {
    if (analysis == NULL) return;
    free(analysis->import_bindings);
    memset(analysis, 0, sizeof(*analysis));
}

static int build_import_bindings(
    const PorpoiseProgram *program,
    const PorpoiseAbiManifest *abi,
    PorpoiseAnalysis *analysis,
    PorpoiseDiagnostics *diagnostics) {
    size_t function_index;
    size_t binding_count = 0U;
    size_t binding_index = 0U;
    bool valid = true;

    for (function_index = 0U;
         function_index < abi->function_count;
         function_index++) {
        const PorpoiseAbiFunction *function = &abi->functions[function_index];
        const PorpoiseFunction *owner = NULL;

        if (function->kind != PORPOISE_ABI_IMPORT) continue;
        if (!porpoise_program_resolve_declared_function(
                program, function->symbol, &owner, NULL, NULL)) {
            continue;
        }
        if (owner == NULL || !owner->skipped) {
            porpoise_diagnostics_add(
                diagnostics,
                PORPOISE_SEVERITY_ERROR,
                NULL,
                0U,
                owner == NULL ? 0U : owner->start_address,
                "ABI import %s conflicts with a translated function",
                function->symbol);
            valid = false;
            continue;
        }
        binding_count++;
    }

    if (!valid) return PORPOISE_EXIT_USAGE;
    if (binding_count == 0U) return PORPOISE_EXIT_OK;
    if (binding_count > SIZE_MAX / sizeof(*analysis->import_bindings)) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "too many skipped ABI import bindings");
        return PORPOISE_EXIT_INTERNAL;
    }
    analysis->import_bindings = (PorpoiseImportBinding *)calloc(
        binding_count, sizeof(*analysis->import_bindings));
    if (analysis->import_bindings == NULL) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "out of memory while binding skipped ABI imports");
        return PORPOISE_EXIT_INTERNAL;
    }

    for (function_index = 0U;
         function_index < abi->function_count;
         function_index++) {
        const PorpoiseAbiFunction *function = &abi->functions[function_index];
        const PorpoiseFunction *owner = NULL;
        const PorpoiseAddressAlias *alias = NULL;
        uint32_t guest_address = 0U;
        size_t previous;

        if (function->kind != PORPOISE_ABI_IMPORT) continue;
        if (!porpoise_program_resolve_declared_function(
                program,
                function->symbol,
                &owner,
                &alias,
                &guest_address) ||
            owner == NULL || !owner->skipped) {
            continue;
        }

        for (previous = 0U; previous < binding_index; previous++) {
            const PorpoiseImportBinding *candidate =
                &analysis->import_bindings[previous];
            if (candidate->guest_address != guest_address) continue;
            porpoise_diagnostics_add(
                diagnostics,
                PORPOISE_SEVERITY_ERROR,
                NULL,
                0U,
                guest_address,
                "ABI imports %s and %s bind to the same skipped guest address",
                candidate->import->symbol,
                function->symbol);
            valid = false;
            break;
        }
        if (!valid) continue;

        analysis->import_bindings[binding_index].import = function;
        analysis->import_bindings[binding_index].owner = owner;
        analysis->import_bindings[binding_index].alias = alias;
        analysis->import_bindings[binding_index].guest_address = guest_address;
        binding_index++;
    }

    if (!valid) {
        free(analysis->import_bindings);
        analysis->import_bindings = NULL;
        return PORPOISE_EXIT_USAGE;
    }
    analysis->import_binding_count = binding_index;
    return PORPOISE_EXIT_OK;
}

int porpoise_analyze_program(
    const PorpoiseProgram *program,
    const PorpoiseAbiManifest *abi,
    const char *requested_entry,
    PorpoiseAnalysis *analysis,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseAnalysis candidate;
    size_t file_index;
    size_t function_index;
    int namespace_result;
    int binding_result;
    bool valid = true;
    if (program == NULL || abi == NULL || analysis == NULL || diagnostics == NULL)
        return PORPOISE_EXIT_INTERNAL;
    porpoise_analysis_init(&candidate);
    namespace_result = validate_abi_namespace(program, abi, diagnostics);
    if (namespace_result != PORPOISE_EXIT_OK) {
        return namespace_result;
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        for (function_index = 0U; function_index < file->function_count; function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            if (function->skipped) continue;
            candidate.translated_function_count++;
        }
    }
    if (candidate.translated_function_count == 0U) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, NULL, 0U, 0U,
                                 "no functions remain to translate");
        valid = false;
    }
    for (function_index = 0U; function_index < abi->function_count; function_index++) {
        const PorpoiseAbiFunction *function = &abi->functions[function_index];
        if (function->kind == PORPOISE_ABI_EXPORT &&
            porpoise_program_find_function(program, function->symbol) == NULL) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, NULL, 0U, 0U,
                                     "ABI export %s has no translated function", function->symbol);
            valid = false;
        }
    }
    if (!valid) return PORPOISE_EXIT_TRANSLATION;
    if (requested_entry != NULL && requested_entry[0] != '\0') {
        if (strcmp(requested_entry, "__start") == 0) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, requested_entry, 0U, 0U,
                                     "console __start cannot be used as the host entry; select a title function");
            return PORPOISE_EXIT_USAGE;
        }
        candidate.entry = porpoise_program_find_function(program, requested_entry);
        if (candidate.entry == NULL) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, requested_entry, 0U, 0U,
                                     "entry symbol is not a translated input function");
            return PORPOISE_EXIT_USAGE;
        }
    } else {
        size_t main_count = 0U;
        for (file_index = 0U; file_index < program->file_count; file_index++) {
            const PorpoiseSourceFile *file = &program->files[file_index];
            for (function_index = 0U; function_index < file->function_count; function_index++) {
                const PorpoiseFunction *function = &file->functions[function_index];
                if (!function->skipped &&
                    function_has_global_input_name(function, "main")) {
                    candidate.entry = function;
                    main_count++;
                }
            }
        }
        if (main_count != 1U) candidate.entry = NULL;
    }

    binding_result = build_import_bindings(
        program, abi, &candidate, diagnostics);
    if (binding_result != PORPOISE_EXIT_OK) {
        porpoise_analysis_free(&candidate);
        return binding_result;
    }

    porpoise_analysis_free(analysis);
    *analysis = candidate;
    return PORPOISE_EXIT_OK;
}
