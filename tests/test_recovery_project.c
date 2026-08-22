#include "porpoise/recovery_project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <process.h>
#define TEST_GETPID() _getpid()
#define TEST_MKDIR(path) _mkdir(path)
#define TEST_RMDIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define TEST_GETPID() getpid()
#define TEST_MKDIR(path) mkdir((path), 0777)
#define TEST_RMDIR(path) rmdir(path)
#endif

static unsigned int failures = 0U;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                     \
                    __FILE__, __LINE__, #condition);                            \
            failures++;                                                        \
        }                                                                       \
    } while (0)

static bool fixture_path(
    char *output,
    size_t capacity,
    const char *root,
    const char *name) {
    int written = snprintf(
        output, capacity, "%s/tests/fixtures/recovery_project/%s",
        root, name);
    return written >= 0 && (size_t)written < capacity;
}

static void reset_diagnostics(PorpoiseDiagnostics *diagnostics) {
    porpoise_diagnostics_free(diagnostics);
    porpoise_diagnostics_init(diagnostics);
}

static char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    long size;
    char *text;
    if (file == NULL) return NULL;
    if (fseek(file, 0L, SEEK_END) != 0 || (size = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    text = (char *)malloc((size_t)size + 1U);
    if (text == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(text, 1U, (size_t)size, file) != (size_t)size) {
        fclose(file);
        free(text);
        return NULL;
    }
    fclose(file);
    text[size] = '\0';
    return text;
}

static bool write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    int write_result;
    int close_result;
    if (file == NULL) return false;
    write_result = fputs(text, file);
    close_result = fclose(file);
    return write_result >= 0 && close_result == 0;
}

static void check_loaded_project(const PorpoiseRecoveryProject *project) {
    const PorpoiseRecoveryTarget *main_target;
    const PorpoiseRecoveryTarget *overlay;
    CHECK(project->schema_version == PORPOISE_RECOVERY_PROJECT_SCHEMA_VERSION);
    CHECK(project->path != NULL);
    CHECK(project->directory != NULL);
    CHECK(project->sdk_catalog_count == 2U);
    CHECK(project->abi_contract_count == 2U);
    CHECK(project->target_count == 2U);
    CHECK(strcmp(project->sdk_catalogs[1].resolved,
                 "/opt/porpoise/catalogs/local.json") == 0);
    CHECK(strcmp(project->abi_contracts[1].resolved,
                 "//server/share/contracts.json") == 0);

    main_target = porpoise_recovery_project_find_target(project, "main-dol");
    overlay = porpoise_recovery_project_find_target(project, "overlay-rel");
    CHECK(main_target != NULL);
    CHECK(overlay != NULL);
    if (main_target != NULL) {
        CHECK(main_target->enabled);
        CHECK(main_target->source_kind == PORPOISE_RECOVERY_SOURCE_ASSEMBLY);
        CHECK(main_target->strict);
        CHECK(main_target->sdk_policy == PORPOISE_SDK_POLICY_IMPORTED);
        CHECK(strcmp(main_target->entry, "_start") == 0);
        CHECK(main_target->symbol_source_count == 2U);
        CHECK(main_target->has_skip_list);
        CHECK(main_target->override_count == 1U);
        CHECK(main_target->annotation_count == 1U);
        CHECK(main_target->cache.dependency_count == 1U);
        CHECK(main_target->cache.match_count == 1U);
        CHECK(main_target->cache.dependencies[0].size == UINT64_C(9876543210));
        CHECK(main_target->cache.dependencies[0].mtime_ns == UINT64_MAX);
        CHECK(strcmp(main_target->cache.dependencies[0].path.resolved,
                     "Z:/NintendoSDK/demo.a") == 0);
        CHECK(main_target->overrides[0].action == PORPOISE_OVERRIDE_IMPORT);
        CHECK(main_target->overrides[0].acknowledge_conflict);
        CHECK(strcmp(main_target->overrides[0].contract_name,
                     "OSReport") == 0);
        CHECK(main_target->annotations[0].interpretation ==
              PORPOISE_RECOVERY_ANNOTATION_SHIFT_JIS);
        CHECK(main_target->annotations[0].element_count == 8U);
        CHECK(strcmp(main_target->annotations[0].encoding,
                     "shift-jis") == 0);
        /* Loading is deliberately independent of cache freshness/existence. */
        CHECK(main_target->input.resolved != NULL);
        CHECK(main_target->cache.input_sha256 != NULL);
        CHECK(!main_target->has_title_host);
    }
    if (overlay != NULL) {
        CHECK(!overlay->enabled);
        CHECK(overlay->source_kind == PORPOISE_RECOVERY_SOURCE_MANAGED_ELF);
        CHECK(overlay->entry == NULL);
        CHECK(overlay->sdk_policy == PORPOISE_SDK_POLICY_KEEP);
        CHECK(strcmp(overlay->input.resolved,
                     "C:/recovery-inputs/overlay.elf") == 0);
        CHECK(!overlay->has_title_host);
    }
}

static void test_load_and_stale_independence(const char *root) {
    char path[PORPOISE_PATH_CAPACITY];
    PorpoiseRecoveryProject project;
    PorpoiseDiagnostics diagnostics;
    CHECK(fixture_path(
        path, sizeof(path), root, "valid.porpoise.json"));
    porpoise_recovery_project_init(&project);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_recovery_project_load(
              &project, path, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    check_loaded_project(&project);
    CHECK(strcmp(project.sdk_catalogs[1].value,
                 "/opt/porpoise/catalogs/local.json") == 0);
    CHECK(porpoise_recovery_project_find_target(&project, "missing") == NULL);
    CHECK(porpoise_recovery_project_find_target_mutable(
              &project, "main-dol") == &project.targets[0]);
    porpoise_diagnostics_free(&diagnostics);
    porpoise_recovery_project_free(&project);
}

static void test_strict_failures_are_transactional(const char *root) {
    static const char *const malformed[] = {
        "unknown-key.porpoise.json",
        "duplicate-key.porpoise.json",
        "wrong-type.porpoise.json",
        "expansion.porpoise.json",
        "duplicate-target-field.porpoise.json",
        "wrong-nested-type.porpoise.json",
        "bad-fingerprint.porpoise.json",
        "invalid-target-id.porpoise.json",
        "invalid-v2-missing-title-host.porpoise.json"
    };
    char valid_path[PORPOISE_PATH_CAPACITY];
    PorpoiseRecoveryProject project;
    PorpoiseDiagnostics diagnostics;
    size_t index;
    CHECK(fixture_path(
        valid_path, sizeof(valid_path), root, "valid.porpoise.json"));
    porpoise_recovery_project_init(&project);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_recovery_project_load(
              &project, valid_path, &diagnostics) == PORPOISE_EXIT_OK);
    for (index = 0U; index < sizeof(malformed) / sizeof(malformed[0]); index++) {
        char path[PORPOISE_PATH_CAPACITY];
        CHECK(fixture_path(path, sizeof(path), root, malformed[index]));
        reset_diagnostics(&diagnostics);
        CHECK(porpoise_recovery_project_load(
                  &project, path, &diagnostics) == PORPOISE_EXIT_USAGE);
        CHECK(porpoise_diagnostics_have_errors(&diagnostics));
        CHECK(project.target_count == 2U);
        CHECK(porpoise_recovery_project_find_target(
                  &project, "main-dol") != NULL);
    }
    porpoise_diagnostics_free(&diagnostics);
    porpoise_recovery_project_free(&project);
}

static void test_save_rebase_reopen_and_determinism(const char *root) {
    char input_path[PORPOISE_PATH_CAPACITY];
    char first_path[PORPOISE_PATH_CAPACITY];
    char second_path[PORPOISE_PATH_CAPACITY];
    PorpoiseRecoveryProject project;
    PorpoiseRecoveryProject reopened;
    PorpoiseDiagnostics diagnostics;
    char *first_text;
    char *second_text;
    CHECK(snprintf(
              first_path, sizeof(first_path),
              "recovery-project-roundtrip-%lu-a.json",
              (unsigned long)TEST_GETPID()) > 0);
    CHECK(snprintf(
              second_path, sizeof(second_path),
              "recovery-project-roundtrip-%lu-b.json",
              (unsigned long)TEST_GETPID()) > 0);
    (void)remove(first_path);
    (void)remove(second_path);
    CHECK(fixture_path(
        input_path, sizeof(input_path), root, "valid.porpoise.json"));
    porpoise_recovery_project_init(&project);
    porpoise_recovery_project_init(&reopened);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_recovery_project_load(
              &project, input_path, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(porpoise_recovery_project_save(
              &project, first_path, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(porpoise_recovery_project_save(
              &project, second_path, &diagnostics) == PORPOISE_EXIT_OK);
    first_text = read_text(first_path);
    second_text = read_text(second_path);
    CHECK(first_text != NULL);
    CHECK(second_text != NULL);
    if (first_text != NULL && second_text != NULL) {
        CHECK(strcmp(first_text, second_text) == 0);
        /* The canonical spelling is relative when the test build and this
         * synthetic C: input happen to share a volume, and absolute when
         * they do not. Reopening below verifies the resolved identity. */
        CHECK(strstr(first_text,
                     "recovery-inputs/overlay.elf") != NULL);
        CHECK(strstr(first_text, "Z:/NintendoSDK/demo.a") != NULL);
        CHECK(strstr(first_text,
                     "/opt/porpoise/catalogs/local.json") != NULL);
        CHECK(strstr(first_text,
                     "//server/share/contracts.json") != NULL);
        CHECK(strstr(first_text, "\"schema_version\": 2") != NULL);
        CHECK(strstr(first_text, "\"title_host\": null") != NULL);
        CHECK(strstr(first_text, "${") == NULL);
    }
    CHECK(porpoise_recovery_project_load(
              &reopened, first_path, &diagnostics) == PORPOISE_EXIT_OK);
    check_loaded_project(&reopened);
#ifdef _WIN32
    CHECK(strcmp(reopened.sdk_catalogs[1].value,
                 "/opt/porpoise/catalogs/local.json") == 0);
#else
    /* POSIX paths share one root, so canonical saves rebase /opt as relative. */
    CHECK(reopened.sdk_catalogs[1].value[0] != '/');
#endif
    CHECK(strcmp(project.targets[0].input.resolved,
                 reopened.targets[0].input.resolved) == 0);
    CHECK(strcmp(project.targets[0].output.resolved,
                 reopened.targets[0].output.resolved) == 0);
    CHECK(strcmp(project.sdk_catalogs[0].resolved,
                 reopened.sdk_catalogs[0].resolved) == 0);
    free(first_text);
    free(second_text);
    (void)remove(first_path);
    (void)remove(second_path);
    porpoise_diagnostics_free(&diagnostics);
    porpoise_recovery_project_free(&reopened);
    porpoise_recovery_project_free(&project);
}

static void test_v2_title_host_roundtrip(const char *root) {
    char input_path[PORPOISE_PATH_CAPACITY];
    char output_path[PORPOISE_PATH_CAPACITY];
    PorpoiseRecoveryProject project;
    PorpoiseRecoveryProject reopened;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseRecoveryTarget *target;
    char *text;
    CHECK(fixture_path(
        input_path, sizeof(input_path), root, "valid-v2.porpoise.json"));
    CHECK(snprintf(
              output_path, sizeof(output_path),
              "recovery-project-v2-roundtrip-%lu.json",
              (unsigned long)TEST_GETPID()) > 0);
    (void)remove(output_path);
    porpoise_recovery_project_init(&project);
    porpoise_recovery_project_init(&reopened);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_recovery_project_load(
              &project, input_path, &diagnostics) == PORPOISE_EXIT_OK);
    target = porpoise_recovery_project_find_target(&project, "main");
    CHECK(target != NULL && target->has_title_host);
    if (target != NULL && target->has_title_host) {
        CHECK(target->title_host.entry_address == UINT32_C(0x80001000));
        CHECK(target->title_host.gpr[1] == UINT32_C(0x80004000));
        CHECK(target->title_host.gpr[2] == UINT32_C(0x80005000));
        CHECK(target->title_host.gpr[13] == UINT32_C(0x80006000));
        CHECK(target->title_host.startup_function_count == 1U);
        CHECK(target->title_host.initial_word_count == 1U);
        CHECK(target->title_host.initialize_dvd);
    }
    CHECK(porpoise_recovery_project_save(
              &project, output_path, &diagnostics) == PORPOISE_EXIT_OK);
    text = read_text(output_path);
    CHECK(text != NULL && strstr(text, "\"title_host\": {") != NULL);
    CHECK(text != NULL && strstr(text, "\"entry_address\": 2147487744") != NULL);
    free(text);
    CHECK(porpoise_recovery_project_load(
              &reopened, output_path, &diagnostics) == PORPOISE_EXIT_OK);
    target = porpoise_recovery_project_find_target(&reopened, "main");
    CHECK(target != NULL && target->has_title_host);
    CHECK(target != NULL &&
          target->title_host.initial_words[0].value == UINT32_MAX);
    (void)remove(output_path);
    porpoise_diagnostics_free(&diagnostics);
    porpoise_recovery_project_free(&reopened);
    porpoise_recovery_project_free(&project);
}

static void test_v1_legacy_target_id_roundtrip(const char *root) {
    char input_path[PORPOISE_PATH_CAPACITY];
    char output_path[PORPOISE_PATH_CAPACITY];
    PorpoiseRecoveryProject project;
    PorpoiseRecoveryProject reopened;
    PorpoiseDiagnostics diagnostics;
    char *text;
    CHECK(fixture_path(
        input_path, sizeof(input_path), root,
        "valid-v1-legacy-id.porpoise.json"));
    CHECK(snprintf(
              output_path, sizeof(output_path),
              "recovery-project-v1-legacy-id-%lu.json",
              (unsigned long)TEST_GETPID()) > 0);
    (void)remove(output_path);
    porpoise_recovery_project_init(&project);
    porpoise_recovery_project_init(&reopened);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_recovery_project_load(
              &project, input_path, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(porpoise_recovery_project_find_target(
              &project, "legacy.overlay/one") != NULL);
    CHECK(porpoise_recovery_project_save(
              &project, output_path, &diagnostics) == PORPOISE_EXIT_OK);
    text = read_text(output_path);
    CHECK(text != NULL && strstr(text, "\"schema_version\": 2") != NULL);
    CHECK(text != NULL &&
          strstr(text, "\"id\": \"legacy.overlay/one\"") != NULL);
    free(text);
    CHECK(porpoise_recovery_project_load(
              &reopened, output_path, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(porpoise_recovery_project_find_target(
              &reopened, "legacy.overlay/one") != NULL);
    (void)remove(output_path);
    porpoise_diagnostics_free(&diagnostics);
    porpoise_recovery_project_free(&reopened);
    porpoise_recovery_project_free(&project);
}

static void test_atomic_save_preserves_prior_destination(const char *root) {
    static const char prior_serialization[] =
        "prior project bytes before serialization failure\n";
    static const char prior_publication[] =
        "prior project bytes before publication failure\n";
    char save_path[PORPOISE_PATH_CAPACITY];
    char directory_path[PORPOISE_PATH_CAPACITY];
    char directory_sentinel[PORPOISE_PATH_CAPACITY];
    char input_path[PORPOISE_PATH_CAPACITY];
    PorpoiseRecoveryProject project;
    PorpoiseRecoveryProject reopened;
    PorpoiseDiagnostics diagnostics;
    char *saved_catalog_value;
    char *saved_catalog_resolved;
    char *text;

    CHECK(snprintf(
              save_path, sizeof(save_path),
              "recovery-project-atomic-save-%lu.json",
              (unsigned long)TEST_GETPID()) > 0);
    CHECK(snprintf(
              directory_path, sizeof(directory_path),
              "recovery-project-save-directory-%lu",
              (unsigned long)TEST_GETPID()) > 0);
    CHECK(snprintf(
              directory_sentinel, sizeof(directory_sentinel),
              "%s/prior.txt", directory_path) > 0);
    CHECK(fixture_path(
        input_path, sizeof(input_path), root, "valid.porpoise.json"));
    porpoise_recovery_project_init(&project);
    porpoise_recovery_project_init(&reopened);
    porpoise_diagnostics_init(&diagnostics);
    (void)remove(save_path);
    (void)remove(directory_sentinel);
    (void)TEST_RMDIR(directory_path);
    CHECK(porpoise_recovery_project_load(
              &project, input_path, &diagnostics) == PORPOISE_EXIT_OK);

    CHECK(write_text(save_path, prior_serialization));
    saved_catalog_value = project.sdk_catalogs[0].value;
    saved_catalog_resolved = project.sdk_catalogs[0].resolved;
    project.sdk_catalogs[0].value = NULL;
    project.sdk_catalogs[0].resolved = NULL;
    reset_diagnostics(&diagnostics);
    CHECK(porpoise_recovery_project_save(
              &project, save_path, &diagnostics) == PORPOISE_EXIT_IO);
    project.sdk_catalogs[0].value = saved_catalog_value;
    project.sdk_catalogs[0].resolved = saved_catalog_resolved;
    text = read_text(save_path);
    CHECK(text != NULL && strcmp(text, prior_serialization) == 0);
    free(text);

    reset_diagnostics(&diagnostics);
    CHECK(porpoise_recovery_project_save(
              &project, save_path, &diagnostics) == PORPOISE_EXIT_OK);
    text = read_text(save_path);
    CHECK(text != NULL && strcmp(text, prior_serialization) != 0);
    CHECK(text != NULL && strncmp(text, "{\n", 2U) == 0);
    free(text);
    CHECK(porpoise_recovery_project_load(
              &reopened, save_path, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(reopened.target_count == project.target_count);

#ifdef _WIN32
    {
        HANDLE locked;
        CHECK(write_text(save_path, prior_publication));
        locked = CreateFileA(
            save_path, GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK(locked != INVALID_HANDLE_VALUE);
        if (locked != INVALID_HANDLE_VALUE) {
            reset_diagnostics(&diagnostics);
            CHECK(porpoise_recovery_project_save(
                      &project, save_path, &diagnostics) ==
                  PORPOISE_EXIT_IO);
            CHECK(CloseHandle(locked) != 0);
            text = read_text(save_path);
            CHECK(text != NULL && strcmp(text, prior_publication) == 0);
            free(text);
        }
    }
#endif

    CHECK(TEST_MKDIR(directory_path) == 0);
    CHECK(write_text(directory_sentinel, prior_publication));
    reset_diagnostics(&diagnostics);
    CHECK(porpoise_recovery_project_save(
              &project, directory_path, &diagnostics) == PORPOISE_EXIT_IO);
    text = read_text(directory_sentinel);
    CHECK(text != NULL && strcmp(text, prior_publication) == 0);
    free(text);

    (void)remove(save_path);
    (void)remove(directory_sentinel);
    (void)TEST_RMDIR(directory_path);
    porpoise_diagnostics_free(&diagnostics);
    porpoise_recovery_project_free(&reopened);
    porpoise_recovery_project_free(&project);
}

static void test_enum_names(void) {
    PorpoiseRecoverySourceKind source_kind;
    PorpoiseRecoveryAnnotationInterpretation interpretation;
    int value;
    CHECK(porpoise_recovery_source_kind_from_name(
        "assembly", &source_kind));
    CHECK(source_kind == PORPOISE_RECOVERY_SOURCE_ASSEMBLY);
    CHECK(porpoise_recovery_source_kind_from_name(
        "managed_elf", &source_kind));
    CHECK(source_kind == PORPOISE_RECOVERY_SOURCE_MANAGED_ELF);
    CHECK(porpoise_recovery_source_kind_from_name(
        "dtk_prepared_assembly", &source_kind));
    CHECK(source_kind == PORPOISE_RECOVERY_SOURCE_DTK_PREPARED_ASSEMBLY);
    CHECK(!porpoise_recovery_source_kind_from_name("elf", NULL));
    for (value = PORPOISE_RECOVERY_ANNOTATION_RAW_BYTES;
         value <= PORPOISE_RECOVERY_ANNOTATION_POINTER32_ARRAY; value++) {
        const char *name = porpoise_recovery_annotation_interpretation_name(
            (PorpoiseRecoveryAnnotationInterpretation)value);
        CHECK(porpoise_recovery_annotation_interpretation_from_name(
            name, &interpretation));
        CHECK((int)interpretation == value);
    }
    CHECK(!porpoise_recovery_annotation_interpretation_from_name(
        "cstring", NULL));
    CHECK(porpoise_recovery_target_id_is_valid("main-dol_2"));
    CHECK(porpoise_recovery_target_id_is_valid("main.overlay"));
    CHECK(porpoise_recovery_target_id_is_valid("legacy/overlay"));
    CHECK(porpoise_recovery_target_id_is_valid(".."));
    CHECK(!porpoise_recovery_target_id_is_valid(NULL));
    CHECK(!porpoise_recovery_target_id_is_valid(""));
    {
        char first[PORPOISE_RECOVERY_TARGET_CACHE_KEY_SIZE];
        char second[PORPOISE_RECOVERY_TARGET_CACHE_KEY_SIZE];
        CHECK(porpoise_recovery_target_cache_key("main-dol_2", first));
        CHECK(strcmp(first, "main-dol_2") == 0);
        CHECK(porpoise_recovery_target_cache_key("CON", first));
        CHECK(strncmp(first, "target-", 7U) == 0);
        CHECK(porpoise_recovery_target_cache_key("main/overlay", first));
        CHECK(porpoise_recovery_target_cache_key("main\\overlay", second));
        CHECK(strncmp(first, "target-", 7U) == 0);
        CHECK(strlen(first) == 71U);
        CHECK(strcmp(first, second) != 0);
        CHECK(!porpoise_recovery_target_cache_key("", first));
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: test_recovery_project SOURCE_ROOT\n");
        return 2;
    }
    test_load_and_stale_independence(argv[1]);
    test_strict_failures_are_transactional(argv[1]);
    test_save_rebase_reopen_and_determinism(argv[1]);
    test_v2_title_host_roundtrip(argv[1]);
    test_v1_legacy_target_id_roundtrip(argv[1]);
    test_atomic_save_preserves_prior_destination(argv[1]);
    test_enum_names();
    if (failures != 0U) {
        fprintf(stderr, "%u recovery project test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
