#include "porpoise/project.h"

#include "porpoise/analysis.h"
#include "porpoise/lower.h"
#include "porpoise/util.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <process.h>
#define PORPOISE_GETPID() _getpid()
#else
#include <unistd.h>
#define PORPOISE_GETPID() getpid()
#endif

typedef struct ProjectDataChunk {
    uint32_t address;
    size_t size;
    size_t capacity;
    uint8_t *bytes;
} ProjectDataChunk;

typedef struct ProjectContext {
    const PorpoiseProgram *program;
    const PorpoiseAbiManifest *abi;
    const PorpoiseProjectOptions *options;
    PorpoiseReport *report;
    PorpoiseDiagnostics *diagnostics;
    const PorpoiseAnalysis *analysis;
    const PorpoiseFunction *entry;
    int failure_code;
    char stage[PORPOISE_PATH_CAPACITY];
    char project_name[PORPOISE_NAME_CAPACITY];
    uint16_t *registry_shards;
    size_t registry_shard_count;
    ProjectDataChunk *data_chunks;
    size_t data_chunk_count;
    size_t data_chunk_capacity;
} ProjectContext;

enum {
    PORPOISE_REGISTRY_SHARD_SHIFT = 16,
    PORPOISE_REGISTRY_SHARD_COUNT = 1 << PORPOISE_REGISTRY_SHARD_SHIFT
};

typedef struct PorpoiseRegistryEntry {
    uint32_t address;
    const PorpoiseFunction *function;
    const PorpoiseAbiFunction *import;
} PorpoiseRegistryEntry;

static void record_failure(ProjectContext *context, int failure_code) {
    if (failure_code == PORPOISE_EXIT_OK) return;
    if (context->failure_code == PORPOISE_EXIT_INTERNAL) return;
    if (failure_code == PORPOISE_EXIT_INTERNAL || context->failure_code == PORPOISE_EXIT_OK ||
        (failure_code == PORPOISE_EXIT_IO && context->failure_code == PORPOISE_EXIT_TRANSLATION)) {
        context->failure_code = failure_code;
    }
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

static size_t translated_function_count(const PorpoiseProgram *program) {
    size_t count = 0U;
    size_t file_index;
    size_t function_index;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        for (function_index = 0U; function_index < program->files[file_index].function_count; function_index++) {
            if (!program->files[file_index].functions[function_index].skipped) count++;
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

static bool make_unique_sibling(
    const char *output,
    const char *tag,
    char *path,
    PorpoiseDiagnostics *diagnostics) {
    char parent[PORPOISE_PATH_CAPACITY];
    char base[PORPOISE_PATH_CAPACITY];
    unsigned int attempt;
    unsigned long seed = (unsigned long)time(NULL) ^ (unsigned long)PORPOISE_GETPID();
    if (!porpoise_path_parent(parent, sizeof(parent), output) ||
        !porpoise_path_basename(base, sizeof(base), output) ||
        !porpoise_make_directories(parent, diagnostics)) return false;
    for (attempt = 0U; attempt < 1000U; attempt++) {
        char name[PORPOISE_PATH_CAPACITY];
        if (!porpoise_format(name, sizeof(name), ".%s.porpoise-%s-%08lx-%u",
                             base, tag, seed, attempt) ||
            !porpoise_path_join(path, PORPOISE_PATH_CAPACITY, parent, name)) return false;
        if (!porpoise_path_exists(path)) return true;
    }
    porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, output, 0U, 0U,
                             "cannot allocate a sibling staging path");
    return false;
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
        if (!function->skipped)
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
        if (!function->skipped) {
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
        "    PORPOISE_DISPATCH_IMPORT\n"
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
    const PorpoiseAbiFunction *import)
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
    return true;
}

static bool collect_registry_entries(
    const PorpoiseProgram *program,
    PorpoiseRegistryEntry **entries,
    size_t *entry_count,
    size_t *entry_capacity)
{
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

            if (function->skipped) continue;
            if (!append_registry_entry(
                    entries,
                    entry_count,
                    entry_capacity,
                    function->start_address,
                    function,
                    NULL)) {
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
                        NULL)) {
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
                        NULL)) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool collect_import_registry_entries(
    const PorpoiseAnalysis *analysis,
    PorpoiseRegistryEntry **entries,
    size_t *entry_count,
    size_t *entry_capacity)
{
    size_t binding_index;

    for (binding_index = 0U;
         binding_index < analysis->import_binding_count;
         binding_index++) {
        const PorpoiseImportBinding *binding =
            &analysis->import_bindings[binding_index];

        if (!append_registry_entry(
                entries,
                entry_count,
                entry_capacity,
                binding->guest_address,
                NULL,
                binding->import)) {
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
            context->program,
            &entries,
            &entry_count,
            &entry_capacity) ||
        !collect_import_registry_entries(
            context->analysis,
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
        "        target.kind != PORPOISE_DISPATCH_IMPORT) {\n"
        "        porpoise_state_set_fault(state, PORPOISE_FAULT_INVALID_STATE, address, \"invalid generated dispatch kind\");\n"
        "        return 0;\n"
        "    }\n"
        "    state->pc = address;\n"
        "    if (target.kind == PORPOISE_DISPATCH_IMPORT) {\n"
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

static bool prepare_data_chunks(ProjectContext *context) {
    size_t span_index;
    uint64_t previous_end = 0U;
    bool have_previous = false;

    for (span_index = 0U;
         span_index < context->program->data_span_count;
         span_index++) {
        const PorpoiseDataSpan *span =
            &context->program->data_spans[span_index];
        uint64_t span_end = (uint64_t)span->address + (uint64_t)span->size;
        size_t offset = 0U;

        if (span->size == 0U) {
            return report_invalid_data_ir(
                context, span, "assembly data IR contains an empty span");
        }
        if (span_end > (UINT64_C(1) << 32)) {
            return report_invalid_data_ir(
                context, span, "assembly data span exceeds the 32-bit guest address space");
        }
        if (have_previous && (uint64_t)span->address < previous_end) {
            return report_invalid_data_ir(
                context, span, "assembly data spans overlap or are not sorted");
        }
        have_previous = true;
        previous_end = span_end;

        if (span->kind == PORPOISE_DATA_SPAN_ZERO_FILL) continue;
        if (span->kind != PORPOISE_DATA_SPAN_INITIALIZED || span->bytes == NULL) {
            return report_invalid_data_ir(
                context, span, "assembly data span has an invalid kind or missing bytes");
        }

        while (offset < (size_t)span->size) {
            ProjectDataChunk *chunk = NULL;
            uint32_t address = span->address + (uint32_t)offset;
            size_t remaining = (size_t)span->size - offset;
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
            memcpy(chunk->bytes + chunk->size, span->bytes + offset, amount);
            chunk->size += amount;
            offset += amount;
        }
    }
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
        for (function_index = 0U; function_index < file->function_count; function_index++)
            if (!file->functions[function_index].skipped) has_function = true;
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

static void write_data_fixups(
    FILE *output,
    const PorpoiseDataObject *object) {
    size_t fixup_index;
    fputc('[', output);
    for (fixup_index = 0U;
         fixup_index < object->fixup_count;
         fixup_index++) {
        const PorpoiseDataFixup *fixup = &object->fixups[fixup_index];
        if (fixup_index != 0U) fputc(',', output);
        fputs("{\"kind\": ", output);
        porpoise_json_write_string(
            output,
            fixup->kind == PORPOISE_DATA_FIXUP_ABSOLUTE_32
                ? "absolute_32"
                : "rel_target_32");
        fprintf(
            output,
            ", \"line\": %lu, \"offset\": %lu, "
            "\"width\": %u, \"target\": ",
            (unsigned long)fixup->source_line,
            (unsigned long)fixup->offset,
            (unsigned int)fixup->width);
        if (fixup->target_symbol == NULL) {
            fputs("null", output);
        } else {
            porpoise_json_write_string(output, fixup->target_symbol);
        }
        fprintf(output, ", \"target_addend\": %lld, \"base\": ",
                (long long)fixup->target_addend);
        if (fixup->base_symbol == NULL) {
            fputs("null", output);
        } else {
            porpoise_json_write_string(output, fixup->base_symbol);
        }
        fprintf(output, ", \"base_addend\": %lld}",
                (long long)fixup->base_addend);
    }
    fputc(']', output);
}

static bool write_report(ProjectContext *context) {
    char full[PORPOISE_PATH_CAPACITY];
    FILE *output = open_generated_file(context, "porpoise-report.json", full);
    size_t file_index;
    size_t function_index;
    size_t object_index;
    size_t anonymous_index;
    size_t span_index;
    size_t instruction_index;
    size_t diagnostic_index;
    size_t data_object_count = 0U;
    size_t anonymous_data_count = 0U;
    size_t data_fixup_count = 0U;
    uint64_t anonymous_explicit_bytes = 0U;
    uint64_t initialized_data_bytes = 0U;
    uint64_t zero_fill_data_bytes = 0U;
    bool first;
    if (output == NULL) return false;
    for (file_index = 0U;
         file_index < context->program->file_count;
         file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        data_object_count += file->data_object_count;
        for (object_index = 0U;
             object_index < file->data_object_count;
             object_index++) {
            data_fixup_count += file->data_objects[object_index].fixup_count;
        }
        anonymous_data_count += file->anonymous_data_count;
        for (anonymous_index = 0U;
             anonymous_index < file->anonymous_data_count;
             anonymous_index++) {
            const PorpoiseAnonymousData *anonymous =
                &file->anonymous_data[anonymous_index];
            size_t byte_index;
            data_fixup_count += anonymous->storage.fixup_count;
            for (byte_index = 0U;
                 byte_index < (size_t)anonymous->storage.size;
                 byte_index++) {
                if (anonymous->present[byte_index] != 0U) {
                    anonymous_explicit_bytes++;
                }
            }
        }
    }
    for (span_index = 0U;
         span_index < context->program->data_span_count;
         span_index++) {
        const PorpoiseDataSpan *span =
            &context->program->data_spans[span_index];
        if (span->kind == PORPOISE_DATA_SPAN_INITIALIZED) {
            initialized_data_bytes += (uint64_t)span->size;
        } else if (span->kind == PORPOISE_DATA_SPAN_ZERO_FILL) {
            zero_fill_data_bytes += (uint64_t)span->size;
        }
    }

    fprintf(
        output,
        "{\n  \"schema_version\": 2,\n"
        "  \"data_model\": {\"source\": \"annotated_assembly\", "
        "\"chunks\": %lu},\n"
        "  \"files\": [\n",
        (unsigned long)context->data_chunk_count);
    for (file_index = 0U; file_index < context->program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        fputs(file_index == 0U ? "    {\"input\": " : ",\n    {\"input\": ", output);
        porpoise_json_write_string(output, file->relative_path);
        fputs(", \"generated\": ", output);
        {
            char path[PORPOISE_PATH_CAPACITY];
            porpoise_format(path, sizeof(path), "src/lifted/%s.c", file->output_stem);
            porpoise_json_write_string(output, path);
        }
        fputc('}', output);
    }
    fputs("\n  ],\n  \"functions\": [\n", output);
    first = true;
    for (file_index = 0U; file_index < context->program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        for (function_index = 0U; function_index < file->function_count; function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            const char *status;
            size_t binding_index;
            bool imported = false;

            for (binding_index = 0U;
                 binding_index < context->analysis->import_binding_count;
                 binding_index++) {
                if (context->analysis->import_bindings[binding_index].owner ==
                    function) {
                    imported = true;
                    break;
                }
            }
            status = function->data_region
                         ? "data"
                         : (function->skipped
                                ? (imported ? "imported" : "skipped")
                                : "lifted");
            fputs(first ? "    {\"symbol\": " : ",\n    {\"symbol\": ", output); first = false;
            porpoise_json_write_string(output, function->name);
            fputs(", \"c_symbol\": ", output); porpoise_json_write_string(output, function->c_name);
            fputs(", \"file\": ", output); porpoise_json_write_string(output, file->relative_path);
            fprintf(output, ", \"address\": %lu, \"size\": %lu, \"status\": \"%s\"}",
                    (unsigned long)function->start_address, (unsigned long)function->size,
                    status);
        }
    }
    fputs("\n  ],\n  \"data_objects\": [\n", output);
    first = true;
    for (file_index = 0U; file_index < context->program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        for (object_index = 0U;
             object_index < file->data_object_count;
             object_index++) {
            const PorpoiseDataObject *object =
                &file->data_objects[object_index];
            fputs(first ? "    {\"symbol\": " : ",\n    {\"symbol\": ", output);
            first = false;
            porpoise_json_write_string(output, object->name);
            fputs(", \"file\": ", output);
            porpoise_json_write_string(output, file->relative_path);
            fputs(", \"section\": ", output);
            porpoise_json_write_string(output, object->section);
            fprintf(
                output,
                ", \"binding\": \"%s\", \"metadata_line\": %lu, "
                "\"line\": %lu, \"end_line\": %lu, "
                "\"section_offset\": %lu, \"address\": %lu, "
                "\"size\": %lu, \"fixups\": ",
                object->is_global ? "global" : "local",
                (unsigned long)object->metadata_line,
                (unsigned long)object->source_line,
                (unsigned long)object->end_source_line,
                (unsigned long)object->section_offset,
                (unsigned long)object->address,
                (unsigned long)object->size);
            write_data_fixups(output, object);
            fputc('}', output);
        }
    }
    fputs("\n  ],\n  \"anonymous_data\": [\n", output);
    first = true;
    for (file_index = 0U;
         file_index < context->program->file_count;
         file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        for (anonymous_index = 0U;
             anonymous_index < file->anonymous_data_count;
             anonymous_index++) {
            const PorpoiseAnonymousData *anonymous =
                &file->anonymous_data[anonymous_index];
            const PorpoiseDataObject *storage = &anonymous->storage;
            size_t byte_index;
            uint64_t explicit_bytes = 0U;
            uint64_t initialized_bytes = 0U;
            uint64_t zero_fill_bytes = 0U;
            for (byte_index = 0U;
                 byte_index < (size_t)storage->size;
                 byte_index++) {
                if (anonymous->present[byte_index] == 0U) continue;
                explicit_bytes++;
                if (storage->initialized[byte_index] != 0U) {
                    initialized_bytes++;
                } else {
                    zero_fill_bytes++;
                }
            }
            fputs(first ? "    {\"file\": " : ",\n    {\"file\": ", output);
            first = false;
            porpoise_json_write_string(output, file->relative_path);
            fputs(", \"section\": ", output);
            porpoise_json_write_string(output, storage->section);
            fprintf(
                output,
                ", \"line\": %lu, \"address\": %lu, \"size\": %lu, "
                "\"explicit_bytes\": %llu, \"initialized_bytes\": %llu, "
                "\"zero_fill_bytes\": %llu, \"fixups\": ",
                (unsigned long)storage->source_line,
                (unsigned long)storage->address,
                (unsigned long)storage->size,
                (unsigned long long)explicit_bytes,
                (unsigned long long)initialized_bytes,
                (unsigned long long)zero_fill_bytes);
            write_data_fixups(output, storage);
            fputc('}', output);
        }
    }
    fputs("\n  ],\n  \"data_spans\": [\n", output);
    for (span_index = 0U;
         span_index < context->program->data_span_count;
         span_index++) {
        const PorpoiseDataSpan *span =
            &context->program->data_spans[span_index];
        const PorpoiseSourceFile *source_file = NULL;
        const PorpoiseDataObject *source_object = NULL;
        if (span->source_file_index < context->program->file_count) {
            source_file = &context->program->files[span->source_file_index];
            if (span->data_object_index < source_file->data_object_count) {
                source_object = &source_file->data_objects[span->data_object_index];
            }
        }
        fputs(span_index == 0U ? "    {\"kind\": " : ",\n    {\"kind\": ", output);
        porpoise_json_write_string(
            output,
            span->kind == PORPOISE_DATA_SPAN_INITIALIZED
                ? "initialized"
                : "zero_fill");
        fprintf(
            output,
            ", \"address\": %lu, \"size\": %lu, \"file\": ",
            (unsigned long)span->address,
            (unsigned long)span->size);
        if (source_file == NULL) {
            fputs("null", output);
        } else {
            porpoise_json_write_string(output, source_file->relative_path);
        }
        fputs(", \"object\": ", output);
        if (source_object == NULL) {
            fputs("null", output);
        } else {
            porpoise_json_write_string(output, source_object->name);
        }
        fprintf(
            output,
            ", \"line\": %lu, \"contribution_padding\": %s}",
            (unsigned long)span->source_line,
            span->contribution_padding ? "true" : "false");
    }
    fputs("\n  ],\n  \"instructions\": [\n", output);
    for (instruction_index = 0U; instruction_index < context->report->instruction_count; instruction_index++) {
        const PorpoiseInstructionReport *item = &context->report->instructions[instruction_index];
        fputs(instruction_index == 0U ? "    {\"file\": " : ",\n    {\"file\": ", output);
        porpoise_json_write_string(output, item->file);
        fprintf(output, ", \"line\": %lu, \"address\": %lu, \"mnemonic\": ",
                (unsigned long)item->line, (unsigned long)item->address);
        porpoise_json_write_string(output, item->mnemonic);
        fputs(", \"status\": ", output); porpoise_json_write_string(output, porpoise_lowering_status_name(item->status));
        fprintf(output, ", \"semantic_test\": %s, \"detail\": ", item->semantic_test ? "true" : "false");
        porpoise_json_write_string(output, item->detail == NULL ? "" : item->detail);
        fputc('}', output);
    }
    fputs("\n  ],\n  \"approximations\": [", output);
    first = true;
    for (instruction_index = 0U; instruction_index < context->report->instruction_count; instruction_index++) {
        const PorpoiseInstructionReport *item = &context->report->instructions[instruction_index];
        if (item->status == PORPOISE_APPROXIMATE) {
            if (!first) fputc(',', output);
            fputs("\n    {\"file\": ", output); porpoise_json_write_string(output, item->file);
            fprintf(output, ", \"line\": %lu, \"address\": %lu, \"mnemonic\": ",
                    (unsigned long)item->line, (unsigned long)item->address);
            porpoise_json_write_string(output, item->mnemonic); fputc('}', output); first = false;
        }
    }
    if (!first) fputc('\n', output);
    fputs("  ],\n  \"diagnostics\": [", output);
    for (diagnostic_index = 0U; diagnostic_index < context->diagnostics->count; diagnostic_index++) {
        const PorpoiseDiagnostic *item = &context->diagnostics->items[diagnostic_index];
        if (diagnostic_index != 0U) fputc(',', output);
        fputs("\n    {\"severity\": ", output);
        porpoise_json_write_string(output, item->severity == PORPOISE_SEVERITY_ERROR ? "error" :
                                          item->severity == PORPOISE_SEVERITY_WARNING ? "warning" : "info");
        fputs(", \"file\": ", output); porpoise_json_write_string(output, item->file);
        fprintf(output, ", \"line\": %lu, \"address\": %lu, \"message\": ",
                (unsigned long)item->line, (unsigned long)item->address);
        porpoise_json_write_string(output, item->message); fputc('}', output);
    }
    if (context->diagnostics->count != 0U) fputc('\n', output);
    fputs("  ],\n  \"summary\": {", output);
    fprintf(output, "\"files\": %lu, \"functions\": %lu, \"data_words\": %lu, \"data_objects\": %lu, \"anonymous_contributions\": %lu, \"anonymous_explicit_bytes\": %llu, \"data_fixups\": %lu, \"data_spans\": %lu, \"initialized_data_bytes\": %llu, \"zero_fill_data_bytes\": %llu, \"data_chunks\": %lu, \"lowered\": %lu, \"host_equivalent_noop\": %lu, \"approximate\": %lu, \"unsupported\": %lu}",
            (unsigned long)context->program->file_count,
            (unsigned long)translated_function_count(context->program),
            (unsigned long)data_word_count(context->program),
            (unsigned long)data_object_count,
            (unsigned long)anonymous_data_count,
            (unsigned long long)anonymous_explicit_bytes,
            (unsigned long)data_fixup_count,
            (unsigned long)context->program->data_span_count,
            (unsigned long long)initialized_data_bytes,
            (unsigned long long)zero_fill_data_bytes,
            (unsigned long)context->data_chunk_count,
            (unsigned long)context->report->status_counts[PORPOISE_LOWERED],
            (unsigned long)context->report->status_counts[PORPOISE_HOST_NOOP],
            (unsigned long)context->report->status_counts[PORPOISE_APPROXIMATE],
            (unsigned long)context->report->status_counts[PORPOISE_UNSUPPORTED]);
    fputs("\n}\n", output);
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
    context->report->function_count = translated_function_count(context->program);
    if (!porpoise_make_directories(context->stage, context->diagnostics) ||
        !copy_runtime(context) || !write_dispatch_declaration(context) ||
        !write_generated_facade(context) ||
        !write_generated_facade_source(context) ||
        !write_function_registry(context) ||
        !write_data_chunks(context) ||
        !write_data_initializer(context) ||
        !write_imports(context) || !write_exports(context)) return false;
    for (file_index = 0U; file_index < context->program->file_count; file_index++) {
        const PorpoiseSourceFile *source = &context->program->files[file_index];
        int source_result;
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
    }
    if (!ok || porpoise_diagnostics_have_errors(context->diagnostics)) return false;
    return write_entry(context) && write_meson(context) && write_report(context) && write_project_readme(context);
}

static bool publish_stage(ProjectContext *context) {
    char backup[PORPOISE_PATH_CAPACITY];
    const char *output = context->options->output_path;
    bool had_output = porpoise_path_exists(output);
    if (had_output) {
        if (!make_unique_sibling(output, "backup", backup, context->diagnostics) ||
            !porpoise_move_path(output, backup, context->diagnostics)) return false;
    }
    if (!porpoise_move_path(context->stage, output, context->diagnostics)) {
        if (had_output) {
            if (!porpoise_move_path(backup, output, context->diagnostics))
                porpoise_diagnostics_add(context->diagnostics, PORPOISE_SEVERITY_ERROR, output, 0U, 0U,
                                         "publication failed and the previous output could not be restored from %s", backup);
        }
        return false;
    }
    if (had_output) {
        PorpoiseDiagnostics cleanup_diagnostics;
        porpoise_diagnostics_init(&cleanup_diagnostics);
        if (!porpoise_remove_tree(backup, &cleanup_diagnostics)) {
            porpoise_diagnostics_add(context->diagnostics, PORPOISE_SEVERITY_WARNING, backup, 0U, 0U,
                                     "generated output was published, but its recoverable backup could not be removed");
        }
        porpoise_diagnostics_free(&cleanup_diagnostics);
    }
    return true;
}

int porpoise_project_generate(
    const PorpoiseProgram *program,
    const PorpoiseAbiManifest *abi,
    const PorpoiseProjectOptions *options,
    PorpoiseReport *report,
    PorpoiseDiagnostics *diagnostics) {
    ProjectContext context;
    PorpoiseAnalysis analysis;
    char base[PORPOISE_PATH_CAPACITY];
    bool generated;
    int analysis_result;
    if (program == NULL || abi == NULL || options == NULL || options->output_path == NULL ||
        options->runtime_directory == NULL || report == NULL || diagnostics == NULL) return PORPOISE_EXIT_INTERNAL;
    porpoise_analysis_init(&analysis);
    memset(&context, 0, sizeof(context));
    context.program = program;
    context.abi = abi;
    context.options = options;
    context.report = report;
    context.diagnostics = diagnostics;
    if (porpoise_path_exists(options->output_path)) {
        if (!porpoise_path_is_directory(options->output_path)) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, options->output_path, 0U, 0U,
                                     "output path exists and is not a directory");
            return PORPOISE_EXIT_IO;
        }
        if (!porpoise_directory_is_empty(options->output_path) && !options->force) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, options->output_path, 0U, 0U,
                                     "output directory is not empty; use --force to replace it");
            return PORPOISE_EXIT_USAGE;
        }
    }
    if (!porpoise_path_basename(base, sizeof(base), options->output_path)) return PORPOISE_EXIT_INTERNAL;
    porpoise_sanitize_identifier(base, context.project_name, sizeof(context.project_name));
    analysis_result = porpoise_analyze_program(program, abi, options->entry_symbol,
                                               &analysis, diagnostics);
    if (analysis_result != PORPOISE_EXIT_OK) {
        porpoise_analysis_free(&analysis);
        return analysis_result;
    }
    context.analysis = &analysis;
    context.entry = analysis.entry;
    if (!prepare_data_chunks(&context)) {
        int result = context.failure_code != PORPOISE_EXIT_OK
                         ? context.failure_code
                         : PORPOISE_EXIT_INTERNAL;
        free_data_chunks(&context);
        porpoise_analysis_free(&analysis);
        return result;
    }
    if (!make_unique_sibling(options->output_path, "stage", context.stage, diagnostics)) {
        free_data_chunks(&context);
        porpoise_analysis_free(&analysis);
        return PORPOISE_EXIT_IO;
    }
    generated = generate_stage(&context);
    if (!generated) {
        free(context.registry_shards);
        free_data_chunks(&context);
        porpoise_analysis_free(&analysis);
        if (!porpoise_remove_tree(context.stage, diagnostics)) return PORPOISE_EXIT_IO;
        return context.failure_code != PORPOISE_EXIT_OK
                   ? context.failure_code
                   : PORPOISE_EXIT_IO;
    }
    if (!publish_stage(&context)) {
        free(context.registry_shards);
        free_data_chunks(&context);
        porpoise_analysis_free(&analysis);
        if (porpoise_path_exists(context.stage)) (void)porpoise_remove_tree(context.stage, diagnostics);
        return PORPOISE_EXIT_IO;
    }
    free(context.registry_shards);
    free_data_chunks(&context);
    porpoise_analysis_free(&analysis);
    return PORPOISE_EXIT_OK;
}
