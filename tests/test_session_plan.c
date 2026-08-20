#include "porpoise/plan.h"
#include "porpoise/project.h"
#include "porpoise/session.h"
#include "porpoise/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int failures = 0U;

typedef struct CancellationProbe {
    bool cancel;
    size_t progress_count;
} CancellationProbe;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                     \
                    __FILE__, __LINE__, #condition);                            \
            failures++;                                                        \
        }                                                                       \
    } while (0)

static bool fixture_path(
    char *path,
    size_t capacity,
    const char *source_root,
    const char *leaf) {
    int written = snprintf(
        path,
        capacity,
        "%s/tests/fixtures/session_plan/%s",
        source_root,
        leaf);
    return written >= 0 && (size_t)written < capacity;
}

static bool diagnostics_contain(
    const PorpoiseDiagnostics *diagnostics,
    const char *fragment) {
    size_t index;
    for (index = 0U; index < diagnostics->count; index++) {
        if (strstr(diagnostics->items[index].message, fragment) != NULL) {
            return true;
        }
    }
    return false;
}

static void cancel_at_generation(
    void *user_data,
    PorpoiseOperationPhase phase,
    size_t completed,
    size_t total,
    const char *detail) {
    CancellationProbe *probe = (CancellationProbe *)user_data;
    (void)total;
    (void)detail;
    probe->progress_count++;
    if (phase == PORPOISE_PHASE_GENERATE && completed == 0U) {
        probe->cancel = true;
    }
}

static bool cancellation_requested(void *user_data) {
    return ((CancellationProbe *)user_data)->cancel;
}

static char *read_file_snapshot(const char *path, size_t *size_out) {
    FILE *file = fopen(path, "rb");
    long length;
    char *bytes;
    size_t size;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    size = (size_t)length;
    bytes = (char *)malloc(size + 1U);
    if (bytes == NULL) {
        fclose(file);
        return NULL;
    }
    if (size != 0U && fread(bytes, 1U, size, file) != size) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    if (fclose(file) != 0) {
        free(bytes);
        return NULL;
    }
    bytes[size] = '\0';
    *size_out = size;
    return bytes;
}

static void test_project_api_parity(const char *source_root) {
    const char *output = ".porpoise-session-plan-api-parity";
    char input[PORPOISE_PATH_CAPACITY];
    char skip[PORPOISE_PATH_CAPACITY];
    char abi[PORPOISE_PATH_CAPACITY];
    char runtime[PORPOISE_PATH_CAPACITY];
    char report_path[PORPOISE_PATH_CAPACITY];
    char meson_path[PORPOISE_PATH_CAPACITY];
    char lifted_path[PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PorpoiseProjectOptions project_options;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseReport plan_report;
    PorpoiseReport legacy_report;
    PorpoiseDiagnostics plan_diagnostics;
    PorpoiseDiagnostics legacy_diagnostics;
    char *plan_report_json = NULL;
    char *plan_meson = NULL;
    char *plan_lifted = NULL;
    char *legacy_report_json = NULL;
    char *legacy_meson = NULL;
    char *legacy_lifted = NULL;
    size_t plan_report_size = 0U;
    size_t plan_meson_size = 0U;
    size_t plan_lifted_size = 0U;
    size_t legacy_report_size = 0U;
    size_t legacy_meson_size = 0U;
    size_t legacy_lifted_size = 0U;
    size_t status_index;
    int result;

    CHECK(fixture_path(input, sizeof(input), source_root, "input"));
    CHECK(fixture_path(skip, sizeof(skip), source_root, "skip.txt"));
    CHECK(fixture_path(abi, sizeof(abi), source_root, "abi.json"));
    CHECK(porpoise_path_join(
        runtime, sizeof(runtime), source_root, "runtime"));
    CHECK(porpoise_path_join(
        report_path, sizeof(report_path), output, "porpoise-report.json"));
    CHECK(porpoise_path_join(
        meson_path, sizeof(meson_path), output, "meson.build"));
    CHECK(porpoise_path_join(
        lifted_path, sizeof(lifted_path), output, "src/lifted/a.c"));

    porpoise_diagnostics_init(&plan_diagnostics);
    porpoise_diagnostics_init(&legacy_diagnostics);
    porpoise_report_init(&plan_report);
    porpoise_report_init(&legacy_report);
    CHECK(porpoise_remove_tree(output, &plan_diagnostics));

    porpoise_session_open_options_init(&session_options);
    session_options.input_path = input;
    session_options.skip_list_path = skip;
    session_options.abi_path = abi;
    result = porpoise_session_open(
        &session_options, &session, &plan_diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);

    porpoise_plan_options_init(&plan_options);
    plan_options.entry_symbol = "lift_me";
    result = porpoise_plan_build(
        session, &plan_options, &plan, &plan_diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(porpoise_plan_validate(plan, &plan_diagnostics) ==
          PORPOISE_EXIT_OK);

    memset(&project_options, 0, sizeof(project_options));
    project_options.output_path = output;
    project_options.runtime_directory = runtime;
    project_options.entry_symbol = "lift_me";
    project_options.force = true;
    result = porpoise_project_generate_plan(
        plan, &project_options, &plan_report, &plan_diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(plan_report.source_count == 2U);
    CHECK(plan_report.function_count == 1U);
    plan_report_json = read_file_snapshot(
        report_path, &plan_report_size);
    plan_meson = read_file_snapshot(meson_path, &plan_meson_size);
    plan_lifted = read_file_snapshot(lifted_path, &plan_lifted_size);
    CHECK(plan_report_json != NULL);
    CHECK(plan_meson != NULL);
    CHECK(plan_lifted != NULL);
    CHECK(plan_report_json != NULL &&
          strstr(plan_report_json, "\"status\": \"imported\"") != NULL);
    CHECK(plan_report_json != NULL &&
          strstr(plan_report_json, "\"status\": \"skipped\"") != NULL);
    CHECK(plan_meson != NULL &&
          strstr(plan_meson, "src/lifted/a.c") != NULL);
    CHECK(plan_meson != NULL &&
          strstr(plan_meson, "src/lifted/b.c") == NULL);

    result = porpoise_project_generate(
        porpoise_session_program(session),
        porpoise_session_abi(session),
        &project_options,
        &legacy_report,
        &legacy_diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    legacy_report_json = read_file_snapshot(
        report_path, &legacy_report_size);
    legacy_meson = read_file_snapshot(meson_path, &legacy_meson_size);
    legacy_lifted = read_file_snapshot(lifted_path, &legacy_lifted_size);
    CHECK(legacy_report_json != NULL);
    CHECK(legacy_meson != NULL);
    CHECK(legacy_lifted != NULL);
    CHECK(plan_report_size == legacy_report_size &&
          plan_report_json != NULL && legacy_report_json != NULL &&
          memcmp(plan_report_json, legacy_report_json, plan_report_size) == 0);
    CHECK(plan_meson_size == legacy_meson_size &&
          plan_meson != NULL && legacy_meson != NULL &&
          memcmp(plan_meson, legacy_meson, plan_meson_size) == 0);
    CHECK(plan_lifted_size == legacy_lifted_size &&
          plan_lifted != NULL && legacy_lifted != NULL &&
          memcmp(plan_lifted, legacy_lifted, plan_lifted_size) == 0);
    CHECK(plan_report.source_count == legacy_report.source_count);
    CHECK(plan_report.function_count == legacy_report.function_count);
    CHECK(plan_report.instruction_count == legacy_report.instruction_count);
    for (status_index = 0U;
         status_index < sizeof(plan_report.status_counts) /
                            sizeof(plan_report.status_counts[0]);
         status_index++) {
        CHECK(plan_report.status_counts[status_index] ==
              legacy_report.status_counts[status_index]);
    }

    free(legacy_lifted);
    free(legacy_meson);
    free(legacy_report_json);
    free(plan_lifted);
    free(plan_meson);
    free(plan_report_json);
    CHECK(porpoise_remove_tree(output, &plan_diagnostics));
    porpoise_report_free(&legacy_report);
    porpoise_report_free(&plan_report);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&legacy_diagnostics);
    porpoise_diagnostics_free(&plan_diagnostics);
}

static void test_session_and_legacy_plan(const char *source_root) {
    char input[PORPOISE_PATH_CAPACITY];
    char skip[PORPOISE_PATH_CAPACITY];
    char abi[PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseProgram *program;
    const PorpoiseFunctionPlanView *view;
    int result;

    CHECK(fixture_path(input, sizeof(input), source_root, "input"));
    CHECK(fixture_path(skip, sizeof(skip), source_root, "skip.txt"));
    CHECK(fixture_path(abi, sizeof(abi), source_root, "abi.json"));
    porpoise_diagnostics_init(&diagnostics);
    porpoise_session_open_options_init(&session_options);
    CHECK(session_options.input_path == NULL);
    CHECK(session_options.abi_path == NULL);
    CHECK(session_options.skip_list_path == NULL);
    session_options.input_path = input;
    session_options.skip_list_path = skip;
    session_options.abi_path = abi;

    result = porpoise_session_open(
        &session_options, &session, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(session != NULL);
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    program = porpoise_session_program(session);
    CHECK(program != NULL);
    CHECK(porpoise_session_abi(session) != NULL);
    CHECK(program != NULL && program->file_count == 2U);
    CHECK(porpoise_session_abi(session)->function_count == 1U);

    /* The immutable bridge snapshots, but does not rewrite, legacy flags. */
    CHECK(!program->files[0].functions[0].skipped);
    CHECK(program->files[0].functions[1].skipped);
    CHECK(program->files[0].functions[1].data_region);
    CHECK(program->files[1].functions[0].skipped);
    CHECK(program->files[1].functions[1].skipped);

    porpoise_plan_options_init(&plan_options);
    CHECK(plan_options.sdk_policy == PORPOISE_SDK_POLICY_KEEP);
    plan_options.entry_symbol = "lift_me";
    plan_options.sdk_policy = PORPOISE_SDK_POLICY_OMIT;
    result = porpoise_plan_build(
        session, &plan_options, &plan, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(plan != NULL);
    CHECK(porpoise_plan_session(plan) == session);
    CHECK(porpoise_plan_sdk_policy(plan) == PORPOISE_SDK_POLICY_OMIT);
    CHECK(porpoise_plan_function_count(plan) == 4U);
    CHECK(porpoise_plan_function_at(plan, 4U) == NULL);

    view = porpoise_plan_function_at(plan, 0U);
    CHECK(view != NULL && strcmp(view->function->name, "lift_me") == 0);
    CHECK(view != NULL && view->source == &program->files[0]);
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_LIFT);
    CHECK(view != NULL && view->origin == PORPOISE_PLAN_ORIGIN_DEFAULT);
    CHECK(view != NULL && view->binding == NULL);
    CHECK(view != NULL && view->binding_address == UINT32_C(0x80001000));
    CHECK(porpoise_plan_entry(plan) == view);

    view = porpoise_plan_function_at(plan, 1U);
    CHECK(view != NULL &&
          strcmp(view->function->name, "gap_01_80002000_text") == 0);
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_DATA);
    CHECK(view != NULL && view->origin == PORPOISE_PLAN_ORIGIN_INPUT_DATA);

    view = porpoise_plan_function_at(plan, 2U);
    CHECK(view != NULL && strcmp(view->function->name, "import_me") == 0);
    CHECK(view != NULL && view->source == &program->files[1]);
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_IMPORT);
    CHECK(view != NULL && view->origin == PORPOISE_PLAN_ORIGIN_ABI_IMPORT);
    CHECK(view != NULL && view->binding != NULL);
    CHECK(view != NULL && strcmp(view->binding->symbol, "import_me") == 0);
    CHECK(view != NULL && view->binding_alias == NULL);
    CHECK(view != NULL && view->binding_address == UINT32_C(0x80003000));

    view = porpoise_plan_function_at(plan, 3U);
    CHECK(view != NULL && strcmp(view->function->name, "omit_me") == 0);
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_OMIT);
    CHECK(view != NULL && view->origin == PORPOISE_PLAN_ORIGIN_SKIP_LIST);
    CHECK(porpoise_plan_find_function(plan, "omit_me") == view);
    CHECK(porpoise_plan_find_function(plan, "missing") == NULL);
    CHECK(porpoise_plan_find_function(plan, NULL) == NULL);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);

    CHECK(strcmp(porpoise_sdk_policy_name(PORPOISE_SDK_POLICY_IMPORTED),
                 "imported") == 0);
    CHECK(strcmp(porpoise_plan_action_name(PORPOISE_PLAN_ACTION_DATA),
                 "data") == 0);
    CHECK(strcmp(porpoise_plan_origin_name(PORPOISE_PLAN_ORIGIN_SKIP_LIST),
                 "skip-list") == 0);

    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_plan_without_abi(const char *source_root) {
    char input[PORPOISE_PATH_CAPACITY];
    char skip[PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions options;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseFunctionPlanView *view;
    int result;

    CHECK(fixture_path(input, sizeof(input), source_root, "input"));
    CHECK(fixture_path(skip, sizeof(skip), source_root, "skip.txt"));
    porpoise_diagnostics_init(&diagnostics);
    porpoise_session_open_options_init(&options);
    options.input_path = input;
    options.skip_list_path = skip;
    result = porpoise_session_open(&options, &session, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(porpoise_session_abi(session)->function_count == 0U);

    result = porpoise_plan_build(session, NULL, &plan, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(porpoise_plan_entry(plan) == NULL);
    view = porpoise_plan_find_function(plan, "import_me");
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_OMIT);
    CHECK(view != NULL && view->binding == NULL);

    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_invalid_inputs(const char *source_root) {
    char input[PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseDiagnostics diagnostics;
    int result;

    porpoise_diagnostics_init(&diagnostics);
    porpoise_session_open_options_init(&session_options);
    result = porpoise_session_open(
        &session_options, &session, &diagnostics);
    CHECK(result == PORPOISE_EXIT_INTERNAL);
    CHECK(session == NULL);
    CHECK(diagnostics_contain(&diagnostics, "input path is required"));
    porpoise_diagnostics_free(&diagnostics);

    CHECK(fixture_path(input, sizeof(input), source_root, "input"));
    porpoise_diagnostics_init(&diagnostics);
    session_options.input_path = input;
    result = porpoise_session_open(
        &session_options, &session, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    porpoise_plan_options_init(&plan_options);
    plan_options.sdk_policy = (PorpoiseSdkPolicy)99;
    result = porpoise_plan_build(
        session, &plan_options, &plan, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(plan == NULL);
    CHECK(diagnostics_contain(&diagnostics, "invalid SDK policy"));

    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_generation_cancellation_preserves_output(
    const char *source_root) {
    const char *output = ".porpoise-session-plan-cancel";
    char input[PORPOISE_PATH_CAPACITY];
    char runtime[PORPOISE_PATH_CAPACITY];
    char sentinel[PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PorpoiseProjectOptions project_options;
    PorpoiseOperationCallbacks callbacks;
    CancellationProbe probe;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseReport report;
    PorpoiseDiagnostics diagnostics;
    FILE *file;
    int result;

    CHECK(fixture_path(input, sizeof(input), source_root, "input"));
    CHECK(porpoise_path_join(runtime, sizeof(runtime), source_root, "runtime"));
    CHECK(porpoise_path_join(sentinel, sizeof(sentinel), output, "sentinel.txt"));
    porpoise_diagnostics_init(&diagnostics);
    porpoise_report_init(&report);
    CHECK(porpoise_remove_tree(output, &diagnostics));
    CHECK(porpoise_make_directories(output, &diagnostics));
    file = fopen(sentinel, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fputs("keep", file) >= 0);
        CHECK(fclose(file) == 0);
    }

    porpoise_session_open_options_init(&session_options);
    session_options.input_path = input;
    result = porpoise_session_open(
        &session_options, &session, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    porpoise_plan_options_init(&plan_options);
    plan_options.entry_symbol = "lift_me";
    result = porpoise_plan_build(
        session, &plan_options, &plan, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);

    memset(&probe, 0, sizeof(probe));
    porpoise_operation_callbacks_init(&callbacks);
    callbacks.progress = cancel_at_generation;
    callbacks.cancelled = cancellation_requested;
    callbacks.user_data = &probe;
    porpoise_project_options_init(&project_options);
    project_options.output_path = output;
    project_options.runtime_directory = runtime;
    project_options.entry_symbol = "lift_me";
    project_options.force = true;
    project_options.operation = &callbacks;
    result = porpoise_project_generate_plan(
        plan, &project_options, &report, &diagnostics);
    CHECK(result == PORPOISE_EXIT_CANCELLED);
    CHECK(probe.progress_count != 0U);
    CHECK(porpoise_path_exists(sentinel));
    CHECK(diagnostics_contain(&diagnostics, "cancelled before publication"));
    CHECK(porpoise_remove_tree(output, &diagnostics));

    porpoise_report_free(&report);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s SOURCE_ROOT\n", argv[0]);
        return 2;
    }
    test_session_and_legacy_plan(argv[1]);
    test_plan_without_abi(argv[1]);
    test_invalid_inputs(argv[1]);
    test_project_api_parity(argv[1]);
    test_generation_cancellation_preserves_output(argv[1]);
    if (failures != 0U) {
        fprintf(stderr, "%u session/plan test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
