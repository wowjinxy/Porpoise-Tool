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

typedef struct BatchCancellationProbe {
    bool cancel;
    size_t publish_progress;
} BatchCancellationProbe;

typedef struct PublicationProbe {
    size_t publish_progress;
} PublicationProbe;

typedef struct DestinationRaceProbe {
    const char *output;
    const char *sentinel;
    PorpoiseDiagnostics *diagnostics;
    bool injected;
} DestinationRaceProbe;

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

static void cancel_after_first_batch_publish(
    void *user_data,
    PorpoiseOperationPhase phase,
    size_t completed,
    size_t total,
    const char *detail) {
    BatchCancellationProbe *probe =
        (BatchCancellationProbe *)user_data;
    (void)detail;
    if (phase != PORPOISE_PHASE_PUBLISH) return;
    probe->publish_progress++;
    if (completed == 1U && total == 2U) probe->cancel = true;
}

static bool batch_cancellation_requested(void *user_data) {
    return ((BatchCancellationProbe *)user_data)->cancel;
}

static void record_publication_progress(
    void *user_data,
    PorpoiseOperationPhase phase,
    size_t completed,
    size_t total,
    const char *detail) {
    PublicationProbe *probe = (PublicationProbe *)user_data;
    (void)completed;
    (void)total;
    (void)detail;
    if (phase == PORPOISE_PHASE_PUBLISH) probe->publish_progress++;
}

static void create_destination_during_publication(
    void *user_data,
    PorpoiseOperationPhase phase,
    size_t completed,
    size_t total,
    const char *detail) {
    DestinationRaceProbe *probe = (DestinationRaceProbe *)user_data;
    FILE *file;
    (void)total;
    (void)detail;
    if (phase != PORPOISE_PHASE_PUBLISH || completed != 0U ||
        probe->injected) {
        return;
    }
    probe->injected = true;
    CHECK(porpoise_make_directories(
        probe->output, probe->diagnostics));
    file = fopen(probe->sentinel, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fputs("created-during-publication", file) >= 0);
        CHECK(fclose(file) == 0);
    }
}

static int stage_with_stack_callbacks(
    const PorpoiseTranslationPlan *plan,
    const char *output,
    const char *runtime,
    PublicationProbe *probe,
    PorpoiseReport *report,
    PorpoiseStagedProject **staged_out,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseProjectOptions options;
    PorpoiseOperationCallbacks callbacks;
    int result;
    porpoise_operation_callbacks_init(&callbacks);
    callbacks.progress = record_publication_progress;
    callbacks.user_data = probe;
    porpoise_project_options_init(&options);
    options.output_path = output;
    options.runtime_directory = runtime;
    options.entry_symbol = "lift_me";
    options.operation = &callbacks;
    result = porpoise_project_stage_plan(
        plan, &options, report, staged_out, diagnostics);
    memset(&callbacks, 0, sizeof(callbacks));
    return result;
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

static void test_additive_abi_contracts(const char *source_root) {
    char input[PORPOISE_PATH_CAPACITY];
    char abi[PORPOISE_PATH_CAPACITY];
    char extra[PORPOISE_PATH_CAPACITY];
    char conflict[PORPOISE_PATH_CAPACITY];
    const char *abi_paths[2];
    PorpoiseSessionOpenOptions options;
    PorpoiseSession *session = NULL;
    PorpoiseDiagnostics diagnostics;
    int result;

    CHECK(fixture_path(input, sizeof(input), source_root, "input"));
    CHECK(fixture_path(abi, sizeof(abi), source_root, "abi.json"));
    CHECK(fixture_path(
        conflict, sizeof(conflict), source_root, "abi-conflict.json"));
    CHECK(porpoise_path_join(
        extra, sizeof(extra), source_root, "tests/fixtures/abi/imports.json"));

    porpoise_diagnostics_init(&diagnostics);
    porpoise_session_open_options_init(&options);
    options.input_path = input;
    options.abi_path = abi;
    abi_paths[0] = abi;
    abi_paths[1] = extra;
    options.abi_paths = abi_paths;
    options.abi_path_count = 2U;
    result = porpoise_session_open(&options, &session, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(session != NULL);
    CHECK(session != NULL &&
          porpoise_session_abi(session)->function_count == 5U);
    porpoise_session_close(session);
    session = NULL;
    porpoise_diagnostics_free(&diagnostics);

    porpoise_diagnostics_init(&diagnostics);
    abi_paths[0] = conflict;
    options.abi_paths = abi_paths;
    options.abi_path_count = 1U;
    result = porpoise_session_open(&options, &session, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(session == NULL);
    CHECK(diagnostics_contain(&diagnostics, "conflicts with an earlier"));
    porpoise_diagnostics_free(&diagnostics);
}

static void test_treat_as_data_generation(const char *source_root) {
    const char *output = ".porpoise-session-plan-function-data";
    char input[PORPOISE_PATH_CAPACITY];
    char runtime[PORPOISE_PATH_CAPACITY];
    char data_source[PORPOISE_PATH_CAPACITY];
    char report_path[PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PorpoiseProjectOptions project_options;
    PorpoiseFunctionOverride override;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *baseline = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    const PorpoiseFunctionPlanView *function_view;
    PorpoiseReport report;
    PorpoiseDiagnostics diagnostics;
    char *contents;
    size_t size = 0U;
    int result;

    CHECK(fixture_path(input, sizeof(input), source_root, "input"));
    CHECK(porpoise_path_join(runtime, sizeof(runtime), source_root, "runtime"));
    CHECK(porpoise_path_join(
        data_source, sizeof(data_source), output,
        "src/data/porpoise_data_0001.c"));
    CHECK(porpoise_path_join(
        report_path, sizeof(report_path), output,
        "porpoise-report.json"));
    porpoise_report_init(&report);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_remove_tree(output, &diagnostics));

    porpoise_session_open_options_init(&session_options);
    session_options.input_path = input;
    result = porpoise_session_open(
        &session_options, &session, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    porpoise_plan_options_init(&plan_options);
    plan_options.entry_symbol = "lift_me";
    result = porpoise_plan_build(
        session, &plan_options, &baseline, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    function_view = porpoise_plan_find_function(baseline, "import_me");
    CHECK(function_view != NULL);
    memset(&override, 0, sizeof(override));
    if (function_view != NULL) {
        override.module = "";
        override.address = function_view->function->start_address;
        override.size = function_view->function->size;
        override.normalized_fingerprint =
            function_view->signature.digest_hex;
        override.action = PORPOISE_OVERRIDE_TREAT_AS_DATA;
    }
    plan_options.overrides = &override;
    plan_options.override_count = 1U;
    result = porpoise_plan_build(
        session, &plan_options, &plan, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);
    function_view = porpoise_plan_find_function(plan, "import_me");
    CHECK(function_view != NULL &&
          function_view->action == PORPOISE_PLAN_ACTION_DATA);

    porpoise_project_options_init(&project_options);
    project_options.output_path = output;
    project_options.runtime_directory = runtime;
    project_options.entry_symbol = "lift_me";
    project_options.force = true;
    result = porpoise_project_generate_plan(
        plan, &project_options, &report, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    contents = read_file_snapshot(data_source, &size);
    CHECK(contents != NULL);
    CHECK(contents != NULL &&
          strstr(contents, "0x4E, 0x80, 0x00, 0x20") != NULL);
    free(contents);
    contents = read_file_snapshot(report_path, &size);
    CHECK(contents != NULL &&
          strstr(contents, "\"symbol\": \"import_me\"") != NULL);
    CHECK(contents != NULL &&
          strstr(contents, "\"resolved_action\": \"data\"") != NULL);
    free(contents);
    CHECK(porpoise_remove_tree(output, &diagnostics));

    porpoise_report_free(&report);
    porpoise_plan_free(plan);
    porpoise_plan_free(baseline);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_transactional_batch_publication(
    const char *source_root) {
    const char *outputs[2] = {
        ".porpoise-session-plan-batch-a",
        ".porpoise-session-plan-batch-b"
    };
    char input[PORPOISE_PATH_CAPACITY];
    char runtime[PORPOISE_PATH_CAPACITY];
    char sentinels[2][PORPOISE_PATH_CAPACITY];
    char generated[2][PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PorpoiseProjectOptions project_options[2];
    PorpoiseOperationCallbacks callbacks;
    BatchCancellationProbe probe;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseStagedProject *staged[2] = {NULL, NULL};
    PorpoiseReport reports[2];
    PorpoiseDiagnostics diagnostics;
    size_t index;
    int result;

    CHECK(fixture_path(input, sizeof(input), source_root, "input"));
    CHECK(porpoise_path_join(runtime, sizeof(runtime), source_root, "runtime"));
    porpoise_diagnostics_init(&diagnostics);
    for (index = 0U; index < 2U; index++) {
        FILE *file;
        porpoise_report_init(&reports[index]);
        CHECK(porpoise_remove_tree(outputs[index], &diagnostics));
        CHECK(porpoise_make_directories(outputs[index], &diagnostics));
        CHECK(porpoise_path_join(
            sentinels[index], sizeof(sentinels[index]),
            outputs[index], "sentinel.txt"));
        CHECK(porpoise_path_join(
            generated[index], sizeof(generated[index]),
            outputs[index], "meson.build"));
        file = fopen(sentinels[index], "wb");
        CHECK(file != NULL);
        if (file != NULL) {
            CHECK(fputs(index == 0U ? "old-a" : "old-b", file) >= 0);
            CHECK(fclose(file) == 0);
        }
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
    callbacks.progress = cancel_after_first_batch_publish;
    callbacks.cancelled = batch_cancellation_requested;
    callbacks.user_data = &probe;
    for (index = 0U; index < 2U; index++) {
        porpoise_project_options_init(&project_options[index]);
        project_options[index].output_path = outputs[index];
        project_options[index].runtime_directory = runtime;
        project_options[index].entry_symbol = "lift_me";
        project_options[index].force = true;
        project_options[index].operation = &callbacks;
        result = porpoise_project_stage_plan(
            plan, &project_options[index], &reports[index],
            &staged[index], &diagnostics);
        CHECK(result == PORPOISE_EXIT_OK);
        CHECK(staged[index] != NULL);
        CHECK(staged[index] != NULL &&
              porpoise_path_is_directory(
                  porpoise_staged_project_stage_path(staged[index])));
        CHECK(porpoise_path_exists(sentinels[index]));
    }
    result = porpoise_project_publish_batch(staged, 2U, &diagnostics);
    CHECK(result == PORPOISE_EXIT_CANCELLED);
    CHECK(probe.publish_progress >= 2U);
    for (index = 0U; index < 2U; index++) {
        CHECK(porpoise_path_exists(sentinels[index]));
        CHECK(!porpoise_path_exists(generated[index]));
        porpoise_staged_project_free(staged[index]);
        staged[index] = NULL;
        porpoise_report_free(&reports[index]);
        porpoise_report_init(&reports[index]);
    }

    memset(&probe, 0, sizeof(probe));
    callbacks.progress = NULL;
    callbacks.cancelled = NULL;
    callbacks.user_data = NULL;
    for (index = 0U; index < 2U; index++) {
        result = porpoise_project_stage_plan(
            plan, &project_options[index], &reports[index],
            &staged[index], &diagnostics);
        CHECK(result == PORPOISE_EXIT_OK);
    }
    result = porpoise_project_publish_batch(staged, 2U, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    for (index = 0U; index < 2U; index++) {
        CHECK(!porpoise_path_exists(sentinels[index]));
        CHECK(porpoise_path_exists(generated[index]));
        porpoise_staged_project_free(staged[index]);
        porpoise_report_free(&reports[index]);
        CHECK(porpoise_remove_tree(outputs[index], &diagnostics));
    }
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_staged_publication_copies_callbacks(
    const char *source_root) {
    const char *output = ".porpoise-session-plan-callback-copy";
    char input[PORPOISE_PATH_CAPACITY];
    char runtime[PORPOISE_PATH_CAPACITY];
    char generated[PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PublicationProbe probe;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseStagedProject *staged = NULL;
    PorpoiseReport report;
    PorpoiseDiagnostics diagnostics;
    int result;

    CHECK(fixture_path(input, sizeof(input), source_root, "input"));
    CHECK(porpoise_path_join(runtime, sizeof(runtime), source_root, "runtime"));
    CHECK(porpoise_path_join(
        generated, sizeof(generated), output, "meson.build"));
    porpoise_diagnostics_init(&diagnostics);
    porpoise_report_init(&report);
    CHECK(porpoise_remove_tree(output, &diagnostics));

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
    result = stage_with_stack_callbacks(
        plan, output, runtime, &probe, &report, &staged, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    probe.publish_progress = 0U;
    result = porpoise_project_publish_staged(staged, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(probe.publish_progress >= 2U);
    CHECK(porpoise_path_exists(generated));

    porpoise_staged_project_free(staged);
    CHECK(porpoise_remove_tree(output, &diagnostics));
    porpoise_report_free(&report);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_staged_publication_rejects_destination_race(
    const char *source_root) {
    const char *output = ".porpoise-session-plan-output-race";
    char input[PORPOISE_PATH_CAPACITY];
    char runtime[PORPOISE_PATH_CAPACITY];
    char sentinel[PORPOISE_PATH_CAPACITY];
    char generated[PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PorpoiseProjectOptions project_options;
    PorpoiseOperationCallbacks callbacks;
    DestinationRaceProbe probe;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseStagedProject *staged = NULL;
    PorpoiseReport report;
    PorpoiseDiagnostics diagnostics;
    int result;

    CHECK(fixture_path(input, sizeof(input), source_root, "input"));
    CHECK(porpoise_path_join(runtime, sizeof(runtime), source_root, "runtime"));
    CHECK(porpoise_path_join(
        sentinel, sizeof(sentinel), output, "sentinel.txt"));
    CHECK(porpoise_path_join(
        generated, sizeof(generated), output, "meson.build"));
    porpoise_diagnostics_init(&diagnostics);
    porpoise_report_init(&report);
    CHECK(porpoise_remove_tree(output, &diagnostics));

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

    porpoise_project_options_init(&project_options);
    memset(&probe, 0, sizeof(probe));
    probe.output = output;
    probe.sentinel = sentinel;
    probe.diagnostics = &diagnostics;
    porpoise_operation_callbacks_init(&callbacks);
    callbacks.progress = create_destination_during_publication;
    callbacks.user_data = &probe;
    project_options.output_path = output;
    project_options.runtime_directory = runtime;
    project_options.entry_symbol = "lift_me";
    project_options.force = false;
    project_options.operation = &callbacks;
    result = porpoise_project_stage_plan(
        plan, &project_options, &report, &staged, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);

    result = porpoise_project_publish_staged(staged, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(probe.injected);
    CHECK(diagnostics_contain(
        &diagnostics, "appeared before publication"));
    CHECK(porpoise_path_exists(sentinel));
    CHECK(!porpoise_path_exists(generated));
    CHECK(porpoise_path_is_directory(
        porpoise_staged_project_stage_path(staged)));

    porpoise_staged_project_free(staged);
    CHECK(porpoise_path_exists(sentinel));
    CHECK(porpoise_remove_tree(output, &diagnostics));
    porpoise_report_free(&report);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_plan_binding_digest(const char *source_root) {
    const char *output = ".porpoise-session-plan-binding-mismatch";
    char input[PORPOISE_PATH_CAPACITY];
    char abi[PORPOISE_PATH_CAPACITY];
    char skip[PORPOISE_PATH_CAPACITY];
    char runtime[PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PorpoiseProjectOptions project_options;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *first = NULL;
    PorpoiseTranslationPlan *same = NULL;
    PorpoiseTranslationPlan *different_entry = NULL;
    PorpoiseTranslationPlan *bound = NULL;
    PorpoiseProgram *program;
    PorpoiseAbiManifest *manifest;
    PorpoiseFunctionPlanView *view;
    PorpoiseAbiFunction *effective_binding;
    PorpoiseAbiValue saved_result;
    PorpoisePlanAction saved_requested_action;
    uint32_t saved_word;
    char saved_digest[PORPOISE_SHA256_HEX_SIZE];
    PorpoiseReport report;
    PorpoiseDiagnostics diagnostics;
    int result;

    CHECK(fixture_path(input, sizeof(input), source_root, "input"));
    CHECK(fixture_path(abi, sizeof(abi), source_root, "abi.json"));
    CHECK(fixture_path(skip, sizeof(skip), source_root, "skip.txt"));
    CHECK(porpoise_path_join(
        runtime, sizeof(runtime), source_root, "runtime"));
    porpoise_diagnostics_init(&diagnostics);
    porpoise_report_init(&report);
    CHECK(porpoise_remove_tree(output, &diagnostics));

    /* Entry selection is part of the stable settings identity. */
    porpoise_session_open_options_init(&session_options);
    session_options.input_path = input;
    CHECK(porpoise_session_open(
              &session_options, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    porpoise_plan_options_init(&plan_options);
    plan_options.entry_symbol = "lift_me";
    plan_options.target_id = "binding-test";
    CHECK(porpoise_plan_build(
              session, &plan_options, &first, &diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(porpoise_plan_build(
              session, &plan_options, &same, &diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(strcmp(
              porpoise_plan_digest(first),
              porpoise_plan_digest(same)) == 0);
    plan_options.entry_symbol = "import_me";
    CHECK(porpoise_plan_build(
              session, &plan_options, &different_entry, &diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(strcmp(
              porpoise_plan_digest(first),
              porpoise_plan_digest(different_entry)) != 0);

    /* A structurally valid plan-view edit is still rejected as tampering. */
    view = (PorpoiseFunctionPlanView *)porpoise_plan_function_at(first, 2U);
    CHECK(view != NULL);
    if (view != NULL) {
        saved_requested_action = view->requested_action;
        view->requested_action = PORPOISE_PLAN_ACTION_OMIT;
        CHECK(porpoise_plan_validate(first, &diagnostics) ==
              PORPOISE_EXIT_TRANSLATION);
        CHECK(diagnostics_contain(&diagnostics, "binding digest mismatch"));
        view->requested_action = saved_requested_action;
        CHECK(porpoise_plan_validate(first, &diagnostics) ==
              PORPOISE_EXIT_OK);
    }
    memcpy(
        saved_digest, porpoise_plan_digest(first), sizeof(saved_digest));
    ((char *)porpoise_plan_digest(first))[0] =
        saved_digest[0] == '0' ? '1' : '0';
    CHECK(porpoise_plan_validate(first, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    memcpy(
        (char *)porpoise_plan_digest(first),
        saved_digest,
        sizeof(saved_digest));
    CHECK(porpoise_plan_validate(first, &diagnostics) == PORPOISE_EXIT_OK);

    porpoise_plan_free(different_entry);
    porpoise_plan_free(same);
    porpoise_plan_free(first);
    different_entry = NULL;
    same = NULL;
    first = NULL;
    porpoise_session_close(session);
    session = NULL;
    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);

    /* Program bytes and every ABI mapping are bound to the plan. */
    porpoise_session_open_options_init(&session_options);
    session_options.input_path = input;
    session_options.abi_path = abi;
    session_options.skip_list_path = skip;
    CHECK(porpoise_session_open(
              &session_options, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    porpoise_plan_options_init(&plan_options);
    plan_options.entry_symbol = "lift_me";
    plan_options.target_id = "binding-test";
    CHECK(porpoise_plan_build(
              session, &plan_options, &bound, &diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(porpoise_plan_validate(bound, &diagnostics) == PORPOISE_EXIT_OK);

    program = (PorpoiseProgram *)porpoise_session_program(session);
    CHECK(program != NULL && program->file_count != 0U);
    CHECK(program != NULL && program->files[0].function_count != 0U);
    CHECK(program != NULL &&
          program->files[0].functions[0].item_count != 0U);
    saved_word = program->files[0].functions[0].items[0].word;
    program->files[0].functions[0].items[0].word ^= UINT32_C(1);
    CHECK(porpoise_plan_validate(bound, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);

    porpoise_project_options_init(&project_options);
    project_options.output_path = output;
    project_options.runtime_directory = runtime;
    project_options.force = true;
    result = porpoise_project_generate_plan(
        bound, &project_options, &report, &diagnostics);
    CHECK(result == PORPOISE_EXIT_TRANSLATION);
    CHECK(!porpoise_path_exists(output));
    program->files[0].functions[0].items[0].word = saved_word;
    CHECK(porpoise_plan_validate(bound, &diagnostics) == PORPOISE_EXIT_OK);

    manifest = (PorpoiseAbiManifest *)porpoise_session_abi(session);
    CHECK(manifest != NULL && manifest->function_count == 1U);
    saved_result = manifest->functions[0].result;
    manifest->functions[0].result.type = PORPOISE_ABI_U32;
    manifest->functions[0].result.register_class =
        PORPOISE_ABI_REGISTER_GPR;
    manifest->functions[0].result.register_index = 3U;
    CHECK(porpoise_plan_validate(bound, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    manifest->functions[0].result = saved_result;
    CHECK(porpoise_plan_validate(bound, &diagnostics) == PORPOISE_EXIT_OK);

    view = (PorpoiseFunctionPlanView *)
        porpoise_plan_find_function(bound, "import_me");
    CHECK(view != NULL && view->binding != NULL);
    effective_binding = view == NULL
                            ? NULL
                            : (PorpoiseAbiFunction *)view->binding;
    if (effective_binding != NULL) {
        saved_result = effective_binding->result;
        effective_binding->result.type = PORPOISE_ABI_U32;
        effective_binding->result.register_class =
            PORPOISE_ABI_REGISTER_GPR;
        effective_binding->result.register_index = 3U;
        CHECK(porpoise_plan_validate(bound, &diagnostics) ==
              PORPOISE_EXIT_TRANSLATION);
        effective_binding->result = saved_result;
        CHECK(porpoise_plan_validate(bound, &diagnostics) ==
              PORPOISE_EXIT_OK);
    }

    porpoise_plan_free(bound);
    porpoise_session_close(session);
    porpoise_report_free(&report);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_plan_binding_includes_data(const char *source_root) {
    char input[PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseProgram *program;
    PorpoiseDataSpan *span = NULL;
    PorpoiseDiagnostics diagnostics;
    uint8_t saved_byte = 0U;
    size_t index;

    CHECK(porpoise_path_join(
        input, sizeof(input), source_root,
        "tests/fixtures/inputs/asm_data/data_layout.s"));
    porpoise_diagnostics_init(&diagnostics);
    porpoise_session_open_options_init(&session_options);
    session_options.input_path = input;
    CHECK(porpoise_session_open(
              &session_options, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    porpoise_plan_options_init(&plan_options);
    plan_options.entry_symbol = "entry_fn";
    CHECK(porpoise_plan_build(
              session, &plan_options, &plan, &diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);

    program = (PorpoiseProgram *)porpoise_session_program(session);
    for (index = 0U;
         program != NULL && index < program->data_span_count;
         index++) {
        if (program->data_spans[index].kind ==
                PORPOISE_DATA_SPAN_INITIALIZED &&
            program->data_spans[index].size != 0U &&
            program->data_spans[index].bytes != NULL) {
            span = &program->data_spans[index];
            break;
        }
    }
    CHECK(span != NULL);
    if (span != NULL) {
        saved_byte = span->bytes[0];
        span->bytes[0] ^= UINT8_C(1);
        CHECK(porpoise_plan_validate(plan, &diagnostics) ==
              PORPOISE_EXIT_TRANSLATION);
        span->bytes[0] = saved_byte;
        CHECK(porpoise_plan_validate(plan, &diagnostics) ==
              PORPOISE_EXIT_OK);
    }

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
    test_additive_abi_contracts(argv[1]);
    test_treat_as_data_generation(argv[1]);
    test_transactional_batch_publication(argv[1]);
    test_staged_publication_copies_callbacks(argv[1]);
    test_staged_publication_rejects_destination_race(argv[1]);
    test_plan_binding_digest(argv[1]);
    test_plan_binding_includes_data(argv[1]);
    if (failures != 0U) {
        fprintf(stderr, "%u session/plan test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
