#include "sdk_guest_layout_internal.h"

#include "porpoise/project.h"
#include "porpoise/report.h"
#include "porpoise/session.h"
#include "porpoise/signature.h"
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

static unsigned int failures;

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            (void)fprintf(                                                 \
                stderr, "%s:%d: check failed: %s\n",                    \
                __FILE__, __LINE__, #condition);                           \
            failures++;                                                    \
        }                                                                  \
    } while (0)

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

static const PorpoiseSourceFile *function_source(
    const PorpoiseProgram *program,
    const char *name,
    const PorpoiseFunction **function_out) {
    size_t file_index;
    if (function_out != NULL) *function_out = NULL;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *source = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U;
             function_index < source->function_count;
             function_index++) {
            if (strcmp(source->functions[function_index].name, name) == 0) {
                if (function_out != NULL) {
                    *function_out = &source->functions[function_index];
                }
                return source;
            }
        }
    }
    return NULL;
}

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

static bool write_catalog(
    const char *path,
    const char *identity,
    const PorpoiseFunctionSignature *signature) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    fprintf(
        file,
        "{\"schema_version\":1,\"signature_algorithm_version\":1,"
        "\"entries\":[{\"canonical_identity\":\"%s\","
        "\"category\":\"nintendo_dolphin\",\"contract\":\"OSInit\","
        "\"signature\":{\"sha256\":\"%s\",\"function_size\":%lu,"
        "\"instruction_count\":%lu,\"fixed_instruction_count\":%lu,"
        "\"meaningful_fixed_words\":%lu,\"relocation_count\":%lu,"
        "\"internal_branch_count\":%lu,\"external_branch_count\":%lu,"
        "\"external_target_count\":%lu,\"issue_flags\":%lu}}]}\n",
        identity,
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
    return fclose(file) == 0;
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
    catalogs[0] = catalog;
    options.sdk_catalog_paths = catalogs;
    options.sdk_catalog_path_count = catalog == NULL ? 0U : 1U;
    return porpoise_session_open(&options, session, diagnostics);
}

static PorpoiseTranslationPlan *build_imported_plan(
    PorpoiseSession *session,
    PorpoiseDiagnostics *diagnostics) {
    PorpoisePlanOptions options;
    PorpoiseTranslationPlan *plan = NULL;
    porpoise_plan_options_init(&options);
    options.entry_symbol = "title_main";
    options.target_id = "sdk-layout";
    options.module = "";
    options.sdk_policy = PORPOISE_SDK_POLICY_IMPORTED;
    CHECK(porpoise_plan_build(
              session, &options, &plan, diagnostics) == PORPOISE_EXIT_OK);
    return plan;
}

static void test_symbol_resolution(const char *source_root) {
    static const struct {
        const char *fixture;
        PorpoiseSdkGuestLayoutResolution expected;
        const char *problem;
    } cases[] = {
        {"valid", PORPOISE_SDK_GUEST_LAYOUT_RESOLVED, NULL},
        {"ambiguous", PORPOISE_SDK_GUEST_LAYOUT_AMBIGUOUS, "BootInfo"},
        {"missing", PORPOISE_SDK_GUEST_LAYOUT_MISSING, "__OSArenaLo"}
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char input[PORPOISE_PATH_CAPACITY];
        PorpoiseProgram program;
        PorpoiseDiagnostics diagnostics;
        PorpoiseSdkGuestOsLayout layout;
        const PorpoiseSourceFile *os_source;
        const char *problem = NULL;

        CHECK(snprintf(
                  input, sizeof(input),
                  "%s/tests/fixtures/sdk_guest_layout/%s",
                  source_root, cases[index].fixture) > 0);
        porpoise_program_init(&program);
        porpoise_diagnostics_init(&diagnostics);
        CHECK(porpoise_program_load(
                  &program, input, &diagnostics) == PORPOISE_EXIT_OK);
        os_source = function_source(&program, "OSInit", NULL);
        CHECK(os_source != NULL);
        CHECK(porpoise_sdk_guest_os_layout_resolve(
                  &program, os_source, &layout, &problem) ==
              cases[index].expected);
        if (cases[index].problem != NULL) {
            CHECK(problem != NULL &&
                  strcmp(problem, cases[index].problem) == 0);
        }
        if (cases[index].expected ==
            PORPOISE_SDK_GUEST_LAYOUT_RESOLVED) {
            CHECK(layout.arena_lo == UINT32_C(0x80010100));
            CHECK(layout.arena_hi == UINT32_C(0x80010104));
            CHECK(layout.initialized == UINT32_C(0x80010108));
            /* The same-source OS BootInfo wins over dvdfs's global. */
            CHECK(layout.boot_info == UINT32_C(0x8001010C));
            CHECK(layout.bi2_debug_flag == UINT32_C(0x80010110));
            CHECK(layout.dvd_long_file_name_flag ==
                  UINT32_C(0x80010114));
        }
        porpoise_program_free(&program);
        porpoise_diagnostics_free(&diagnostics);
    }
}

static void test_exact_plan_and_generation(const char *source_root) {
    char valid_input[PORPOISE_PATH_CAPACITY];
    char missing_input[PORPOISE_PATH_CAPACITY];
    char runtime[PORPOISE_PATH_CAPACITY];
    char catalog[PORPOISE_PATH_CAPACITY];
    char wrong_catalog[PORPOISE_PATH_CAPACITY];
    char output[PORPOISE_PATH_CAPACITY];
    char generated_source[PORPOISE_PATH_CAPACITY];
    PorpoiseSession *signature_session = NULL;
    PorpoiseSession *session = NULL;
    PorpoiseTranslationPlan *plan = NULL;
    PorpoiseDiagnostics diagnostics;
    PorpoiseFunctionSignature signature;
    const PorpoiseProgram *program;
    const PorpoiseFunction *os_init;
    const PorpoiseFunctionPlanView *view;
    PorpoiseProjectOptions project_options;
    PorpoiseReport report;
    char *generated = NULL;

    CHECK(snprintf(
              valid_input, sizeof(valid_input),
              "%s/tests/fixtures/sdk_guest_layout/valid",
              source_root) > 0);
    CHECK(snprintf(
              missing_input, sizeof(missing_input),
              "%s/tests/fixtures/sdk_guest_layout/missing",
              source_root) > 0);
    CHECK(snprintf(runtime, sizeof(runtime), "%s/runtime", source_root) > 0);
    CHECK(snprintf(
              catalog, sizeof(catalog), ".porpoise-sdk-layout-%lu.json",
              (unsigned long)TEST_GETPID()) > 0);
    CHECK(snprintf(
              wrong_catalog, sizeof(wrong_catalog),
              ".porpoise-sdk-layout-wrong-%lu.json",
              (unsigned long)TEST_GETPID()) > 0);
    CHECK(snprintf(
              output, sizeof(output), ".porpoise-sdk-layout-output-%lu",
              (unsigned long)TEST_GETPID()) > 0);
    CHECK(porpoise_path_join(
        generated_source, sizeof(generated_source), output,
        "src/porpoise_generated.c"));

    porpoise_diagnostics_init(&diagnostics);
    CHECK(open_session(
              valid_input, NULL, &signature_session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    program = porpoise_session_program(signature_session);
    os_init = function_source(program, "OSInit", NULL) != NULL
                  ? porpoise_program_find_function(program, "OSInit")
                  : NULL;
    CHECK(os_init != NULL);
    CHECK(porpoise_signature_compute(program, os_init, &signature));
    CHECK(porpoise_signature_is_automatic_match_eligible(&signature));
    CHECK(write_catalog(catalog, "os.a/OS.c/OSInit", &signature));
    CHECK(write_catalog(
        wrong_catalog, "other.a/Other.c/OSInit", &signature));
    porpoise_session_close(signature_session);
    signature_session = NULL;

    reset_diagnostics(&diagnostics);
    CHECK(open_session(valid_input, catalog, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    plan = build_imported_plan(session, &diagnostics);
    view = porpoise_plan_find_function(plan, "OSInit");
    CHECK(view != NULL && porpoise_sdk_guest_os_init_requires_layout(view));
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_IMPORT);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);

    CHECK(porpoise_remove_tree(output, &diagnostics));
    porpoise_project_options_init(&project_options);
    project_options.output_path = output;
    project_options.runtime_directory = runtime;
    project_options.entry_symbol = "title_main";
    project_options.force = true;
    porpoise_report_init(&report);
    CHECK(porpoise_project_generate_plan(
              plan, &project_options, &report, &diagnostics) ==
          PORPOISE_EXIT_OK);
    generated = read_text(generated_source);
    CHECK(generated != NULL &&
          strstr(generated, "PorpoiseLibporpoiseGuestSdkLayoutV1") != NULL);
    CHECK(generated != NULL &&
          strstr(generated, "UINT32_C(0x80010100)") != NULL);
    CHECK(generated != NULL &&
          strstr(
              generated,
              "porpoise_libporpoise_bind_guest_sdk_layout_v1") != NULL);
    free(generated);
    porpoise_report_free(&report);
    CHECK(porpoise_remove_tree(output, &diagnostics));
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    plan = NULL;
    session = NULL;

    /* A canonical-looking but wrong owner never gets the exact startup
     * adapter, so missing OS globals do not authorize or block it. */
    reset_diagnostics(&diagnostics);
    CHECK(open_session(
              missing_input, wrong_catalog, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    plan = build_imported_plan(session, &diagnostics);
    view = porpoise_plan_find_function(plan, "OSInit");
    CHECK(view != NULL && !porpoise_sdk_guest_os_init_requires_layout(view));
    CHECK(view != NULL && view->action == PORPOISE_PLAN_ACTION_LIFT);
    CHECK(porpoise_plan_validate(plan, &diagnostics) == PORPOISE_EXIT_OK);
    porpoise_plan_free(plan);
    porpoise_session_close(session);
    plan = NULL;
    session = NULL;

    reset_diagnostics(&diagnostics);
    CHECK(open_session(missing_input, catalog, &session, &diagnostics) ==
          PORPOISE_EXIT_OK);
    plan = build_imported_plan(session, &diagnostics);
    view = porpoise_plan_find_function(plan, "OSInit");
    CHECK(view != NULL && porpoise_sdk_guest_os_init_requires_layout(view));
    CHECK(porpoise_plan_validate(plan, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    CHECK(diagnostics_contain(&diagnostics, "__OSArenaLo"));
    CHECK(diagnostics_contain(&diagnostics, "is missing"));

    porpoise_plan_free(plan);
    porpoise_session_close(session);
    porpoise_diagnostics_free(&diagnostics);
    (void)remove(wrong_catalog);
    (void)remove(catalog);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        (void)fprintf(stderr, "usage: test_sdk_guest_layout SOURCE_ROOT\n");
        return 2;
    }
    test_symbol_resolution(argv[1]);
    test_exact_plan_and_generation(argv[1]);
    if (failures != 0U) {
        (void)fprintf(
            stderr, "%u SDK guest-layout test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
