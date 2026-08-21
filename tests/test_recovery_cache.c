#include "porpoise/recovery_cache.h"

#include "porpoise/signature.h"
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

static bool join_path(
    char *output,
    size_t capacity,
    const char *left,
    const char *right) {
    return porpoise_path_join(output, capacity, left, right);
}

static bool write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    bool okay;
    if (file == NULL) return false;
    okay = fwrite(text, 1U, strlen(text), file) == strlen(text);
    return fclose(file) == 0 && okay;
}

static const PorpoiseFunction *find_function(
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

static void write_catalog_entry(
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
    if (contract != NULL)
        fprintf(file, ",\"contract\":\"%s\"", contract);
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

static bool create_catalog(
    const char *path,
    const PorpoiseFunctionSignature *first,
    const PorpoiseFunctionSignature *second) {
    FILE *file = fopen(path, "wb");
    bool okay;
    if (file == NULL) return false;
    fputs(
        "{\"schema_version\":1,\"signature_algorithm_version\":1,"
        "\"entries\":[",
        file);
    write_catalog_entry(file, "GXInit", "GXInit", first, true);
    write_catalog_entry(file, "UnknownSdk", NULL, second, false);
    fputs("]}\n", file);
    okay = !ferror(file);
    return fclose(file) == 0 && okay;
}

static int open_session(
    const char *input,
    const char *catalog,
    PorpoiseSession **session,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSessionOpenOptions options;
    const char *catalogs[1];
    porpoise_session_open_options_init(&options);
    options.input_path = input;
    if (catalog != NULL) {
        catalogs[0] = catalog;
        options.sdk_catalog_paths = catalogs;
        options.sdk_catalog_path_count = 1U;
    }
    return porpoise_session_open(&options, session, diagnostics);
}

static PorpoiseTranslationPlan *build_plan(
    PorpoiseSession *session,
    PorpoiseDiagnostics *diagnostics) {
    PorpoisePlanOptions options;
    PorpoiseTranslationPlan *plan = NULL;
    porpoise_plan_options_init(&options);
    options.target_id = "cache-target";
    options.module = "main";
    options.entry_symbol = "title_main";
    CHECK(porpoise_plan_build(session, &options, &plan, diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(porpoise_plan_validate(plan, diagnostics) == PORPOISE_EXIT_OK);
    return plan;
}

static void target_init(
    PorpoiseRecoveryTarget *target,
    const char *input) {
    memset(target, 0, sizeof(*target));
    target->id = porpoise_strdup("cache-target");
    target->input.value = porpoise_strdup(input);
    target->input.resolved = porpoise_strdup(input);
}

static void target_free(PorpoiseRecoveryTarget *target) {
    porpoise_recovery_target_cache_clear(&target->cache);
    free(target->id);
    free(target->input.value);
    free(target->input.resolved);
    memset(target, 0, sizeof(*target));
}

static bool cache_equal(
    const PorpoiseRecoveryTargetCache *left,
    const PorpoiseRecoveryTargetCache *right) {
    size_t index;
    if (strcmp(left->input_sha256, right->input_sha256) != 0 ||
        strcmp(left->settings_sha256, right->settings_sha256) != 0 ||
        ((left->dtk_version == NULL) != (right->dtk_version == NULL)) ||
        (left->dtk_version != NULL &&
         strcmp(left->dtk_version, right->dtk_version) != 0) ||
        left->dependency_count != right->dependency_count ||
        left->match_count != right->match_count) return false;
    for (index = 0U; index < left->dependency_count; index++) {
        const PorpoiseRecoveryDependencyCacheEntry *a =
            &left->dependencies[index];
        const PorpoiseRecoveryDependencyCacheEntry *b =
            &right->dependencies[index];
        if (strcmp(a->path.resolved, b->path.resolved) != 0 ||
            strcmp(a->sha256, b->sha256) != 0 ||
            a->size != b->size || a->mtime_ns != b->mtime_ns) return false;
    }
    for (index = 0U; index < left->match_count; index++) {
        const PorpoiseRecoveryMatchCacheEntry *a = &left->matches[index];
        const PorpoiseRecoveryMatchCacheEntry *b = &right->matches[index];
        if (strcmp(a->module, b->module) != 0 ||
            a->address != b->address || a->size != b->size ||
            strcmp(a->normalized_fingerprint,
                   b->normalized_fingerprint) != 0 ||
            strcmp(a->canonical_identity, b->canonical_identity) != 0 ||
            ((a->contract_name == NULL) != (b->contract_name == NULL)) ||
            (a->contract_name != NULL &&
             strcmp(a->contract_name, b->contract_name) != 0)) return false;
    }
    return true;
}

static void fill_dtk_metadata(
    PorpoiseDtkImportMetadata *metadata,
    const char *input_sha256) {
    memset(metadata, 0, sizeof(*metadata));
    metadata->schema_version = PORPOISE_DTK_IMPORT_METADATA_SCHEMA_VERSION;
    metadata->source_kind = PORPOISE_DTK_SOURCE_MANAGED_ELF;
    strcpy(metadata->dtk_version, "dtk 1.8.0");
    strcpy(metadata->input_sha256, input_sha256);
    memset(metadata->tool_sha256, '1', 64U);
    memset(metadata->settings_sha256, '2', 64U);
    memset(metadata->dependency_sha256, '3', 64U);
    memset(metadata->content_sha256, '4', 64U);
    metadata->tool_sha256[64] = '\0';
    metadata->settings_sha256[64] = '\0';
    metadata->dependency_sha256[64] = '\0';
    metadata->content_sha256[64] = '\0';
}

static void test_miss_states(
    const char *input,
    const PorpoiseRecoveryCacheInputs *inputs,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseRecoveryTarget target;
    PorpoiseRecoveryCacheValidation validation;
    target_init(&target, input);
    CHECK(porpoise_recovery_target_cache_validate(
              &target, inputs, NULL, &validation, diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(validation.state == PORPOISE_RECOVERY_CACHE_MISS);
    CHECK(validation.reason_flags == PORPOISE_RECOVERY_CACHE_REASON_ABSENT);
    target.cache.input_sha256 = porpoise_strdup(
        "0000000000000000000000000000000000000000000000000000000000000000");
    CHECK(porpoise_recovery_target_cache_validate(
              &target, inputs, NULL, &validation, diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(validation.state == PORPOISE_RECOVERY_CACHE_MISS);
    CHECK((validation.reason_flags &
           PORPOISE_RECOVERY_CACHE_REASON_INCOMPLETE) != 0U);
    target_free(&target);
}

static void test_rebuild_and_invalidation(
    const char *input,
    const char *dependency_a,
    const char *dependency_b,
    PorpoiseSession *session,
    PorpoiseTranslationPlan *plan,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseRecoveryTarget first;
    PorpoiseRecoveryTarget second;
    PorpoiseRecoveryTarget dtk_target;
    PorpoiseRecoveryCacheDependencyInput dependencies_a[3];
    PorpoiseRecoveryCacheDependencyInput dependencies_b[2];
    PorpoiseRecoveryCacheInputs inputs;
    PorpoiseRecoveryCacheInputs changed;
    PorpoiseRecoveryCacheValidation validation;
    PorpoiseDtkImportMetadata metadata;
    char preserved_hash[PORPOISE_SHA256_HEX_SIZE];
    bool matches_plan = false;
    size_t original_matches;
    int result;

    dependencies_a[0].path = dependency_b;
    dependencies_a[1].path = dependency_a;
    dependencies_a[2].path = dependency_a;
    dependencies_b[0].path = dependency_a;
    dependencies_b[1].path = dependency_b;
    porpoise_recovery_cache_inputs_init(&inputs);
    inputs.source_path = input;
    inputs.settings_identity = "settings-v1";
    inputs.dependencies = dependencies_a;
    inputs.dependency_count = 3U;
    target_init(&first, input);
    target_init(&second, input);
    target_init(&dtk_target, input);

    CHECK(porpoise_recovery_target_cache_rebuild(
              &first, &inputs, session, plan, NULL, diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(first.cache.dependency_count == 2U);
    CHECK(first.cache.match_count == 2U);
    CHECK(strcmp(first.cache.matches[0].module, "main") == 0);
    CHECK(strlen(first.cache.matches[0].normalized_fingerprint) == 64U);
    CHECK(first.cache.matches[0].canonical_identity != NULL);
    CHECK(strcmp(first.cache.dependencies[0].path.resolved,
                 first.cache.dependencies[1].path.resolved) < 0);
    CHECK(porpoise_recovery_target_cache_validate(
              &first, &inputs, NULL, &validation, diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(validation.state == PORPOISE_RECOVERY_CACHE_HIT);
    CHECK(validation.reason_flags == PORPOISE_RECOVERY_CACHE_REASON_NONE);
    CHECK(porpoise_recovery_target_cache_matches_plan(
              &first, plan, &matches_plan, diagnostics) == PORPOISE_EXIT_OK);
    CHECK(matches_plan);
    if (first.cache.match_count != 0U) {
        char *saved_identity = first.cache.matches[0].canonical_identity;
        char *different_identity = porpoise_strdup("DifferentSdkIdentity");
        CHECK(different_identity != NULL);
        if (different_identity != NULL) {
            first.cache.matches[0].canonical_identity = different_identity;
            matches_plan = true;
            CHECK(porpoise_recovery_target_cache_matches_plan(
                      &first, plan, &matches_plan, diagnostics) ==
                  PORPOISE_EXIT_OK);
            CHECK(!matches_plan);
            free(first.cache.matches[0].canonical_identity);
            first.cache.matches[0].canonical_identity = saved_identity;
        }
    }

    changed = inputs;
    changed.dependencies = dependencies_b;
    changed.dependency_count = 2U;
    CHECK(porpoise_recovery_target_cache_rebuild(
              &second, &changed, session, plan, NULL, diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(cache_equal(&first.cache, &second.cache));

    changed.settings_identity = "settings-v2";
    CHECK(porpoise_recovery_target_cache_validate(
              &first, &changed, NULL, &validation, diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(validation.state == PORPOISE_RECOVERY_CACHE_STALE);
    CHECK((validation.reason_flags &
           PORPOISE_RECOVERY_CACHE_REASON_SETTINGS_CHANGED) != 0U);

    fill_dtk_metadata(&metadata, first.cache.input_sha256);
    CHECK(porpoise_recovery_target_cache_rebuild(
              &dtk_target, &inputs, session, plan, &metadata, diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(dtk_target.cache.dtk_version != NULL);
    CHECK(porpoise_recovery_target_cache_validate(
              &dtk_target, &inputs, &metadata, &validation, diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(validation.state == PORPOISE_RECOVERY_CACHE_HIT);
    strcpy(metadata.dtk_version, "dtk 1.9.0");
    CHECK(porpoise_recovery_target_cache_validate(
              &dtk_target, &inputs, &metadata, &validation, diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK((validation.reason_flags &
           PORPOISE_RECOVERY_CACHE_REASON_DTK_CHANGED) != 0U);

    CHECK(write_text(dependency_a, "dependency-a changed\n"));
    CHECK(porpoise_recovery_target_cache_validate(
              &first, &inputs, NULL, &validation, diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(validation.state == PORPOISE_RECOVERY_CACHE_STALE);
    CHECK((validation.reason_flags &
           PORPOISE_RECOVERY_CACHE_REASON_DEPENDENCY_CONTENT_CHANGED) != 0U);
    CHECK(validation.dependency_index != SIZE_MAX);

    changed = inputs;
    changed.dependencies = dependencies_b;
    changed.dependency_count = 1U;
    CHECK(porpoise_recovery_target_cache_validate(
              &first, &changed, NULL, &validation, diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK((validation.reason_flags &
           PORPOISE_RECOVERY_CACHE_REASON_DEPENDENCY_SET_CHANGED) != 0U);

    strcpy(preserved_hash, first.cache.input_sha256);
    original_matches = first.cache.match_count;
    CHECK(remove(dependency_b) == 0);
    CHECK(porpoise_recovery_target_cache_validate(
              &first, &inputs, NULL, &validation, diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK((validation.reason_flags &
           PORPOISE_RECOVERY_CACHE_REASON_DEPENDENCY_MISSING) != 0U);
    result = porpoise_recovery_target_cache_rebuild(
        &first, &inputs, session, plan, NULL, diagnostics);
    CHECK(result == PORPOISE_EXIT_IO || result == PORPOISE_EXIT_USAGE);
    CHECK(strcmp(first.cache.input_sha256, preserved_hash) == 0);
    CHECK(first.cache.match_count == original_matches);

    CHECK(write_text(input, "# source changed after validated session\n"));
    CHECK(porpoise_recovery_target_cache_validate(
              &first, &inputs, NULL, &validation, diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK((validation.reason_flags &
           PORPOISE_RECOVERY_CACHE_REASON_INPUT_CHANGED) != 0U);

    if (second.cache.match_count != 0U) {
        PorpoiseRecoveryMatchCacheEntry *duplicate;
        size_t old_count = second.cache.match_count;
        duplicate = (PorpoiseRecoveryMatchCacheEntry *)realloc(
            second.cache.matches,
            (old_count + 1U) * sizeof(*second.cache.matches));
        CHECK(duplicate != NULL);
        if (duplicate != NULL) {
            PorpoiseRecoveryMatchCacheEntry *source;
            second.cache.matches = duplicate;
            source = &second.cache.matches[0];
            duplicate = &second.cache.matches[old_count];
            memset(duplicate, 0, sizeof(*duplicate));
            duplicate->module = porpoise_strdup(source->module);
            duplicate->address = source->address;
            duplicate->size = source->size;
            duplicate->normalized_fingerprint = porpoise_strdup(
                source->normalized_fingerprint);
            duplicate->canonical_identity = porpoise_strdup(
                "ConflictingSdkIdentity");
            duplicate->contract_name = porpoise_strdup(source->contract_name);
            second.cache.match_count++;
            CHECK(porpoise_recovery_target_cache_validate(
                      &second, &changed, NULL, &validation, diagnostics) ==
                  PORPOISE_EXIT_USAGE);
            CHECK(validation.state == PORPOISE_RECOVERY_CACHE_INVALID);
            CHECK((validation.reason_flags &
                   PORPOISE_RECOVERY_CACHE_REASON_CONFLICTING_MATCH) != 0U);
        }
    }

    target_free(&dtk_target);
    target_free(&second);
    target_free(&first);
}

int main(int argc, char **argv) {
    char fixture[PORPOISE_PATH_CAPACITY];
    char temporary[PORPOISE_PATH_CAPACITY];
    char input[PORPOISE_PATH_CAPACITY];
    char dependency_a[PORPOISE_PATH_CAPACITY];
    char dependency_b[PORPOISE_PATH_CAPACITY];
    char catalog[PORPOISE_PATH_CAPACITY];
    PorpoiseSession *unsigned_session = NULL;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseFunctionSignature first_signature;
    PorpoiseFunctionSignature second_signature;
    const PorpoiseProgram *program;
    PorpoiseRecoveryCacheInputs inputs;
    PorpoiseDiagnostics diagnostics;

    if (argc != 3) return 2;
    porpoise_diagnostics_init(&diagnostics);
    CHECK(join_path(
        fixture, sizeof(fixture), argv[1],
        "tests/fixtures/sdk_policy/input.s"));
    CHECK(join_path(
        temporary, sizeof(temporary), argv[2], "recovery-cache-test"));
    CHECK(porpoise_remove_tree(temporary, &diagnostics));
    CHECK(porpoise_make_directories(temporary, &diagnostics));
    CHECK(join_path(input, sizeof(input), temporary, "input.s"));
    CHECK(join_path(
        dependency_a, sizeof(dependency_a), temporary, "a.map"));
    CHECK(join_path(
        dependency_b, sizeof(dependency_b), temporary, "b.json"));
    CHECK(join_path(catalog, sizeof(catalog), temporary, "catalog.json"));
    CHECK(porpoise_copy_file(fixture, input, &diagnostics));
    CHECK(write_text(dependency_a, "dependency-a\n"));
    CHECK(write_text(dependency_b, "dependency-b\n"));

    CHECK(open_session(input, NULL, &unsigned_session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    program = porpoise_session_program(unsigned_session);
    CHECK(porpoise_signature_compute(
        program, find_function(program, "GXInit"), &first_signature));
    CHECK(porpoise_signature_compute(
        program, find_function(program, "UnknownSdk"), &second_signature));
    CHECK(create_catalog(catalog, &first_signature, &second_signature));
    porpoise_session_close(unsigned_session);
    unsigned_session = NULL;

    CHECK(open_session(input, catalog, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    plan = build_plan(session, &diagnostics);
    porpoise_recovery_cache_inputs_init(&inputs);
    inputs.source_path = input;
    inputs.settings_identity = "settings-v1";
    test_miss_states(input, &inputs, &diagnostics);
    test_rebuild_and_invalidation(
        input, dependency_a, dependency_b, session, plan, &diagnostics);

    porpoise_plan_free(plan);
    porpoise_session_close(session);
    CHECK(porpoise_remove_tree(temporary, &diagnostics));
    porpoise_diagnostics_free(&diagnostics);
    return failures == 0U ? 0 : 1;
}
