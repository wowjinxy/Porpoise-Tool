#include "porpoise/options.h"
#include "porpoise/plan.h"
#include "porpoise/project.h"
#include "porpoise/report.h"
#include "porpoise/session.h"
#include "porpoise/util.h"

#include <stdio.h>
#include <string.h>

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
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseReport report;
    PorpoiseDiagnostics diagnostics;
    PorpoiseProjectOptions project_options;
    PorpoiseSessionSymbolSource symbol_sources[2];
    const char *sdk_catalog_paths[1];
    size_t symbol_source_count = 0U;
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
        if (options.verbosity == PORPOISE_VERBOSITY_VERBOSE) {
            fprintf(stderr, "porpoise: scanning %s\n", options.input_path);
            if (options.abi_path[0] != '\0') {
                fprintf(
                    stderr,
                    "porpoise: loading ABI manifest %s\n",
                    options.abi_path);
            }
        }
        porpoise_session_open_options_init(&session_options);
        memset(symbol_sources, 0, sizeof(symbol_sources));
        session_options.input_path = options.input_path;
        session_options.abi_path = options.abi_path;
        session_options.skip_list_path = options.skip_list_path;
        if (options.map_path[0] != '\0') {
            symbol_sources[symbol_source_count].kind =
                PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP;
            symbol_sources[symbol_source_count].path = options.map_path;
            symbol_sources[symbol_source_count].module =
                options.module[0] != '\0' ? options.module : NULL;
            symbol_source_count++;
        }
        if (options.dtk_symbols_path[0] != '\0') {
            symbol_sources[symbol_source_count].kind =
                PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS;
            symbol_sources[symbol_source_count].path =
                options.dtk_symbols_path;
            symbol_sources[symbol_source_count].auxiliary_path =
                options.dtk_splits_path;
            symbol_sources[symbol_source_count].module =
                options.module[0] != '\0' ? options.module : NULL;
            symbol_source_count++;
        }
        session_options.symbol_sources = symbol_sources;
        session_options.symbol_source_count = symbol_source_count;
        if (options.sdk_catalog_path[0] != '\0') {
            sdk_catalog_paths[0] = options.sdk_catalog_path;
            session_options.sdk_catalog_paths = sdk_catalog_paths;
            session_options.sdk_catalog_path_count = 1U;
        }
        result = porpoise_session_open(
            &session_options, &session, &diagnostics);
    }
    if (result == PORPOISE_EXIT_OK) {
        porpoise_plan_options_init(&plan_options);
        plan_options.entry_symbol = options.entry_symbol;
        plan_options.module =
            options.module[0] != '\0' ? options.module : NULL;
        plan_options.sdk_policy = options.sdk_policy;
        result = porpoise_plan_build(
            session, &plan_options, &plan, &diagnostics);
    }
    if (result == PORPOISE_EXIT_OK) {
        result = porpoise_plan_validate(plan, &diagnostics);
    }
    if (result == PORPOISE_EXIT_OK) {
        porpoise_project_options_init(&project_options);
        project_options.output_path = options.output_path;
        project_options.runtime_directory = select_runtime_directory();
        project_options.entry_symbol = options.entry_symbol;
        project_options.force = options.force;
        project_options.strict = options.strict;
        if (options.verbosity == PORPOISE_VERBOSITY_VERBOSE)
            fprintf(stderr, "porpoise: generating %s\n", options.output_path);
        result = porpoise_project_generate_plan(
            plan, &project_options, &report, &diagnostics);
    }

    print_diagnostics(&diagnostics, options.verbosity);
    if (result == PORPOISE_EXIT_OK && options.verbosity != PORPOISE_VERBOSITY_QUIET)
        fprintf(stdout, "Generated %lu file(s), %lu function(s), and %lu instruction record(s) in %s\n",
                (unsigned long)report.source_count, (unsigned long)report.function_count,
                (unsigned long)report.instruction_count, options.output_path);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_report_free(&report);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    return result;
}
