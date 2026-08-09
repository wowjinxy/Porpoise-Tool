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

typedef struct ProjectContext {
    const PorpoiseProgram *program;
    const PorpoiseAbiManifest *abi;
    const PorpoiseProjectOptions *options;
    PorpoiseReport *report;
    PorpoiseDiagnostics *diagnostics;
    const PorpoiseFunction *entry;
    int failure_code;
    char stage[PORPOISE_PATH_CAPACITY];
    char project_name[PORPOISE_NAME_CAPACITY];
} ProjectContext;

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
    if (!porpoise_format(relative, sizeof(relative), "include/porpoise/generated/%s.h", source->output_stem)) return false;
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
    fprintf(output, "#include \"porpoise/generated/%s.h\"\n", source->output_stem);
    fputs("#include \"porpoise_dispatch.h\"\n#include \"porpoise_imports.h\"\n\n", output);
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
    FILE *output = open_generated_file(context, "include/porpoise_dispatch.h", full);
    if (output == NULL) return false;
    fputs("#ifndef PORPOISE_DISPATCH_H\n#define PORPOISE_DISPATCH_H\n\n", output);
    fputs("#include <stdint.h>\n#include \"porpoise_lifted.h\"\n\n", output);
    fputs("int porpoise_dispatch_available(uint32_t address);\n", output);
    fputs("int porpoise_call_address(PorpoisePpcState *state, uint32_t address);\n\n#endif\n", output);
    return checked_close(output, full, context->diagnostics);
}

static bool write_function_declarations(ProjectContext *context) {
    char full[PORPOISE_PATH_CAPACITY];
    FILE *output = open_generated_file(context, "include/porpoise_generated.h", full);
    size_t file_index;
    size_t function_index;
    if (output == NULL) return false;
    fputs("#ifndef PORPOISE_GENERATED_H\n#define PORPOISE_GENERATED_H\n\n", output);
    fputs("#include \"porpoise_dispatch.h\"\n\n", output);
    for (file_index = 0U; file_index < context->program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        for (function_index = 0U; function_index < file->function_count; function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            if (!function->skipped)
                fprintf(output, "void porpoise_lifted_%s(PorpoisePpcState *state);\n", function->c_name);
        }
    }
    fputs("\n#endif\n", output);
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

static bool write_function_registry(ProjectContext *context) {
    char full[PORPOISE_PATH_CAPACITY];
    FILE *output = open_generated_file(context, "src/porpoise_function_registry.c", full);
    size_t file_index;
    size_t function_index;
    if (output == NULL) return false;
    fputs("#include <stdint.h>\n#include \"porpoise_generated.h\"\n\n", output);
    fputs("static PorpoiseLiftedFunction porpoise_resolve_address(uint32_t address)\n{\n    switch (address) {\n", output);
    for (file_index = 0U; file_index < context->program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        for (function_index = 0U; function_index < file->function_count; function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            size_t item_index;
            size_t alias_index;
            if (function->skipped) continue;
            fprintf(output,
                    "    case UINT32_C(0x%08lX): return porpoise_lifted_%s;\n",
                    (unsigned long)function->start_address,
                    function->c_name);
            for (alias_index = 0U; alias_index < function->alias_count; alias_index++) {
                const PorpoiseAddressAlias *alias = &function->aliases[alias_index];
                size_t earlier;
                bool duplicate_address = alias->address == function->start_address;
                for (earlier = 0U; !duplicate_address && earlier < alias_index; earlier++) {
                    if (function->aliases[earlier].address == alias->address)
                        duplicate_address = true;
                }
                if (duplicate_address) continue;
                fprintf(output,
                        "    case UINT32_C(0x%08lX): return porpoise_lifted_%s;\n",
                        (unsigned long)alias->address,
                        function->c_name);
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
                fprintf(output,
                        "    case UINT32_C(0x%08lX): return porpoise_lifted_%s;\n",
                        (unsigned long)item->address,
                        function->c_name);
            }
        }
    }
    fputs(
        "    default: return (PorpoiseLiftedFunction)0;\n"
        "    }\n"
        "}\n\n"
        "int porpoise_dispatch_available(uint32_t address)\n"
        "{\n"
        "    return porpoise_resolve_address(address) != (PorpoiseLiftedFunction)0;\n"
        "}\n\n"
        "int porpoise_call_address(PorpoisePpcState *state, uint32_t address)\n"
        "{\n"
        "    PorpoiseLiftedFunction function;\n"
        "    if (porpoise_state_should_stop(state)) return 0;\n"
        "    function = porpoise_resolve_address(address);\n"
        "    if (function == (PorpoiseLiftedFunction)0) {\n"
        "        porpoise_state_set_fault(state, PORPOISE_FAULT_UNSUPPORTED_OPERATION, address, \"unknown lifted function address\");\n"
        "        return 0;\n"
        "    }\n"
        "    state->pc = address;\n"
        "    function(state);\n"
        "    return !porpoise_state_should_stop(state);\n"
        "}\n",
        output);
    return checked_close(output, full, context->diagnostics);
}

static bool write_data_initializer(ProjectContext *context) {
    char header_path[PORPOISE_PATH_CAPACITY];
    char source_path[PORPOISE_PATH_CAPACITY];
    FILE *header = open_generated_file(context, "include/porpoise_data.h", header_path);
    FILE *source;
    size_t file_index;
    if (header == NULL) return false;
    fputs("#ifndef PORPOISE_DATA_H\n#define PORPOISE_DATA_H\n\n#include \"porpoise_lifted.h\"\n\n", header);
    fputs("void porpoise_initialize_data(PorpoisePpcState *state);\n\n#endif\n", header);
    if (!checked_close(header, header_path, context->diagnostics)) return false;
    source = open_generated_file(context, "src/porpoise_data.c", source_path);
    if (source == NULL) return false;
    fputs("#include <stdint.h>\n#include \"porpoise_data.h\"\n\n", source);
    fputs("void porpoise_initialize_data(PorpoisePpcState *state)\n{\n    if (porpoise_state_should_stop(state)) return;\n", source);
    for (file_index = 0U; file_index < context->program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        size_t data_index;
        for (data_index = 0U; data_index < file->data_word_count; data_index++) {
            const PorpoiseDataWord *word = &file->data_words[data_index];
            fprintf(source, "    porpoise_store_u32(state, UINT32_C(0x%08lX), UINT32_C(0x%08lX));\n",
                    (unsigned long)word->address, (unsigned long)word->word);
            fputs("    if (porpoise_state_has_fault(state)) return;\n", source);
        }
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
    FILE *header = open_generated_file(context, "include/porpoise_imports.h", header_path);
    FILE *source;
    size_t index;
    if (header == NULL) return false;
    fputs("#ifndef PORPOISE_IMPORTS_H\n#define PORPOISE_IMPORTS_H\n\n#include \"porpoise_lifted.h\"\n\n", header);
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
    fputs("#include <stdint.h>\n#include \"porpoise_imports.h\"\n", source);
    for (index = 0U; index < context->abi->function_count; index++) {
        const PorpoiseAbiFunction *function = &context->abi->functions[index];
        size_t earlier;
        bool seen = false;
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
    fputs("#include <stddef.h>\n#include <stdint.h>\n#include \"porpoise_exports.h\"\n#include \"porpoise_generated.h\"\n\n", source);
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
    fputs("static PorpoisePpcState *porpoise_export_state;\n\nvoid porpoise_bind_export_state(PorpoisePpcState *state)\n{\n    porpoise_export_state = state;\n}\n\n", source);
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
                "    if (!porpoise_call_address(state, UINT32_C(0x%08lX))) {\n",
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
        "src/porpoise_libporpoise_adapter.c"
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
    fputs("#include <stdio.h>\n#include \"porpoise_data.h\"\n#include \"porpoise_exports.h\"\n#include \"porpoise_generated.h\"\n#include \"porpoise_libporpoise_adapter.h\"\n#include \"porpoise_title_host.h\"\n\n", output);
    fputs("void DolphinMain(void)\n{\n    PorpoiseHostAdapter host;\n    PorpoisePpcState state;\n    PorpoiseHostResult result;\n    int title_host_result;\n\n", output);
    fputs("    result = porpoise_libporpoise_adapter_init(&host);\n    if (result != PORPOISE_HOST_OK) {\n        fprintf(stderr, \"Porpoise host initialization failed: %s\\n\", porpoise_host_result_string(result));\n        return;\n    }\n", output);
    fprintf(output,
            "    porpoise_state_init(&state, &host);\n"
            "    state.pc = UINT32_C(0x%08lX);\n"
            "    title_host_result = PorpoiseHostPrepareTitleEntryV1(\n"
            "        UINT32_C(0x%08lX), state.gpr);\n"
            "    if (title_host_result != PORPOISE_TITLE_HOST_OK) {\n"
            "        fprintf(stderr, \"Porpoise title host failed to prepare entry state (%%d)\\n\", title_host_result);\n"
            "        porpoise_libporpoise_adapter_shutdown(&host);\n"
            "        return;\n"
            "    }\n"
            "    if (!porpoise_state_prepare_title_entry(&state)) {\n"
            "        fprintf(stderr, \"Porpoise title-state initialization fault at 0x%%08lX: %%s\\n\", (unsigned long)state.fault_address, porpoise_state_fault_message(&state));\n"
            "        porpoise_libporpoise_adapter_shutdown(&host);\n"
            "        return;\n"
            "    }\n"
            "    porpoise_initialize_data(&state);\n"
            "    if (porpoise_state_has_fault(&state)) {\n"
            "        fprintf(stderr, \"Porpoise data initialization fault at 0x%%08lX: %%s\\n\", (unsigned long)state.fault_address, porpoise_state_fault_message(&state));\n"
            "        porpoise_libporpoise_adapter_shutdown(&host);\n"
            "        return;\n"
            "    }\n"
            "    porpoise_bind_export_state(&state);\n",
            (unsigned long)context->entry->start_address,
            (unsigned long)context->entry->start_address);
    fprintf(output,
            "    state.status = PORPOISE_EXECUTION_RUNNING;\n"
            "    (void)porpoise_call_address(&state, UINT32_C(0x%08lX));\n",
            (unsigned long)context->entry->start_address);
    fputs("    if (!porpoise_state_has_fault(&state)) state.status = PORPOISE_EXECUTION_RETURNED;\n", output);
    fputs("    porpoise_bind_export_state(NULL);\n    if (porpoise_state_has_fault(&state)) {\n        fprintf(stderr, \"Porpoise execution fault at 0x%08lX: %s\\n\", (unsigned long)state.fault_address, porpoise_state_fault_message(&state));\n    }\n    porpoise_libporpoise_adapter_shutdown(&host);\n}\n", output);
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
    fputs("libporpoise_dep = dependency('libPorpoise', fallback: ['libPorpoise', 'libporpoise_dep'])\n", output);
    fputs("cc = meson.get_compiler('c')\nmath_dep = cc.find_library('m', required: false)\n", output);
    fputs("generated_inc = include_directories('include')\n\nlifted_sources = files(\n", output);
    fputs("  'src/porpoise_lifted.c',\n  'src/porpoise_libporpoise_adapter.c',\n  'src/porpoise_function_registry.c',\n  'src/porpoise_data.c',\n  'src/porpoise_imports.c',\n  'src/porpoise_exports.c'", output);
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
    fputs("porpoise_lifted_library = static_library(\n  'porpoise_lifted',\n  lifted_sources,\n  include_directories: generated_inc,\n  dependencies: [libporpoise_dep, math_dep],\n  install: false,\n)\n\n", output);
    fputs("porpoise_lifted_dep = declare_dependency(\n  link_with: porpoise_lifted_library,\n  include_directories: generated_inc,\n  dependencies: [libporpoise_dep, math_dep],\n)\nmeson.override_dependency('porpoise-generated', porpoise_lifted_dep)\n", output);
    if (context->entry != NULL) {
        fputs("\nporpoise_title_host_dep = dependency('porpoise-title-host', fallback: ['porpoise-title-host', 'porpoise_title_host_dep'])\n", output);
        fputs("porpoise_executable = executable(\n  'porpoise_title',\n  'src/porpoise_entry.c',\n  dependencies: [porpoise_lifted_dep, porpoise_title_host_dep],\n  install: false,\n)\n", output);
    }
    return checked_close(output, full, context->diagnostics);
}

static bool write_report(ProjectContext *context) {
    char full[PORPOISE_PATH_CAPACITY];
    FILE *output = open_generated_file(context, "porpoise-report.json", full);
    size_t file_index;
    size_t function_index;
    size_t data_index;
    size_t instruction_index;
    size_t diagnostic_index;
    bool first;
    if (output == NULL) return false;
    fputs("{\n  \"schema_version\": 1,\n  \"files\": [\n", output);
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
            fputs(first ? "    {\"symbol\": " : ",\n    {\"symbol\": ", output); first = false;
            porpoise_json_write_string(output, function->name);
            fputs(", \"c_symbol\": ", output); porpoise_json_write_string(output, function->c_name);
            fputs(", \"file\": ", output); porpoise_json_write_string(output, file->relative_path);
            fprintf(output, ", \"address\": %lu, \"size\": %lu, \"status\": \"%s\"}",
                    (unsigned long)function->start_address, (unsigned long)function->size,
                    function->skipped ? "skipped" : "lifted");
        }
    }
    fputs("\n  ],\n  \"data\": [\n", output);
    first = true;
    for (file_index = 0U; file_index < context->program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        for (data_index = 0U; data_index < file->data_word_count; data_index++) {
            const PorpoiseDataWord *word = &file->data_words[data_index];
            fputs(first ? "    {\"file\": " : ",\n    {\"file\": ", output); first = false;
            porpoise_json_write_string(output, file->relative_path);
            fprintf(output, ", \"line\": %lu, \"address\": %lu, \"word\": %lu, \"directive\": ",
                    (unsigned long)word->source_line, (unsigned long)word->address,
                    (unsigned long)word->word);
            porpoise_json_write_string(output, word->directive);
            fputc('}', output);
        }
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
    fprintf(output, "\"files\": %lu, \"functions\": %lu, \"data_words\": %lu, \"lowered\": %lu, \"host_equivalent_noop\": %lu, \"approximate\": %lu, \"unsupported\": %lu}",
            (unsigned long)context->program->file_count,
            (unsigned long)translated_function_count(context->program),
            (unsigned long)data_word_count(context->program),
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
    if (context->entry != NULL) {
        fputs("This output includes `porpoise_title`. Its consumer must also provide the `porpoise-title-host` Meson dependency implementing `PorpoiseHostPrepareTitleEntryV1` from `include/porpoise_title_host.h`; that versioned hook supplies the initial GPR image after `OSInit`.\n\n", output);
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
        !write_function_declarations(context) ||
        !write_function_registry(context) || !write_data_initializer(context) ||
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
    if (analysis_result != PORPOISE_EXIT_OK) return analysis_result;
    context.entry = analysis.entry;
    if (!make_unique_sibling(options->output_path, "stage", context.stage, diagnostics)) return PORPOISE_EXIT_IO;
    generated = generate_stage(&context);
    if (!generated) {
        if (!porpoise_remove_tree(context.stage, diagnostics)) return PORPOISE_EXIT_IO;
        return context.failure_code != PORPOISE_EXIT_OK ? context.failure_code : PORPOISE_EXIT_IO;
    }
    if (!publish_stage(&context)) {
        if (porpoise_path_exists(context.stage)) (void)porpoise_remove_tree(context.stage, diagnostics);
        return PORPOISE_EXIT_IO;
    }
    return PORPOISE_EXIT_OK;
}
