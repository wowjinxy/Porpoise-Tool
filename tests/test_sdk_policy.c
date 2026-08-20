#include "porpoise/plan.h"
#include "porpoise/project.h"
#include "porpoise/report.h"
#include "porpoise/session.h"
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

static bool path_join(
    char *output,
    size_t capacity,
    const char *root,
    const char *leaf) {
    return porpoise_path_join(output, capacity, root, leaf);
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

static void write_signature(
    FILE *file,
    const char *identity,
    const char *contract,
    const PorpoiseFunctionSignature *signature,
    bool first) {
    fprintf(
        file,
        "%s{\"canonical_identity\":\"%s\","
        "\"category\":\"nintendo_dolphin\"",
        first ? "" : ",",
        identity);
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

static bool create_catalog(
    const char *path,
    const PorpoiseFunctionSignature *gx,
    const PorpoiseFunctionSignature *unknown) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    fputs(
        "{\"schema_version\":1,\"signature_algorithm_version\":1,"
        "\"entries\":[",
        file);
    write_signature(file, "GXInit", "GXInit", gx, true);
    write_signature(file, "UnknownSdk", NULL, unknown, false);
    fputs("]}\n", file);
    return fclose(file) == 0;
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    long length;
    char *text;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    text = (char *)malloc((size_t)length + 1U);
    if (text == NULL ||
        ((size_t)length != 0U &&
         fread(text, 1U, (size_t)length, file) != (size_t)length)) {
        free(text);
        fclose(file);
        return NULL;
    }
    text[length] = '\0';
    fclose(file);
    return text;
}

static int open_session(
    const char *input,
    const char *catalog,
    const char *map,
    PorpoiseSession **session,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSessionOpenOptions options;
    PorpoiseSessionSymbolSource symbol_source;
    const char *catalogs[1];
    porpoise_session_open_options_init(&options);
    options.input_path = input;
    catalogs[0] = catalog;
    options.sdk_catalog_paths = catalogs;
    options.sdk_catalog_path_count = catalog == NULL ? 0U : 1U;
    if (map != NULL) {
        memset(&symbol_source, 0, sizeof(symbol_source));
        symbol_source.kind = PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP;
        symbol_source.path = map;
        symbol_source.module = "main";
        options.symbol_sources = &symbol_source;
        options.symbol_source_count = 1U;
    }
    return porpoise_session_open(&options, session, diagnostics);
}

static void collect_signatures(
    const char *input,
    PorpoiseFunctionSignature *gx,
    PorpoiseFunctionSignature *unknown) {
    PorpoiseSession *session = NULL;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseProgram *program;
    porpoise_diagnostics_init(&diagnostics);
    CHECK(open_session(input, NULL, NULL, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    program = porpoise_session_program(session);
    CHECK(porpoise_signature_compute(
        program, find_function(program, "GXInit"), gx));
    CHECK(porpoise_signature_compute(
        program, find_function(program, "UnknownSdk"), unknown));
    CHECK(porpoise_signature_is_automatic_match_eligible(gx));
    CHECK(porpoise_signature_is_automatic_match_eligible(unknown));
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

static PorpoiseTranslationPlan *build_plan(
    PorpoiseSession *session,
    PorpoiseSdkPolicy policy,
    const PorpoiseFunctionOverride *override,
    PorpoiseDiagnostics *diagnostics) {
    PorpoisePlanOptions options;
    PorpoiseTranslationPlan *plan = NULL;
    porpoise_plan_options_init(&options);
    options.entry_symbol = "title_main";
    options.target_id = "sample";
    options.module = "main";
    options.sdk_policy = policy;
    options.overrides = override;
    options.override_count = override == NULL ? 0U : 1U;
    CHECK(porpoise_plan_build(
        session, &options, &plan, diagnostics) == PORPOISE_EXIT_OK);
    return plan;
}

static void test_policies(
    const char *source_root,
    const char *input,
    const char *catalog,
    const char *map) {
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseFunctionPlanView *gx;
    const PorpoiseFunctionPlanView *unknown;
    char runtime[PORPOISE_PATH_CAPACITY];
    char registry[PORPOISE_PATH_CAPACITY];
    char lifted[PORPOISE_PATH_CAPACITY];
    char report_path[PORPOISE_PATH_CAPACITY];
    const char *output = ".porpoise-sdk-policy-output";
    PorpoiseProjectOptions project_options;
    PorpoiseReport report;
    char *text;

    porpoise_diagnostics_init(&diagnostics);
    CHECK(open_session(input, catalog, NULL, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(porpoise_session_symbols(session)->symbol_count == 0U);
    plan = build_plan(session, PORPOISE_SDK_POLICY_KEEP, NULL, &diagnostics);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);
    gx = porpoise_plan_find_function(plan, "GXInit");
    unknown = porpoise_plan_find_function(plan, "UnknownSdk");
    CHECK(gx != NULL && gx->action == PORPOISE_PLAN_ACTION_LIFT);
    CHECK(gx != NULL && gx->confidence == PORPOISE_MATCH_CONFIDENCE_EXACT);
    CHECK(gx != NULL && gx->map_symbol == NULL);
    CHECK(unknown != NULL && unknown->action == PORPOISE_PLAN_ACTION_LIFT);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);

    porpoise_diagnostics_init(&diagnostics);
    CHECK(open_session(input, catalog, map, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    plan = build_plan(
        session, PORPOISE_SDK_POLICY_IMPORTED, NULL, &diagnostics);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);
    gx = porpoise_plan_find_function(plan, "GXInit");
    unknown = porpoise_plan_find_function(plan, "UnknownSdk");
    CHECK(gx != NULL && gx->action == PORPOISE_PLAN_ACTION_IMPORT);
    CHECK(gx != NULL && gx->binding != NULL);
    CHECK(gx != NULL && gx->map_symbol != NULL);
    CHECK(unknown != NULL && unknown->action == PORPOISE_PLAN_ACTION_LIFT);
    porpoise_plan_free(plan);

    plan = build_plan(session, PORPOISE_SDK_POLICY_OMIT, NULL, &diagnostics);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);
    gx = porpoise_plan_find_function(plan, "GXInit");
    unknown = porpoise_plan_find_function(plan, "UnknownSdk");
    CHECK(gx != NULL && gx->action == PORPOISE_PLAN_ACTION_IMPORT);
    CHECK(unknown != NULL && unknown->action == PORPOISE_PLAN_ACTION_OMIT);
    CHECK(unknown != NULL &&
          unknown->origin == PORPOISE_PLAN_ORIGIN_SDK_POLICY);

    CHECK(path_join(runtime, sizeof(runtime), source_root, "runtime"));
    CHECK(path_join(
        registry, sizeof(registry), output,
        "src/porpoise_function_registry_8001.c"));
    CHECK(path_join(lifted, sizeof(lifted), output, "src/lifted/input.c"));
    CHECK(path_join(
        report_path, sizeof(report_path), output,
        "porpoise-report.json"));
    porpoise_report_init(&report);
    CHECK(porpoise_remove_tree(output, &diagnostics));
    porpoise_project_options_init(&project_options);
    project_options.output_path = output;
    project_options.runtime_directory = runtime;
    project_options.entry_symbol = "title_main";
    project_options.force = true;
    CHECK(porpoise_project_generate_plan(
        plan, &project_options, &report, &diagnostics) == PORPOISE_EXIT_OK);
    text = read_file(registry);
    CHECK(text != NULL && strstr(text, "porpoise_import_GXInit") != NULL);
    CHECK(text != NULL && strstr(text, "porpoise_omitted_sdk_trap") != NULL);
    free(text);
    text = read_file(lifted);
    CHECK(text != NULL && strstr(text, "porpoise_lifted_title_main") != NULL);
    CHECK(text != NULL && strstr(text, "porpoise_lifted_GXInit") == NULL);
    CHECK(text != NULL && strstr(text, "porpoise_lifted_UnknownSdk") == NULL);
    free(text);
    text = read_file(report_path);
    CHECK(text != NULL && strstr(text, "\"schema_version\": 3") != NULL);
    CHECK(text != NULL && strstr(text, "\"sdk_policy\": \"omit\"") != NULL);
    CHECK(text != NULL && strstr(text, "\"confidence\": \"exact\"") != NULL);
    free(text);
    CHECK(porpoise_remove_tree(output, &diagnostics));
    porpoise_report_free(&report);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_conflicts_and_overrides(
    const char *input,
    const char *catalog,
    const char *map,
    const PorpoiseFunctionSignature *gx_signature) {
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan;
    PorpoiseFunctionOverride override;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseFunctionPlanView *view;

    porpoise_diagnostics_init(&diagnostics);
    CHECK(open_session(input, catalog, map, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    plan = build_plan(session, PORPOISE_SDK_POLICY_KEEP, NULL, &diagnostics);
    view = porpoise_plan_find_function(plan, "GXInit");
    CHECK(view != NULL &&
          (view->evidence_flags & PORPOISE_PLAN_EVIDENCE_CONFLICT) != 0U);
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_LIFT);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);
    porpoise_plan_free(plan);

    plan = build_plan(
        session, PORPOISE_SDK_POLICY_IMPORTED, NULL, &diagnostics);
    CHECK(porpoise_plan_validate(plan, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    porpoise_plan_free(plan);

    memset(&override, 0, sizeof(override));
    override.module = "main";
    override.address = UINT32_C(0x80010000);
    override.size = UINT32_C(0x20);
    override.normalized_fingerprint = gx_signature->digest_hex;
    override.action = PORPOISE_OVERRIDE_LIFT;
    plan = build_plan(
        session, PORPOISE_SDK_POLICY_IMPORTED, &override, &diagnostics);
    CHECK(porpoise_plan_validate(plan, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    porpoise_plan_free(plan);

    override.acknowledge_conflict = true;
    plan = build_plan(
        session, PORPOISE_SDK_POLICY_IMPORTED, &override, &diagnostics);
    view = porpoise_plan_find_function(plan, "GXInit");
    CHECK(view != NULL && view->overridden);
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_LIFT);
    CHECK(view != NULL &&
          (view->evidence_flags & PORPOISE_PLAN_EVIDENCE_CONFLICT) != 0U);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);
    porpoise_plan_free(plan);

    override.address = UINT32_C(0x80010004);
    plan = build_plan(
        session, PORPOISE_SDK_POLICY_KEEP, &override, &diagnostics);
    CHECK(porpoise_plan_validate(plan, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

int main(int argc, char **argv) {
    const char *catalog = ".porpoise-sdk-policy-catalog.json";
    char input[PORPOISE_PATH_CAPACITY];
    char matching_map[PORPOISE_PATH_CAPACITY];
    char conflicting_map[PORPOISE_PATH_CAPACITY];
    PorpoiseFunctionSignature gx;
    PorpoiseFunctionSignature unknown;
    if (argc != 2) return 2;
    CHECK(path_join(
        input, sizeof(input), argv[1], "tests/fixtures/sdk_policy/input.s"));
    CHECK(path_join(
        matching_map, sizeof(matching_map), argv[1],
        "tests/fixtures/sdk_policy/matching.map"));
    CHECK(path_join(
        conflicting_map, sizeof(conflicting_map), argv[1],
        "tests/fixtures/sdk_policy/conflicting.map"));
    collect_signatures(input, &gx, &unknown);
    CHECK(create_catalog(catalog, &gx, &unknown));
    test_policies(argv[1], input, catalog, matching_map);
    test_conflicts_and_overrides(input, catalog, conflicting_map, &gx);
    CHECK(remove(catalog) == 0);
    if (failures != 0U) {
        fprintf(stderr, "%u SDK policy test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
