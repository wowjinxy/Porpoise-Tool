#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "porpoise/dtk_import.h"

#include "porpoise/util.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static unsigned int failures = 0U;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                     \
                    __FILE__, __LINE__, #condition);                            \
            failures++;                                                        \
        }                                                                       \
    } while (0)

typedef enum FakeDtkMode {
    FAKE_DTK_VALID = 0,
    FAKE_DTK_OLD_VERSION,
    FAKE_DTK_RENAMED_VERSION,
    FAKE_DTK_UNRELATED_VERSION,
    FAKE_DTK_UNSAFE_VERSION,
    FAKE_DTK_BAD_INFO,
    FAKE_DTK_PROCESS_FAILURE,
    FAKE_DTK_ESCAPE_LINK_ORDER,
    FAKE_DTK_INVALID_ASSEMBLY,
    FAKE_DTK_MISSING_LINK_ORDER,
    FAKE_DTK_METADATA_INJECTION,
    FAKE_DTK_CASE_COLLISION
} FakeDtkMode;

typedef struct OperationState {
    bool cancelled;
    size_t progress_count;
    bool saw_publish;
} OperationState;

typedef struct FakeDtk {
    FakeDtkMode mode;
    size_t version_calls;
    size_t info_calls;
    size_t disasm_calls;
    bool stages_were_fresh;
    bool cancel_after_disasm;
    OperationState *operation;
    char executable_path[PORPOISE_PATH_CAPACITY];
} FakeDtk;

static void test_progress(
    void *user_data,
    PorpoiseOperationPhase phase,
    size_t completed,
    size_t total,
    const char *detail) {
    OperationState *state = (OperationState *)user_data;
    (void)completed;
    (void)total;
    (void)detail;
    state->progress_count++;
    if (phase == PORPOISE_PHASE_PUBLISH) state->saw_publish = true;
}

static bool test_cancelled(void *user_data) {
    return ((OperationState *)user_data)->cancelled;
}

static bool make_directory(
    const char *path,
    PorpoiseDiagnostics *diagnostics) {
    return porpoise_make_directories(path, diagnostics);
}

static bool write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    bool ok;
    if (file == NULL) return false;
    ok = fputs(text, file) >= 0;
    if (fclose(file) != 0) ok = false;
    return ok;
}

static bool fake_write_valid_tree(
    const char *stage,
    FakeDtkMode mode,
    PorpoiseDiagnostics *diagnostics) {
    static const char main_assembly[] =
        ".text\n"
        ".fn main, global\n"
        "/* 80001000 00000000 38600001 */ li r3, 1\n"
        "/* 80001004 00000004 4E800020 */ blr\n"
        ".endfn main\n";
    static const char helper_assembly[] =
        ".text\n"
        ".fn helper, local\n"
        "/* 80002000 00000000 60000000 */ nop\n"
        "/* 80002004 00000004 4E800020 */ blr\n"
        ".endfn helper\n";
    char asm_directory[PORPOISE_PATH_CAPACITY];
    char main_path[PORPOISE_PATH_CAPACITY];
    char helper_path[PORPOISE_PATH_CAPACITY];
    char link_order_path[PORPOISE_PATH_CAPACITY];
    char metadata_path[PORPOISE_PATH_CAPACITY];

    if (mode == FAKE_DTK_CASE_COLLISION) {
        if (!porpoise_path_join(main_path, sizeof(main_path), stage, "A.s") ||
            !porpoise_path_join(helper_path, sizeof(helper_path), stage, "a.s") ||
            !porpoise_path_join(
                link_order_path, sizeof(link_order_path), stage,
                "link_order.txt")) {
            return false;
        }
        return write_text(main_path, main_assembly) &&
               write_text(helper_path, helper_assembly) &&
               write_text(link_order_path, "A.s\na.s\n");
    }
    if (!porpoise_path_join(
            asm_directory, sizeof(asm_directory), stage, "asm") ||
        !make_directory(asm_directory, diagnostics) ||
        !porpoise_path_join(
            main_path, sizeof(main_path), asm_directory, "main.s") ||
        !porpoise_path_join(
            helper_path, sizeof(helper_path), asm_directory, "helper.s") ||
        !porpoise_path_join(
            link_order_path, sizeof(link_order_path), stage,
            "link_order.txt")) {
        return false;
    }
    if (!write_text(
            main_path,
            mode == FAKE_DTK_INVALID_ASSEMBLY ?
                ".text\n.fn broken\nnot annotated\n" : main_assembly) ||
        !write_text(helper_path, helper_assembly)) {
        return false;
    }
    if (mode != FAKE_DTK_MISSING_LINK_ORDER &&
        !write_text(
            link_order_path,
            mode == FAKE_DTK_ESCAPE_LINK_ORDER ?
                "../escape.s\n" : "main.o\nhelper.o\n")) {
        return false;
    }
    if (mode == FAKE_DTK_METADATA_INJECTION) {
        if (!porpoise_path_join(
                metadata_path, sizeof(metadata_path), stage,
                PORPOISE_DTK_IMPORT_METADATA_FILE) ||
            !write_text(metadata_path, "forged\n")) {
            return false;
        }
    }
    return true;
}

static int fake_dtk_runner(
    void *user_data,
    const char *const *argv,
    const char *working_directory,
    const PorpoiseOperationCallbacks *operation,
    PorpoiseDtkProcessResult *result,
    PorpoiseDiagnostics *diagnostics) {
    FakeDtk *fake = (FakeDtk *)user_data;
    (void)working_directory;
    (void)operation;
    if (fake->executable_path[0] == '\0') {
        if (!porpoise_copy_string(
                fake->executable_path, sizeof(fake->executable_path),
                argv[0])) {
            return PORPOISE_EXIT_INTERNAL;
        }
    } else if (strcmp(fake->executable_path, argv[0]) != 0) {
        return PORPOISE_EXIT_INTERNAL;
    }
    porpoise_dtk_process_result_init(result);
    result->standard_error = porpoise_strdup("");
    if (result->standard_error == NULL) return PORPOISE_EXIT_INTERNAL;

    if (argv[1] != NULL && strcmp(argv[1], "--version") == 0) {
        fake->version_calls++;
        if (fake->mode == FAKE_DTK_OLD_VERSION) {
            result->standard_output = porpoise_strdup("dtk 1.7.9 old\n");
        } else if (fake->mode == FAKE_DTK_RENAMED_VERSION) {
            result->standard_output = porpoise_strdup(
                "dtk-porpoise.exe 1.8.3 fixture\n");
        } else if (fake->mode == FAKE_DTK_UNRELATED_VERSION) {
            result->standard_output = porpoise_strdup(
                "dtkhelper.exe 1.8.3 fixture\n");
        } else if (fake->mode == FAKE_DTK_UNSAFE_VERSION) {
            result->standard_output = porpoise_strdup(
                "dtk-../evil.exe 1.8.3 fixture\n");
        } else {
            result->standard_output = porpoise_strdup(
                "dtk 1.8.3 fixture\n");
        }
    } else if (argv[1] != NULL && strcmp(argv[1], "--no-color") == 0 &&
               argv[3] != NULL && strcmp(argv[3], "info") == 0) {
        fake->info_calls++;
        result->standard_output = porpoise_strdup(
            fake->mode == FAKE_DTK_BAD_INFO ?
                "not an ELF report\n" :
                "ELF type: Executable\nSection count: 3\nSymbol count: 2\n");
    } else if (argv[1] != NULL && strcmp(argv[1], "--no-color") == 0 &&
               argv[3] != NULL && strcmp(argv[3], "disasm") == 0) {
        fake->disasm_calls++;
        fake->stages_were_fresh = fake->stages_were_fresh &&
            porpoise_directory_is_empty(argv[5]);
        if (fake->mode == FAKE_DTK_PROCESS_FAILURE) {
            result->exit_code = 9;
            free(result->standard_error);
            result->standard_error = porpoise_strdup("fixture disasm failed");
            result->standard_output = porpoise_strdup("");
        } else {
            if (!fake_write_valid_tree(argv[5], fake->mode, diagnostics))
                return PORPOISE_EXIT_IO;
            result->standard_output = porpoise_strdup("disassembled\n");
            if (fake->cancel_after_disasm && fake->operation != NULL)
                fake->operation->cancelled = true;
        }
    } else {
        return PORPOISE_EXIT_INTERNAL;
    }
    if (result->standard_output == NULL) return PORPOISE_EXIT_INTERNAL;
    return PORPOISE_EXIT_OK;
}

static bool fixture_path(
    char *output,
    size_t capacity,
    const char *source_root,
    const char *relative) {
    return porpoise_format(
        output, capacity, "%s/tests/fixtures/dtk_import/%s",
        source_root, relative);
}

static bool has_sha256(const char *text) {
    size_t index;
    if (strlen(text) != 64U) return false;
    for (index = 0U; index < 64U; index++) {
        if (!((text[index] >= '0' && text[index] <= '9') ||
              (text[index] >= 'a' && text[index] <= 'f'))) return false;
    }
    return true;
}

static bool directory_has_import_artifact(const char *path) {
    DIR *directory = opendir(path);
    const struct dirent *entry;
    bool found = false;
    if (directory == NULL) return false;
    while ((entry = readdir(directory)) != NULL) {
        if (strstr(entry->d_name, ".stage.") != NULL ||
            strstr(entry->d_name, ".backup.") != NULL) {
            found = true;
            break;
        }
    }
    closedir(directory);
    return found;
}

static void configure_managed(
    PorpoiseDtkImportOptions *options,
    const char *input,
    const char *tool,
    const char *cache,
    const char *settings,
    FakeDtk *fake,
    const PorpoiseOperationCallbacks *operation) {
    porpoise_dtk_import_options_init(options);
    options->source_kind = PORPOISE_DTK_SOURCE_MANAGED_ELF;
    options->input_path = input;
    options->dtk_path = tool;
    options->cache_path = cache;
    options->settings_identity = settings;
    options->runner = fake_dtk_runner;
    options->runner_user_data = fake;
    options->operation = operation;
}

static void test_prepared(
    const char *source_root,
    PorpoiseDiagnostics *diagnostics) {
    char prepared[PORPOISE_PATH_CAPACITY];
    char single[PORPOISE_PATH_CAPACITY];
    PorpoiseDtkImportResult result;

    CHECK(fixture_path(
        prepared, sizeof(prepared), source_root, "prepared"));
    CHECK(fixture_path(
        single, sizeof(single), source_root, "prepared/asm/main.s"));
    CHECK(porpoise_dtk_validate_prepared(
              prepared, true, "prepared-settings", NULL,
              &result, diagnostics) == PORPOISE_EXIT_OK);
    CHECK(result.metadata.source_kind == PORPOISE_DTK_SOURCE_PREPARED_ASM);
    CHECK(result.metadata.asm_file_count == 2U);
    CHECK(result.metadata.function_count == 2U);
    CHECK(result.metadata.annotation_count == 4U);
    CHECK(has_sha256(result.metadata.input_sha256));
    CHECK(has_sha256(result.metadata.dependency_sha256));
    CHECK(strcmp(result.metadata.input_sha256,
                 result.metadata.content_sha256) == 0);
    CHECK(porpoise_dtk_validate_prepared(
              single, false, "", NULL,
              &result, diagnostics) == PORPOISE_EXIT_OK);
    CHECK(result.metadata.asm_file_count == 1U);
    CHECK(result.metadata.function_count == 1U);
}

static void test_managed_cache(
    const char *source_root,
    const char *temporary,
    PorpoiseDiagnostics *diagnostics) {
    char input[PORPOISE_PATH_CAPACITY];
    char tool[PORPOISE_PATH_CAPACITY];
    char cache[PORPOISE_PATH_CAPACITY];
    char dirty[PORPOISE_PATH_CAPACITY];
    PorpoiseDtkImportOptions options;
    PorpoiseDtkImportResult result;
    PorpoiseOperationCallbacks callbacks;
    OperationState operation;
    FakeDtk fake;
    char first_dependency[PORPOISE_SHA256_HEX_SIZE];

    CHECK(fixture_path(input, sizeof(input), source_root, "input.elf"));
    CHECK(fixture_path(tool, sizeof(tool), source_root, "fake-dtk.bin"));
    CHECK(porpoise_path_join(cache, sizeof(cache), temporary, "cache"));
    memset(&operation, 0, sizeof(operation));
    porpoise_operation_callbacks_init(&callbacks);
    callbacks.progress = test_progress;
    callbacks.cancelled = test_cancelled;
    callbacks.user_data = &operation;
    memset(&fake, 0, sizeof(fake));
    fake.mode = FAKE_DTK_VALID;
    fake.stages_were_fresh = true;
    fake.operation = &operation;
    configure_managed(
        &options, input, tool, cache, "settings-a", &fake, &callbacks);

    CHECK(porpoise_dtk_import_run(
              &options, &result, diagnostics) == PORPOISE_EXIT_OK);
    CHECK(!result.cache_hit);
    CHECK(strcmp(result.validated_path, cache) == 0);
    CHECK(result.metadata.asm_file_count == 2U);
    CHECK(result.metadata.function_count == 2U);
    CHECK(result.metadata.annotation_count == 4U);
    CHECK(has_sha256(result.metadata.input_sha256));
    CHECK(has_sha256(result.metadata.tool_sha256));
    CHECK(has_sha256(result.metadata.settings_sha256));
    CHECK(has_sha256(result.metadata.dependency_sha256));
    CHECK(has_sha256(result.metadata.content_sha256));
    CHECK(fake.version_calls == 1U);
    CHECK(fake.info_calls == 1U);
    CHECK(fake.disasm_calls == 1U);
    CHECK(fake.stages_were_fresh);
    CHECK(operation.saw_publish);
    CHECK(operation.progress_count != 0U);
    CHECK(!directory_has_import_artifact(temporary));
    CHECK(porpoise_copy_string(
        first_dependency, sizeof(first_dependency),
        result.metadata.dependency_sha256));

    operation.saw_publish = false;
    CHECK(porpoise_dtk_import_run(
              &options, &result, diagnostics) == PORPOISE_EXIT_OK);
    CHECK(result.cache_hit);
    CHECK(fake.version_calls == 2U);
    CHECK(fake.info_calls == 2U);
    CHECK(fake.disasm_calls == 1U);
    CHECK(!operation.saw_publish);

    CHECK(porpoise_path_join(
        dirty, sizeof(dirty), cache, "unexpected.txt"));
    CHECK(write_text(dirty, "dirty cache marker\n"));
    CHECK(porpoise_dtk_import_run(
              &options, &result, diagnostics) == PORPOISE_EXIT_OK);
    CHECK(!result.cache_hit);
    CHECK(fake.disasm_calls == 2U);
    CHECK(!porpoise_path_exists(dirty));
    CHECK(fake.stages_were_fresh);
    CHECK(!directory_has_import_artifact(temporary));

    options.settings_identity = "settings-b";
    CHECK(porpoise_dtk_import_run(
              &options, &result, diagnostics) == PORPOISE_EXIT_OK);
    CHECK(!result.cache_hit);
    CHECK(fake.disasm_calls == 3U);
    CHECK(strcmp(first_dependency, result.metadata.dependency_sha256) != 0);
}

static void test_failure_and_cancellation_preserve_cache(
    const char *source_root,
    const char *temporary,
    PorpoiseDiagnostics *diagnostics) {
    char input[PORPOISE_PATH_CAPACITY];
    char tool[PORPOISE_PATH_CAPACITY];
    char cache[PORPOISE_PATH_CAPACITY];
    char marker[PORPOISE_PATH_CAPACITY];
    PorpoiseDtkImportOptions options;
    PorpoiseDtkImportResult result;
    PorpoiseOperationCallbacks callbacks;
    OperationState operation;
    FakeDtk fake;

    CHECK(fixture_path(input, sizeof(input), source_root, "input.elf"));
    CHECK(fixture_path(tool, sizeof(tool), source_root, "fake-dtk.bin"));
    CHECK(porpoise_path_join(cache, sizeof(cache), temporary, "cache"));
    CHECK(porpoise_path_join(marker, sizeof(marker), cache, "keep.marker"));
    CHECK(write_text(marker, "old cache must survive\n"));
    memset(&operation, 0, sizeof(operation));
    porpoise_operation_callbacks_init(&callbacks);
    callbacks.progress = test_progress;
    callbacks.cancelled = test_cancelled;
    callbacks.user_data = &operation;
    memset(&fake, 0, sizeof(fake));
    fake.mode = FAKE_DTK_VALID;
    fake.stages_were_fresh = true;
    fake.cancel_after_disasm = true;
    fake.operation = &operation;
    configure_managed(
        &options, input, tool, cache, "cancelled-settings", &fake, &callbacks);
    CHECK(porpoise_dtk_import_run(
              &options, &result, diagnostics) == PORPOISE_EXIT_CANCELLED);
    CHECK(porpoise_path_exists(marker));
    CHECK(!directory_has_import_artifact(temporary));

    operation.cancelled = false;
    fake.cancel_after_disasm = false;
    fake.mode = FAKE_DTK_ESCAPE_LINK_ORDER;
    options.settings_identity = "invalid-settings";
    CHECK(porpoise_dtk_import_run(
              &options, &result, diagnostics) != PORPOISE_EXIT_OK);
    CHECK(porpoise_path_exists(marker));
    CHECK(!directory_has_import_artifact(temporary));

    fake.mode = FAKE_DTK_PROCESS_FAILURE;
    options.settings_identity = "failed-process-settings";
    CHECK(porpoise_dtk_import_run(
              &options, &result, diagnostics) == PORPOISE_EXIT_TRANSLATION);
    CHECK(porpoise_path_exists(marker));
    CHECK(!directory_has_import_artifact(temporary));
}

static void test_validation_failures(
    const char *source_root,
    const char *temporary,
    PorpoiseDiagnostics *diagnostics) {
    static const FakeDtkMode modes[] = {
        FAKE_DTK_INVALID_ASSEMBLY,
        FAKE_DTK_MISSING_LINK_ORDER,
        FAKE_DTK_METADATA_INJECTION
    };
    char input[PORPOISE_PATH_CAPACITY];
    char tool[PORPOISE_PATH_CAPACITY];
    char cache[PORPOISE_PATH_CAPACITY];
    PorpoiseDtkImportOptions options;
    PorpoiseDtkImportResult result;
    FakeDtk fake;
    size_t index;

    CHECK(fixture_path(input, sizeof(input), source_root, "input.elf"));
    CHECK(fixture_path(tool, sizeof(tool), source_root, "fake-dtk.bin"));
    CHECK(porpoise_path_join(cache, sizeof(cache), temporary, "invalid-cache"));
    for (index = 0U; index < sizeof(modes) / sizeof(modes[0]); index++) {
        memset(&fake, 0, sizeof(fake));
        fake.mode = modes[index];
        fake.stages_were_fresh = true;
        configure_managed(
            &options, input, tool, cache, "invalid", &fake, NULL);
        CHECK(porpoise_dtk_import_run(
                  &options, &result, diagnostics) != PORPOISE_EXIT_OK);
        CHECK(!porpoise_path_exists(cache));
        CHECK(!directory_has_import_artifact(temporary));
    }

#ifndef _WIN32
    memset(&fake, 0, sizeof(fake));
    fake.mode = FAKE_DTK_CASE_COLLISION;
    fake.stages_were_fresh = true;
    configure_managed(
        &options, input, tool, cache, "case-collision", &fake, NULL);
    CHECK(porpoise_dtk_import_run(
              &options, &result, diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(!porpoise_path_exists(cache));
#endif

    memset(&fake, 0, sizeof(fake));
    fake.mode = FAKE_DTK_OLD_VERSION;
    fake.stages_were_fresh = true;
    configure_managed(
        &options, input, tool, cache, "old-version", &fake, NULL);
    CHECK(porpoise_dtk_import_run(
              &options, &result, diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(fake.info_calls == 0U);
    CHECK(fake.disasm_calls == 0U);

    memset(&fake, 0, sizeof(fake));
    fake.mode = FAKE_DTK_BAD_INFO;
    fake.stages_were_fresh = true;
    configure_managed(
        &options, input, tool, cache, "bad-info", &fake, NULL);
    CHECK(porpoise_dtk_import_run(
              &options, &result, diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(fake.info_calls == 1U);
    CHECK(fake.disasm_calls == 0U);
}

static void test_version_product_names(
    const char *source_root,
    const char *temporary) {
    static const FakeDtkMode rejected_modes[] = {
        FAKE_DTK_UNRELATED_VERSION,
        FAKE_DTK_UNSAFE_VERSION
    };
    char input[PORPOISE_PATH_CAPACITY];
    char tool[PORPOISE_PATH_CAPACITY];
    char cache[PORPOISE_PATH_CAPACITY];
    PorpoiseDtkImportOptions options;
    PorpoiseDtkImportResult result;
    PorpoiseDiagnostics diagnostics;
    FakeDtk fake;
    size_t index;

    porpoise_diagnostics_init(&diagnostics);
    CHECK(fixture_path(input, sizeof(input), source_root, "input.elf"));
    CHECK(fixture_path(tool, sizeof(tool), source_root, "fake-dtk.bin"));
    CHECK(porpoise_path_join(
        cache, sizeof(cache), temporary, "version-product-cache"));

    memset(&fake, 0, sizeof(fake));
    fake.mode = FAKE_DTK_RENAMED_VERSION;
    fake.stages_were_fresh = true;
    configure_managed(
        &options, input, tool, cache, "renamed-version", &fake, NULL);
    options.allow_cache_reuse = false;
    CHECK(porpoise_dtk_import_run(
              &options, &result, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(strcmp(
        result.metadata.dtk_version,
        "dtk-porpoise.exe 1.8.3 fixture") == 0);
    CHECK(fake.version_calls == 1U);
    CHECK(fake.info_calls == 1U);
    CHECK(fake.disasm_calls == 1U);
    CHECK(porpoise_remove_tree(cache, &diagnostics));

    for (index = 0U;
         index < sizeof(rejected_modes) / sizeof(rejected_modes[0]);
         index++) {
        porpoise_diagnostics_free(&diagnostics);
        porpoise_diagnostics_init(&diagnostics);
        memset(&fake, 0, sizeof(fake));
        fake.mode = rejected_modes[index];
        fake.stages_were_fresh = true;
        configure_managed(
            &options, input, tool, cache, "rejected-version", &fake, NULL);
        CHECK(porpoise_dtk_import_run(
                  &options, &result, &diagnostics) == PORPOISE_EXIT_USAGE);
        CHECK(fake.version_calls == 1U);
        CHECK(fake.info_calls == 0U);
        CHECK(fake.disasm_calls == 0U);
        CHECK(!porpoise_path_exists(cache));
    }
    porpoise_diagnostics_free(&diagnostics);
}

static bool test_set_environment(
    const char *name,
    const char *value) {
#ifdef _WIN32
    return _putenv_s(name, value == NULL ? "" : value) == 0;
#else
    return value == NULL ? unsetenv(name) == 0 : setenv(name, value, 1) == 0;
#endif
}

static bool test_make_executable(const char *path) {
    if (!write_text(path, "fake DTK discovery fixture\n")) return false;
#ifdef _WIN32
    return true;
#else
    return chmod(path, 0755) == 0;
#endif
}

static bool test_resolved_path_equal(
    const char *resolved,
    const char *expected) {
    char normalized[PORPOISE_PATH_CAPACITY];
    if (!porpoise_path_normalize_lexical(
            normalized, sizeof(normalized), expected)) {
        return false;
    }
#ifdef _WIN32
    return _stricmp(resolved, normalized) == 0;
#else
    return strcmp(resolved, normalized) == 0;
#endif
}

static bool test_diagnostics_contain(
    const PorpoiseDiagnostics *diagnostics,
    const char *fragment) {
    size_t index;
    for (index = 0U; index < diagnostics->count; index++) {
        if (strstr(diagnostics->items[index].message, fragment) != NULL)
            return true;
    }
    return false;
}

static void test_reset_diagnostics(PorpoiseDiagnostics *diagnostics) {
    porpoise_diagnostics_free(diagnostics);
    porpoise_diagnostics_init(diagnostics);
}

static void test_tool_discovery(
    const char *source_root,
    const char *temporary) {
#ifdef _WIN32
    static const char path_tool_name[] = "dtk.exe";
    static const char explicit_tool_name[] = "explicit-dtk.exe";
    static const char environment_tool_name[] = "environment-dtk.exe";
#else
    static const char path_tool_name[] = "dtk";
    static const char explicit_tool_name[] = "explicit-dtk";
    static const char environment_tool_name[] = "environment-dtk";
#endif
    const char *original_path_value = getenv("PATH");
    const char *original_dtk_value = getenv("PORPOISE_DTK");
    bool had_original_path = original_path_value != NULL;
    bool had_original_dtk = original_dtk_value != NULL;
    char *original_path = original_path_value == NULL ?
        NULL : porpoise_strdup(original_path_value);
    char *original_dtk = original_dtk_value == NULL ?
        NULL : porpoise_strdup(original_dtk_value);
    char root[PORPOISE_PATH_CAPACITY];
    char path_directory[PORPOISE_PATH_CAPACITY];
    char missing_directory[PORPOISE_PATH_CAPACITY];
    char input[PORPOISE_PATH_CAPACITY];
    char explicit_tool[PORPOISE_PATH_CAPACITY];
    char environment_tool[PORPOISE_PATH_CAPACITY];
    char path_tool[PORPOISE_PATH_CAPACITY];
    char missing_tool[PORPOISE_PATH_CAPACITY];
    char cache[PORPOISE_PATH_CAPACITY];
    PorpoiseDtkImportOptions options;
    PorpoiseDtkImportResult result;
    PorpoiseDiagnostics diagnostics;
    FakeDtk fake;

    if ((had_original_path && original_path == NULL) ||
        (had_original_dtk && original_dtk == NULL)) {
        CHECK(false);
        free(original_path);
        free(original_dtk);
        return;
    }
    if (!porpoise_path_join(
            root, sizeof(root), temporary, "tool-discovery") ||
        !porpoise_path_join(
            path_directory, sizeof(path_directory), root, "path") ||
        !porpoise_path_join(
            missing_directory, sizeof(missing_directory), root, "missing") ||
        !porpoise_path_join(
            explicit_tool, sizeof(explicit_tool), root, explicit_tool_name) ||
        !porpoise_path_join(
            environment_tool, sizeof(environment_tool), root,
            environment_tool_name) ||
        !porpoise_path_join(
            path_tool, sizeof(path_tool), path_directory, path_tool_name) ||
        !porpoise_path_join(
            missing_tool, sizeof(missing_tool), missing_directory,
            explicit_tool_name) ||
        !fixture_path(input, sizeof(input), source_root, "input.elf")) {
        CHECK(false);
        free(original_path);
        free(original_dtk);
        return;
    }
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_make_directories(path_directory, &diagnostics));
    CHECK(porpoise_make_directories(missing_directory, &diagnostics));
    CHECK(test_make_executable(explicit_tool));
    CHECK(test_make_executable(environment_tool));
    CHECK(test_make_executable(path_tool));

    /* PORPOISE_DTK wins over a different executable available on PATH. */
    CHECK(test_set_environment("PATH", path_directory));
    CHECK(test_set_environment("PORPOISE_DTK", environment_tool));
    CHECK(porpoise_path_join(cache, sizeof(cache), root, "cache-env"));
    memset(&fake, 0, sizeof(fake));
    fake.mode = FAKE_DTK_VALID;
    fake.stages_were_fresh = true;
    configure_managed(
        &options, input, NULL, cache, "discovery-env", &fake, NULL);
    options.allow_cache_reuse = false;
    CHECK(porpoise_dtk_import_run(
              &options, &result, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(test_resolved_path_equal(fake.executable_path, environment_tool));

    /* Without an override, the platform-appropriate dtk name is found on PATH. */
    CHECK(test_set_environment("PORPOISE_DTK", NULL));
    CHECK(porpoise_path_join(cache, sizeof(cache), root, "cache-path"));
    memset(&fake, 0, sizeof(fake));
    fake.mode = FAKE_DTK_VALID;
    fake.stages_were_fresh = true;
    configure_managed(
        &options, input, NULL, cache, "discovery-path", &fake, NULL);
    options.allow_cache_reuse = false;
    CHECK(porpoise_dtk_import_run(
              &options, &result, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(test_resolved_path_equal(fake.executable_path, path_tool));

    /* An explicit selection wins over both PORPOISE_DTK and PATH. */
    CHECK(test_set_environment("PORPOISE_DTK", environment_tool));
    CHECK(porpoise_path_join(cache, sizeof(cache), root, "cache-explicit"));
    memset(&fake, 0, sizeof(fake));
    fake.mode = FAKE_DTK_VALID;
    fake.stages_were_fresh = true;
    configure_managed(
        &options, input, explicit_tool, cache,
        "discovery-explicit", &fake, NULL);
    options.allow_cache_reuse = false;
    CHECK(porpoise_dtk_import_run(
              &options, &result, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(test_resolved_path_equal(fake.executable_path, explicit_tool));

    /* A bad explicit selection is authoritative and does not fall through. */
    test_reset_diagnostics(&diagnostics);
    CHECK(porpoise_path_join(
        cache, sizeof(cache), root, "cache-explicit-missing"));
    memset(&fake, 0, sizeof(fake));
    fake.mode = FAKE_DTK_VALID;
    fake.stages_were_fresh = true;
    configure_managed(
        &options, input, missing_tool, cache,
        "discovery-explicit-missing", &fake, NULL);
    CHECK(porpoise_dtk_import_run(
              &options, &result, &diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(fake.version_calls == 0U);
    CHECK(test_diagnostics_contain(&diagnostics, "explicit DTK selection"));
    CHECK(test_diagnostics_contain(&diagnostics, "PORPOISE_DTK"));

    /* A completely missing default reports every supported selection route. */
    test_reset_diagnostics(&diagnostics);
    CHECK(test_set_environment("PORPOISE_DTK", NULL));
    CHECK(test_set_environment("PATH", missing_directory));
    CHECK(porpoise_path_join(cache, sizeof(cache), root, "cache-missing"));
    memset(&fake, 0, sizeof(fake));
    fake.mode = FAKE_DTK_VALID;
    fake.stages_were_fresh = true;
    configure_managed(
        &options, input, NULL, cache, "discovery-missing", &fake, NULL);
    CHECK(porpoise_dtk_import_run(
              &options, &result, &diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(fake.version_calls == 0U);
    CHECK(test_diagnostics_contain(&diagnostics, "--dtk FILE"));
    CHECK(test_diagnostics_contain(&diagnostics, "PORPOISE_DTK"));
    CHECK(test_diagnostics_contain(&diagnostics, "PATH"));

    CHECK(test_set_environment(
        "PATH", had_original_path ? original_path : NULL));
    CHECK(test_set_environment(
        "PORPOISE_DTK", had_original_dtk ? original_dtk : NULL));
    porpoise_diagnostics_free(&diagnostics);
    free(original_path);
    free(original_dtk);
}

static void test_default_runner(
    const char *executable,
    PorpoiseDiagnostics *diagnostics) {
    const char *arguments[] = {
        executable,
        "--fake-child",
        "space value",
        "quote\"value",
        "trailing\\",
        NULL
    };
    PorpoiseDtkProcessResult process;
    porpoise_dtk_process_result_init(&process);
    CHECK(porpoise_dtk_run_process_default(
              NULL, arguments, NULL, NULL,
              &process, diagnostics) == PORPOISE_EXIT_OK);
    CHECK(process.exit_code == 7);
    CHECK(process.standard_output != NULL);
    if (process.standard_output != NULL) {
        CHECK(strstr(process.standard_output, "[space value]") != NULL);
        CHECK(strstr(process.standard_output, "[quote\"value]") != NULL);
        CHECK(strstr(process.standard_output, "[trailing\\]") != NULL);
    }
    CHECK(process.standard_error != NULL);
    if (process.standard_error != NULL)
        CHECK(strstr(process.standard_error, "fake child stderr") != NULL);
    porpoise_dtk_process_result_free(&process);
}

int main(int argc, char **argv) {
    char temporary[PORPOISE_PATH_CAPACITY];
    PorpoiseDiagnostics diagnostics;

    if (argc >= 2 && strcmp(argv[1], "--fake-child") == 0) {
        int index;
        for (index = 2; index < argc; index++) printf("[%s]\n", argv[index]);
        fprintf(stderr, "fake child stderr\n");
        return 7;
    }
    if (argc != 3) {
        fprintf(stderr, "usage: test_dtk_import SOURCE_ROOT BUILD_ROOT\n");
        return 2;
    }
    if (!porpoise_path_join(
            temporary, sizeof(temporary), argv[2], "dtk-import-test")) {
        fprintf(stderr, "temporary path is too long\n");
        return 2;
    }
    porpoise_diagnostics_init(&diagnostics);
    if (porpoise_path_exists(temporary) &&
        !porpoise_remove_tree(temporary, &diagnostics)) {
        fprintf(stderr, "cannot reset test directory\n");
        porpoise_diagnostics_free(&diagnostics);
        return 2;
    }
    if (!porpoise_make_directories(temporary, &diagnostics)) {
        fprintf(stderr, "cannot create test directory\n");
        porpoise_diagnostics_free(&diagnostics);
        return 2;
    }

    test_prepared(argv[1], &diagnostics);
    test_managed_cache(argv[1], temporary, &diagnostics);
    test_failure_and_cancellation_preserve_cache(
        argv[1], temporary, &diagnostics);
    test_validation_failures(argv[1], temporary, &diagnostics);
    test_version_product_names(argv[1], temporary);
    test_tool_discovery(argv[1], temporary);
    test_default_runner(argv[0], &diagnostics);
    CHECK(!directory_has_import_artifact(temporary));

    if (!porpoise_remove_tree(temporary, &diagnostics)) failures++;
    porpoise_diagnostics_free(&diagnostics);
    if (failures != 0U) {
        fprintf(stderr, "%u DTK import test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
