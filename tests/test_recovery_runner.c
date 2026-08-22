#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "porpoise/recovery_runner.h"

#include "porpoise/signature.h"
#include "porpoise/util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2U
#endif
#else
#include <unistd.h>
#endif

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

static bool replace_path(PorpoiseRecoveryPath *path, const char *value) {
    free(path->value);
    free(path->resolved);
    memset(path, 0, sizeof(*path));
    return set_path(path, value);
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

static const PorpoiseFunction *find_program_function(
    const PorpoiseProgram *program,
    const char *name) {
    size_t file_index;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        size_t function_index;
        for (function_index = 0U;
             function_index < program->files[file_index].function_count;
             function_index++) {
            const PorpoiseFunction *function =
                &program->files[file_index].functions[function_index];
            if (strcmp(function->name, name) == 0) return function;
        }
    }
    return NULL;
}

static void write_runner_catalog_entry(
    FILE *file,
    const char *identity,
    const char *contract,
    const PorpoiseFunctionSignature *signature,
    bool first) {
    fprintf(
        file,
        "%s{\"canonical_identity\":\"%s\","
        "\"category\":\"nintendo_dolphin\"",
        first ? "" : ",", identity);
    if (contract != NULL) {
        fprintf(file, ",\"contract\":\"%s\"", contract);
    }
    fprintf(
        file,
        ",\"signature\":{\"sha256\":\"%s\","
        "\"function_size\":%lu,\"instruction_count\":%lu,"
        "\"fixed_instruction_count\":%lu,"
        "\"meaningful_fixed_words\":%lu,"
        "\"relocation_count\":%lu,\"internal_branch_count\":%lu,"
        "\"external_branch_count\":%lu,\"external_target_count\":%lu,"
        "\"issue_flags\":%lu}}",
        signature->digest_hex,
        (unsigned long)signature->function_size,
        (unsigned long)signature->instruction_count,
        (unsigned long)signature->fixed_instruction_count,
        (unsigned long)signature->meaningful_fixed_instruction_count,
        (unsigned long)signature->relocation_count,
        (unsigned long)signature->internal_branch_count,
        (unsigned long)signature->external_branch_count,
        (unsigned long)signature->external_target_count,
        (unsigned long)signature->issue_flags);
}

static bool create_runner_sdk_catalog(
    const char *input,
    const char *catalog_path,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSessionOpenOptions session_options;
    PorpoiseSession *session = NULL;
    const PorpoiseProgram *program;
    const PorpoiseFunction *gx_function;
    const PorpoiseFunction *unknown_function;
    PorpoiseFunctionSignature gx_signature;
    PorpoiseFunctionSignature unknown_signature;
    FILE *file;
    bool okay;
    porpoise_session_open_options_init(&session_options);
    session_options.input_path = input;
    if (porpoise_session_open(
            &session_options, &session, diagnostics) != PORPOISE_EXIT_OK) {
        return false;
    }
    program = porpoise_session_program(session);
    gx_function = find_program_function(program, "GXInit");
    unknown_function = find_program_function(program, "UnknownSdk");
    okay = gx_function != NULL && unknown_function != NULL &&
        porpoise_signature_compute(program, gx_function, &gx_signature) &&
        porpoise_signature_compute(
            program, unknown_function, &unknown_signature) &&
        porpoise_signature_is_automatic_match_eligible(&gx_signature) &&
        porpoise_signature_is_automatic_match_eligible(&unknown_signature);
    porpoise_session_close(session);
    if (!okay) return false;

    file = fopen(catalog_path, "wb");
    if (file == NULL) return false;
    fputs(
        "{\"schema_version\":1,\"signature_algorithm_version\":1,"
        "\"entries\":[",
        file);
    write_runner_catalog_entry(
        file, "GXInit", "GXInit", &gx_signature, true);
    write_runner_catalog_entry(
        file, "UnknownSdk", NULL, &unknown_signature, false);
    fputs("]}\n", file);
    okay = !ferror(file);
    return fclose(file) == 0 && okay;
}

static bool make_sdk_cache_project(
    PorpoiseRecoveryProject *project,
    const char *temporary_root,
    const char *input,
    const char *catalog) {
    PorpoiseRecoveryTarget *target;
    char project_path[PORPOISE_PATH_CAPACITY];
    char output[PORPOISE_PATH_CAPACITY];
    if (!porpoise_path_join(
            project_path, sizeof(project_path), temporary_root,
            "sdk-cache.porpoise.json") ||
        !porpoise_path_join(
            output, sizeof(output), temporary_root, "sdk-output")) {
        return false;
    }
    porpoise_recovery_project_init(project);
    project->schema_version = PORPOISE_RECOVERY_PROJECT_SCHEMA_VERSION;
    project->path = copy_text(project_path);
    project->directory = copy_text(temporary_root);
    project->sdk_catalogs = (PorpoiseRecoveryPath *)calloc(
        1U, sizeof(*project->sdk_catalogs));
    project->sdk_catalog_count = 1U;
    project->targets = (PorpoiseRecoveryTarget *)calloc(
        1U, sizeof(*project->targets));
    project->target_count = 1U;
    if (project->path == NULL || project->directory == NULL ||
        project->sdk_catalogs == NULL || project->targets == NULL ||
        !set_path(&project->sdk_catalogs[0], catalog)) {
        return false;
    }
    target = &project->targets[0];
    target->id = copy_text("sdk-cache-target");
    target->enabled = true;
    target->source_kind = PORPOISE_RECOVERY_SOURCE_ASSEMBLY;
    target->entry = copy_text("title_main");
    target->sdk_policy = PORPOISE_SDK_POLICY_KEEP;
    return target->id != NULL && target->entry != NULL &&
           set_path(&target->input, input) &&
           set_path(&target->output, output);
}

static PorpoiseRecoveryMatchCacheEntry *find_cached_match(
    PorpoiseRecoveryTarget *target,
    uint32_t address) {
    size_t index;
    for (index = 0U; index < target->cache.match_count; index++) {
        if (target->cache.matches[index].address == address) {
            return &target->cache.matches[index];
        }
    }
    return NULL;
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

static bool write_text_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    bool written;
    if (file == NULL) return false;
    written = fputs(text, file) >= 0;
    if (fclose(file) != 0) written = false;
    return written;
}

static bool create_file_alias(const char *alias, const char *target) {
#ifdef _WIN32
    return CreateHardLinkA(alias, target, NULL) != 0;
#else
    return link(target, alias) == 0;
#endif
}

static bool create_directory_alias(const char *alias, const char *target) {
#ifdef _WIN32
    if (CreateSymbolicLinkA(
            alias, target,
            SYMBOLIC_LINK_FLAG_DIRECTORY |
                SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0) {
        return true;
    }
    return CreateSymbolicLinkA(
               alias, target, SYMBOLIC_LINK_FLAG_DIRECTORY) != 0;
#else
    return symlink(target, alias) == 0;
#endif
}

static bool directory_has_stage_artifact(const char *path) {
    DIR *directory = opendir(path);
    const struct dirent *entry;
    bool found = false;
    if (directory == NULL) return false;
    while ((entry = readdir(directory)) != NULL) {
        if (strstr(entry->d_name, ".porpoise-stage-") != NULL) {
            found = true;
            break;
        }
    }
    closedir(directory);
    return found;
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

static bool sentinel_equals(const char *directory, const char *expected) {
    char path[PORPOISE_PATH_CAPACITY];
    char *contents;
    bool matches;
    if (!porpoise_path_join(
            path, sizeof(path), directory, "sentinel.txt")) {
        return false;
    }
    contents = read_file(path);
    matches = contents != NULL && strcmp(contents, expected) == 0;
    free(contents);
    return matches;
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

static void test_report_destination_safety(
    const char *source_root,
    const char *build_root) {
    char temporary[PORPOISE_PATH_CAPACITY];
    char runtime[PORPOISE_PATH_CAPACITY];
    char report_directory[PORPOISE_PATH_CAPACITY];
    char overlapping_report[PORPOISE_PATH_CAPACITY];
    char project_alias[PORPOISE_PATH_CAPACITY];
    char cache_root[PORPOISE_PATH_CAPACITY];
    char cache_alias[PORPOISE_PATH_CAPACITY];
    char aliased_report[PORPOISE_PATH_CAPACITY];
    PorpoiseRecoveryProject project;
    PorpoiseRecoveryRunOptions options;
    PorpoiseRecoveryRunResult result;
    PorpoiseDiagnostics diagnostics;
    char *contents;
    bool alias_created;
    int run_result;

    CHECK(porpoise_path_join(
        temporary, sizeof(temporary), build_root,
        "recovery-runner-report-safety"));
    CHECK(porpoise_path_join(
        runtime, sizeof(runtime), source_root, "runtime"));
    CHECK(porpoise_path_join(
        report_directory, sizeof(report_directory), temporary,
        "existing-report-directory"));
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_remove_tree(temporary, &diagnostics));
    CHECK(porpoise_make_directories(temporary, &diagnostics));
    CHECK(make_project(&project, source_root, temporary));
    CHECK(write_text_file(
        project.path, "project-file-sentinel\n"));

    CHECK(write_sentinel(report_directory, "report-directory-bytes"));
    porpoise_recovery_run_options_init(&options);
    options.analyze_only = true;
    options.report_path = report_directory;
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_USAGE);
    CHECK(porpoise_path_is_directory(report_directory));
    CHECK(sentinel_equals(report_directory, "report-directory-bytes"));
    CHECK(!porpoise_path_exists(project.targets[0].output.resolved));
    CHECK(!porpoise_path_exists(project.targets[1].output.resolved));
    CHECK(porpoise_recovery_run_write_report(
              &project, &result, report_directory, &diagnostics) ==
          PORPOISE_EXIT_USAGE);
    CHECK(porpoise_path_is_directory(report_directory));
    CHECK(sentinel_equals(report_directory, "report-directory-bytes"));
    porpoise_recovery_run_result_free(&result);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);
    porpoise_recovery_run_options_init(&options);
    options.analyze_only = true;
    options.report_path = project.path;
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_USAGE);
    CHECK(result.target_count == 2U);
    CHECK(result.targets[0].plan == NULL);
    CHECK(result.targets[1].plan == NULL);
    contents = read_file(project.path);
    CHECK(contents != NULL &&
          strcmp(contents, "project-file-sentinel\n") == 0);
    free(contents);
    CHECK(porpoise_recovery_run_write_report(
              &project, &result, project.path, &diagnostics) ==
          PORPOISE_EXIT_USAGE);
    porpoise_recovery_run_result_free(&result);

    CHECK(porpoise_path_join(
        project_alias, sizeof(project_alias), temporary,
        "project-hardlink.porpoise.json"));
    CHECK(create_file_alias(project_alias, project.path));
    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);
    porpoise_recovery_run_options_init(&options);
    options.analyze_only = true;
    options.report_path = project_alias;
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_USAGE);
    CHECK(result.targets[0].plan == NULL);
    contents = read_file(project.path);
    CHECK(contents != NULL &&
          strcmp(contents, "project-file-sentinel\n") == 0);
    free(contents);
    contents = read_file(project_alias);
    CHECK(contents != NULL &&
          strcmp(contents, "project-file-sentinel\n") == 0);
    free(contents);
    porpoise_recovery_run_result_free(&result);

    CHECK(porpoise_path_join(
        cache_root, sizeof(cache_root), temporary, ".porpoise-cache"));
    CHECK(porpoise_path_join(
        cache_alias, sizeof(cache_alias), temporary, "cache-alias"));
    CHECK(write_sentinel(cache_root, "cache-tree-sentinel"));
    alias_created = create_directory_alias(cache_alias, cache_root);
#ifndef _WIN32
    CHECK(alias_created);
#endif
    if (alias_created) {
        CHECK(porpoise_path_join(
            aliased_report, sizeof(aliased_report), cache_alias,
            "aggregate-report.json"));
        porpoise_diagnostics_free(&diagnostics);
        porpoise_diagnostics_init(&diagnostics);
        porpoise_recovery_run_options_init(&options);
        options.analyze_only = true;
        options.report_path = aliased_report;
        porpoise_recovery_run_result_init(&result);
        run_result = porpoise_recovery_project_run(
            &project, &options, &result, &diagnostics);
        CHECK(run_result == PORPOISE_EXIT_USAGE);
        CHECK(result.targets[0].plan == NULL);
        CHECK(!porpoise_path_exists(aliased_report));
        CHECK(sentinel_equals(cache_root, "cache-tree-sentinel"));
        porpoise_recovery_run_result_free(&result);
        CHECK(porpoise_remove_tree(cache_alias, &diagnostics));
    }

    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(write_sentinel(
        project.targets[0].output.resolved, "overlap-output-bytes"));
    CHECK(porpoise_path_join(
        overlapping_report, sizeof(overlapping_report),
        project.targets[0].output.resolved, "aggregate-report.json"));
    porpoise_recovery_run_options_init(&options);
    options.force = true;
    options.runtime_directory = runtime;
    options.report_path = overlapping_report;
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_USAGE);
    CHECK(porpoise_path_is_directory(
        project.targets[0].output.resolved));
    CHECK(sentinel_equals(
        project.targets[0].output.resolved, "overlap-output-bytes"));
    CHECK(!output_has(&project.targets[0], "meson.build"));
    CHECK(!porpoise_path_exists(overlapping_report));
    CHECK(!porpoise_path_exists(project.targets[1].output.resolved));
    porpoise_recovery_run_result_free(&result);

    porpoise_recovery_project_free(&project);
    CHECK(porpoise_remove_tree(temporary, &diagnostics));
    porpoise_diagnostics_free(&diagnostics);
}

static void test_cache_save_failure_no_publish(
    const char *source_root,
    const char *build_root) {
    char temporary[PORPOISE_PATH_CAPACITY];
    char runtime[PORPOISE_PATH_CAPACITY];
    char save_destination[PORPOISE_PATH_CAPACITY];
    PorpoiseRecoveryProject project;
    PorpoiseRecoveryRunOptions options;
    PorpoiseRecoveryRunResult result;
    PorpoiseDiagnostics diagnostics;
    size_t index;
    int run_result;

    CHECK(porpoise_path_join(
        temporary, sizeof(temporary), build_root,
        "recovery-runner-cache-save-failure"));
    CHECK(porpoise_path_join(
        runtime, sizeof(runtime), source_root, "runtime"));
    CHECK(porpoise_path_join(
        save_destination, sizeof(save_destination), temporary,
        "project-destination"));
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_remove_tree(temporary, &diagnostics));
    CHECK(porpoise_make_directories(temporary, &diagnostics));
    CHECK(make_project(&project, source_root, temporary));
    CHECK(porpoise_make_directories(save_destination, &diagnostics));
    free(project.path);
    project.path = copy_text(save_destination);
    CHECK(project.path != NULL);
    for (index = 0U; index < project.target_count; index++) {
        CHECK(write_sentinel(
            project.targets[index].output.resolved,
            index == 0U ? "saved-main" : "saved-overlay"));
    }

    porpoise_recovery_run_options_init(&options);
    options.force = true;
    options.persist_refreshed_caches = true;
    options.runtime_directory = runtime;
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result != PORPOISE_EXIT_OK);
    CHECK(result.target_count == 2U);
    for (index = 0U; index < result.target_count; index++) {
        CHECK(result.targets[index].match_cache_refreshed);
        CHECK(!result.targets[index].generated);
        CHECK(!result.targets[index].published);
        CHECK(sentinel_equals(
            project.targets[index].output.resolved,
            index == 0U ? "saved-main" : "saved-overlay"));
        CHECK(!output_has(&project.targets[index], "meson.build"));
    }

    porpoise_recovery_run_result_free(&result);
    porpoise_recovery_project_free(&project);
    CHECK(porpoise_remove_tree(temporary, &diagnostics));
    porpoise_diagnostics_free(&diagnostics);
}

static void test_global_path_preflight(
    const char *source_root,
    const char *build_root) {
    char temporary[PORPOISE_PATH_CAPACITY];
    char runtime[PORPOISE_PATH_CAPACITY];
    char dependency_directory[PORPOISE_PATH_CAPACITY];
    char abi_copy[PORPOISE_PATH_CAPACITY];
    char main_output[PORPOISE_PATH_CAPACITY];
    char overlay_output[PORPOISE_PATH_CAPACITY];
    char cache_output[PORPOISE_PATH_CAPACITY];
    PorpoiseRecoveryProject project;
    PorpoiseRecoveryRunOptions options;
    PorpoiseRecoveryRunResult result;
    PorpoiseDiagnostics diagnostics;
    char *abi_before;
    char *abi_after;
    int run_result;

    CHECK(porpoise_path_join(
        temporary, sizeof(temporary), build_root,
        "recovery-runner-global-preflight"));
    CHECK(porpoise_path_join(
        runtime, sizeof(runtime), source_root, "runtime"));
    CHECK(porpoise_path_join(
        dependency_directory, sizeof(dependency_directory), temporary,
        "protected-dependency"));
    CHECK(porpoise_path_join(
        abi_copy, sizeof(abi_copy), dependency_directory, "abi.json"));
    CHECK(porpoise_path_join(
        main_output, sizeof(main_output), temporary, "safe-main"));
    CHECK(porpoise_path_join(
        overlay_output, sizeof(overlay_output), temporary, "safe-overlay"));
    CHECK(porpoise_path_join(
        cache_output, sizeof(cache_output), temporary,
        ".porpoise-cache/generated"));
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_remove_tree(temporary, &diagnostics));
    CHECK(porpoise_make_directories(dependency_directory, &diagnostics));
    CHECK(make_project(&project, source_root, temporary));
    CHECK(porpoise_copy_file(
        project.abi_contracts[0].resolved, abi_copy, &diagnostics));
    CHECK(write_sentinel(dependency_directory, "dependency-sentinel"));
    CHECK(replace_path(&project.abi_contracts[0], abi_copy));
    CHECK(replace_path(&project.targets[0].output, dependency_directory));
    abi_before = read_file(abi_copy);
    CHECK(abi_before != NULL);

    porpoise_recovery_run_options_init(&options);
    options.force = true;
    options.runtime_directory = runtime;
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_USAGE);
    CHECK(result.target_count == 2U);
    CHECK(result.targets[0].plan == NULL);
    CHECK(result.targets[1].plan == NULL);
    CHECK(sentinel_equals(
        dependency_directory, "dependency-sentinel"));
    abi_after = read_file(abi_copy);
    CHECK(abi_before != NULL && abi_after != NULL &&
          strcmp(abi_before, abi_after) == 0);
    free(abi_after);
    free(abi_before);
    porpoise_recovery_run_result_free(&result);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(replace_path(&project.targets[0].output, main_output));
    CHECK(replace_path(&project.targets[1].output, main_output));
    porpoise_recovery_run_options_init(&options);
    options.analyze_only = true;
    options.runtime_directory = runtime;
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_USAGE);
    CHECK(result.targets[0].plan == NULL);
    CHECK(result.targets[1].plan == NULL);
    porpoise_recovery_run_result_free(&result);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(replace_path(&project.targets[0].output, cache_output));
    CHECK(replace_path(&project.targets[1].output, overlay_output));
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_USAGE);
    CHECK(result.targets[0].plan == NULL);
    CHECK(result.targets[1].plan == NULL);
    porpoise_recovery_run_result_free(&result);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(replace_path(&project.targets[0].output, runtime));
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_USAGE);
    CHECK(result.targets[0].plan == NULL);
    CHECK(result.targets[1].plan == NULL);
    porpoise_recovery_run_result_free(&result);

    porpoise_recovery_project_free(&project);
    CHECK(porpoise_remove_tree(temporary, &diagnostics));
    porpoise_diagnostics_free(&diagnostics);
}

static void test_sdk_match_cache_hints(
    const char *source_root,
    const char *build_root) {
    char temporary[PORPOISE_PATH_CAPACITY];
    char input[PORPOISE_PATH_CAPACITY];
    char catalog[PORPOISE_PATH_CAPACITY];
    char report_path[PORPOISE_PATH_CAPACITY];
    char post_refresh_report[PORPOISE_PATH_CAPACITY];
    char first_digest[PORPOISE_SHA256_HEX_SIZE];
    PorpoiseRecoveryProject project;
    PorpoiseRecoveryRunOptions options;
    PorpoiseRecoveryRunResult result;
    PorpoiseRecoveryMatchCacheEntry *cached_gx;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseFunctionPlanView *gx;
    const PorpoiseFunctionPlanView *unknown;
    PorpoisePlanAction gx_action;
    PorpoisePlanAction unknown_action;
    unsigned int gx_evidence;
    unsigned int unknown_evidence;
    size_t expected_match_count;
    char *poisoned_identity;
    char *poisoned_contract;
    char *report_contents;
    int run_result;

    CHECK(porpoise_path_join(
        temporary, sizeof(temporary), build_root,
        "recovery-runner-sdk-match-cache"));
    CHECK(porpoise_path_join(
        input, sizeof(input), source_root,
        "tests/fixtures/sdk_policy/input.s"));
    CHECK(porpoise_path_join(
        catalog, sizeof(catalog), temporary, "catalog.json"));
    CHECK(porpoise_path_join(
        report_path, sizeof(report_path), temporary, "report.json"));
    CHECK(porpoise_path_join(
        post_refresh_report, sizeof(post_refresh_report), temporary,
        "post-refresh-report.json"));
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_remove_tree(temporary, &diagnostics));
    CHECK(porpoise_make_directories(temporary, &diagnostics));
    CHECK(create_runner_sdk_catalog(input, catalog, &diagnostics));
    CHECK(make_sdk_cache_project(
        &project, temporary, input, catalog));

    porpoise_recovery_run_options_init(&options);
    options.analyze_only = true;
    options.report_path = report_path;
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_OK);
    CHECK(result.target_count == 1U);
    CHECK(!result.targets[0].match_cache_hit);
    CHECK(result.targets[0].match_cache_refreshed);
    CHECK(result.targets[0].match_cache_hint_used_count == 0U);
    expected_match_count = project.targets[0].cache.match_count;
    CHECK(expected_match_count == 2U);
    CHECK(porpoise_copy_string(
        first_digest, sizeof(first_digest),
        porpoise_plan_digest(result.targets[0].plan)));
    gx = porpoise_plan_find_function(result.targets[0].plan, "GXInit");
    unknown = porpoise_plan_find_function(
        result.targets[0].plan, "UnknownSdk");
    CHECK(gx != NULL && gx->sdk_entry != NULL);
    CHECK(unknown != NULL && unknown->sdk_entry != NULL);
    gx_action = gx == NULL ? PORPOISE_PLAN_ACTION_DATA : gx->action;
    unknown_action = unknown == NULL
        ? PORPOISE_PLAN_ACTION_DATA : unknown->action;
    gx_evidence = gx == NULL ? 0U : gx->evidence_flags;
    unknown_evidence = unknown == NULL ? 0U : unknown->evidence_flags;
    CHECK(porpoise_plan_validate(
              result.targets[0].plan, &diagnostics) == PORPOISE_EXIT_OK);
    porpoise_recovery_run_result_free(&result);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_OK);
    CHECK(result.targets[0].match_cache_hit);
    CHECK(!result.targets[0].match_cache_refreshed);
    CHECK(result.targets[0].match_cache_hint_used_count ==
          expected_match_count);
    CHECK(strcmp(
              porpoise_plan_digest(result.targets[0].plan),
              first_digest) == 0);
    gx = porpoise_plan_find_function(result.targets[0].plan, "GXInit");
    unknown = porpoise_plan_find_function(
        result.targets[0].plan, "UnknownSdk");
    CHECK(gx != NULL && gx->action == gx_action &&
          gx->evidence_flags == gx_evidence);
    CHECK(unknown != NULL && unknown->action == unknown_action &&
          unknown->evidence_flags == unknown_evidence);
    CHECK(porpoise_plan_validate(
              result.targets[0].plan, &diagnostics) == PORPOISE_EXIT_OK);
    porpoise_recovery_run_result_free(&result);

    cached_gx = find_cached_match(
        &project.targets[0], UINT32_C(0x80010000));
    CHECK(cached_gx != NULL);
    poisoned_identity = copy_text("PoisonedSdkIdentity");
    poisoned_contract = copy_text("PoisonedSdkContract");
    CHECK(poisoned_identity != NULL && poisoned_contract != NULL);
    if (cached_gx != NULL && poisoned_identity != NULL &&
        poisoned_contract != NULL) {
        free(cached_gx->canonical_identity);
        free(cached_gx->contract_name);
        cached_gx->canonical_identity = poisoned_identity;
        cached_gx->contract_name = poisoned_contract;
        poisoned_identity = NULL;
        poisoned_contract = NULL;
    }
    free(poisoned_identity);
    free(poisoned_contract);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_OK);
    CHECK(!result.targets[0].match_cache_hit);
    CHECK(result.targets[0].match_cache_refreshed);
    CHECK(result.targets[0].match_cache_hint_used_count + 1U ==
          expected_match_count);
    CHECK(strcmp(
              porpoise_plan_digest(result.targets[0].plan),
              first_digest) == 0);
    gx = porpoise_plan_find_function(result.targets[0].plan, "GXInit");
    unknown = porpoise_plan_find_function(
        result.targets[0].plan, "UnknownSdk");
    CHECK(gx != NULL && gx->action == gx_action &&
          gx->evidence_flags == gx_evidence && gx->sdk_entry != NULL &&
          strcmp(gx->sdk_entry->canonical_identity, "GXInit") == 0 &&
          gx->sdk_entry->contract_name != NULL &&
          strcmp(gx->sdk_entry->contract_name, "GXInit") == 0);
    CHECK(unknown != NULL && unknown->action == unknown_action &&
          unknown->evidence_flags == unknown_evidence);
    CHECK(porpoise_plan_validate(
              result.targets[0].plan, &diagnostics) == PORPOISE_EXIT_OK);

    cached_gx = find_cached_match(
        &project.targets[0], UINT32_C(0x80010000));
    CHECK(project.targets[0].cache.match_count == expected_match_count);
    CHECK(cached_gx != NULL &&
          strcmp(cached_gx->canonical_identity, "GXInit") == 0 &&
          cached_gx->contract_name != NULL &&
          strcmp(cached_gx->contract_name, "GXInit") == 0);
    CHECK(porpoise_recovery_run_write_report(
              &project, &result, post_refresh_report, &diagnostics) ==
          PORPOISE_EXIT_OK);
    report_contents = read_file(post_refresh_report);
    CHECK(report_contents != NULL &&
          strstr(report_contents,
                 "\"canonical_sdk_identity\": \"GXInit\"") != NULL);
    CHECK(report_contents != NULL &&
          strstr(report_contents, "PoisonedSdkIdentity") == NULL &&
          strstr(report_contents, "PoisonedSdkContract") == NULL);
    free(report_contents);

    porpoise_recovery_run_result_free(&result);
    porpoise_recovery_project_free(&project);
    CHECK(porpoise_remove_tree(temporary, &diagnostics));
    porpoise_diagnostics_free(&diagnostics);
}

static void test_runner(const char *source_root, const char *build_root) {
    char temporary[PORPOISE_PATH_CAPACITY];
    char runtime[PORPOISE_PATH_CAPACITY];
    char report_path[PORPOISE_PATH_CAPACITY];
    char strict_input[PORPOISE_PATH_CAPACITY];
    char *normal_overlay_input;
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
    CHECK(porpoise_path_join(
        strict_input, sizeof(strict_input), temporary,
        "strict-approximate.s"));
    CHECK(write_text_file(
        strict_input,
        ".text\n"
        ".fn lift_me, global\n"
        "/* 80001000 00000000 60000000 */ frsp f2, f1\n"
        "/* 80001004 00000004 4E800020 */ blr\n"
        ".endfn lift_me\n"
        ".fn import_me, global\n"
        "/* 80003000 00000008 4E800020 */ blr\n"
        ".endfn import_me\n"
        ".fn omit_me, global\n"
        "/* 80004000 0000000C 4E800020 */ blr\n"
        ".endfn omit_me\n"));

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
    CHECK(!result.targets[0].match_cache_hit);
    CHECK(!result.targets[1].match_cache_hit);
    CHECK(result.targets[0].match_cache_refreshed);
    CHECK(result.targets[1].match_cache_refreshed);
    CHECK(project.targets[0].cache.input_sha256 != NULL);
    CHECK(project.targets[0].cache.settings_sha256 != NULL);
    CHECK(project.targets[0].cache.dependency_count == 2U);
    CHECK(project.targets[1].cache.input_sha256 != NULL);
    CHECK(!porpoise_path_exists(project.path));
    compare_direct_plan(&project, &result.targets[0], &diagnostics);
    report_contents = read_file(report_path);
    CHECK(report_contents != NULL);
    CHECK(report_contents != NULL &&
          strstr(report_contents, "\"schema_version\": 3") != NULL);
    CHECK(report_contents != NULL &&
          strstr(report_contents, "\"id\": \"main\"") != NULL);
    CHECK(report_contents != NULL &&
          strstr(report_contents,
                 "\"canonical_sdk_identity\": null") != NULL);
    CHECK(report_contents != NULL &&
          strstr(report_contents, "\"requested_action\"") != NULL);
    CHECK(report_contents != NULL &&
          strstr(report_contents, "\"match_cache_hit\": false") != NULL);
    CHECK(report_contents != NULL &&
          strstr(report_contents,
                 "\"match_cache_refreshed\": true") != NULL);
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
    CHECK(result.targets[0].match_cache_hit);
    CHECK(!result.targets[0].match_cache_refreshed);
    porpoise_recovery_run_result_free(&result);
    project.targets[1].enabled = true;

    for (index = 0U; index < 2U; index++) {
        CHECK(write_sentinel(
            project.targets[index].output.resolved,
            index == 0U ? "old-main" : "old-overlay"));
    }

    normal_overlay_input = copy_text(project.targets[1].input.resolved);
    CHECK(normal_overlay_input != NULL);
    CHECK(replace_path(&project.targets[1].input, strict_input));
    project.targets[1].strict = true;
    porpoise_recovery_run_options_init(&options);
    options.force = true;
    options.runtime_directory = runtime;
    porpoise_recovery_run_result_init(&result);
    run_result = porpoise_recovery_project_run(
        &project, &options, &result, &diagnostics);
    CHECK(run_result == PORPOISE_EXIT_TRANSLATION);
    CHECK(result.targets[0].generated);
    CHECK(!result.targets[1].generated);
    for (index = 0U; index < 2U; index++) {
        CHECK(result.targets[index].plan != NULL);
        CHECK(result.targets[index].staged == NULL);
        CHECK(output_has(&project.targets[index], "sentinel.txt"));
        CHECK(!output_has(&project.targets[index], "meson.build"));
    }
    CHECK(!directory_has_stage_artifact(temporary));
    porpoise_recovery_run_result_free(&result);
    project.targets[1].strict = false;
    CHECK(replace_path(&project.targets[1].input, normal_overlay_input));
    free(normal_overlay_input);
    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);

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
        CHECK(result.targets[index].plan != NULL);
        CHECK(result.targets[index].report.function_count != 0U);
        CHECK(result.targets[index].staged == NULL);
        CHECK(output_has(&project.targets[index], "sentinel.txt"));
        CHECK(!output_has(&project.targets[index], "meson.build"));
    }
    CHECK(!directory_has_stage_artifact(temporary));
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
    test_report_destination_safety(argv[1], argv[2]);
    test_cache_save_failure_no_publish(argv[1], argv[2]);
    test_global_path_preflight(argv[1], argv[2]);
    test_sdk_match_cache_hints(argv[1], argv[2]);
    test_runner(argv[1], argv[2]);
    if (failures != 0U) {
        fprintf(stderr, "%u recovery runner test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
