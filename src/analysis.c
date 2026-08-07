#include "porpoise/analysis.h"
#include "porpoise/util.h"

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
        /* Generated project entry points and helpers. */
        "main",
        "DolphinMain",
        "porpoise_call_address",
        "porpoise_bind_export_state",
        "porpoise_export_state",
        "porpoise_initialize_data",

        /* Public lifted-runtime functions. */
        "porpoise_state_init",
        "porpoise_state_clear_fault",
        "porpoise_state_set_fault",
        "porpoise_state_has_fault",
        "porpoise_state_fault_message",
        "porpoise_fault_string",
        "porpoise_host_result_string",
        "porpoise_cr_get_field",
        "porpoise_cr_set_field",
        "porpoise_cr_get_bit",
        "porpoise_cr_set_bit",
        "porpoise_shift_left32",
        "porpoise_shift_right32",
        "porpoise_sign_extend8",
        "porpoise_sign_extend16",
        "porpoise_count_leading_zeros32",
        "porpoise_add_with_carry32",
        "porpoise_arithmetic_shift_right32",
        "porpoise_rotate_left32",
        "porpoise_mask32",
        "porpoise_set_cr0_result",
        "porpoise_compare_signed",
        "porpoise_compare_unsigned",
        "porpoise_load_u8",
        "porpoise_load_u16",
        "porpoise_load_u32",
        "porpoise_load_u64",
        "porpoise_load_f32",
        "porpoise_load_f64",
        "porpoise_load_multiple_words",
        "porpoise_store_u8",
        "porpoise_store_u16",
        "porpoise_store_u32",
        "porpoise_store_u64",
        "porpoise_store_f32",
        "porpoise_store_f64",
        "porpoise_store_multiple_words",
        "porpoise_decode_pointer",
        "porpoise_encode_pointer",
        "porpoise_libporpoise_adapter_init",

        /* Runtime ordinary-identifier namespace (typedefs and enumerators). */
        "PorpoisePpcState",
        "PorpoiseHostAdapter",
        "PorpoiseFpr",
        "PorpoiseHostResult",
        "PorpoiseFault",
        "PorpoiseExecutionStatus",
        "PorpoiseHostReadBytesFn",
        "PorpoiseHostWriteBytesFn",
        "PorpoiseHostDecodePointerFn",
        "PorpoiseHostEncodePointerFn",
        "PorpoiseLiftedFunction",
        "PORPOISE_HOST_OK",
        "PORPOISE_HOST_INVALID_ARGUMENT",
        "PORPOISE_HOST_INVALID_POINTER",
        "PORPOISE_HOST_UNMAPPED_ADDRESS",
        "PORPOISE_HOST_UNSUPPORTED_MMIO",
        "PORPOISE_HOST_ADDRESS_OVERFLOW",
        "PORPOISE_HOST_IO_ERROR",
        "PORPOISE_FAULT_NONE",
        "PORPOISE_FAULT_INVALID_STATE",
        "PORPOISE_FAULT_NO_HOST_ADAPTER",
        "PORPOISE_FAULT_MISSING_HOST_CALLBACK",
        "PORPOISE_FAULT_INVALID_ARGUMENT",
        "PORPOISE_FAULT_INVALID_POINTER",
        "PORPOISE_FAULT_UNMAPPED_ADDRESS",
        "PORPOISE_FAULT_UNSUPPORTED_MMIO",
        "PORPOISE_FAULT_ADDRESS_OVERFLOW",
        "PORPOISE_FAULT_HOST_IO",
        "PORPOISE_FAULT_UNSUPPORTED_OPERATION",
        "PORPOISE_EXECUTION_READY",
        "PORPOISE_EXECUTION_RUNNING",
        "PORPOISE_EXECUTION_RETURNED",
        "PORPOISE_EXECUTION_FAULTED",

        /* Macros emitted by generated and runtime headers. */
        "PORPOISE_FAULT_MESSAGE_CAPACITY",
        "PORPOISE_LIFTED_H",
        "PORPOISE_LIBPORPOISE_ADAPTER_H",
        "PORPOISE_GENERATED_H",
        "PORPOISE_DATA_H",
        "PORPOISE_IMPORTS_H",
        "PORPOISE_EXPORTS_H"
    };
    size_t index;

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

int porpoise_analyze_program(
    const PorpoiseProgram *program,
    const PorpoiseAbiManifest *abi,
    const char *requested_entry,
    PorpoiseAnalysis *analysis,
    PorpoiseDiagnostics *diagnostics) {
    size_t file_index;
    size_t function_index;
    int namespace_result;
    bool valid = true;
    if (program == NULL || abi == NULL || analysis == NULL || diagnostics == NULL)
        return PORPOISE_EXIT_INTERNAL;
    memset(analysis, 0, sizeof(*analysis));
    namespace_result = validate_abi_namespace(program, abi, diagnostics);
    if (namespace_result != PORPOISE_EXIT_OK) {
        return namespace_result;
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        for (function_index = 0U; function_index < file->function_count; function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            size_t other_file;
            if (function->skipped) continue;
            analysis->translated_function_count++;
            for (other_file = 0U; other_file <= file_index; other_file++) {
                const PorpoiseSourceFile *candidate_file = &program->files[other_file];
                size_t limit = other_file == file_index ? function_index : candidate_file->function_count;
                size_t other_function;
                for (other_function = 0U; other_function < limit; other_function++) {
                    const PorpoiseFunction *candidate = &candidate_file->functions[other_function];
                    if (!candidate->skipped && candidate->start_address == function->start_address) {
                        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR,
                                                 file->relative_path, 0U, function->start_address,
                                                 "function address collides with %s", candidate->name);
                        valid = false;
                    }
                }
            }
        }
    }
    if (analysis->translated_function_count == 0U) {
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
        analysis->entry = porpoise_program_find_function(program, requested_entry);
        if (analysis->entry == NULL) {
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
                if (!function->skipped && function->is_global && strcmp(function->name, "main") == 0) {
                    analysis->entry = function;
                    main_count++;
                }
            }
        }
        if (main_count != 1U) analysis->entry = NULL;
    }
    return PORPOISE_EXIT_OK;
}
