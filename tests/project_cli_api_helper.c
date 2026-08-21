#include "porpoise/plan.h"
#include "porpoise/project.h"
#include "porpoise/session.h"
#include "porpoise/util.h"

#include <stdio.h>

static bool fixture_path(
    char *path,
    size_t capacity,
    const char *source_root,
    const char *leaf) {
    char fixture[PORPOISE_PATH_CAPACITY];
    return porpoise_path_join(
               fixture,
               sizeof(fixture),
               source_root,
               "tests/fixtures/session_plan") &&
           porpoise_path_join(path, capacity, fixture, leaf);
}

static void print_errors(const PorpoiseDiagnostics *diagnostics) {
    size_t index;
    for (index = 0U; index < diagnostics->count; index++) {
        const PorpoiseDiagnostic *item = &diagnostics->items[index];
        if (item->severity != PORPOISE_SEVERITY_ERROR) continue;
        fprintf(
            stderr,
            "%s:%lu: %s\n",
            item->file != NULL && item->file[0] != '\0'
                ? item->file : "porpoise-api-parity",
            (unsigned long)item->line,
            item->message);
    }
}

int main(int argc, char **argv) {
    char input[PORPOISE_PATH_CAPACITY];
    char abi[PORPOISE_PATH_CAPACITY];
    char skip[PORPOISE_PATH_CAPACITY];
    char runtime[PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PorpoiseProjectOptions project_options;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseReport report;
    PorpoiseDiagnostics diagnostics;
    int result = PORPOISE_EXIT_USAGE;

    if (argc != 3) {
        fprintf(stderr, "usage: %s SOURCE_ROOT OUTPUT\n", argv[0]);
        return PORPOISE_EXIT_USAGE;
    }
    if (!fixture_path(input, sizeof(input), argv[1], "input") ||
        !fixture_path(abi, sizeof(abi), argv[1], "abi.json") ||
        !fixture_path(skip, sizeof(skip), argv[1], "skip.txt") ||
        !porpoise_path_join(
            runtime, sizeof(runtime), argv[1], "runtime")) {
        fputs("porpoise-api-parity: fixture path is too long\n", stderr);
        return PORPOISE_EXIT_USAGE;
    }

    porpoise_report_init(&report);
    porpoise_diagnostics_init(&diagnostics);
    porpoise_session_open_options_init(&session_options);
    session_options.input_path = input;
    session_options.abi_path = abi;
    session_options.skip_list_path = skip;
    result = porpoise_session_open(
        &session_options, &session, &diagnostics);

    if (result == PORPOISE_EXIT_OK) {
        porpoise_plan_options_init(&plan_options);
        plan_options.entry_symbol = "lift_me";
        result = porpoise_plan_build(
            session, &plan_options, &plan, &diagnostics);
    }
    if (result == PORPOISE_EXIT_OK) {
        result = porpoise_plan_validate(plan, &diagnostics);
    }
    if (result == PORPOISE_EXIT_OK) {
        porpoise_project_options_init(&project_options);
        project_options.output_path = argv[2];
        project_options.runtime_directory = runtime;
        project_options.entry_symbol = "lift_me";
        project_options.force = true;
        result = porpoise_project_generate_plan(
            plan, &project_options, &report, &diagnostics);
    }

    if (result != PORPOISE_EXIT_OK) print_errors(&diagnostics);
    porpoise_report_free(&report);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
    return result;
}
