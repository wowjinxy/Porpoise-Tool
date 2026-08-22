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
    write_signature(file, "gx.a/GXInit.c/GXInit", "GXInit", gx, true);
    write_signature(
        file, "gx.a/UnknownSdk.c/UnknownSdk", NULL, unknown, false);
    fputs("]}\n", file);
    return fclose(file) == 0;
}

static bool create_direct_contract_catalog(
    const char *path,
    const char *identity,
    const char *contract,
    const PorpoiseFunctionSignature *signature) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    fputs(
        "{\"schema_version\":1,\"signature_algorithm_version\":1,"
        "\"entries\":[",
        file);
    write_signature(file, identity, contract, signature, true);
    fputs("]}\n", file);
    return fclose(file) == 0;
}

static bool create_ambiguous_catalog(
    const char *path,
    const PorpoiseFunctionSignature *signature) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    fputs(
        "{\"schema_version\":1,\"signature_algorithm_version\":1,"
        "\"entries\":[",
        file);
    write_signature(
        file, "gx.a/GXInit.c/GXInit", "GXInit", signature, true);
    write_signature(
        file, "gx.a/GXInitAlt.c/GXInitAlt", NULL, signature, false);
    fputs("]}\n", file);
    return fclose(file) == 0;
}

static bool create_direct_abi(const char *path) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    fputs(
        "{\"schema_version\":1,\"functions\":[{"
        "\"kind\":\"import\","
        "\"symbol\":\"gx_direct_contract\","
        "\"wrapper\":\"host_gx_direct\","
        "\"header\":\"porpoise/gx_direct.h\","
        "\"return\":{\"type\":\"void\"},"
        "\"arguments\":[]}]}\n",
        file);
    return fclose(file) == 0;
}

static bool create_path_bearing_map(const char *path) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    fputs(
        "Link map of EntryPoint\n"
        "  1] GXInit (func,global) found in C:\\SDK\\Libraries\\GX.A source\\GXInit.c\n"
        "  2] UnknownSdk (func,global) found in C:\\SDK\\Libraries\\DEMO.A source\\UnknownSdk.c\n"
        "\n"
        ".text section layout\n"
        "  Starting        Virtual\n"
        "  address  Size   address\n"
        "  -----------------------\n"
        "  00000000 000020 80010000  4 GXInit C:\\SDK\\Libraries\\GX.A source\\GXInit.c\n"
        "  00000100 000020 80010100  4 UnknownSdk C:\\SDK\\Libraries\\DEMO.A source\\UnknownSdk.c\n"
        "  00000200 000008 80010200  4 title_main title.o\n",
        file);
    return fclose(file) == 0;
}

static bool create_section_collision_symbols(const char *path) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    fputs(
        "GXInit = .init:0x80010000; // type:function size:0x20 scope:global\n"
        "TextSectionOwner = .text:0x80010000; // type:function size:0x20 scope:global\n",
        file);
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

static int open_session_with_abi(
    const char *input,
    const char *catalog,
    const char *abi,
    PorpoiseSession **session,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSessionOpenOptions options;
    const char *catalogs[1];
    porpoise_session_open_options_init(&options);
    options.input_path = input;
    options.abi_path = abi;
    catalogs[0] = catalog;
    options.sdk_catalog_paths = catalogs;
    options.sdk_catalog_path_count = catalog == NULL ? 0U : 1U;
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

static PorpoiseTranslationPlan *build_plan_with_hints(
    PorpoiseSession *session,
    PorpoiseSdkPolicy policy,
    const PorpoisePlanMatchHint *hints,
    size_t hint_count,
    size_t *hint_used_count,
    PorpoiseDiagnostics *diagnostics) {
    PorpoisePlanOptions options;
    PorpoiseTranslationPlan *plan = NULL;
    porpoise_plan_options_init(&options);
    options.entry_symbol = "title_main";
    options.target_id = "sample";
    options.module = "main";
    options.sdk_policy = policy;
    options.match_hints = hints;
    options.match_hint_count = hint_count;
    options.match_hint_used_count_out = hint_used_count;
    CHECK(porpoise_plan_build(
        session, &options, &plan, diagnostics) == PORPOISE_EXIT_OK);
    return plan;
}

static void check_exact_fallback(
    PorpoiseSession *session,
    const PorpoisePlanMatchHint *hint,
    const char *expected_digest,
    PorpoiseDiagnostics *diagnostics) {
    size_t hint_used_count = SIZE_MAX;
    PorpoiseTranslationPlan *plan = build_plan_with_hints(
        session, PORPOISE_SDK_POLICY_KEEP, hint, 1U,
        &hint_used_count, diagnostics);
    const PorpoiseFunctionPlanView *view =
        porpoise_plan_find_function(plan, "GXInit");
    CHECK(view != NULL && view->sdk_entry != NULL);
    CHECK(view != NULL &&
          (view->evidence_flags & PORPOISE_PLAN_EVIDENCE_SIGNATURE) != 0U);
    CHECK(hint_used_count == 0U);
    CHECK(strcmp(porpoise_plan_digest(plan), expected_digest) == 0);
    CHECK(porpoise_plan_validate(plan, diagnostics) == PORPOISE_EXIT_OK);
    porpoise_plan_free(plan);
}

static void test_match_hints(
    const char *source_root,
    const char *input,
    const char *catalog,
    const char *structural_catalog,
    const char *ambiguous_catalog,
    const PorpoiseFunctionSignature *gx_signature) {
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *baseline = NULL;
    PorpoiseTranslationPlan *hinted = NULL;
    PorpoisePlanMatchHint hint;
    PorpoisePlanMatchHint invalid;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseFunctionPlanView *view;
    const PorpoiseFunctionPlanView *baseline_view;
    char baseline_digest[PORPOISE_SHA256_HEX_SIZE];
    char runtime[PORPOISE_PATH_CAPACITY];
    char report_path[PORPOISE_PATH_CAPACITY];
    const char *output = ".porpoise-sdk-hint-parity-output";
    PorpoiseProjectOptions project_options;
    PorpoiseReport report;
    PorpoiseDiagnostics baseline_generation_diagnostics;
    PorpoiseDiagnostics hinted_generation_diagnostics;
    char *baseline_report = NULL;
    char *hinted_report = NULL;
    size_t hint_used_count = 0U;
    PorpoiseSdkPolicy policy;

    porpoise_diagnostics_init(&diagnostics);
    CHECK(open_session(input, catalog, NULL, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    memset(&hint, 0, sizeof(hint));
    hint.target_id = "sample";
    hint.module = "main";
    hint.address = UINT32_C(0x80010000);
    hint.size = UINT32_C(0x20);
    hint.normalized_fingerprint = gx_signature->digest_hex;
    hint.canonical_identity = "gx.a/GXInit.c/GXInit";
    hint.contract_name = "GXInit";

    baseline = build_plan(
        session, PORPOISE_SDK_POLICY_KEEP, NULL, &diagnostics);
    CHECK(porpoise_copy_string(
        baseline_digest, sizeof(baseline_digest),
        porpoise_plan_digest(baseline)));
    baseline_view = porpoise_plan_find_function(baseline, "GXInit");
    hinted = build_plan_with_hints(
        session, PORPOISE_SDK_POLICY_KEEP, &hint, 1U,
        &hint_used_count, &diagnostics);
    view = porpoise_plan_find_function(hinted, "GXInit");
    CHECK(view != NULL && view->sdk_entry != NULL);
    CHECK(hint_used_count == 1U);
    CHECK(view != NULL && baseline_view != NULL &&
          view->evidence_flags == baseline_view->evidence_flags);
    CHECK(strcmp(porpoise_plan_digest(hinted), baseline_digest) == 0);
    CHECK(porpoise_plan_validate(hinted, &diagnostics) == PORPOISE_EXIT_OK);

    CHECK(path_join(runtime, sizeof(runtime), source_root, "runtime"));
    CHECK(path_join(
        report_path, sizeof(report_path), output, "porpoise-report.json"));
    CHECK(porpoise_remove_tree(output, &diagnostics));
    porpoise_project_options_init(&project_options);
    project_options.output_path = output;
    project_options.runtime_directory = runtime;
    project_options.entry_symbol = "title_main";
    project_options.force = true;
    porpoise_report_init(&report);
    porpoise_diagnostics_init(&baseline_generation_diagnostics);
    CHECK(porpoise_project_generate_plan(
              baseline, &project_options, &report,
              &baseline_generation_diagnostics) ==
          PORPOISE_EXIT_OK);
    baseline_report = read_file(report_path);
    porpoise_report_free(&report);
    porpoise_report_init(&report);
    porpoise_diagnostics_init(&hinted_generation_diagnostics);
    CHECK(porpoise_project_generate_plan(
              hinted, &project_options, &report,
              &hinted_generation_diagnostics) ==
          PORPOISE_EXIT_OK);
    hinted_report = read_file(report_path);
    CHECK(baseline_report != NULL && hinted_report != NULL &&
          strcmp(baseline_report, hinted_report) == 0);
    free(hinted_report);
    free(baseline_report);
    porpoise_report_free(&report);
    porpoise_diagnostics_free(&hinted_generation_diagnostics);
    porpoise_diagnostics_free(&baseline_generation_diagnostics);
    CHECK(porpoise_remove_tree(output, &diagnostics));
    porpoise_plan_free(hinted);
    porpoise_plan_free(baseline);

    for (policy = PORPOISE_SDK_POLICY_IMPORTED;
         policy <= PORPOISE_SDK_POLICY_OMIT;
         policy = (PorpoiseSdkPolicy)((int)policy + 1)) {
        baseline = build_plan(session, policy, NULL, &diagnostics);
        hinted = build_plan_with_hints(
            session, policy, &hint, 1U,
            &hint_used_count, &diagnostics);
        CHECK(hint_used_count == 1U);
        CHECK(strcmp(
                  porpoise_plan_digest(baseline),
                  porpoise_plan_digest(hinted)) == 0);
        CHECK(porpoise_plan_validate(baseline, &diagnostics) ==
              PORPOISE_EXIT_OK);
        CHECK(porpoise_plan_validate(hinted, &diagnostics) ==
              PORPOISE_EXIT_OK);
        porpoise_plan_free(hinted);
        porpoise_plan_free(baseline);
    }

    {
        PorpoisePlanMatchHint mixed_hints[2];
        mixed_hints[0] = hint;
        mixed_hints[0].canonical_identity = "gx.a/Missing.c/Missing";
        mixed_hints[1] = hint;
        hinted = build_plan_with_hints(
            session, PORPOISE_SDK_POLICY_KEEP,
            mixed_hints, 2U, &hint_used_count, &diagnostics);
        CHECK(hint_used_count == 1U);
        CHECK(strcmp(porpoise_plan_digest(hinted), baseline_digest) == 0);
        porpoise_plan_free(hinted);

        mixed_hints[0] = hint;
        mixed_hints[1] = hint;
        hinted = build_plan_with_hints(
            session, PORPOISE_SDK_POLICY_KEEP,
            mixed_hints, 2U, &hint_used_count, &diagnostics);
        CHECK(hint_used_count == 1U);
        CHECK(strcmp(porpoise_plan_digest(hinted), baseline_digest) == 0);
        porpoise_plan_free(hinted);
    }

    hint_used_count = SIZE_MAX;
    baseline = build_plan_with_hints(
        session, PORPOISE_SDK_POLICY_KEEP, NULL, 0U,
        &hint_used_count, &diagnostics);
    view = porpoise_plan_find_function(baseline, "GXInit");
    CHECK(hint_used_count == 0U);
    CHECK(view != NULL && view->sdk_entry != NULL);
    CHECK(strcmp(porpoise_plan_digest(baseline), baseline_digest) == 0);
    porpoise_plan_free(baseline);

    invalid = hint;
    invalid.target_id = "other-target";
    check_exact_fallback(session, &invalid, baseline_digest, &diagnostics);
    invalid = hint;
    invalid.module = "other";
    check_exact_fallback(session, &invalid, baseline_digest, &diagnostics);
    invalid = hint;
    invalid.address += 4U;
    check_exact_fallback(session, &invalid, baseline_digest, &diagnostics);
    invalid = hint;
    invalid.size -= 4U;
    check_exact_fallback(session, &invalid, baseline_digest, &diagnostics);
    invalid = hint;
    invalid.normalized_fingerprint =
        "0000000000000000000000000000000000000000000000000000000000000000";
    check_exact_fallback(session, &invalid, baseline_digest, &diagnostics);
    invalid = hint;
    invalid.canonical_identity = "gx.a/Missing.c/Missing";
    check_exact_fallback(session, &invalid, baseline_digest, &diagnostics);
    invalid = hint;
    invalid.contract_name = "NotGXInit";
    check_exact_fallback(session, &invalid, baseline_digest, &diagnostics);
    invalid = hint;
    invalid.contract_name = "";
    check_exact_fallback(session, &invalid, baseline_digest, &diagnostics);
    porpoise_session_close(session);

    session = NULL;
    CHECK(open_session(
              input, structural_catalog, NULL, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    hinted = build_plan_with_hints(
        session, PORPOISE_SDK_POLICY_KEEP, &hint, 1U,
        &hint_used_count, &diagnostics);
    view = porpoise_plan_find_function(hinted, "GXInit");
    CHECK(view != NULL && view->sdk_entry == NULL);
    CHECK(view != NULL &&
          (view->evidence_flags & PORPOISE_PLAN_EVIDENCE_SIGNATURE) == 0U);
    CHECK(hint_used_count == 0U);
    porpoise_plan_free(hinted);
    porpoise_session_close(session);

    session = NULL;
    CHECK(open_session(
              input, ambiguous_catalog, NULL, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    baseline = build_plan(
        session, PORPOISE_SDK_POLICY_KEEP, NULL, &diagnostics);
    hinted = build_plan_with_hints(
        session, PORPOISE_SDK_POLICY_KEEP, &hint, 1U,
        &hint_used_count, &diagnostics);
    view = porpoise_plan_find_function(hinted, "GXInit");
    CHECK(view != NULL && view->sdk_entry == NULL);
    CHECK(view != NULL &&
          (view->evidence_flags &
           PORPOISE_PLAN_EVIDENCE_AMBIGUOUS_SIGNATURE) != 0U);
    CHECK(hint_used_count == 0U);
    CHECK(strcmp(
              porpoise_plan_digest(hinted),
              porpoise_plan_digest(baseline)) == 0);
    porpoise_plan_free(hinted);
    porpoise_plan_free(baseline);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
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

static void test_interior_entry_policy_fallback(
    const char *source_root,
    const char *catalog,
    const char *contract_catalog,
    const char *audited_vtx_desc_catalog,
    const char *audited_vtx_fmt_catalog,
    const char *abi) {
    char input[PORPOISE_PATH_CAPACITY];
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoisePlanOptions options;
    PorpoiseFunctionOverride override;
    PorpoiseFunctionSignature signature;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseProgram *program;
    const PorpoiseFunction *function;
    const PorpoiseFunctionPlanView *view;
    uint32_t function_address = 0U;
    uint32_t function_size = 0U;

    CHECK(path_join(
        input, sizeof(input), source_root,
        "tests/fixtures/inputs/address_taken_jump_table"));
    porpoise_diagnostics_init(&diagnostics);
    CHECK(open_session(input, NULL, NULL, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    program = porpoise_session_program(session);
    function = find_function(program, "jump_dispatch");
    CHECK(function != NULL && function->address_taken_entry_count == 2U);
    CHECK(porpoise_signature_compute(program, function, &signature));
    CHECK(porpoise_signature_is_automatic_match_eligible(&signature));
    if (function != NULL) {
        function_address = function->start_address;
        function_size = function->size;
    }
    porpoise_session_close(session);
    session = NULL;

    CHECK(create_direct_contract_catalog(
        catalog, "gx.a/jump_dispatch.c/jump_dispatch", NULL, &signature));
    CHECK(create_direct_contract_catalog(
        contract_catalog, "gx.a/jump_dispatch.c/jump_dispatch",
        "gx_direct_contract", &signature));
    CHECK(create_direct_contract_catalog(
        audited_vtx_desc_catalog,
        "gx.a/GXAttr.c/GXSetVtxDesc", NULL, &signature));
    CHECK(create_direct_contract_catalog(
        audited_vtx_fmt_catalog,
        "gx.a/GXAttr.c/GXSetVtxAttrFmt", NULL, &signature));

    CHECK(open_session(input, catalog, NULL, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    porpoise_plan_options_init(&options);
    options.entry_symbol = "safe_anchor";
    options.target_id = "sample";
    options.module = "main";
    options.sdk_policy = PORPOISE_SDK_POLICY_OMIT;
    CHECK(porpoise_plan_build(
              session, &options, &plan, &diagnostics) == PORPOISE_EXIT_OK);
    view = porpoise_plan_find_function(plan, "jump_dispatch");
    CHECK(view != NULL &&
          view->requested_action == PORPOISE_PLAN_ACTION_OMIT);
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_LIFT);
    CHECK(view != NULL && view->origin == PORPOISE_PLAN_ORIGIN_SDK_POLICY);
    CHECK(view != NULL && !view->blocked && view->binding == NULL);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);
    porpoise_plan_free(plan);
    plan = NULL;

    memset(&override, 0, sizeof(override));
    override.module = "main";
    override.address = function_address;
    override.size = function_size;
    override.normalized_fingerprint = signature.digest_hex;
    override.action = PORPOISE_OVERRIDE_OMIT;
    options.sdk_policy = PORPOISE_SDK_POLICY_KEEP;
    options.overrides = &override;
    options.override_count = 1U;
    CHECK(porpoise_plan_build(
              session, &options, &plan, &diagnostics) == PORPOISE_EXIT_OK);
    view = porpoise_plan_find_function(plan, "jump_dispatch");
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_OMIT);
    CHECK(view != NULL && view->blocked);
    CHECK(porpoise_plan_validate(plan, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    plan = NULL;
    session = NULL;
    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);

    CHECK(open_session_with_abi(
              input, contract_catalog, abi, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    porpoise_plan_options_init(&options);
    options.entry_symbol = "safe_anchor";
    options.target_id = "sample";
    options.module = "main";
    options.sdk_policy = PORPOISE_SDK_POLICY_IMPORTED;
    CHECK(porpoise_plan_build(
              session, &options, &plan, &diagnostics) == PORPOISE_EXIT_OK);
    view = porpoise_plan_find_function(plan, "jump_dispatch");
    CHECK(view != NULL &&
          view->requested_action == PORPOISE_PLAN_ACTION_IMPORT);
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_LIFT);
    CHECK(view != NULL && view->origin == PORPOISE_PLAN_ORIGIN_SDK_POLICY);
    CHECK(view != NULL && !view->blocked && view->binding == NULL);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);

    {
        static const struct {
            const char *adapter;
        } audited[] = {
            {
                "porpoise_libporpoise_gx_set_vtx_desc_adapter"
            },
            {
                "porpoise_libporpoise_gx_set_vtx_attr_fmt_adapter"
            }
        };
        size_t index;

        /* Assign at runtime because the test paths are caller-owned. */
        for (index = 0U; index < sizeof(audited) / sizeof(audited[0]); index++) {
            const char *selected_catalog = index == 0U
                ? audited_vtx_desc_catalog
                : audited_vtx_fmt_catalog;

            session = NULL;
            plan = NULL;
            porpoise_diagnostics_init(&diagnostics);
            CHECK(open_session(
                      input,
                      selected_catalog,
                      NULL,
                      &session,
                      &diagnostics) == PORPOISE_EXIT_OK);
            porpoise_plan_options_init(&options);
            options.entry_symbol = "safe_anchor";
            options.target_id = "sample";
            options.module = "main";
            options.sdk_policy = PORPOISE_SDK_POLICY_IMPORTED;
            CHECK(porpoise_plan_build(
                      session, &options, &plan, &diagnostics) ==
                  PORPOISE_EXIT_OK);
            view = porpoise_plan_find_function(plan, "jump_dispatch");
            CHECK(view != NULL &&
                  view->requested_action == PORPOISE_PLAN_ACTION_IMPORT);
            CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_IMPORT);
            CHECK(view != NULL && !view->blocked && view->binding != NULL);
            CHECK(view != NULL && view->binding != NULL &&
                  view->binding->adapter != NULL &&
                  strcmp(view->binding->adapter, audited[index].adapter) == 0);
            CHECK(porpoise_plan_validate(plan, &diagnostics) ==
                  PORPOISE_EXIT_OK);

            if (index == 0U) {
                const char *output =
                    ".porpoise-sdk-policy-audited-interior-output";
                char runtime[PORPOISE_PATH_CAPACITY];
                char registry[PORPOISE_PATH_CAPACITY];
                PorpoiseProjectOptions project_options;
                PorpoiseReport report;
                char *registry_text;

                CHECK(path_join(runtime, sizeof(runtime), source_root,
                                "runtime"));
                CHECK(path_join(
                    registry,
                    sizeof(registry),
                    output,
                    "src/porpoise_function_registry_8000.c"));
                CHECK(porpoise_remove_tree(output, &diagnostics));
                porpoise_project_options_init(&project_options);
                project_options.output_path = output;
                project_options.runtime_directory = runtime;
                project_options.entry_symbol = "safe_anchor";
                project_options.force = true;
                porpoise_report_init(&report);
                CHECK(porpoise_project_generate_plan(
                          plan,
                          &project_options,
                          &report,
                          &diagnostics) == PORPOISE_EXIT_OK);
                registry_text = read_file(registry);
                CHECK(registry_text != NULL &&
                      strstr(registry_text,
                             "porpoise_import_jump_dispatch") != NULL);
                CHECK(registry_text != NULL &&
                      strstr(registry_text, "0x80008000") != NULL);
                /* The private switch targets remain inert data only. No
                 * callable interior registry entry survives codegen. */
                CHECK(registry_text != NULL &&
                      strstr(registry_text, "0x80008014") == NULL);
                CHECK(registry_text != NULL &&
                      strstr(registry_text, "0x8000801C") == NULL);
                free(registry_text);
                porpoise_report_free(&report);
                CHECK(porpoise_remove_tree(output, &diagnostics));
            }

            porpoise_plan_free(plan);
            porpoise_session_close(session);
            porpoise_diagnostics_free(&diagnostics);
        }
    }
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

static void test_direct_contract_resolution(
    const char *input,
    const char *catalog,
    const char *abi,
    const PorpoiseFunctionSignature *gx_signature) {
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan;
    PorpoiseFunctionOverride override;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseFunctionPlanView *view;

    porpoise_diagnostics_init(&diagnostics);
    CHECK(open_session_with_abi(
              input, catalog, abi, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);

    plan = build_plan(
        session, PORPOISE_SDK_POLICY_IMPORTED, NULL, &diagnostics);
    view = porpoise_plan_find_function(plan, "GXInit");
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_IMPORT);
    CHECK(view != NULL &&
          view->origin == PORPOISE_PLAN_ORIGIN_SDK_POLICY);
    CHECK(view != NULL && view->binding != NULL);
    CHECK(view != NULL && view->binding != NULL &&
          strcmp(view->binding->symbol, "gx_direct_contract") == 0);
    CHECK(view != NULL && view->binding != NULL &&
          strcmp(view->binding->wrapper, "host_gx_direct") == 0);
    CHECK(view != NULL && view->binding != NULL &&
          view->binding->adapter == NULL);
    CHECK(view != NULL && view->contract_name != NULL &&
          strcmp(view->contract_name, "gx_direct_contract") == 0);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);
    porpoise_plan_free(plan);

    memset(&override, 0, sizeof(override));
    override.module = "main";
    override.address = UINT32_C(0x80010000);
    override.size = UINT32_C(0x20);
    override.normalized_fingerprint = gx_signature->digest_hex;
    override.action = PORPOISE_OVERRIDE_IMPORT;
    override.contract_name = "gx_direct_contract";
    plan = build_plan(
        session, PORPOISE_SDK_POLICY_KEEP, &override, &diagnostics);
    view = porpoise_plan_find_function(plan, "GXInit");
    CHECK(view != NULL && view->overridden);
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_IMPORT);
    CHECK(view != NULL &&
          view->origin == PORPOISE_PLAN_ORIGIN_MANUAL_OVERRIDE);
    CHECK(view != NULL && view->binding != NULL &&
          strcmp(view->binding->symbol, "gx_direct_contract") == 0);
    CHECK(view != NULL && view->binding != NULL &&
          view->binding->adapter == NULL);
    CHECK(view != NULL && view->contract_name != NULL &&
          strcmp(view->contract_name, "gx_direct_contract") == 0);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);
    porpoise_plan_free(plan);

    override.contract_name = "missing_direct_contract";
    plan = build_plan(
        session, PORPOISE_SDK_POLICY_KEEP, &override, &diagnostics);
    view = porpoise_plan_find_function(plan, "GXInit");
    CHECK(view != NULL && view->blocked);
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_LIFT);
    CHECK(porpoise_plan_validate(plan, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_path_bearing_map_ownership(
    const char *input,
    const char *catalog,
    const char *map) {
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseFunctionPlanView *gx;
    const PorpoiseFunctionPlanView *unknown;

    porpoise_diagnostics_init(&diagnostics);
    CHECK(open_session(input, catalog, map, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    plan = build_plan(
        session, PORPOISE_SDK_POLICY_IMPORTED, NULL, &diagnostics);
    gx = porpoise_plan_find_function(plan, "GXInit");
    CHECK(gx != NULL && gx->map_symbol != NULL);
    CHECK(gx != NULL && gx->map_symbol != NULL &&
          strcmp(
              gx->map_symbol->library,
              "C:\\SDK\\Libraries\\GX.A") == 0);
    CHECK(gx != NULL && gx->map_symbol != NULL &&
          strcmp(gx->map_symbol->object, "source\\GXInit.c") == 0);
    CHECK(gx != NULL && gx->canonical_sdk_identity != NULL &&
          strcmp(
              gx->canonical_sdk_identity,
              "gx.a/source/GXInit.c/GXInit") == 0);
    CHECK(gx != NULL &&
          (gx->evidence_flags & PORPOISE_PLAN_EVIDENCE_CONFLICT) == 0U);
    CHECK(gx != NULL && gx->action == PORPOISE_PLAN_ACTION_IMPORT);

    unknown = porpoise_plan_find_function(plan, "UnknownSdk");
    CHECK(unknown != NULL && unknown->sdk_entry == NULL);
    CHECK(unknown != NULL && unknown->has_sdk_category);
    CHECK(unknown != NULL &&
          unknown->sdk_category == PORPOISE_SDK_CATEGORY_DEMO);
    CHECK(unknown != NULL && unknown->canonical_sdk_identity != NULL &&
          strcmp(
              unknown->canonical_sdk_identity,
              "DEMO.A/source/UnknownSdk.c/UnknownSdk") == 0);
    CHECK(unknown != NULL && unknown->action == PORPOISE_PLAN_ACTION_LIFT);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_dtk_section_aware_map_selection(
    const char *input,
    const char *symbols_path) {
    PorpoiseSessionOpenOptions session_options;
    PorpoiseSessionSymbolSource symbol_source;
    PorpoisePlanOptions plan_options;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseFunctionPlanView *view;

    porpoise_diagnostics_init(&diagnostics);
    porpoise_session_open_options_init(&session_options);
    memset(&symbol_source, 0, sizeof(symbol_source));
    session_options.input_path = input;
    symbol_source.kind = PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS;
    symbol_source.path = symbols_path;
    symbol_source.module = "rel:sample";
    session_options.symbol_sources = &symbol_source;
    session_options.symbol_source_count = 1U;
    CHECK(porpoise_session_open(
              &session_options, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);

    porpoise_plan_options_init(&plan_options);
    plan_options.entry_symbol = "title_main";
    plan_options.target_id = "sample-rel";
    plan_options.module = "rel:sample";
    CHECK(porpoise_plan_build(
              session, &plan_options, &plan, &diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);
    view = porpoise_plan_find_function(plan, "GXInit");
    CHECK(view != NULL && view->map_symbol != NULL);
    CHECK(view != NULL && view->map_symbol != NULL &&
          strcmp(view->map_symbol->name, "TextSectionOwner") == 0);
    CHECK(view != NULL && view->map_symbol != NULL &&
          strcmp(view->map_symbol->section, ".text") == 0);
    CHECK(view != NULL && view->map_symbol != NULL &&
          strcmp(view->map_symbol->module, "rel:sample") == 0);
    CHECK(view != NULL && view->map_symbol != NULL &&
          view->map_symbol->provenance.kind ==
              PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS);
    CHECK(view != NULL &&
          (view->evidence_flags & PORPOISE_PLAN_EVIDENCE_MAP) != 0U);

    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
}

int main(int argc, char **argv) {
    const char *catalog = ".porpoise-sdk-policy-catalog.json";
    const char *direct_catalog =
        ".porpoise-sdk-policy-direct-catalog.json";
    const char *direct_abi = ".porpoise-sdk-policy-direct-abi.json";
    const char *path_catalog = ".porpoise-sdk-policy-path-catalog.json";
    const char *path_map = ".porpoise-sdk-policy-path.map";
    const char *section_symbols =
        ".porpoise-sdk-policy-section-symbols.txt";
    const char *structural_catalog =
        ".porpoise-sdk-policy-structural-catalog.json";
    const char *ambiguous_catalog =
        ".porpoise-sdk-policy-ambiguous-catalog.json";
    const char *interior_catalog =
        ".porpoise-sdk-policy-interior-catalog.json";
    const char *interior_contract_catalog =
        ".porpoise-sdk-policy-interior-contract-catalog.json";
    const char *audited_vtx_desc_catalog =
        ".porpoise-sdk-policy-audited-vtx-desc-catalog.json";
    const char *audited_vtx_fmt_catalog =
        ".porpoise-sdk-policy-audited-vtx-fmt-catalog.json";
    char input[PORPOISE_PATH_CAPACITY];
    char matching_map[PORPOISE_PATH_CAPACITY];
    char conflicting_map[PORPOISE_PATH_CAPACITY];
    PorpoiseFunctionSignature gx;
    PorpoiseFunctionSignature unknown;
    PorpoiseFunctionSignature structurally_changed_gx;
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
    CHECK(create_direct_contract_catalog(
        direct_catalog, "gx.a/GXInit.c/GXInit",
        "gx_direct_contract", &gx));
    CHECK(create_direct_abi(direct_abi));
    CHECK(create_direct_contract_catalog(
        path_catalog, "gx.a/source/GXInit.c/GXInit", "GXInit", &gx));
    CHECK(create_path_bearing_map(path_map));
    CHECK(create_section_collision_symbols(section_symbols));
    structurally_changed_gx = gx;
    structurally_changed_gx.meaningful_fixed_instruction_count--;
    CHECK(create_direct_contract_catalog(
        structural_catalog, "gx.a/GXInit.c/GXInit", "GXInit",
        &structurally_changed_gx));
    CHECK(create_ambiguous_catalog(ambiguous_catalog, &gx));
    test_policies(argv[1], input, catalog, matching_map);
    test_interior_entry_policy_fallback(
        argv[1], interior_catalog, interior_contract_catalog,
        audited_vtx_desc_catalog, audited_vtx_fmt_catalog, direct_abi);
    test_conflicts_and_overrides(input, catalog, conflicting_map, &gx);
    test_direct_contract_resolution(
        input, direct_catalog, direct_abi, &gx);
    test_path_bearing_map_ownership(input, path_catalog, path_map);
    test_dtk_section_aware_map_selection(input, section_symbols);
    test_match_hints(
        argv[1], input, catalog, structural_catalog,
        ambiguous_catalog, &gx);
    CHECK(remove(catalog) == 0);
    CHECK(remove(direct_catalog) == 0);
    CHECK(remove(direct_abi) == 0);
    CHECK(remove(path_catalog) == 0);
    CHECK(remove(path_map) == 0);
    CHECK(remove(section_symbols) == 0);
    CHECK(remove(structural_catalog) == 0);
    CHECK(remove(ambiguous_catalog) == 0);
    CHECK(remove(interior_catalog) == 0);
    CHECK(remove(interior_contract_catalog) == 0);
    CHECK(remove(audited_vtx_desc_catalog) == 0);
    CHECK(remove(audited_vtx_fmt_catalog) == 0);
    if (failures != 0U) {
        fprintf(stderr, "%u SDK policy test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
