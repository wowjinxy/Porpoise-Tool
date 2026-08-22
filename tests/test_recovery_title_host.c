#include "porpoise/recovery_title_host.h"

#include "porpoise/session.h"
#include "porpoise/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#define TEST_GETPID() _getpid()
#else
#include <unistd.h>
#define TEST_GETPID() getpid()
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

static char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    long length;
    char *text;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (length = ftell(file)) < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    text = (char *)malloc((size_t)length + 1U);
    if (text == NULL ||
        fread(text, 1U, (size_t)length, file) != (size_t)length ||
        fclose(file) != 0) {
        free(text);
        return NULL;
    }
    text[length] = '\0';
    return text;
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

static void reset_diagnostics(PorpoiseDiagnostics *diagnostics) {
    porpoise_diagnostics_free(diagnostics);
    porpoise_diagnostics_init(diagnostics);
}

static void test_validation_and_generation(const char *source_root) {
    static const char input_digest[] =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    char input[PORPOISE_PATH_CAPACITY];
    char symbols[PORPOISE_PATH_CAPACITY];
    char output[PORPOISE_PATH_CAPACITY];
    char source_path[PORPOISE_PATH_CAPACITY];
    char meson_path[PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions session_options;
    PorpoiseSessionSymbolSource symbol_source;
    PorpoiseRecoverySymbolSource target_symbol_source;
    PorpoisePlanOptions plan_options;
    PorpoisePlanOptions override_plan_options;
    PorpoiseFunctionOverride omit_override;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseTranslationPlan *omit_plan = NULL;
    PorpoiseRecoveryTarget target;
    PorpoiseRecoveryTitleHostProfile *profile;
    PorpoiseRecoveryTitleHostProfile inferred;
    const PorpoiseFunctionPlanView *startup;
    PorpoiseDiagnostics diagnostics;
    char *source = NULL;
    char *meson = NULL;
    char prior_fingerprint;

    CHECK(snprintf(
              input, sizeof(input), "%s/tests/fixtures/title_host/input.s",
              source_root) > 0);
    CHECK(snprintf(
              symbols, sizeof(symbols),
              "%s/tests/fixtures/title_host/symbols.txt",
              source_root) > 0);
    CHECK(snprintf(
              output, sizeof(output), ".porpoise-title-host-%lu",
              (unsigned long)TEST_GETPID()) > 0);
    CHECK(porpoise_path_join(
        source_path, sizeof(source_path), output,
        "porpoise_recovery_title_host.c"));
    CHECK(porpoise_path_join(
        meson_path, sizeof(meson_path), output, "meson.build"));

    memset(&target, 0, sizeof(target));
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_remove_tree(output, &diagnostics));
    porpoise_session_open_options_init(&session_options);
    session_options.input_path = input;
    memset(&symbol_source, 0, sizeof(symbol_source));
    symbol_source.kind = PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS;
    symbol_source.path = symbols;
    symbol_source.module = "";
    session_options.symbol_sources = &symbol_source;
    session_options.symbol_source_count = 1U;
    CHECK(porpoise_session_open(
              &session_options, &session, &diagnostics) == PORPOISE_EXIT_OK);
    porpoise_plan_options_init(&plan_options);
    plan_options.entry_symbol = "main";
    plan_options.target_id = "main";
    plan_options.module = "";
    CHECK(porpoise_plan_build(
              session, &plan_options, &plan, &diagnostics) ==
          PORPOISE_EXIT_OK);
    startup = porpoise_plan_find_function(plan, "__OSThreadInit");
    CHECK(startup != NULL);

    target.id = "main";
    target.input.resolved = input;
    target.cache.input_sha256 = (char *)input_digest;
    memset(&target_symbol_source, 0, sizeof(target_symbol_source));
    target_symbol_source.kind = PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS;
    target_symbol_source.path.resolved = symbols;
    target_symbol_source.module = "";
    target.symbol_sources = &target_symbol_source;
    target.symbol_source_count = 1U;
    target.has_title_host = true;
    profile = &target.title_host;
    porpoise_recovery_title_host_profile_init(profile);
    porpoise_recovery_title_host_profile_init(&inferred);
    CHECK(porpoise_recovery_title_host_infer(
              &target, plan, &inferred, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(inferred.entry_address == UINT32_C(0x80001000));
    CHECK(inferred.gpr[1] == UINT32_C(0x80008000));
    CHECK(inferred.gpr[2] == UINT32_C(0x8000A000));
    CHECK(inferred.gpr[13] == UINT32_C(0x80009000));
    CHECK(inferred.arena_lo == UINT32_C(0x80010000));
    CHECK(inferred.arena_hi == UINT32_C(0x81700000));
    CHECK(inferred.startup_function_count == 2U);
    CHECK(inferred.startup_functions[0].address == UINT32_C(0x80002000));
    CHECK(inferred.startup_functions[1].address == UINT32_C(0x80003000));
    CHECK(inferred.initial_word_count == 2U);
    CHECK(inferred.initial_words[0].address == UINT32_C(0x80008000));
    CHECK(inferred.initial_words[0].value == UINT32_MAX);
    CHECK(!inferred.initialize_dvd);
    CHECK(inferred.symbol_sources_sha256 != NULL);
    CHECK(inferred.symbol_sources_sha256 == NULL ||
          strlen(inferred.symbol_sources_sha256) == 64U);
    CHECK(inferred.sdk_catalogs_sha256 == NULL);
    profile->entry_address = UINT32_C(0x80001000);
    profile->gpr[1] = UINT32_C(0x80004000);
    profile->gpr[2] = UINT32_C(0x80005000);
    profile->gpr[13] = UINT32_C(0x80006000);
    profile->arena_lo = UINT32_C(0x80007000);
    profile->arena_hi = UINT32_C(0x81700000);
    profile->startup_function_count = 1U;
    profile->startup_functions[0].module = porpoise_strdup("");
    profile->startup_functions[0].address = UINT32_C(0x80002000);
    profile->startup_functions[0].size = 4U;
    profile->startup_functions[0].normalized_fingerprint =
        porpoise_strdup(startup == NULL ? "" : startup->signature.digest_hex);
    profile->startup_functions[0].flags =
        PORPOISE_RECOVERY_TITLE_STARTUP_ESTABLISH_GUEST_MAIN_THREAD_AFTER;
    profile->initial_word_count = 2U;
    profile->initial_words[0].address = UINT32_C(0x80003ff8);
    profile->initial_words[0].value = UINT32_MAX;
    profile->initial_words[1].address = UINT32_C(0x80003ffc);
    profile->initial_words[1].value = UINT32_MAX;
    profile->initialize_dvd = true;
    profile->input_sha256 = porpoise_strdup(input_digest);
    profile->symbol_sources_sha256 = porpoise_strdup(
        inferred.symbol_sources_sha256 == NULL
            ? "" : inferred.symbol_sources_sha256);
    porpoise_recovery_title_host_profile_free(&inferred);
    CHECK(profile->startup_functions[0].module != NULL);
    CHECK(profile->startup_functions[0].normalized_fingerprint != NULL);
    CHECK(profile->input_sha256 != NULL);
    CHECK(profile->symbol_sources_sha256 != NULL);

    CHECK(porpoise_recovery_title_host_validate(
              &target, plan, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(porpoise_recovery_title_host_generate(
              &target, plan, output, &diagnostics) == PORPOISE_EXIT_OK);
    source = read_text(source_path);
    meson = read_text(meson_path);
    CHECK(source != NULL && strstr(source, "UINT32_C(0x80001000)") != NULL);
    CHECK(source != NULL && strstr(source, "getenv(\"PORPOISE_DVD_ROOT\")") != NULL);
    CHECK(source != NULL && strstr(source, "state_out->gpr[13]") != NULL);
    CHECK(source != NULL && strstr(source, "startup_function_count = 1U") != NULL);
    CHECK(source != NULL && strstr(source, "initial_word_count = 2U") != NULL);
    CHECK(meson != NULL && strstr(meson, "dependency('porpoise-title-contract')") != NULL);
    CHECK(meson != NULL && strstr(meson, "porpoise_title_host_dep") != NULL);
    free(source);
    free(meson);

    memset(&omit_override, 0, sizeof(omit_override));
    omit_override.module = "";
    omit_override.address = profile->startup_functions[0].address;
    omit_override.size = profile->startup_functions[0].size;
    omit_override.normalized_fingerprint =
        profile->startup_functions[0].normalized_fingerprint;
    omit_override.action = PORPOISE_OVERRIDE_OMIT;
    override_plan_options = plan_options;
    override_plan_options.overrides = &omit_override;
    override_plan_options.override_count = 1U;
    reset_diagnostics(&diagnostics);
    CHECK(porpoise_plan_build(
              session, &override_plan_options, &omit_plan, &diagnostics) ==
          PORPOISE_EXIT_OK);
    reset_diagnostics(&diagnostics);
    CHECK(porpoise_recovery_title_host_validate(
              &target, omit_plan, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    CHECK(diagnostics_contain(&diagnostics, "not resolved to Lift"));
    porpoise_plan_free(omit_plan);
    omit_plan = NULL;

    profile->arena_hi = profile->arena_lo;
    reset_diagnostics(&diagnostics);
    CHECK(porpoise_recovery_title_host_validate(
              &target, plan, &diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "arena bounds"));
    profile->arena_hi = UINT32_C(0x81700000);

    profile->input_sha256[0] = 'b';
    reset_diagnostics(&diagnostics);
    CHECK(porpoise_recovery_title_host_validate(
              &target, plan, &diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "current input digest"));
    profile->input_sha256[0] = 'a';

    prior_fingerprint = profile->startup_functions[0].normalized_fingerprint[0];
    profile->startup_functions[0].normalized_fingerprint[0] =
        prior_fingerprint == '0' ? '1' : '0';
    reset_diagnostics(&diagnostics);
    CHECK(porpoise_recovery_title_host_validate(
              &target, plan, &diagnostics) == PORPOISE_EXIT_TRANSLATION);
    CHECK(diagnostics_contain(&diagnostics, "stale locator"));
    profile->startup_functions[0].normalized_fingerprint[0] = prior_fingerprint;

    profile->gpr[2] = 0U;
    reset_diagnostics(&diagnostics);
    CHECK(porpoise_recovery_title_host_validate(
              &target, plan, &diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "nonzero r2/r13"));
    profile->gpr[2] = UINT32_C(0x80005000);

    profile->initial_words[1].address = profile->initial_words[0].address;
    reset_diagnostics(&diagnostics);
    CHECK(porpoise_recovery_title_host_validate(
              &target, plan, &diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "duplicate initial-word"));

    CHECK(porpoise_remove_tree(output, &diagnostics));
    porpoise_recovery_title_host_profile_free(profile);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_inference_requires_map_evidence(const char *source_root) {
    static const char input_digest[] =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    char input[PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseRecoveryTarget target;
    PorpoiseRecoveryTitleHostProfile output;
    PorpoiseDiagnostics diagnostics;
    CHECK(snprintf(
              input, sizeof(input), "%s/tests/fixtures/title_host/input.s",
              source_root) > 0);
    memset(&target, 0, sizeof(target));
    porpoise_recovery_title_host_profile_init(&output);
    output.entry_address = UINT32_C(0x12345678);
    porpoise_diagnostics_init(&diagnostics);
    porpoise_session_open_options_init(&session_options);
    session_options.input_path = input;
    CHECK(porpoise_session_open(
              &session_options, &session, &diagnostics) == PORPOISE_EXIT_OK);
    porpoise_plan_options_init(&plan_options);
    plan_options.entry_symbol = "main";
    plan_options.target_id = "main";
    plan_options.module = "";
    CHECK(porpoise_plan_build(
              session, &plan_options, &plan, &diagnostics) ==
          PORPOISE_EXIT_OK);
    target.id = "main";
    target.input.resolved = input;
    target.cache.input_sha256 = (char *)input_digest;
    CHECK(porpoise_recovery_title_host_infer(
              &target, plan, &output, &diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "_stack_addr"));
    CHECK(output.entry_address == UINT32_C(0x12345678));
    porpoise_recovery_title_host_profile_free(&output);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_inference_rejects_ambiguous_startup(
    const char *source_root,
    const char *fixture,
    const char *ambiguous_name) {
    static const char input_digest[] =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    char input[PORPOISE_PATH_CAPACITY];
    char symbols[PORPOISE_PATH_CAPACITY];
    PorpoiseSessionOpenOptions session_options;
    PorpoiseSessionSymbolSource session_symbol_source;
    PorpoiseRecoverySymbolSource target_symbol_source;
    PorpoisePlanOptions plan_options;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseRecoveryTarget target;
    PorpoiseRecoveryTitleHostProfile output;
    PorpoiseDiagnostics diagnostics;

    CHECK(snprintf(
              input, sizeof(input),
              "%s/tests/fixtures/title_host/%s",
              source_root, fixture) > 0);
    CHECK(snprintf(
              symbols, sizeof(symbols),
              "%s/tests/fixtures/title_host/symbols.txt",
              source_root) > 0);
    memset(&target, 0, sizeof(target));
    memset(&target_symbol_source, 0, sizeof(target_symbol_source));
    porpoise_recovery_title_host_profile_init(&output);
    output.entry_address = UINT32_C(0x12345678);
    porpoise_diagnostics_init(&diagnostics);
    porpoise_session_open_options_init(&session_options);
    session_options.input_path = input;
    memset(&session_symbol_source, 0, sizeof(session_symbol_source));
    session_symbol_source.kind = PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS;
    session_symbol_source.path = symbols;
    session_symbol_source.module = "";
    session_options.symbol_sources = &session_symbol_source;
    session_options.symbol_source_count = 1U;
    CHECK(porpoise_session_open(
              &session_options, &session, &diagnostics) == PORPOISE_EXIT_OK);
    porpoise_plan_options_init(&plan_options);
    plan_options.entry_symbol = "main";
    plan_options.target_id = "main";
    plan_options.module = "";
    CHECK(porpoise_plan_build(
              session, &plan_options, &plan, &diagnostics) ==
          PORPOISE_EXIT_OK);
    target.id = "main";
    target.input.resolved = input;
    target.cache.input_sha256 = (char *)input_digest;
    target_symbol_source.kind = PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS;
    target_symbol_source.path.resolved = symbols;
    target_symbol_source.module = "";
    target.symbol_sources = &target_symbol_source;
    target.symbol_source_count = 1U;
    reset_diagnostics(&diagnostics);
    CHECK(porpoise_recovery_title_host_infer(
              &target, plan, &output, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    {
        char expected[PORPOISE_MESSAGE_CAPACITY];
        CHECK(snprintf(
                  expected, sizeof(expected),
                  "startup function '%s' is ambiguous",
                  ambiguous_name) > 0);
        CHECK(diagnostics_contain(&diagnostics, expected));
    }
    CHECK(diagnostics_contain(&diagnostics, "2 candidates"));
    CHECK(output.entry_address == UINT32_C(0x12345678));
    porpoise_recovery_title_host_profile_free(&output);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_evidence_provenance_staleness(const char *source_root) {
    static const char input_digest[] =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    char input[PORPOISE_PATH_CAPACITY];
    char symbols[PORPOISE_PATH_CAPACITY];
    char changed_symbols[PORPOISE_PATH_CAPACITY];
    char sdk_catalog[PORPOISE_PATH_CAPACITY];
    const char *sdk_catalogs[1];
    PorpoiseSessionOpenOptions session_options;
    PorpoiseSessionSymbolSource session_symbol_source;
    PorpoiseRecoverySymbolSource target_symbol_source;
    PorpoisePlanOptions plan_options;
    PorpoiseSession *baseline_session = NULL;
    PorpoiseSession *changed_session = NULL;
    PorpoiseSession *sdk_session = NULL;
    PorpoiseTranslationPlan *baseline_plan = NULL;
    PorpoiseTranslationPlan *changed_plan = NULL;
    PorpoiseTranslationPlan *sdk_plan = NULL;
    PorpoiseRecoveryTarget target;
    PorpoiseRecoveryTarget sdk_target;
    PorpoiseRecoveryTitleHostProfile sdk_profile;
    PorpoiseDiagnostics diagnostics;

    CHECK(snprintf(
              input, sizeof(input), "%s/tests/fixtures/title_host/input.s",
              source_root) > 0);
    CHECK(snprintf(
              symbols, sizeof(symbols),
              "%s/tests/fixtures/title_host/symbols.txt",
              source_root) > 0);
    CHECK(snprintf(
              changed_symbols, sizeof(changed_symbols),
              "%s/tests/fixtures/title_host/symbols-changed.txt",
              source_root) > 0);
    CHECK(snprintf(
              sdk_catalog, sizeof(sdk_catalog),
              "%s/tests/fixtures/sdk_catalog/valid.json",
              source_root) > 0);
    memset(&target, 0, sizeof(target));
    memset(&sdk_target, 0, sizeof(sdk_target));
    memset(&target_symbol_source, 0, sizeof(target_symbol_source));
    memset(&session_symbol_source, 0, sizeof(session_symbol_source));
    porpoise_recovery_title_host_profile_init(&sdk_profile);
    porpoise_diagnostics_init(&diagnostics);
    porpoise_session_open_options_init(&session_options);
    session_options.input_path = input;
    session_symbol_source.kind = PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS;
    session_symbol_source.path = symbols;
    session_symbol_source.module = "";
    session_options.symbol_sources = &session_symbol_source;
    session_options.symbol_source_count = 1U;
    CHECK(porpoise_session_open(
              &session_options, &baseline_session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    porpoise_plan_options_init(&plan_options);
    plan_options.entry_symbol = "main";
    plan_options.target_id = "main";
    plan_options.module = "";
    CHECK(porpoise_plan_build(
              baseline_session, &plan_options, &baseline_plan,
              &diagnostics) == PORPOISE_EXIT_OK);
    target.id = "main";
    target.input.resolved = input;
    target.cache.input_sha256 = (char *)input_digest;
    target_symbol_source.kind = PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS;
    target_symbol_source.path.resolved = symbols;
    target_symbol_source.module = "";
    target.symbol_sources = &target_symbol_source;
    target.symbol_source_count = 1U;
    target.has_title_host = true;
    porpoise_recovery_title_host_profile_init(&target.title_host);
    CHECK(porpoise_recovery_title_host_infer(
              &target, baseline_plan, &target.title_host, &diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(target.title_host.symbol_sources_sha256 != NULL);
    CHECK(target.title_host.sdk_catalogs_sha256 == NULL);
    CHECK(porpoise_recovery_title_host_validate(
              &target, baseline_plan, &diagnostics) == PORPOISE_EXIT_OK);

    reset_diagnostics(&diagnostics);
    session_symbol_source.path = changed_symbols;
    CHECK(porpoise_session_open(
              &session_options, &changed_session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(porpoise_plan_build(
              changed_session, &plan_options, &changed_plan,
              &diagnostics) == PORPOISE_EXIT_OK);
    target_symbol_source.path.resolved = changed_symbols;
    reset_diagnostics(&diagnostics);
    CHECK(porpoise_recovery_title_host_validate(
              &target, changed_plan, &diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(
        &diagnostics, "stale for the current symbol-source evidence"));

    target_symbol_source.path.resolved = symbols;
    session_symbol_source.path = symbols;
    sdk_catalogs[0] = sdk_catalog;
    session_options.sdk_catalog_paths = sdk_catalogs;
    session_options.sdk_catalog_path_count = 1U;
    reset_diagnostics(&diagnostics);
    CHECK(porpoise_session_open(
              &session_options, &sdk_session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(porpoise_plan_build(
              sdk_session, &plan_options, &sdk_plan, &diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(porpoise_recovery_title_host_infer(
              &target, sdk_plan, &sdk_profile, &diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(sdk_profile.symbol_sources_sha256 != NULL);
    CHECK(sdk_profile.sdk_catalogs_sha256 != NULL);
    sdk_target = target;
    sdk_target.title_host = sdk_profile;
    sdk_target.has_title_host = true;
    CHECK(porpoise_recovery_title_host_validate(
              &sdk_target, sdk_plan, &diagnostics) == PORPOISE_EXIT_OK);
    reset_diagnostics(&diagnostics);
    CHECK(porpoise_recovery_title_host_validate(
              &target, sdk_plan, &diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(
        &diagnostics, "stale for the current SDK-catalog evidence"));
    reset_diagnostics(&diagnostics);
    CHECK(porpoise_recovery_title_host_validate(
              &sdk_target, baseline_plan, &diagnostics) ==
          PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(
        &diagnostics, "stale for the current SDK-catalog evidence"));

    porpoise_recovery_title_host_profile_free(&sdk_profile);
    porpoise_recovery_title_host_profile_free(&target.title_host);
    porpoise_plan_free(sdk_plan);
    porpoise_session_close(sdk_session);
    porpoise_plan_free(changed_plan);
    porpoise_session_close(changed_session);
    porpoise_plan_free(baseline_plan);
    porpoise_session_close(baseline_session);
    porpoise_diagnostics_free(&diagnostics);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: test_recovery_title_host SOURCE_ROOT\n");
        return 2;
    }
    test_validation_and_generation(argv[1]);
    test_inference_requires_map_evidence(argv[1]);
    test_inference_rejects_ambiguous_startup(
        argv[1], "input-ambiguous-startup.s", "__OSThreadInit");
    test_inference_rejects_ambiguous_startup(
        argv[1], "input-ambiguous-user-startup.s", "__init_user");
    test_evidence_provenance_staleness(argv[1]);
    if (failures != 0U) {
        fprintf(stderr, "%u recovery title-host test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
