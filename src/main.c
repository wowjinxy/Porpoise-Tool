#include "porpoise/abi.h"
#include "porpoise/options.h"
#include "porpoise/program.h"
#include "porpoise/project.h"
#include "porpoise/report.h"
#include "porpoise/util.h"

#include <stdio.h>

#ifndef PORPOISE_RUNTIME_DIR
#define PORPOISE_RUNTIME_DIR "runtime"
#endif
#ifndef PORPOISE_INSTALLED_RUNTIME_DIR
#define PORPOISE_INSTALLED_RUNTIME_DIR "runtime"
#endif

static const char *select_runtime_directory(void) {
    const char *candidates[] = {
        PORPOISE_RUNTIME_DIR,
        PORPOISE_INSTALLED_RUNTIME_DIR
    };
    char probe[PORPOISE_PATH_CAPACITY];
    size_t index;
    for (index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); index++) {
        if (porpoise_path_join(probe, sizeof(probe), candidates[index],
                               "include/porpoise_lifted.h") &&
            porpoise_path_exists(probe)) return candidates[index];
    }
    return PORPOISE_RUNTIME_DIR;
}

static void print_diagnostics(
    const PorpoiseDiagnostics *diagnostics,
    PorpoiseVerbosity verbosity) {
    size_t index;
    for (index = 0U; index < diagnostics->count; index++) {
        const PorpoiseDiagnostic *item = &diagnostics->items[index];
        const char *severity = item->severity == PORPOISE_SEVERITY_ERROR ? "error" :
                               item->severity == PORPOISE_SEVERITY_WARNING ? "warning" : "info";
        if (verbosity == PORPOISE_VERBOSITY_QUIET && item->severity != PORPOISE_SEVERITY_ERROR) continue;
        if (item->file != NULL && item->file[0] != '\0') fprintf(stderr, "%s", item->file);
        else fputs("porpoise", stderr);
        if (item->line != 0U) fprintf(stderr, ":%lu", (unsigned long)item->line);
        if (item->address != 0U) fprintf(stderr, " [0x%08lX]", (unsigned long)item->address);
        fprintf(stderr, ": %s: %s\n", severity, item->message);
    }
}

int main(int argc, char **argv) {
    PorpoiseOptions options;
    PorpoiseProgram program;
    PorpoiseAbiManifest abi;
    PorpoiseReport report;
    PorpoiseDiagnostics diagnostics;
    PorpoiseProjectOptions project_options;
    char normalized_output[PORPOISE_PATH_CAPACITY];
    bool output_contains_working_directory;
    bool paths_overlap;
    int result;

    porpoise_options_init(&options);
    result = porpoise_options_parse(&options, argc, argv, stderr);
    if (result != PORPOISE_EXIT_OK) return result;
    if (options.show_help) {
        porpoise_options_print_help(stdout, argc > 0 ? argv[0] : "porpoise");
        return PORPOISE_EXIT_OK;
    }
    if (options.show_version) {
        porpoise_options_print_version(stdout);
        return PORPOISE_EXIT_OK;
    }

    porpoise_program_init(&program);
    porpoise_abi_init(&abi);
    porpoise_report_init(&report);
    porpoise_diagnostics_init(&diagnostics);

    if (!porpoise_path_normalize_lexical(normalized_output, sizeof(normalized_output),
                                         options.output_path) ||
        !porpoise_copy_string(options.output_path, sizeof(options.output_path), normalized_output)) {
        porpoise_diagnostics_add(&diagnostics, PORPOISE_SEVERITY_ERROR,
                                 options.output_path, 0U, 0U,
                                 "cannot normalize output path safely");
        result = PORPOISE_EXIT_IO;
    } else if (!porpoise_path_contains_path(options.output_path, ".",
                                            &output_contains_working_directory) ||
               !porpoise_path_trees_overlap(options.input_path, options.output_path, &paths_overlap)) {
        porpoise_diagnostics_add(&diagnostics, PORPOISE_SEVERITY_ERROR,
                                 options.output_path, 0U, 0U,
                                 "cannot resolve input and output paths safely");
        result = PORPOISE_EXIT_IO;
    } else if (output_contains_working_directory) {
        porpoise_diagnostics_add(&diagnostics, PORPOISE_SEVERITY_ERROR,
                                 options.output_path, 0U, 0U,
                                 "output must not contain the current working directory");
        result = PORPOISE_EXIT_USAGE;
    } else if (paths_overlap) {
        porpoise_diagnostics_add(&diagnostics, PORPOISE_SEVERITY_ERROR,
                                 options.output_path, 0U, 0U,
                                 "input and output directory trees must not overlap");
        result = PORPOISE_EXIT_USAGE;
    } else {
        if (options.verbosity == PORPOISE_VERBOSITY_VERBOSE)
            fprintf(stderr, "porpoise: scanning %s\n", options.input_path);
        result = porpoise_program_load(&program, options.input_path, &diagnostics);
    }
    if (result == PORPOISE_EXIT_OK && options.skip_list_path[0] != '\0')
        result = porpoise_program_apply_skip_list(&program, options.skip_list_path, &diagnostics);
    if (result == PORPOISE_EXIT_OK && options.abi_path[0] != '\0') {
        if (options.verbosity == PORPOISE_VERBOSITY_VERBOSE)
            fprintf(stderr, "porpoise: loading ABI manifest %s\n", options.abi_path);
        result = porpoise_abi_load(&abi, options.abi_path, &diagnostics);
    }
    if (result == PORPOISE_EXIT_OK) {
        project_options.output_path = options.output_path;
        project_options.runtime_directory = select_runtime_directory();
        project_options.entry_symbol = options.entry_symbol;
        project_options.force = options.force;
        project_options.strict = options.strict;
        if (options.verbosity == PORPOISE_VERBOSITY_VERBOSE)
            fprintf(stderr, "porpoise: generating %s\n", options.output_path);
        result = porpoise_project_generate(&program, &abi, &project_options, &report, &diagnostics);
    }

    print_diagnostics(&diagnostics, options.verbosity);
    if (result == PORPOISE_EXIT_OK && options.verbosity != PORPOISE_VERBOSITY_QUIET)
        fprintf(stdout, "Generated %lu file(s), %lu function(s), and %lu instruction record(s) in %s\n",
                (unsigned long)report.source_count, (unsigned long)report.function_count,
                (unsigned long)report.instruction_count, options.output_path);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_report_free(&report);
    porpoise_abi_free(&abi);
    porpoise_program_free(&program);
    return result;
}
