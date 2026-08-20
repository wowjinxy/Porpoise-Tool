#include "porpoise/recovery_runner.h"

#include "porpoise/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            failures++;                                                        \
        }                                                                       \
    } while (0)

typedef struct PublishCancellation {
    bool cancel;
    size_t progress_count;
} PublishCancellation;

static char *copy_text(const char *text) {
    size_t size = strlen(text) + 1U;
    char *copy = (char *)malloc(size);
    if (copy != NULL) memcpy(copy, text, size);
    return copy;
}

static bool set_path(PorpoiseRecoveryPath *path, const char *value) {
    path->value = copy_text(value);
    path->resolved = copy_text(value);
    return path->value != NULL && path->resolved != NULL;
}

static bool fixture_path(
    char *path,
    size_t capacity,
    const char *source_root,
    const char *leaf) {
    char base[PORPOISE_PATH_CAPACITY];
    return porpoise_path_join(
               base, sizeof(base), source_root,
               "tests/fixtures/session_plan") &&
           porpoise_path_join(path, capacity, base, leaf);
}

static bool make_project(
    PorpoiseRecoveryProject *project,
    const char *source_root,
    const char *temporary_root) {
    char input[PORPOISE_PATH_CAPACITY];
    char abi[PORPOISE_PATH_CAPACITY];
    char skip[PORPOISE_PATH_CAPACITY];
    char project_path[PORPOISE_PATH_CAPACITY];
    size_t index;
    if (!fixture_path(input, sizeof(input), source_root, "input") ||
        !fixture_path(abi, sizeof(abi), source_root, "abi.json") ||
        !fixture_path(skip, sizeof(skip), source_root, "skip.txt") ||
        !porpoise_path_join(
            project_path, sizeof(project_path), temporary_root,
            "runner.porpoise.json")) {
        return false;
    }
    porpoise_recovery_project_init(project);
    project->schema_version = PORPOISE_RECOVERY_PROJECT_SCHEMA_VERSION;
    project->path = copy_text(project_path);
    project->directory = copy_text(temporary_root);
    project->abi_contracts = (PorpoiseRecoveryPath *)calloc(
        1U, sizeof(*project->abi_contracts));
    project->abi_contract_count = 1U;
    project->targets = (PorpoiseRecoveryTarget *)calloc(
        2U, sizeof(*project->targets));
    project->target_count = 2U;
    if (project->path == NULL || project->directory == NULL ||
        project->abi_contracts == NULL || project->targets == NULL ||
        !set_path(&project->abi_contracts[0], abi)) {
        return false;
    }
    for (index = 0U; index < 2U; index++) {
        PorpoiseRecoveryTarget *target = &project->targets[index];
        char output[PORPOISE_PATH_CAPACITY];
        const char *id = index == 0U ? "main" : "overlay";
        if (!porpoise_path_join(
                output, sizeof(output), temporary_root,
                index == 0U ? "out-main" : "out-overlay")) {
            return false;
        }
        target->id = copy_text(id);
        target->enabled = true;
        target->source_kind = PORPOISE_RECOVERY_SOURCE_ASSEMBLY;
        target->entry = copy_text("lift_me");
        target->sdk_policy = PORPOISE_SDK_POLICY_KEEP;
        target->has_skip_list = true;
        if (target->id == NULL || target->entry == NULL ||
            !set_path(&target->input, input) ||
            !set_path(&target->output, output) ||
            !set_path(&target->skip_list, skip)) {
            return false;
        }
    }
    return true;
}

static bool write_sentinel(const char *output, const char *text) {
    char path[PORPOISE_PATH_CAPACITY];
    PorpoiseDiagnostics diagnostics;
    FILE *file;
    porpoise_diagnostics_init(&diagnostics);
    if (!porpoise_make_directories(output, &diagnostics) ||
        !porpoise_path_join(path, sizeof(path), output, "sentinel.txt")) {
        porpoise_diagnostics_free(&diagnostics);
        return false;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        porpoise_diagnostics_free(&diagnostics);
        return false;
    }
    if (fputs(text, file) < 0 || fclose(file) != 0) {
        porpoise_diagnostics_free(&diagnostics);
        return false;
    }
    porpoise_diagnostics_free(&diagnostics);
    return true;
}

static bool output_has(
    const PorpoiseRecoveryTarget *target,
    const char *relative) {
    char path[PORPOISE_PATH_CAPACITY];
    return porpoise_path_join(
               path, sizeof(path), target->output.resolved, relative) &&
           porpoise_path_exists(path);
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    long length;
    char *contents;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    contents = (char *)malloc((size_t)length + 1U);
    if (contents == NULL) {
        fclose(file);
        return NULL;
    }
    if ((length != 0L &&
         fread(contents, 1U, (size_t)length, file) != (size_t)length) ||
        fclose(file) != 0) {
        free(contents);
        return NULL;
    }
    contents[length] = '\0';
    return contents;
}

static void cancel_after_first_publish(
    void *user_data,
    PorpoiseOperationPhase phase,
    size_t completed,
    size_t total,
    const char *detail) {
    PublishCancellation *probe = (PublishCancellation *)user_data;
    (void)detail;
    if (phase != PORPOISE_PHASE_PUBLISH) return;
    probe->progress_count++;
    if (completed == 1U && total == 2U) probe->cancel = true;
}

static bool cancellation_requested(void *user_data) {
    return ((PublishCancellation *)user_data)->cancel;
}

static void compare_direct_plan(
    const PorpoiseRecoveryProject *project,
    const PorpoiseRecoveryRunTarget *run_target,
    PorpoiseDiagnostics *diagnostics) {
    const char *abi_paths[1];
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    size_t index;
    int result;
    abi_paths[0] = project->abi_contracts[0].resolved;
    porpoise_session_open_options_init(&session_options);
    session_options.input_path = run_target->target->input.resolved;
    session_options.skip_list_path =
        run_target->target->skip_list.resolved;
    session_options.abi_paths = abi_paths;
    session_options.abi_path_count = 1U;
    result = porpoise_session_open(
        &session_options, &session, diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    porpoise_plan_options_init(&plan_options);
    plan_options.entry_symbol = run_target->target->entry;
    plan_options.target_id = run_target->target->id;
    plan_options.module = "";
    result = porpoise_plan_build(
        session, &plan_options, &plan, diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(porpoise_plan_function_count(plan) ==
          porpoise_plan_function_count(run_target->plan));
    for (index = 0U;
         index < porpoise_plan_function_count(plan);
         index++) {
        const PorpoiseFunctionPlanView *direct =
            porpoise_plan_function_at(plan, index);
        const PorpoiseFunctionPlanView *project_view =
            porpoise_plan_function_at(run_target->plan, index);
        CHECK(strcmp(direct->function->name,
                     project_view->function->name) == 0);
        CHECK(direct->action == project_view->action);
        CHECK(direct->requested_action == project_view->requested_action);
        CHECK(strcmp(direct->signature.digest_hex,
                     project_view->signature.digest_hex) == 0);
    }
    porpoise_plan_free(plan);
    porpoise_session_close(session);
}

static void test_runner(const char *source_root, const char *build_root) {
    char temporary[PORPOISE_PATH_CAPACITY];
    char runtime[PORPOISE_PATH_CAPACITY];
    char report_path[PORPOISE_PATH_CAPACITY];
    PorpoiseRecoveryProject project;
    PorpoiseRecoveryRunOptions options;
    PorpoiseRecoveryRunResult result;
    PorpoiseDiagnostics diagnostics;
    PorpoiseOperationCallbacks callbacks;
    PublishCancellation cancellation;
    const char *selector[1] = {"overlay"};
    char *report_contents;
    size_t index;
    int run_result;

    CHECK(porpoise_path_join(
        temporary, sizeof(temporary), build_root,
        "recovery-runner-test"));
    CHECK(porpoise_path_join(
        runtime, sizeof(runtime), source_root, "runtime"));
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_remove_tree(temporary, &diagnostics));
    CHECK(porpoise_make_directories(temporary, &diagnostics));
    CHECK(make_project(&project, source_root, temporary));
    CHECK(porpoise_path_join(
        report_path, sizeof(report_path), temporary,
        "aggregate-report.json"));

    porpoise_recovery_run_options_init(&options);
    options.analyze_only = true;
    options.report_path = report_path;
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_OK);
    CHECK(result.target_count == 2U);
    CHECK(result.targets[0].plan != NULL);
    CHECK(result.targets[1].plan != NULL);
    CHECK(!result.targets[0].generated && !result.targets[1].generated);
    CHECK(project.targets[0].cache.input_sha256 != NULL);
    CHECK(project.targets[0].cache.settings_sha256 != NULL);
    CHECK(project.targets[0].cache.dependency_count == 2U);
    CHECK(project.targets[1].cache.input_sha256 != NULL);
    compare_direct_plan(&project, &result.targets[0], &diagnostics);
    report_contents = read_file(report_path);
    CHECK(report_contents != NULL);
    CHECK(report_contents != NULL &&
          strstr(report_contents, "\"schema_version\": 3") != NULL);
    CHECK(report_contents != NULL &&
          strstr(report_contents, "\"id\": \"main\"") != NULL);
    CHECK(report_contents != NULL &&
          strstr(report_contents, "\"requested_action\"") != NULL);
    free(report_contents);
    porpoise_recovery_run_result_free(&result);

    project.targets[1].enabled = false;
    options.target_ids = selector;
    options.target_id_count = 1U;
    options.report_path = NULL;
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_OK);
    CHECK(result.target_count == 1U);
    CHECK(result.targets[0].target == &project.targets[1]);
    porpoise_recovery_run_result_free(&result);
    project.targets[1].enabled = true;

    for (index = 0U; index < 2U; index++) {
        CHECK(write_sentinel(
            project.targets[index].output.resolved,
            index == 0U ? "old-main" : "old-overlay"));
    }
    memset(&cancellation, 0, sizeof(cancellation));
    porpoise_operation_callbacks_init(&callbacks);
    callbacks.progress = cancel_after_first_publish;
    callbacks.cancelled = cancellation_requested;
    callbacks.user_data = &cancellation;
    porpoise_recovery_run_options_init(&options);
    options.force = true;
    options.runtime_directory = runtime;
    options.operation = &callbacks;
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_CANCELLED);
    CHECK(cancellation.progress_count >= 2U);
    for (index = 0U; index < 2U; index++) {
        CHECK(output_has(&project.targets[index], "sentinel.txt"));
        CHECK(!output_has(&project.targets[index], "meson.build"));
    }
    porpoise_recovery_run_result_free(&result);

    porpoise_recovery_run_options_init(&options);
    options.force = true;
    options.runtime_directory = runtime;
    options.report_path = report_path;
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_OK);
    CHECK(result.target_count == 2U);
    for (index = 0U; index < 2U; index++) {
        CHECK(result.targets[index].generated);
        CHECK(result.targets[index].published);
        CHECK(!output_has(&project.targets[index], "sentinel.txt"));
        CHECK(output_has(&project.targets[index], "meson.build"));
        CHECK(output_has(
            &project.targets[index], "porpoise-report.json"));
    }
    report_contents = read_file(report_path);
    CHECK(report_contents != NULL &&
          strstr(report_contents, "\"published\": true") != NULL);
    free(report_contents);
    porpoise_recovery_run_result_free(&result);

    porpoise_recovery_project_free(&project);
    CHECK(porpoise_remove_tree(temporary, &diagnostics));
    porpoise_diagnostics_free(&diagnostics);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: test_recovery_runner SOURCE_ROOT BUILD_ROOT\n");
        return 2;
    }
    test_runner(argv[1], argv[2]);
    if (failures != 0U) {
        fprintf(stderr, "%u recovery runner test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
