#include "porpoise/plan.h"
#include "porpoise/session.h"

#include <stdio.h>
#include <string.h>

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

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s SOURCE_ROOT\n", argv[0]);
        return 2;
    }
    test_session_and_legacy_plan(argv[1]);
    test_plan_without_abi(argv[1]);
    test_invalid_inputs(argv[1]);
    if (failures != 0U) {
        fprintf(stderr, "%u session/plan test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
