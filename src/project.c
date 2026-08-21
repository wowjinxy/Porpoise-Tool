#include "porpoise/project.h"

#include "porpoise/analysis.h"
#include "porpoise/lower.h"
#include "porpoise/util.h"
#include "project_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ProjectDataRange {
    uint32_t address;
    uint32_t size;
    const uint8_t *bytes;
    uint8_t *owned_bytes;
    const PorpoiseDataSpan *span;
    const PorpoiseFunction *function;
    bool zero_fill;
} ProjectDataRange;

enum {
    PORPOISE_REGISTRY_SHARD_SHIFT = 16,
    PORPOISE_REGISTRY_SHARD_COUNT = 1 << PORPOISE_REGISTRY_SHARD_SHIFT
};

typedef struct PorpoiseRegistryEntry {
    uint32_t address;
    const PorpoiseFunction *function;
    const PorpoiseAbiFunction *import;
    bool trap;
} PorpoiseRegistryEntry;

static void record_failure(ProjectContext *context, int failure_code) {
    if (failure_code == PORPOISE_EXIT_OK) return;
    if (context->failure_code == PORPOISE_EXIT_INTERNAL) return;
    if (failure_code == PORPOISE_EXIT_INTERNAL || context->failure_code == PORPOISE_EXIT_OK ||
        (failure_code == PORPOISE_EXIT_IO && context->failure_code == PORPOISE_EXIT_TRANSLATION)) {
        context->failure_code = failure_code;
    }
}

void porpoise_project_options_init(PorpoiseProjectOptions *options) {
    if (options != NULL) memset(options, 0, sizeof(*options));
}

static bool project_cancelled(ProjectContext *context) {
    if (!porpoise_operation_cancelled(context->options->operation)) {
        return false;
    }
    if (!context->cancellation_reported) {
        porpoise_diagnostics_add(
            context->diagnostics,
            PORPOISE_SEVERITY_INFO,
            context->options->output_path,
            0U,
            0U,
            "project generation was cancelled before publication");
        context->cancellation_reported = true;
    }
    context->failure_code = PORPOISE_EXIT_CANCELLED;
    return true;
}

static bool checked_close(FILE *file, const char *path, PorpoiseDiagnostics *diagnostics) {
    bool failed = ferror(file) != 0;
    if (fclose(file) != 0) failed = true;
    if (failed) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, 0U, 0U,
                                 "failed while writing generated file");
        return false;
    }
    return true;
}

static FILE *open_generated_file(ProjectContext *context, const char *relative_path, char *full_path) {
    char parent[PORPOISE_PATH_CAPACITY];
    FILE *file;
    if (!porpoise_path_join(full_path, PORPOISE_PATH_CAPACITY, context->stage, relative_path) ||
        !porpoise_path_parent(parent, sizeof(parent), full_path) ||
        !porpoise_make_directories(parent, context->diagnostics)) return NULL;
    file = fopen(full_path, "wb");
    if (file == NULL) {
        porpoise_diagnostics_add(context->diagnostics, PORPOISE_SEVERITY_ERROR, full_path, 0U, 0U,
                                 "cannot create generated file: %s", strerror(errno));
    }
    return file;
}

static const PorpoiseFunctionPlanView *find_function_plan(
    const ProjectContext *context,
    const PorpoiseFunction *function) {
    size_t index;
    if (context->plan == NULL) return NULL;
    for (index = 0U;
         index < porpoise_plan_function_count(context->plan);
         index++) {
        const PorpoiseFunctionPlanView *view =
            porpoise_plan_function_at(context->plan, index);
        if (view != NULL && view->function == function) return view;
    }
    return NULL;
}

static PorpoisePlanAction function_action(
    const ProjectContext *context,
    const PorpoiseFunction *function) {
    const PorpoiseFunctionPlanView *view =
        find_function_plan(context, function);
    size_t binding_index;
    if (view != NULL) return view->action;
    if (function->data_region) return PORPOISE_PLAN_ACTION_DATA;
    if (!function->skipped) return PORPOISE_PLAN_ACTION_LIFT;
    for (binding_index = 0U;
         context->analysis != NULL &&
         binding_index < context->analysis->import_binding_count;
         binding_index++) {
        if (context->analysis->import_bindings[binding_index].owner ==
            function) {
            return PORPOISE_PLAN_ACTION_IMPORT;
        }
    }
    return PORPOISE_PLAN_ACTION_OMIT;
}

static const PorpoiseAbiFunction *function_import_binding(
    const ProjectContext *context,
    const PorpoiseFunction *function) {
    size_t index;
    if (context->analysis == NULL) return NULL;
    for (index = 0U;
         index < context->analysis->import_binding_count;
         index++) {
        if (context->analysis->import_bindings[index].owner == function) {
            return context->analysis->import_bindings[index].import;
        }
    }
    return NULL;
}

static size_t translated_function_count(const ProjectContext *context) {
    size_t count = 0U;
    size_t file_index;
    size_t function_index;
    for (file_index = 0U;
         file_index < context->program->file_count;
         file_index++) {
        const PorpoiseSourceFile *source =
            &context->program->files[file_index];
        for (function_index = 0U;
             function_index < source->function_count;
             function_index++) {
            if (function_action(context, &source->functions[function_index]) ==
                PORPOISE_PLAN_ACTION_LIFT) {
                count++;
            }
        }
    }
    return count;
}

static size_t data_word_count(const PorpoiseProgram *program) {
    size_t count = 0U;
    size_t file_index;
    for (file_index = 0U; file_index < program->file_count; file_index++)
        count += program->files[file_index].data_word_count;
    return count;
}

FILE *porpoise_project_open_generated_file(
    ProjectContext *context,
    const char *relative_path,
    char *full_path) {
    return open_generated_file(context, relative_path, full_path);
}

bool porpoise_project_checked_close(
    FILE *file,
    const char *path,
    PorpoiseDiagnostics *diagnostics) {
    return checked_close(file, path, diagnostics);
}

const PorpoiseFunctionPlanView *porpoise_project_find_function_plan(
    const ProjectContext *context,
    const PorpoiseFunction *function) {
    return find_function_plan(context, function);
}

PorpoisePlanAction porpoise_project_function_action(
    const ProjectContext *context,
    const PorpoiseFunction *function) {
    return function_action(context, function);
}

const PorpoiseAbiFunction *porpoise_project_function_import_binding(
    const ProjectContext *context,
    const PorpoiseFunction *function) {
    return function_import_binding(context, function);
}

size_t porpoise_project_translated_function_count(
    const ProjectContext *context) {
    return translated_function_count(context);
}

size_t porpoise_project_data_word_count(const PorpoiseProgram *program) {
    return data_word_count(program);
}

static bool write_generated_header(ProjectContext *context, const PorpoiseSourceFile *source) {
    char relative[PORPOISE_PATH_CAPACITY];
    char full[PORPOISE_PATH_CAPACITY];
    char guard[PORPOISE_PATH_CAPACITY];
    FILE *output;
    size_t index;
    uint32_t guard_hash = UINT32_C(2166136261);
    if (!porpoise_format(relative, sizeof(relative), "src/generated/%s.h", source->output_stem)) return false;
    porpoise_sanitize_identifier(source->output_stem, guard, sizeof(guard));
    for (index = 0U; source->relative_path[index] != '\0'; index++) {
        guard_hash ^= (unsigned char)source->relative_path[index];
        guard_hash *= UINT32_C(16777619);
    }
    output = open_generated_file(context, relative, full);
    if (output == NULL) return false;
    fprintf(output, "#ifndef PORPOISE_GENERATED_%s_%08lX_H\n#define PORPOISE_GENERATED_%s_%08lX_H\n\n",
            guard, (unsigned long)guard_hash, guard, (unsigned long)guard_hash);
    fputs("#include \"porpoise_lifted.h\"\n\n", output);
    for (index = 0U; index < source->function_count; index++) {
        const PorpoiseFunction *function = &source->functions[index];
        if (function_action(context, function) == PORPOISE_PLAN_ACTION_LIFT)
            fprintf(output, "void porpoise_lifted_%s(PorpoisePpcState *state);\n", function->c_name);
    }
    fputs("\n#endif\n", output);
    return checked_close(output, full, context->diagnostics);
}

static int write_generated_source(ProjectContext *context, const PorpoiseSourceFile *source) {
    char relative[PORPOISE_PATH_CAPACITY];
    char full[PORPOISE_PATH_CAPACITY];
    FILE *output;
    size_t index;
    int result = PORPOISE_EXIT_OK;
    PorpoiseLoweringOptions lowering_options;
    if (!porpoise_format(relative, sizeof(relative), "src/lifted/%s.c", source->output_stem))
        return PORPOISE_EXIT_IO;
    output = open_generated_file(context, relative, full);
    if (output == NULL) return PORPOISE_EXIT_IO;
    fputs("#include <math.h>\n#include <stdint.h>\n\n", output);
    fprintf(output, "#include \"generated/%s.h\"\n", source->output_stem);
    fputs("#include \"porpoise_dispatch_private.h\"\n#include \"porpoise_imports_private.h\"\n\n", output);
    lowering_options.strict = context->options->strict;
    for (index = 0U; index < source->function_count; index++) {
        const PorpoiseFunction *function = &source->functions[index];
        if (function_action(context, function) == PORPOISE_PLAN_ACTION_LIFT) {
            int lowering_result = porpoise_lower_function(
                output, context->program, source, function, context->abi,
                &lowering_options, context->report, context->diagnostics);
            if (lowering_result != PORPOISE_EXIT_OK) {
                if (result != PORPOISE_EXIT_INTERNAL &&
                    (lowering_result == PORPOISE_EXIT_INTERNAL || result == PORPOISE_EXIT_OK ||
                     (lowering_result == PORPOISE_EXIT_IO && result == PORPOISE_EXIT_TRANSLATION)))
                    result = lowering_result;
                if (lowering_result != PORPOISE_EXIT_TRANSLATION) break;
            }
        }
    }
    if (!checked_close(output, full, context->diagnostics) && result != PORPOISE_EXIT_INTERNAL)
        result = PORPOISE_EXIT_IO;
    return result;
}

static bool write_dispatch_declaration(ProjectContext *context) {
    char full[PORPOISE_PATH_CAPACITY];
    uint32_t arq_callback_hack_address = 0U;
    FILE *output = open_generated_file(context, "src/porpoise_dispatch_private.h", full);
    if (output == NULL) return false;
    fputs("#ifndef PORPOISE_DISPATCH_PRIVATE_H\n#define PORPOISE_DISPATCH_PRIVATE_H\n\n", output);
    fputs("#include <stdint.h>\n#include \"porpoise_lifted.h\"\n\n", output);
    if (porpoise_program_resolve_symbol(
            context->program,
            "__ARQCallbackHack",
            NULL,
            NULL,
            &arq_callback_hack_address)) {
        fprintf(
            output,
            "#define PORPOISE_GUEST_ARQ_CALLBACK_HACK_ADDRESS "
            "UINT32_C(0x%08lX)\n\n",
            (unsigned long)arq_callback_hack_address);
    }
    fputs(
        "enum porpoise_dispatch_kind {\n"
        "    PORPOISE_DISPATCH_NONE = 0,\n"
        "    PORPOISE_DISPATCH_LIFTED,\n"
        "    PORPOISE_DISPATCH_IMPORT,\n"
        "    PORPOISE_DISPATCH_TRAP\n"
        "};\n\n"
        "struct porpoise_dispatch_target {\n"
        "    PorpoiseLiftedFunction function;\n"
        "    enum porpoise_dispatch_kind kind;\n"
        "};\n\n",
        output);
    fputs("int porpoise_dispatch_available(uint32_t address);\n", output);
    fputs("int porpoise_call_address(PorpoisePpcState *state, uint32_t address);\n\n#endif\n", output);
    return checked_close(output, full, context->diagnostics);
}

static bool write_generated_facade(ProjectContext *context) {
    char full[PORPOISE_PATH_CAPACITY];
    FILE *output = open_generated_file(context, "include/porpoise_generated.h", full);
    if (output == NULL) return false;
    fputs("#ifndef PORPOISE_GENERATED_H\n#define PORPOISE_GENERATED_H\n\n", output);
    fputs(
        "#include \"porpoise_libporpoise_adapter.h\"\n\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n\n"
        "PorpoiseHostResult porpoise_generated_bind(\n"
        "    PorpoiseHostAdapter *host);\n\n"
        "#ifdef __cplusplus\n"
        "}\n"
        "#endif\n\n"
        "#endif\n",
        output);
    return checked_close(output, full, context->diagnostics);
}

static bool write_generated_facade_source(ProjectContext *context) {
    char full[PORPOISE_PATH_CAPACITY];
    FILE *output = open_generated_file(
        context,
        "src/porpoise_generated.c",
        full);
    if (output == NULL) return false;
    fputs(
        "#include \"porpoise_generated.h\"\n"
        "#include \"porpoise_dispatch_private.h\"\n"
        "#include \"porpoise_exports.h\"\n\n"
        "PorpoiseHostResult porpoise_generated_bind(\n"
        "    PorpoiseHostAdapter *host)\n"
        "{\n"
        "    return porpoise_libporpoise_bind_guest_runtime(\n"
        "        host, porpoise_call_address, porpoise_bind_export_state);\n"
        "}\n",
        output);
    return checked_close(output, full, context->diagnostics);
}

static bool function_has_alias_address(
    const PorpoiseFunction *function,
    uint32_t address)
{
    size_t alias_index;

    for (alias_index = 0U;
         alias_index < function->alias_count;
         alias_index++) {
        if (function->aliases[alias_index].address == address) {
            return true;
        }
    }
    return false;
}

static bool instruction_has_preceding_label(
    const PorpoiseFunction *function,
    size_t item_index)
{
    return item_index != 0U &&
           function->items[item_index - 1U].kind == PORPOISE_ASM_LABEL;
}

static bool append_registry_entry(
    PorpoiseRegistryEntry **entries,
    size_t *entry_count,
    size_t *entry_capacity,
    uint32_t address,
    const PorpoiseFunction *function,
    const PorpoiseAbiFunction *import,
    bool trap)
{
    PorpoiseRegistryEntry *entry;

    if (*entry_count == SIZE_MAX ||
        !porpoise_grow_array(
            (void **)entries,
            entry_capacity,
            sizeof(**entries),
            *entry_count + 1U)) {
        return false;
    }
    entry = &(*entries)[(*entry_count)++];
    entry->address = address;
    entry->function = function;
    entry->import = import;
    entry->trap = trap;
    return true;
}

static bool collect_registry_entries(
    const ProjectContext *context,
    PorpoiseRegistryEntry **entries,
    size_t *entry_count,
    size_t *entry_capacity)
{
    const PorpoiseProgram *program = context->program;
    size_t file_index;
    size_t function_index;

    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            size_t item_index;
            size_t alias_index;

            if (function_action(context, function) !=
                PORPOISE_PLAN_ACTION_LIFT) {
                continue;
            }
            if (!append_registry_entry(
                    entries,
                    entry_count,
                    entry_capacity,
                    function->start_address,
                    function,
                    NULL,
                    false)) {
                return false;
            }
            for (alias_index = 0U;
                 alias_index < function->alias_count;
                 alias_index++) {
                const PorpoiseAddressAlias *alias =
                    &function->aliases[alias_index];
                size_t earlier;
                bool duplicate_address =
                    alias->address == function->start_address;

                for (earlier = 0U;
                     !duplicate_address && earlier < alias_index;
                     earlier++) {
                    if (function->aliases[earlier].address == alias->address) {
                        duplicate_address = true;
                    }
                }
                if (duplicate_address) continue;
                if (!append_registry_entry(
                        entries,
                        entry_count,
                        entry_capacity,
                        alias->address,
                        function,
                        NULL,
                        false)) {
                    return false;
                }
            }
            for (item_index = 0U;
                 item_index < function->item_count;
                 item_index++) {
                const PorpoiseAsmItem *item = &function->items[item_index];

                if (item->kind != PORPOISE_ASM_INSTRUCTION ||
                    !instruction_has_preceding_label(function, item_index) ||
                    item->address == function->start_address ||
                    function_has_alias_address(function, item->address)) {
                    continue;
                }
                if (!append_registry_entry(
                        entries,
                        entry_count,
                        entry_capacity,
                        item->address,
                        function,
                        NULL,
                        false)) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool collect_import_registry_entries(
    const ProjectContext *context,
    PorpoiseRegistryEntry **entries,
    size_t *entry_count,
    size_t *entry_capacity)
{
    const PorpoiseAnalysis *analysis = context->analysis;
    size_t binding_index;

    if (context->plan != NULL) {
        for (binding_index = 0U;
             binding_index < porpoise_plan_function_count(context->plan);
             binding_index++) {
            const PorpoiseFunctionPlanView *view =
                porpoise_plan_function_at(context->plan, binding_index);
            bool trap;
            if (view == NULL) return false;
            trap = view->action == PORPOISE_PLAN_ACTION_OMIT &&
                   (view->origin == PORPOISE_PLAN_ORIGIN_SDK_POLICY ||
                    view->origin == PORPOISE_PLAN_ORIGIN_MANUAL_OVERRIDE);
            if (view->action != PORPOISE_PLAN_ACTION_IMPORT && !trap) {
                continue;
            }
            if (!append_registry_entry(
                    entries,
                    entry_count,
                    entry_capacity,
                    view->binding_address,
                    NULL,
                    view->binding,
                    trap)) {
                return false;
            }
        }
        return true;
    }
    if (analysis == NULL) return false;
    for (binding_index = 0U;
         binding_index < analysis->import_binding_count;
         binding_index++) {
        const PorpoiseImportBinding *binding =
            &analysis->import_bindings[binding_index];

        if (context->plan != NULL &&
            function_action(context, binding->owner) !=
                PORPOISE_PLAN_ACTION_IMPORT) {
            continue;
        }
        if (!append_registry_entry(
                entries,
                entry_count,
                entry_capacity,
                binding->guest_address,
                NULL,
                binding->import,
                false)) {
            return false;
        }
    }
    return true;
}

static bool write_function_registry_shard(
    ProjectContext *context,
    const PorpoiseRegistryEntry *entries,
    size_t entry_count,
    uint16_t shard)
{
    char relative[PORPOISE_PATH_CAPACITY];
    char full[PORPOISE_PATH_CAPACITY];
    FILE *output;
    const PorpoiseFunction *last_declared = NULL;
    size_t entry_index;

    if (!porpoise_format(
            relative,
            sizeof(relative),
            "src/porpoise_function_registry_%04lX.c",
            (unsigned long)shard)) {
        return false;
    }
    output = open_generated_file(context, relative, full);
    if (output == NULL) return false;
    fputs(
        "#include <stdint.h>\n"
        "#include \"porpoise_dispatch_private.h\"\n"
        "#include \"porpoise_imports_private.h\"\n"
        "\n",
        output);
    for (entry_index = 0U; entry_index < entry_count; entry_index++) {
        const PorpoiseRegistryEntry *entry = &entries[entry_index];

        if (entry->function == NULL ||
            (entry->address >> PORPOISE_REGISTRY_SHARD_SHIFT) != shard ||
            entry->function == last_declared) {
            continue;
        }
        fprintf(
            output,
            "void porpoise_lifted_%s(PorpoisePpcState *state);\n",
            entry->function->c_name);
        last_declared = entry->function;
    }
    fprintf(
        output,
        "\nstruct porpoise_dispatch_target "
        "porpoise_resolve_address_%04lX(uint32_t address)\n"
        "{\n"
        "    switch (address) {\n",
        (unsigned long)shard);
    for (entry_index = 0U; entry_index < entry_count; entry_index++) {
        const PorpoiseRegistryEntry *entry = &entries[entry_index];

        if ((entry->address >> PORPOISE_REGISTRY_SHARD_SHIFT) != shard) {
            continue;
        }
        if (entry->function != NULL) {
            fprintf(
                output,
                "    case UINT32_C(0x%08lX): return "
                "(struct porpoise_dispatch_target){porpoise_lifted_%s, "
                "PORPOISE_DISPATCH_LIFTED};\n",
                (unsigned long)entry->address,
                entry->function->c_name);
        } else if (entry->trap) {
            fprintf(
                output,
                "    case UINT32_C(0x%08lX): return "
                "(struct porpoise_dispatch_target){porpoise_omitted_sdk_trap, "
                "PORPOISE_DISPATCH_TRAP};\n",
                (unsigned long)entry->address);
        } else {
            char c_name[PORPOISE_NAME_CAPACITY];

            porpoise_sanitize_identifier(
                entry->import->symbol,
                c_name,
                sizeof(c_name));
            fprintf(
                output,
                "    case UINT32_C(0x%08lX): return "
                "(struct porpoise_dispatch_target){porpoise_import_%s, "
                "PORPOISE_DISPATCH_IMPORT};\n",
                (unsigned long)entry->address,
                c_name);
        }
    }
    fputs(
        "    default: return (struct porpoise_dispatch_target){"
        "(PorpoiseLiftedFunction)0, PORPOISE_DISPATCH_NONE};\n"
        "    }\n"
        "}\n",
        output);
    return checked_close(output, full, context->diagnostics);
}

static bool write_function_registry(ProjectContext *context) {
    char full[PORPOISE_PATH_CAPACITY];
    FILE *output;
    PorpoiseRegistryEntry *entries = NULL;
    bool *shard_used = NULL;
    size_t entry_count = 0U;
    size_t entry_capacity = 0U;
    size_t entry_index;
    size_t shard_index;

    if (!collect_registry_entries(
            context,
            &entries,
            &entry_count,
            &entry_capacity) ||
        !collect_import_registry_entries(
            context,
            &entries,
            &entry_count,
            &entry_capacity)) {
        porpoise_diagnostics_add(
            context->diagnostics,
            PORPOISE_SEVERITY_ERROR,
            context->stage,
            0U,
            0U,
            "cannot allocate generated function registry");
        record_failure(context, PORPOISE_EXIT_INTERNAL);
        free(entries);
        return false;
    }
    shard_used = (bool *)calloc(
        PORPOISE_REGISTRY_SHARD_COUNT,
        sizeof(*shard_used));
    if (shard_used == NULL) {
        porpoise_diagnostics_add(
            context->diagnostics,
            PORPOISE_SEVERITY_ERROR,
            context->stage,
            0U,
            0U,
            "cannot allocate generated function registry shards");
        record_failure(context, PORPOISE_EXIT_INTERNAL);
        free(entries);
        return false;
    }
    for (entry_index = 0U; entry_index < entry_count; entry_index++) {
        shard_used[entries[entry_index].address >>
                   PORPOISE_REGISTRY_SHARD_SHIFT] = true;
    }
    for (shard_index = 0U;
         shard_index < PORPOISE_REGISTRY_SHARD_COUNT;
         shard_index++) {
        if (shard_used[shard_index]) context->registry_shard_count++;
    }
    if (context->registry_shard_count != 0U) {
        size_t shard_cursor = 0U;

        context->registry_shards = (uint16_t *)malloc(
            context->registry_shard_count *
            sizeof(*context->registry_shards));
        if (context->registry_shards == NULL) {
            porpoise_diagnostics_add(
                context->diagnostics,
                PORPOISE_SEVERITY_ERROR,
                context->stage,
                0U,
                0U,
                "cannot allocate generated function registry shard list");
            record_failure(context, PORPOISE_EXIT_INTERNAL);
            free(shard_used);
            free(entries);
            return false;
        }
        for (shard_index = 0U;
             shard_index < PORPOISE_REGISTRY_SHARD_COUNT;
             shard_index++) {
            if (shard_used[shard_index]) {
                context->registry_shards[shard_cursor++] =
                    (uint16_t)shard_index;
            }
        }
    }

    output = open_generated_file(
        context,
        "src/porpoise_function_registry.c",
        full);
    if (output == NULL) {
        free(shard_used);
        free(entries);
        return false;
    }
    fputs("#include <stdint.h>\n#include \"porpoise_dispatch_private.h\"\n\n", output);
    for (shard_index = 0U;
         shard_index < context->registry_shard_count;
         shard_index++) {
        fprintf(
            output,
            "struct porpoise_dispatch_target "
            "porpoise_resolve_address_%04lX(uint32_t address);\n",
            (unsigned long)context->registry_shards[shard_index]);
    }
    fputs(
        "\nstatic struct porpoise_dispatch_target "
        "porpoise_resolve_address(uint32_t address)\n"
        "{\n"
        "    switch (address >> 16U) {\n",
        output);
    for (shard_index = 0U;
         shard_index < context->registry_shard_count;
         shard_index++) {
        unsigned long shard =
            (unsigned long)context->registry_shards[shard_index];

        fprintf(
            output,
            "    case UINT32_C(0x%04lX): return porpoise_resolve_address_%04lX(address);\n",
            shard,
            shard);
    }
    fputs(
        "    default: return (struct porpoise_dispatch_target){"
        "(PorpoiseLiftedFunction)0, PORPOISE_DISPATCH_NONE};\n"
        "    }\n"
        "}\n\n"
        "int porpoise_dispatch_available(uint32_t address)\n"
        "{\n"
        "    struct porpoise_dispatch_target target = porpoise_resolve_address(address);\n"
        "    return target.function != (PorpoiseLiftedFunction)0 &&\n"
        "           target.kind != PORPOISE_DISPATCH_NONE;\n"
        "}\n\n"
        "int porpoise_call_address(PorpoisePpcState *state, uint32_t address)\n"
        "{\n"
        "    struct porpoise_dispatch_target target;\n"
        "    if (porpoise_state_should_stop(state)) return 0;\n"
        "    target = porpoise_resolve_address(address);\n"
        "    if (target.function == (PorpoiseLiftedFunction)0 ||\n"
        "        target.kind == PORPOISE_DISPATCH_NONE) {\n"
        "        porpoise_state_set_fault(state, PORPOISE_FAULT_UNSUPPORTED_OPERATION, address, \"unknown lifted function address\");\n"
        "        return 0;\n"
        "    }\n"
        "    if (target.kind != PORPOISE_DISPATCH_LIFTED &&\n"
        "        target.kind != PORPOISE_DISPATCH_IMPORT &&\n"
        "        target.kind != PORPOISE_DISPATCH_TRAP) {\n"
        "        porpoise_state_set_fault(state, PORPOISE_FAULT_INVALID_STATE, address, \"invalid generated dispatch kind\");\n"
        "        return 0;\n"
        "    }\n"
        "    state->pc = address;\n"
        "    if (target.kind == PORPOISE_DISPATCH_IMPORT ||\n"
        "        target.kind == PORPOISE_DISPATCH_TRAP) {\n"
        "        target.function(state);\n"
        "        return !porpoise_state_should_stop(state);\n"
        "    }\n"
        "    if (state->lifted_call_depth == UINT32_MAX) {\n"
        "        porpoise_state_set_fault(state, PORPOISE_FAULT_INVALID_STATE, address, \"lifted call depth overflow\");\n"
        "        return 0;\n"
        "    }\n"
        "    state->lifted_call_depth++;\n"
        "    target.function(state);\n"
        "    state->lifted_call_depth--;\n"
        "    if (porpoise_state_should_stop(state)) return 0;\n"
        "    if ((state->msr & PORPOISE_MSR_EE) != 0U &&\n"
        "        !porpoise_poll_host_events(state, address)) {\n"
        "        return 0;\n"
        "    }\n"
        "    return !porpoise_state_should_stop(state);\n"
        "}\n",
        output);
    if (!checked_close(output, full, context->diagnostics)) {
        free(shard_used);
        free(entries);
        return false;
    }
    for (shard_index = 0U;
         shard_index < context->registry_shard_count;
         shard_index++) {
        if (!write_function_registry_shard(
                context,
                entries,
                entry_count,
                context->registry_shards[shard_index])) {
            free(shard_used);
            free(entries);
            return false;
        }
    }
    free(shard_used);
    free(entries);
    return true;
}

static void free_data_chunks(ProjectContext *context) {
    size_t index;
    for (index = 0U; index < context->data_chunk_count; index++) {
        free(context->data_chunks[index].bytes);
    }
    free(context->data_chunks);
    context->data_chunks = NULL;
    context->data_chunk_count = 0U;
    context->data_chunk_capacity = 0U;
}

static bool report_invalid_data_ir(
    ProjectContext *context,
    const PorpoiseDataSpan *span,
    const char *message) {
    const char *file = "";
    if (span != NULL &&
        span->source_file_index < context->program->file_count) {
        file = context->program->files[span->source_file_index].relative_path;
    }
    porpoise_diagnostics_add(
        context->diagnostics,
        PORPOISE_SEVERITY_ERROR,
        file,
        span != NULL ? span->source_line : 0U,
        span != NULL ? span->address : 0U,
        "%s",
        message);
    record_failure(context, PORPOISE_EXIT_INTERNAL);
    return false;
}

static bool begin_data_chunk(ProjectContext *context, uint32_t address) {
    ProjectDataChunk *chunk;
    size_t capacity = (size_t)(
        UINT32_C(0x10000) - (address & UINT32_C(0xFFFF)));
    if (!porpoise_grow_array(
            (void **)&context->data_chunks,
            &context->data_chunk_capacity,
            sizeof(*context->data_chunks),
            context->data_chunk_count + 1U)) {
        porpoise_diagnostics_add(
            context->diagnostics,
            PORPOISE_SEVERITY_ERROR,
            "",
            0U,
            address,
            "out of memory while preparing assembly data chunks");
        record_failure(context, PORPOISE_EXIT_INTERNAL);
        return false;
    }
    chunk = &context->data_chunks[context->data_chunk_count];
    memset(chunk, 0, sizeof(*chunk));
    chunk->bytes = (uint8_t *)malloc(capacity);
    if (chunk->bytes == NULL) {
        porpoise_diagnostics_add(
            context->diagnostics,
            PORPOISE_SEVERITY_ERROR,
            "",
            0U,
            address,
            "out of memory while preparing an assembly data chunk");
        record_failure(context, PORPOISE_EXIT_INTERNAL);
        return false;
    }
    chunk->address = address;
    chunk->capacity = capacity;
    context->data_chunk_count++;
    return true;
}

static int compare_project_data_ranges(const void *left, const void *right) {
    const ProjectDataRange *left_range = (const ProjectDataRange *)left;
    const ProjectDataRange *right_range = (const ProjectDataRange *)right;
    if (left_range->address < right_range->address) return -1;
    if (left_range->address > right_range->address) return 1;
    if (left_range->size < right_range->size) return -1;
    if (left_range->size > right_range->size) return 1;
    return 0;
}

static void free_project_data_ranges(
    ProjectDataRange *ranges,
    size_t range_count) {
    size_t index;
    for (index = 0U; index < range_count; index++) {
        free(ranges[index].owned_bytes);
    }
    free(ranges);
}

static bool prepare_function_data_range(
    ProjectContext *context,
    const PorpoiseFunction *function,
    ProjectDataRange *range) {
    uint64_t expected_address = function->start_address;
    uint64_t end_address = expected_address + function->size;
    size_t item_index;
    if (function->size == 0U || (function->size & UINT32_C(3)) != 0U ||
        end_address > (UINT64_C(1) << 32)) {
        porpoise_diagnostics_add(
            context->diagnostics, PORPOISE_SEVERITY_ERROR, NULL, 0U,
            function->start_address,
            "function %s cannot be treated as data because its byte range is invalid",
            function->name);
        record_failure(context, PORPOISE_EXIT_TRANSLATION);
        return false;
    }
    range->owned_bytes = (uint8_t *)malloc((size_t)function->size);
    if (range->owned_bytes == NULL) {
        record_failure(context, PORPOISE_EXIT_INTERNAL);
        return false;
    }
    for (item_index = 0U; item_index < function->item_count; item_index++) {
        const PorpoiseAsmItem *item = &function->items[item_index];
        size_t offset;
        if (item->kind != PORPOISE_ASM_INSTRUCTION) continue;
        if ((uint64_t)item->address != expected_address ||
            expected_address + 4U > end_address) {
            porpoise_diagnostics_add(
                context->diagnostics, PORPOISE_SEVERITY_ERROR, NULL,
                item->source_line, item->address,
                "function %s cannot be treated as data because its instruction bytes are not contiguous",
                function->name);
            record_failure(context, PORPOISE_EXIT_TRANSLATION);
            return false;
        }
        offset = (size_t)(expected_address - function->start_address);
        range->owned_bytes[offset] = (uint8_t)(item->word >> 24U);
        range->owned_bytes[offset + 1U] = (uint8_t)(item->word >> 16U);
        range->owned_bytes[offset + 2U] = (uint8_t)(item->word >> 8U);
        range->owned_bytes[offset + 3U] = (uint8_t)item->word;
        expected_address += 4U;
    }
    if (expected_address != end_address) {
        porpoise_diagnostics_add(
            context->diagnostics, PORPOISE_SEVERITY_ERROR, NULL, 0U,
            function->start_address,
            "function %s cannot be treated as data because its annotated bytes do not cover its range",
            function->name);
        record_failure(context, PORPOISE_EXIT_TRANSLATION);
        return false;
    }
    range->address = function->start_address;
    range->size = function->size;
    range->bytes = range->owned_bytes;
    range->function = function;
    return true;
}

static bool append_project_data_bytes(
    ProjectContext *context,
    uint32_t start_address,
    const uint8_t *bytes,
    size_t size) {
    size_t offset = 0U;
    while (offset < size) {
        ProjectDataChunk *chunk = NULL;
        uint32_t address = start_address + (uint32_t)offset;
        size_t remaining = size - offset;
        size_t amount;
        if (context->data_chunk_count != 0U) {
            ProjectDataChunk *candidate =
                &context->data_chunks[context->data_chunk_count - 1U];
            uint64_t candidate_end =
                (uint64_t)candidate->address + (uint64_t)candidate->size;
            if (candidate_end == (uint64_t)address &&
                candidate->size < candidate->capacity) {
                chunk = candidate;
            }
        }
        if (chunk == NULL) {
            if (!begin_data_chunk(context, address)) return false;
            chunk = &context->data_chunks[context->data_chunk_count - 1U];
        }
        amount = chunk->capacity - chunk->size;
        if (amount > remaining) amount = remaining;
        memcpy(chunk->bytes + chunk->size, bytes + offset, amount);
        chunk->size += amount;
        offset += amount;
    }
    return true;
}

static bool prepare_data_chunks(ProjectContext *context) {
    ProjectDataRange *ranges;
    size_t range_capacity = context->program->data_span_count;
    size_t range_count = 0U;
    size_t file_index;
    size_t range_index;
    uint64_t previous_end = 0U;
    bool have_previous = false;

    if (context->plan != NULL) {
        for (file_index = 0U;
             file_index < context->program->file_count;
             file_index++) {
            const PorpoiseSourceFile *file =
                &context->program->files[file_index];
            size_t function_index;
            for (function_index = 0U;
                 function_index < file->function_count;
                 function_index++) {
                const PorpoiseFunction *function =
                    &file->functions[function_index];
                if (!function->data_region &&
                    function_action(context, function) ==
                        PORPOISE_PLAN_ACTION_DATA) {
                    if (range_capacity == SIZE_MAX) return false;
                    range_capacity++;
                }
            }
        }
    }
    if (range_capacity > SIZE_MAX / sizeof(*ranges)) return false;
    ranges = range_capacity == 0U
        ? NULL
        : (ProjectDataRange *)calloc(range_capacity, sizeof(*ranges));
    if (range_capacity != 0U && ranges == NULL) return false;
    for (range_index = 0U;
         range_index < context->program->data_span_count;
         range_index++) {
        const PorpoiseDataSpan *span =
            &context->program->data_spans[range_index];
        ProjectDataRange *range = &ranges[range_count++];
        range->address = span->address;
        range->size = span->size;
        range->bytes = span->bytes;
        range->span = span;
        range->zero_fill = span->kind == PORPOISE_DATA_SPAN_ZERO_FILL;
        if (span->kind != PORPOISE_DATA_SPAN_ZERO_FILL &&
            (span->kind != PORPOISE_DATA_SPAN_INITIALIZED ||
             span->bytes == NULL)) {
            free_project_data_ranges(ranges, range_count);
            return report_invalid_data_ir(
                context, span,
                "assembly data span has an invalid kind or missing bytes");
        }
    }
    if (context->plan != NULL) {
        for (file_index = 0U;
             file_index < context->program->file_count;
             file_index++) {
            const PorpoiseSourceFile *file =
                &context->program->files[file_index];
            size_t function_index;
            for (function_index = 0U;
                 function_index < file->function_count;
                 function_index++) {
                const PorpoiseFunction *function =
                    &file->functions[function_index];
                if (function->data_region ||
                    function_action(context, function) !=
                        PORPOISE_PLAN_ACTION_DATA) {
                    continue;
                }
                if (!prepare_function_data_range(
                        context, function, &ranges[range_count])) {
                    free_project_data_ranges(ranges, range_count + 1U);
                    return false;
                }
                range_count++;
            }
        }
    }
    if (range_count > 1U) {
        qsort(ranges, range_count, sizeof(*ranges),
              compare_project_data_ranges);
    }
    for (range_index = 0U; range_index < range_count; range_index++) {
        const ProjectDataRange *range = &ranges[range_index];
        uint64_t range_end =
            (uint64_t)range->address + (uint64_t)range->size;
        if (range->size == 0U || range_end > (UINT64_C(1) << 32)) {
            bool result = range->span != NULL
                ? report_invalid_data_ir(
                      context, range->span,
                      "assembly data span is empty or exceeds the 32-bit guest address space")
                : false;
            if (range->function != NULL) {
                porpoise_diagnostics_add(
                    context->diagnostics, PORPOISE_SEVERITY_ERROR,
                    NULL, 0U, range->address,
                    "data action for %s has an invalid range",
                    range->function->name);
                record_failure(context, PORPOISE_EXIT_TRANSLATION);
            }
            free_project_data_ranges(ranges, range_count);
            return result;
        }
        if (have_previous && (uint64_t)range->address < previous_end) {
            porpoise_diagnostics_add(
                context->diagnostics, PORPOISE_SEVERITY_ERROR, NULL,
                0U, range->address,
                "data action ranges overlap existing assembly data");
            record_failure(
                context,
                range->function != NULL
                    ? PORPOISE_EXIT_TRANSLATION
                    : PORPOISE_EXIT_INTERNAL);
            free_project_data_ranges(ranges, range_count);
            return false;
        }
        have_previous = true;
        previous_end = range_end;
        if (!range->zero_fill &&
            !append_project_data_bytes(
                context, range->address, range->bytes,
                (size_t)range->size)) {
            free_project_data_ranges(ranges, range_count);
            return false;
        }
    }
    free_project_data_ranges(ranges, range_count);
    return true;
}

static bool write_data_chunk(
    ProjectContext *context,
    size_t chunk_index,
    const ProjectDataChunk *chunk) {
    char relative[PORPOISE_PATH_CAPACITY];
    char full[PORPOISE_PATH_CAPACITY];
    FILE *output;
    size_t index;

    if (!porpoise_format(
            relative, sizeof(relative),
            "src/data/porpoise_data_%04lu.c",
            (unsigned long)chunk_index)) {
        return false;
    }
    output = open_generated_file(context, relative, full);
    if (output == NULL) return false;
    fputs("#include <stddef.h>\n#include <stdint.h>\n\n"
          "#include \"porpoise_lifted.h\"\n\n"
          "static const uint8_t porpoise_assembly_data_bytes[] = {",
          output);
    for (index = 0U; index < chunk->size; index++) {
        if (index % 16U == 0U) fputs("\n    ", output);
        fprintf(output, "0x%02X", (unsigned int)chunk->bytes[index]);
        if (index + 1U != chunk->size) fputs(", ", output);
    }
    fprintf(
        output,
        "\n};\n\n"
        "int porpoise_assembly_data_chunk_%04lu(PorpoisePpcState *state)\n"
        "{\n"
        "    return porpoise_store_bytes(\n"
        "        state, UINT32_C(0x%08lX),\n"
        "        porpoise_assembly_data_bytes,\n"
        "        sizeof(porpoise_assembly_data_bytes));\n"
        "}\n",
        (unsigned long)chunk_index,
        (unsigned long)chunk->address);
    return checked_close(output, full, context->diagnostics);
}

static bool write_data_chunks(ProjectContext *context) {
    size_t chunk_index;
    for (chunk_index = 0U;
         chunk_index < context->data_chunk_count;
         chunk_index++) {
        if (!write_data_chunk(
                context, chunk_index, &context->data_chunks[chunk_index])) {
            return false;
        }
    }
    return true;
}

static bool write_data_initializer(ProjectContext *context) {
    char header_path[PORPOISE_PATH_CAPACITY];
    char source_path[PORPOISE_PATH_CAPACITY];
    FILE *header = open_generated_file(context, "src/porpoise_data_private.h", header_path);
    FILE *source;
    size_t span_index;
    size_t chunk_index;
    if (header == NULL) return false;
    fputs("#ifndef PORPOISE_DATA_PRIVATE_H\n#define PORPOISE_DATA_PRIVATE_H\n\n#include \"porpoise_lifted.h\"\n\n", header);
    fputs("void porpoise_initialize_data(PorpoisePpcState *state);\n\n#endif\n", header);
    if (!checked_close(header, header_path, context->diagnostics)) return false;
    source = open_generated_file(context, "src/porpoise_data.c", source_path);
    if (source == NULL) return false;
    fputs("#include <stddef.h>\n#include <stdint.h>\n\n"
          "#include \"porpoise_data_private.h\"\n\n", source);
    for (chunk_index = 0U;
         chunk_index < context->data_chunk_count;
         chunk_index++) {
        fprintf(source,
                "int porpoise_assembly_data_chunk_%04lu(PorpoisePpcState *state);\n",
                (unsigned long)chunk_index);
    }
    if (context->data_chunk_count != 0U) fputc('\n', source);
    fputs("void porpoise_initialize_data(PorpoisePpcState *state)\n{\n    if (porpoise_state_should_stop(state)) return;\n", source);
    for (chunk_index = 0U;
         chunk_index < context->data_chunk_count;
         chunk_index++) {
        fprintf(source,
                "    if (!porpoise_assembly_data_chunk_%04lu(state)) return;\n",
                (unsigned long)chunk_index);
    }
    for (span_index = 0U;
         span_index < context->program->data_span_count;
         span_index++) {
        const PorpoiseDataSpan *span =
            &context->program->data_spans[span_index];
        if (span->kind != PORPOISE_DATA_SPAN_ZERO_FILL) continue;
        fprintf(
            source,
            "    if (!porpoise_zero_bytes(state, UINT32_C(0x%08lX), "
            "(size_t)UINT32_C(0x%08lX))) return;\n",
            (unsigned long)span->address,
            (unsigned long)span->size);
    }
    fputs("}\n", source);
    return checked_close(source, source_path, context->diagnostics);
}

static const char *abi_c_type(PorpoiseAbiType type) {
    switch (type) {
        case PORPOISE_ABI_VOID: return "void";
        case PORPOISE_ABI_U8: return "uint8_t";
        case PORPOISE_ABI_U16: return "uint16_t";
        case PORPOISE_ABI_U32: return "uint32_t";
        case PORPOISE_ABI_S8: return "int8_t";
        case PORPOISE_ABI_S16: return "int16_t";
        case PORPOISE_ABI_S32: return "int32_t";
        case PORPOISE_ABI_F32: return "float";
        case PORPOISE_ABI_F64: return "double";
        case PORPOISE_ABI_POINTER: return "void *";
        default: return "void";
    }
}

static bool emit_abi_argument_declarations(FILE *output, const PorpoiseAbiFunction *function) {
    size_t index;
    for (index = 0U; index < function->argument_count; index++) {
        const PorpoiseAbiValue *argument = &function->arguments[index];
        const char *name = argument->name != NULL ? argument->name : "argument";
        fprintf(output, "    %s %s = ", abi_c_type(argument->type), name);
        if (argument->type == PORPOISE_ABI_POINTER)
            fprintf(output, "porpoise_decode_pointer(state, state->gpr[%u]);\n", argument->register_index);
        else if (argument->register_class == PORPOISE_ABI_REGISTER_FPR)
            fprintf(output, "(%s)porpoise_fpr_get_f64(state, %uU, 0U);\n",
                    abi_c_type(argument->type), argument->register_index);
        else
            fprintf(output, "(%s)state->gpr[%u];\n", abi_c_type(argument->type), argument->register_index);
        fputs("    if (porpoise_state_should_stop(state)) return;\n", output);
    }
    return ferror(output) == 0;
}

static void emit_argument_names(FILE *output, const PorpoiseAbiFunction *function) {
    size_t index;
    for (index = 0U; index < function->argument_count; index++) {
        if (index != 0U) fputs(", ", output);
        fputs(function->arguments[index].name, output);
    }
}

static bool write_imports(ProjectContext *context) {
    char header_path[PORPOISE_PATH_CAPACITY];
    char source_path[PORPOISE_PATH_CAPACITY];
    FILE *header = open_generated_file(context, "src/porpoise_imports_private.h", header_path);
    FILE *source;
    size_t index;
    if (header == NULL) return false;
    fputs("#ifndef PORPOISE_IMPORTS_PRIVATE_H\n#define PORPOISE_IMPORTS_PRIVATE_H\n\n#include \"porpoise_lifted.h\"\n\n", header);
    fputs(
        "void porpoise_omitted_sdk_trap(PorpoisePpcState *state);\n",
        header);
    for (index = 0U; index < context->abi->function_count; index++) {
        const PorpoiseAbiFunction *function = &context->abi->functions[index];
        char c_name[PORPOISE_NAME_CAPACITY];
        if (function->kind != PORPOISE_ABI_IMPORT) continue;
        porpoise_sanitize_identifier(function->symbol, c_name, sizeof(c_name));
        fprintf(header, "void porpoise_import_%s(PorpoisePpcState *state);\n", c_name);
    }
    fputs("\n#endif\n", header);
    if (!checked_close(header, header_path, context->diagnostics)) return false;
    source = open_generated_file(context, "src/porpoise_imports.c", source_path);
    if (source == NULL) return false;
    fputs(
        "#include <stdint.h>\n"
        "#include \"porpoise_imports_private.h\"\n"
        "#include \"porpoise_libporpoise_builtins_private.h\"\n",
        source);
    for (index = 0U; index < context->abi->function_count; index++) {
        const PorpoiseAbiFunction *function = &context->abi->functions[index];
        size_t earlier;
        bool seen = function->header != NULL &&
                    strcmp(
                        function->header,
                        "porpoise_libporpoise_builtins_private.h") == 0;
        if (function->kind != PORPOISE_ABI_IMPORT || function->header == NULL) continue;
        for (earlier = 0U; earlier < index; earlier++) {
            const PorpoiseAbiFunction *candidate = &context->abi->functions[earlier];
            if (candidate->kind == PORPOISE_ABI_IMPORT && candidate->header != NULL &&
                strcmp(candidate->header, function->header) == 0) seen = true;
        }
        if (!seen) fprintf(source, "#include <%s>\n", function->header);
    }
    fputc('\n', source);
    fputs(
        "void porpoise_omitted_sdk_trap(PorpoisePpcState *state)\n"
        "{\n"
        "    if (porpoise_state_should_stop(state)) return;\n"
        "    porpoise_state_set_fault(\n"
        "        state, PORPOISE_FAULT_UNSUPPORTED_OPERATION, state->pc,\n"
        "        \"verified SDK function was omitted without a host contract\");\n"
        "}\n\n",
        source);
    for (index = 0U; index < context->abi->function_count; index++) {
        const PorpoiseAbiFunction *function = &context->abi->functions[index];
        char c_name[PORPOISE_NAME_CAPACITY];
        if (function->kind != PORPOISE_ABI_IMPORT) continue;
        porpoise_sanitize_identifier(function->symbol, c_name, sizeof(c_name));
        if (function->adapter != NULL && function->header == NULL)
            fprintf(source, "extern void %s(PorpoisePpcState *state);\n", function->adapter);
        fprintf(source, "void porpoise_import_%s(PorpoisePpcState *state)\n{\n", c_name);
        fputs("    if (porpoise_state_should_stop(state)) return;\n", source);
        if (function->adapter != NULL) {
            fprintf(source, "    %s(state);\n", function->adapter);
        } else {
            const char *wrapper = function->wrapper != NULL ? function->wrapper : function->symbol;
            if (!emit_abi_argument_declarations(source, function)) {
                fclose(source);
                return false;
            }
            if (function->result.type == PORPOISE_ABI_VOID) {
                fprintf(source, "    %s(", wrapper);
                emit_argument_names(source, function);
                fputs(");\n", source);
            } else {
                fprintf(source, "    %s result = %s(", abi_c_type(function->result.type), wrapper);
                emit_argument_names(source, function);
                fputs(");\n", source);
                if (function->result.type == PORPOISE_ABI_POINTER)
                    fprintf(source, "    state->gpr[%u] = porpoise_encode_pointer(state, result);\n", function->result.register_index);
                else if (function->result.register_class == PORPOISE_ABI_REGISTER_FPR)
                    fprintf(source, "    porpoise_fpr_set_f64(state, %uU, 0U, (double)result);\n",
                            function->result.register_index);
                else
                    fprintf(source, "    state->gpr[%u] = (uint32_t)result;\n", function->result.register_index);
            }
        }
        fputs("}\n\n", source);
    }
    return checked_close(source, source_path, context->diagnostics);
}

static void write_typed_parameters(FILE *output, const PorpoiseAbiFunction *function) {
    size_t index;
    if (function->argument_count == 0U) {
        fputs("void", output);
        return;
    }
    for (index = 0U; index < function->argument_count; index++) {
        if (index != 0U) fputs(", ", output);
        fprintf(output, "%s %s", abi_c_type(function->arguments[index].type), function->arguments[index].name);
    }
}

static void emit_empty_export_return(FILE *output, PorpoiseAbiType type) {
    if (type == PORPOISE_ABI_VOID) fputs("        return;\n", output);
    else if (type == PORPOISE_ABI_POINTER) fputs("        return NULL;\n", output);
    else fputs("        return 0;\n", output);
}

static bool write_exports(ProjectContext *context) {
    char header_path[PORPOISE_PATH_CAPACITY];
    char source_path[PORPOISE_PATH_CAPACITY];
    FILE *header = open_generated_file(context, "include/porpoise_exports.h", header_path);
    FILE *source;
    size_t index;
    if (header == NULL) return false;
    fputs("#ifndef PORPOISE_EXPORTS_H\n#define PORPOISE_EXPORTS_H\n\n#include <stdint.h>\n#include \"porpoise_lifted.h\"\n\n", header);
    fputs("void porpoise_bind_export_state(PorpoisePpcState *state);\n", header);
    for (index = 0U; index < context->abi->function_count; index++) {
        const PorpoiseAbiFunction *function = &context->abi->functions[index];
        if (function->kind != PORPOISE_ABI_EXPORT) continue;
        fprintf(header, "%s %s(", abi_c_type(function->result.type), function->wrapper);
        write_typed_parameters(header, function);
        fputs(");\n", header);
    }
    fputs("\n#endif\n", header);
    if (!checked_close(header, header_path, context->diagnostics)) return false;
    source = open_generated_file(context, "src/porpoise_exports.c", source_path);
    if (source == NULL) return false;
    fputs("#include <stddef.h>\n#include <stdint.h>\n#include \"porpoise_exports.h\"\n#include \"porpoise_generated.h\"\n#include \"porpoise_libporpoise_adapter.h\"\n\n", source);
    for (index = 0U; index < context->abi->function_count; index++) {
        const PorpoiseAbiFunction *function = &context->abi->functions[index];
        size_t earlier;
        bool seen = false;
        if (function->kind != PORPOISE_ABI_EXPORT || function->header == NULL) continue;
        for (earlier = 0U; earlier < index; earlier++) {
            const PorpoiseAbiFunction *candidate = &context->abi->functions[earlier];
            if (candidate->kind == PORPOISE_ABI_EXPORT && candidate->header != NULL &&
                strcmp(candidate->header, function->header) == 0) seen = true;
        }
        if (!seen) fprintf(source, "#include <%s>\n", function->header);
    }
    fputc('\n', source);
    fputs("static __thread PorpoisePpcState *porpoise_export_state;\n\nvoid porpoise_bind_export_state(PorpoisePpcState *state)\n{\n    porpoise_export_state = state;\n}\n\n", source);
    for (index = 0U; index < context->abi->function_count; index++) {
        const PorpoiseAbiFunction *function = &context->abi->functions[index];
        const PorpoiseFunction *lifted;
        size_t argument_index;
        if (function->kind != PORPOISE_ABI_EXPORT) continue;
        lifted = porpoise_program_find_function(context->program, function->symbol);
        fprintf(source, "%s %s(", abi_c_type(function->result.type), function->wrapper);
        write_typed_parameters(source, function);
        fputs(")\n{\n    PorpoisePpcState *state = porpoise_export_state;\n    if (porpoise_state_should_stop(state)) {\n", source);
        emit_empty_export_return(source, function->result.type);
        fputs("    }\n", source);
        for (argument_index = 0U; argument_index < function->argument_count; argument_index++) {
            const PorpoiseAbiValue *argument = &function->arguments[argument_index];
            if (argument->type == PORPOISE_ABI_POINTER)
                fprintf(source, "    state->gpr[%u] = porpoise_encode_pointer(state, %s);\n",
                        argument->register_index, argument->name);
            else if (argument->register_class == PORPOISE_ABI_REGISTER_FPR)
                fprintf(source, "    porpoise_fpr_set_f64(state, %uU, 0U, (double)%s);\n",
                        argument->register_index, argument->name);
            else
                fprintf(source, "    state->gpr[%u] = (uint32_t)%s;\n", argument->register_index, argument->name);
        }
        fputs("    if (porpoise_state_should_stop(state)) {\n", source);
        emit_empty_export_return(source, function->result.type);
        fputs("    }\n", source);
        fprintf(source,
                "    if (!porpoise_libporpoise_run_guest(state, UINT32_C(0x%08lX))) {\n",
                (unsigned long)lifted->start_address);
        emit_empty_export_return(source, function->result.type);
        fputs("    }\n", source);
        if (function->result.type == PORPOISE_ABI_VOID) fputs("    return;\n", source);
        else if (function->result.type == PORPOISE_ABI_POINTER)
            fprintf(source, "    return porpoise_decode_pointer(state, state->gpr[%u]);\n", function->result.register_index);
        else if (function->result.register_class == PORPOISE_ABI_REGISTER_FPR)
            fprintf(source, "    return (%s)porpoise_fpr_get_f64(state, %uU, 0U);\n",
                    abi_c_type(function->result.type), function->result.register_index);
        else
            fprintf(source, "    return (%s)state->gpr[%u];\n", abi_c_type(function->result.type), function->result.register_index);
        fputs("}\n\n", source);
    }
    return checked_close(source, source_path, context->diagnostics);
}

static bool copy_runtime(ProjectContext *context) {
    static const char *sources[] = {
        "include/porpoise_lifted.h",
        "include/porpoise_libporpoise_adapter.h",
        "include/porpoise_title_host.h",
        "src/porpoise_lifted.c",
        "src/porpoise_libporpoise_adapter.c",
        "src/porpoise_libporpoise_ai.c",
        "src/porpoise_libporpoise_ar.c",
        "src/porpoise_libporpoise_arena.c",
        "src/porpoise_libporpoise_card.c",
        "src/porpoise_libporpoise_gx.c",
        "src/porpoise_libporpoise_gx_headers.h",
        "src/porpoise_libporpoise_gx_objects.c",
        "src/porpoise_libporpoise_gx_values.c",
        "src/porpoise_libporpoise_guest_os.c",
        "src/porpoise_libporpoise_message_queue.c",
        "src/porpoise_libporpoise_os_report.c",
        "src/porpoise_libporpoise_builtins_private.h",
        "src/porpoise_libporpoise_private.h"
    };
    size_t index;
    for (index = 0U; index < sizeof(sources) / sizeof(sources[0]); index++) {
        char source[PORPOISE_PATH_CAPACITY];
        char destination[PORPOISE_PATH_CAPACITY];
        char parent[PORPOISE_PATH_CAPACITY];
        if (!porpoise_path_join(source, sizeof(source), context->options->runtime_directory, sources[index]) ||
            !porpoise_path_join(destination, sizeof(destination), context->stage, sources[index]) ||
            !porpoise_path_parent(parent, sizeof(parent), destination) ||
            !porpoise_make_directories(parent, context->diagnostics) ||
            !porpoise_copy_file(source, destination, context->diagnostics)) return false;
    }
    return true;
}

static bool write_entry(ProjectContext *context) {
    char full[PORPOISE_PATH_CAPACITY];
    FILE *output;
    if (context->entry == NULL) return true;
    output = open_generated_file(context, "src/porpoise_entry.c", full);
    if (output == NULL) return false;
    fputs("#include <stdio.h>\n#include <string.h>\n#include \"porpoise_data_private.h\"\n#include \"porpoise_exports.h\"\n#include \"porpoise_generated.h\"\n#include \"porpoise_libporpoise_adapter.h\"\n#include \"porpoise_title_host.h\"\n\n", output);
    fputs("void DolphinMain(void)\n{\n    PorpoiseHostAdapter host;\n    PorpoisePpcState state;\n    PorpoiseTitleRuntimeConfigV1 runtime_config;\n    PorpoiseTitleEntryStateV3 title_state;\n    PorpoiseHostResult result;\n    uint32_t initial_word_index;\n    uint32_t startup_function_index;\n    int title_host_result;\n    int export_state_bound = 0;\n\n", output);
    fprintf(output,
            "    memset(&runtime_config, 0, sizeof(runtime_config));\n"
            "    title_host_result = PorpoiseHostPrepareRuntimeV1(\n"
            "        UINT32_C(0x%08lX), &runtime_config);\n"
            "    if (title_host_result != PORPOISE_TITLE_HOST_OK) {\n"
            "        fprintf(stderr, \"Porpoise title host failed to prepare runtime (%%d)\\n\", title_host_result);\n"
            "        return;\n"
            "    }\n"
            "    if ((runtime_config.flags & ~PORPOISE_TITLE_RUNTIME_KNOWN_FLAGS) != 0U ||\n"
            "        (runtime_config.dvd_root_directory != NULL &&\n"
            "         (runtime_config.flags & PORPOISE_TITLE_RUNTIME_INITIALIZE_DVD) == 0U)) {\n"
            "        fprintf(stderr, \"Porpoise title host returned invalid runtime configuration\\n\");\n"
            "        return;\n"
            "    }\n"
            "    result = porpoise_libporpoise_adapter_init_for_title(\n"
            "        &host,\n"
            "        runtime_config.dvd_root_directory,\n"
            "        (runtime_config.flags & PORPOISE_TITLE_RUNTIME_INITIALIZE_DVD) != 0U);\n"
            "    if (result != PORPOISE_HOST_OK) {\n"
            "        fprintf(stderr, \"Porpoise host initialization failed: %%s\\n\", porpoise_host_result_string(result));\n"
            "        return;\n"
            "    }\n"
            "    result = porpoise_generated_bind(&host);\n"
            "    if (result != PORPOISE_HOST_OK) {\n"
            "        fprintf(stderr, \"Porpoise guest dispatcher binding failed: %%s\\n\", porpoise_host_result_string(result));\n"
            "        porpoise_libporpoise_adapter_shutdown(&host);\n"
            "        return;\n"
            "    }\n",
            (unsigned long)context->entry->start_address);
    fprintf(output,
            "    porpoise_state_init(&state, &host);\n"
            "    state.pc = UINT32_C(0x%08lX);\n"
            "    porpoise_initialize_data(&state);\n"
            "    if (porpoise_state_has_fault(&state)) {\n"
            "        fprintf(stderr, \"Porpoise data initialization fault at 0x%%08lX: %%s\\n\", (unsigned long)state.fault_address, porpoise_state_fault_message(&state));\n"
            "        goto shutdown;\n"
            "    }\n"
            "    memset(&title_state, 0, sizeof(title_state));\n"
            "    title_host_result = PorpoiseHostPrepareTitleEntryV3(\n"
            "        UINT32_C(0x%08lX), &title_state);\n"
            "    if (title_host_result != PORPOISE_TITLE_HOST_OK) {\n"
            "        fprintf(stderr, \"Porpoise title host failed to prepare entry state (%%d)\\n\", title_host_result);\n"
            "        goto shutdown;\n"
            "    }\n"
            "    if (title_state.initial_word_count > PORPOISE_TITLE_HOST_INITIAL_WORD_CAPACITY) {\n"
            "        fprintf(stderr, \"Porpoise title host returned too many initial memory words\\n\");\n"
            "        goto shutdown;\n"
            "    }\n"
            "    if (title_state.startup_function_count >\n"
            "        PORPOISE_TITLE_HOST_STARTUP_FUNCTION_CAPACITY) {\n"
            "        fprintf(stderr, \"Porpoise title host returned too many startup functions\\n\");\n"
            "        goto shutdown;\n"
            "    }\n"
            "    for (startup_function_index = 0U;\n"
            "         startup_function_index < title_state.startup_function_count;\n"
            "         startup_function_index++) {\n"
            "        if (title_state.startup_functions[startup_function_index].guest_address == 0U) {\n"
            "            fprintf(stderr, \"Porpoise title host returned a null startup function\\n\");\n"
            "            goto shutdown;\n"
            "        }\n"
            "        if ((title_state.startup_functions[startup_function_index].flags &\n"
            "             ~PORPOISE_TITLE_STARTUP_KNOWN_FLAGS) != 0U) {\n"
            "            fprintf(stderr, \"Porpoise title host returned unknown startup-function flags\\n\");\n"
            "            goto shutdown;\n"
            "        }\n"
            "    }\n"
            "    result = porpoise_libporpoise_configure_title_arena(\n"
            "        &host, title_state.arena_lo, title_state.arena_hi);\n"
            "    if (result != PORPOISE_HOST_OK) {\n"
            "        fprintf(stderr, \"Porpoise title arena initialization failed: %%s\\n\", porpoise_host_result_string(result));\n"
            "        goto shutdown;\n"
            "    }\n"
            "    memcpy(state.gpr, title_state.gpr, sizeof(state.gpr));\n"
            "    for (initial_word_index = 0U;\n"
            "         initial_word_index < title_state.initial_word_count;\n"
            "         initial_word_index++) {\n"
            "        porpoise_store_u32(\n"
            "            &state,\n"
            "            title_state.initial_words[initial_word_index].guest_address,\n"
            "            title_state.initial_words[initial_word_index].value);\n"
            "        if (porpoise_state_has_fault(&state)) goto shutdown;\n"
            "    }\n"
            "    if (!porpoise_state_prepare_title_entry(&state)) {\n"
            "        fprintf(stderr, \"Porpoise title-state initialization fault at 0x%%08lX: %%s\\n\", (unsigned long)state.fault_address, porpoise_state_fault_message(&state));\n"
            "        goto shutdown;\n"
            "    }\n"
            "    porpoise_bind_export_state(&state);\n"
            "    export_state_bound = 1;\n"
            "    state.status = PORPOISE_EXECUTION_RUNNING;\n"
            "    for (startup_function_index = 0U;\n"
            "         startup_function_index < title_state.startup_function_count;\n"
            "         startup_function_index++) {\n"
            "        (void)porpoise_libporpoise_run_guest(\n"
            "            &state,\n"
            "            title_state.startup_functions[startup_function_index].guest_address);\n"
            "        if (porpoise_state_should_stop(&state)) goto shutdown;\n"
            "        if ((title_state.startup_functions[startup_function_index].flags &\n"
            "             PORPOISE_TITLE_STARTUP_ESTABLISH_GUEST_MAIN_THREAD_AFTER) != 0U) {\n"
            "            result = porpoise_libporpoise_bind_guest_main_thread(&state);\n"
            "            if (result != PORPOISE_HOST_OK) {\n"
            "                fprintf(stderr, \"Porpoise guest main-thread binding failed: %%s\\n\", porpoise_host_result_string(result));\n"
            "                goto shutdown;\n"
            "            }\n"
            "        }\n"
            "        memcpy(state.gpr, title_state.gpr, sizeof(state.gpr));\n"
            "    }\n"
            "    state.pc = UINT32_C(0x%08lX);\n",
            (unsigned long)context->entry->start_address,
            (unsigned long)context->entry->start_address,
            (unsigned long)context->entry->start_address);
    fprintf(output,
            "    (void)porpoise_libporpoise_run_guest(&state, UINT32_C(0x%08lX));\n",
            (unsigned long)context->entry->start_address);
    fputs("    if (!porpoise_state_has_fault(&state)) state.status = PORPOISE_EXECUTION_RETURNED;\n", output);
    fputs("shutdown:\n    if (export_state_bound) porpoise_bind_export_state(NULL);\n    if (porpoise_state_has_fault(&state)) {\n        fprintf(stderr, \"Porpoise execution fault at PC 0x%08lX, guest address 0x%08lX: %s\\n\", (unsigned long)state.pc, (unsigned long)state.fault_address, porpoise_state_fault_message(&state));\n    }\n    porpoise_libporpoise_adapter_shutdown(&host);\n}\n", output);
    return checked_close(output, full, context->diagnostics);
}

static void meson_write_string(FILE *output, const char *value) {
    const unsigned char *cursor = (const unsigned char *)value;
    fputc('\'', output);
    while (*cursor != '\0') {
        if (*cursor == '\\' || *cursor == '\'') fputc('\\', output);
        fputc(*cursor, output);
        cursor++;
    }
    fputc('\'', output);
}

static bool write_meson(ProjectContext *context) {
    char full[PORPOISE_PATH_CAPACITY];
    FILE *output = open_generated_file(context, "meson.build", full);
    size_t file_index;
    if (output == NULL) return false;
    fputs("project(", output); meson_write_string(output, context->project_name);
    fputs(", 'c', version: '1.0.0', default_options: ['c_std=c99', 'warning_level=3', 'werror=true'])\n\n", output);
    fputs("libporpoise_raw_dep = dependency('libPorpoise', fallback: ['libPorpoise','libporpoise_dep'])\n", output);
    fputs("libporpoise_dep = libporpoise_raw_dep.partial_dependency(\n", output);
    fputs("  compile_args: false,\n", output);
    fputs("  includes: true,\n", output);
    fputs("  link_args: true,\n", output);
    fputs("  links: true,\n", output);
    fputs("  sources: true,\n", output);
    fputs(").as_system('system')\n\n", output);
    fputs("if host_machine.system() == 'windows'\n", output);
    fputs("  porpoise_consumer_c_args = [\n", output);
    fputs("    '-DLIBPORPOISE_PORT',\n", output);
    fputs("    '-DLIBPORPOISE_BUILD_WIN64',\n", output);
    fputs("  ]\n", output);
    fputs("elif host_machine.system() == 'linux'\n", output);
    fputs("  porpoise_consumer_c_args = [\n", output);
    fputs("    '-DLIBPORPOISE_PORT',\n", output);
    fputs("    '-DLIBPORPOISE_BUILD_LINUX',\n", output);
    fputs("    '-D_POSIX_C_SOURCE=200112L',\n", output);
    fputs("  ]\n", output);
    fputs("else\n", output);
    fputs("  error('Porpoise generated projects support only Windows and Linux hosts')\n", output);
    fputs("endif\n", output);
    fputs("porpoise_consumer_c_args += [\n", output);
    fputs("  '-DPORPOISE_AUTODETECT_LIBPORPOISE_HOST_THREAD_CARRIER_V1=1',\n", output);
    fputs("]\n\n", output);
    fputs("cc = meson.get_compiler('c')\nmath_dep = cc.find_library('m', required: false)\n\n", output);
    fputs("generated_inc = include_directories('include')\ngenerated_private_inc = include_directories('src')\n\nlifted_sources = files(\n", output);
    fputs("  'src/porpoise_lifted.c',\n  'src/porpoise_libporpoise_adapter.c',\n  'src/porpoise_libporpoise_ai.c',\n  'src/porpoise_libporpoise_ar.c',\n  'src/porpoise_libporpoise_arena.c',\n  'src/porpoise_libporpoise_card.c',\n  'src/porpoise_libporpoise_gx.c',\n  'src/porpoise_libporpoise_gx_objects.c',\n  'src/porpoise_libporpoise_gx_values.c',\n  'src/porpoise_libporpoise_guest_os.c',\n  'src/porpoise_libporpoise_message_queue.c',\n  'src/porpoise_libporpoise_os_report.c',\n  'src/porpoise_function_registry.c'", output);
    for (file_index = 0U;
         file_index < context->registry_shard_count;
         file_index++) {
        fprintf(
            output,
            ",\n  'src/porpoise_function_registry_%04lX.c'",
            (unsigned long)context->registry_shards[file_index]);
    }
    fputs(",\n  'src/porpoise_data.c',\n  'src/porpoise_generated.c',\n  'src/porpoise_imports.c',\n  'src/porpoise_exports.c'", output);
    for (file_index = 0U;
         file_index < context->data_chunk_count;
         file_index++) {
        fprintf(
            output,
            ",\n  'src/data/porpoise_data_%04lu.c'",
            (unsigned long)file_index);
    }
    for (file_index = 0U; file_index < context->program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        size_t function_index;
        bool has_function = false;
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            if (function_action(context, &file->functions[function_index]) ==
                PORPOISE_PLAN_ACTION_LIFT) {
                has_function = true;
            }
        }
        if (has_function) {
            char path[PORPOISE_PATH_CAPACITY];
            porpoise_format(path, sizeof(path), "src/lifted/%s.c", file->output_stem);
            fputs(",\n  ", output); meson_write_string(output, path);
        }
    }
    fputs("\n)\n\n", output);
    fputs("porpoise_lifted_library = static_library(\n  'porpoise_lifted',\n  lifted_sources,\n  include_directories: [generated_inc, generated_private_inc],\n  dependencies: [libporpoise_dep, math_dep],\n  c_args: porpoise_consumer_c_args,\n  install: false,\n)\n\n", output);
    fputs("porpoise_lifted_dep = declare_dependency(\n  link_with: porpoise_lifted_library,\n  include_directories: generated_inc,\n  dependencies: [libporpoise_dep, math_dep],\n)\nmeson.override_dependency('porpoise-generated', porpoise_lifted_dep)\n", output);
    if (context->entry != NULL) {
        fputs("\nporpoise_title_contract_dep = declare_dependency(include_directories: generated_inc)\nmeson.override_dependency('porpoise-title-contract', porpoise_title_contract_dep)\n", output);
        fputs("\nporpoise_title_host_dep = dependency('porpoise-title-host', fallback: ['porpoise-title-host', 'porpoise_title_host_dep'])\n", output);
        fputs("porpoise_executable = executable(\n  'porpoise_title',\n  'src/porpoise_entry.c',\n  include_directories: generated_private_inc,\n  dependencies: [porpoise_lifted_dep, porpoise_title_host_dep],\n  c_args: porpoise_consumer_c_args,\n  install: false,\n)\n", output);
    }
    return checked_close(output, full, context->diagnostics);
}


static bool write_project_readme(ProjectContext *context) {
    char full[PORPOISE_PATH_CAPACITY];
    FILE *output = open_generated_file(context, "README.md", full);
    if (output == NULL) return false;
    fputs("# Generated Porpoise project\n\nThis directory contains lifted PowerPC title code. It is generated; edit the assembly or ABI manifest and regenerate instead of editing lifted sources.\n\nProvide libPorpoise either as an installed Meson dependency or as `subprojects/libPorpoise`. No wrap or checkout is generated.\n\n", output);
    fputs("The generated C sources materialize initialized data and zero-fill ranges exclusively from the annotated assembly input. No linked executable image is embedded or required at build or run time. Any linker-synthesized data used by the title must be provided explicitly as annotated assembly.\n\n", output);
    if (context->entry != NULL) {
        fputs("This output includes `porpoise_title`. Its consumer must also provide the `porpoise-title-host` Meson dependency implementing `PorpoiseHostPrepareRuntimeV1` and `PorpoiseHostPrepareTitleEntryV3` from `include/porpoise_title_host.h`. The runtime hook supplies host configuration before libPorpoise initialization, including an optional DVD root/bootstrap request. The entry hook supplies linked arena bounds, the direct-entry GPR image, optional startup words, and an ordered list of guest-only lifted initializers after `OSInit` and assembly-data initialization.\n\n", output);
    }
    fputs("```sh\nmeson setup build\nmeson compile -C build\n```\n\nThe currently evolving checkout defaults its version-specific `build_target` option to `gc`. When using that checkout as a subproject, add `-DlibPorpoise:build_target=linux` or `-DlibPorpoise:build_target=win64` to the setup command.\n\nSee `porpoise-report.json` for the measured lowering status of each instruction. Lowered does not imply complete PowerPC ISA verification.\n", output);
    return checked_close(output, full, context->diagnostics);
}

static bool generate_stage(ProjectContext *context) {
    size_t file_index;
    bool ok = true;
    context->report->source_count = context->program->file_count;
    context->report->function_count = translated_function_count(context);
    porpoise_operation_progress(
        context->options->operation,
        PORPOISE_PHASE_GENERATE,
        0U,
        context->program->file_count + 1U,
        context->stage);
    if (project_cancelled(context) ||
        !porpoise_make_directories(context->stage, context->diagnostics) ||
        !copy_runtime(context) || project_cancelled(context) ||
        !write_dispatch_declaration(context) ||
        !write_generated_facade(context) ||
        !write_generated_facade_source(context) ||
        !write_function_registry(context) || project_cancelled(context) ||
        !write_data_chunks(context) ||
        !write_data_initializer(context) ||
        !write_imports(context) || !write_exports(context) ||
        project_cancelled(context)) return false;
    for (file_index = 0U; file_index < context->program->file_count; file_index++) {
        const PorpoiseSourceFile *source = &context->program->files[file_index];
        int source_result;
        if (project_cancelled(context)) return false;
        if (!write_generated_header(context, source)) {
            record_failure(context, PORPOISE_EXIT_IO);
            ok = false;
            continue;
        }
        source_result = write_generated_source(context, source);
        if (source_result != PORPOISE_EXIT_OK) {
            record_failure(context, source_result);
            ok = false;
        }
        porpoise_operation_progress(
            context->options->operation,
            PORPOISE_PHASE_GENERATE,
            file_index + 1U,
            context->program->file_count + 1U,
            source->relative_path);
    }
    if (!ok || porpoise_diagnostics_have_errors(context->diagnostics) ||
        project_cancelled(context)) return false;
    if (!write_entry(context) || !write_meson(context) ||
        !porpoise_project_write_report(context) ||
        !write_project_readme(context) ||
        project_cancelled(context)) return false;
    porpoise_operation_progress(
        context->options->operation,
        PORPOISE_PHASE_GENERATE,
        context->program->file_count + 1U,
        context->program->file_count + 1U,
        context->stage);
    return true;
}

bool porpoise_project_generation_cancelled(ProjectContext *context) {
    return project_cancelled(context);
}

bool porpoise_project_prepare_generation(ProjectContext *context) {
    return prepare_data_chunks(context);
}

bool porpoise_project_generate_artifacts(ProjectContext *context) {
    return generate_stage(context);
}

void porpoise_project_release_generation(ProjectContext *context) {
    free(context->registry_shards);
    context->registry_shards = NULL;
    free_data_chunks(context);
}


int porpoise_project_generate_plan(
    const PorpoiseTranslationPlan *plan,
    const PorpoiseProjectOptions *options,
    PorpoiseReport *report,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseStagedProject *staged = NULL;
    int result = porpoise_project_stage_plan(
        plan, options, report, &staged, diagnostics);
    if (result == PORPOISE_EXIT_OK) {
        result = porpoise_project_publish_staged(staged, diagnostics);
    }
    porpoise_staged_project_free(staged);
    return result;
}

int porpoise_project_generate(
    const PorpoiseProgram *program,
    const PorpoiseAbiManifest *abi,
    const PorpoiseProjectOptions *options,
    PorpoiseReport *report,
    PorpoiseDiagnostics *diagnostics) {
    ProjectContext context;
    PorpoiseAnalysis analysis;
    PorpoiseStagedProject *staged = NULL;
    int result;

    if (program == NULL || abi == NULL || options == NULL ||
        options->output_path == NULL || options->runtime_directory == NULL ||
        report == NULL || diagnostics == NULL) {
        return PORPOISE_EXIT_INTERNAL;
    }
    memset(&context, 0, sizeof(context));
    context.program = program;
    context.abi = abi;
    context.options = options;
    context.report = report;
    context.diagnostics = diagnostics;
    result = porpoise_project_prepare_output(&context);
    if (result != PORPOISE_EXIT_OK) return result;

    porpoise_analysis_init(&analysis);
    result = porpoise_analyze_program(
        program, abi, options->entry_symbol, &analysis, diagnostics);
    if (result != PORPOISE_EXIT_OK) {
        porpoise_analysis_free(&analysis);
        return result;
    }
    context.analysis = &analysis;
    context.entry = analysis.entry;
    result = porpoise_project_stage_context(&context, &staged);
    porpoise_analysis_free(&analysis);
    if (result == PORPOISE_EXIT_OK) {
        result = porpoise_project_publish_staged(staged, diagnostics);
    }
    porpoise_staged_project_free(staged);
    return result;
}
