#include "plan_internal.h"

#include <stdlib.h>
#include <string.h>

void porpoise_plan_options_init(PorpoisePlanOptions *options) {
    if (options == NULL) return;
    memset(options, 0, sizeof(*options));
    options->sdk_policy = PORPOISE_SDK_POLICY_KEEP;
}

static bool sdk_policy_valid(PorpoiseSdkPolicy policy) {
    return policy == PORPOISE_SDK_POLICY_KEEP ||
           policy == PORPOISE_SDK_POLICY_IMPORTED ||
           policy == PORPOISE_SDK_POLICY_OMIT;
}

static int plan_error(
    PorpoiseDiagnostics *diagnostics,
    int result,
    const char *message) {
    if (diagnostics != NULL) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "%s",
            message);
    }
    return result;
}

static const PorpoiseImportBinding *find_import_binding(
    const PorpoiseAnalysis *analysis,
    const PorpoiseFunction *function) {
    size_t index;
    for (index = 0U; index < analysis->import_binding_count; index++) {
        if (analysis->import_bindings[index].owner == function) {
            return &analysis->import_bindings[index];
        }
    }
    return NULL;
}

static bool count_program_functions(
    const PorpoiseProgram *program,
    size_t *count_out) {
    size_t file_index;
    size_t count = 0U;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        size_t file_count = program->files[file_index].function_count;
        if (file_count > SIZE_MAX - count) return false;
        count += file_count;
    }
    *count_out = count;
    return true;
}

static void populate_function_view(
    PorpoiseFunctionPlanView *view,
    const PorpoiseSourceFile *source,
    const PorpoiseFunction *function,
    const PorpoiseAnalysis *analysis) {
    const PorpoiseImportBinding *binding =
        find_import_binding(analysis, function);

    memset(view, 0, sizeof(*view));
    view->source = source;
    view->function = function;
    view->binding_address = function->start_address;

    if (function->data_region) {
        view->action = PORPOISE_PLAN_ACTION_DATA;
        view->origin = PORPOISE_PLAN_ORIGIN_INPUT_DATA;
    } else if (!function->skipped) {
        view->action = PORPOISE_PLAN_ACTION_LIFT;
        view->origin = PORPOISE_PLAN_ORIGIN_DEFAULT;
    } else if (binding != NULL) {
        view->action = PORPOISE_PLAN_ACTION_IMPORT;
        view->origin = PORPOISE_PLAN_ORIGIN_ABI_IMPORT;
        view->binding = binding->import;
        view->binding_alias = binding->alias;
        view->binding_address = binding->guest_address;
    } else {
        view->action = PORPOISE_PLAN_ACTION_OMIT;
        view->origin = PORPOISE_PLAN_ORIGIN_SKIP_LIST;
    }
}

int porpoise_plan_build(
    const PorpoiseSession *session,
    const PorpoisePlanOptions *options,
    PorpoiseTranslationPlan **plan_out,
    PorpoiseDiagnostics *diagnostics) {
    PorpoisePlanOptions defaults;
    PorpoiseTranslationPlan *plan;
    const PorpoiseProgram *program;
    const PorpoiseAbiManifest *abi;
    size_t file_index;
    size_t view_index = 0U;
    int result;

    if (plan_out == NULL || diagnostics == NULL) {
        return PORPOISE_EXIT_INTERNAL;
    }
    *plan_out = NULL;
    if (session == NULL) {
        return plan_error(
            diagnostics,
            PORPOISE_EXIT_INTERNAL,
            "recovery session is required to build a translation plan");
    }
    if (options == NULL) {
        porpoise_plan_options_init(&defaults);
        options = &defaults;
    }
    if (!sdk_policy_valid(options->sdk_policy)) {
        return plan_error(
            diagnostics,
            PORPOISE_EXIT_USAGE,
            "translation plan contains an invalid SDK policy");
    }

    program = porpoise_session_program(session);
    abi = porpoise_session_abi(session);
    if (program == NULL || abi == NULL) {
        return plan_error(
            diagnostics,
            PORPOISE_EXIT_INTERNAL,
            "recovery session is incomplete");
    }

    plan = (PorpoiseTranslationPlan *)calloc(1U, sizeof(*plan));
    if (plan == NULL) {
        return plan_error(
            diagnostics,
            PORPOISE_EXIT_INTERNAL,
            "out of memory while creating translation plan");
    }
    plan->session = session;
    plan->sdk_policy = options->sdk_policy;
    porpoise_analysis_init(&plan->analysis);

    result = porpoise_analyze_program(
        program,
        abi,
        options->entry_symbol,
        &plan->analysis,
        diagnostics);
    if (result != PORPOISE_EXIT_OK) {
        porpoise_plan_free(plan);
        return result;
    }
    if (!count_program_functions(program, &plan->function_count) ||
        (plan->function_count != 0U &&
         plan->function_count > SIZE_MAX / sizeof(*plan->functions))) {
        porpoise_plan_free(plan);
        return plan_error(
            diagnostics,
            PORPOISE_EXIT_INTERNAL,
            "too many functions in translation plan");
    }
    if (plan->function_count != 0U) {
        plan->functions = (PorpoiseFunctionPlanView *)calloc(
            plan->function_count, sizeof(*plan->functions));
        if (plan->functions == NULL) {
            porpoise_plan_free(plan);
            return plan_error(
                diagnostics,
                PORPOISE_EXIT_INTERNAL,
                "out of memory while snapshotting translation plan");
        }
    }

    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *source = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U;
             function_index < source->function_count;
             function_index++) {
            const PorpoiseFunction *function =
                &source->functions[function_index];
            PorpoiseFunctionPlanView *view = &plan->functions[view_index++];
            populate_function_view(view, source, function, &plan->analysis);
            if (function == plan->analysis.entry) plan->entry = view;
        }
    }

    result = porpoise_plan_validate(plan, diagnostics);
    if (result != PORPOISE_EXIT_OK) {
        porpoise_plan_free(plan);
        return result;
    }
    *plan_out = plan;
    return PORPOISE_EXIT_OK;
}

void porpoise_plan_free(PorpoiseTranslationPlan *plan) {
    if (plan == NULL) return;
    free(plan->functions);
    porpoise_analysis_free(&plan->analysis);
    free(plan);
}

static bool binding_matches_view(
    const PorpoiseAnalysis *analysis,
    const PorpoiseFunctionPlanView *view) {
    size_t index;
    for (index = 0U; index < analysis->import_binding_count; index++) {
        const PorpoiseImportBinding *binding =
            &analysis->import_bindings[index];
        if (binding->owner == view->function &&
            binding->import == view->binding &&
            binding->alias == view->binding_alias &&
            binding->guest_address == view->binding_address) {
            return true;
        }
    }
    return false;
}

int porpoise_plan_validate(
    const PorpoiseTranslationPlan *plan,
    PorpoiseDiagnostics *diagnostics) {
    const PorpoiseProgram *program;
    size_t expected_count;
    size_t file_index;
    size_t view_index = 0U;
    size_t lifted_count = 0U;

    if (diagnostics == NULL) return PORPOISE_EXIT_INTERNAL;
    if (plan == NULL || plan->session == NULL) {
        return plan_error(
            diagnostics,
            PORPOISE_EXIT_INTERNAL,
            "translation plan is not initialized");
    }
    if (!sdk_policy_valid(plan->sdk_policy)) {
        return plan_error(
            diagnostics,
            PORPOISE_EXIT_INTERNAL,
            "translation plan SDK policy is corrupt");
    }
    program = porpoise_session_program(plan->session);
    if (program == NULL ||
        !count_program_functions(program, &expected_count) ||
        expected_count != plan->function_count ||
        (expected_count != 0U && plan->functions == NULL)) {
        return plan_error(
            diagnostics,
            PORPOISE_EXIT_INTERNAL,
            "translation plan function snapshot is inconsistent");
    }

    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *source = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U;
             function_index < source->function_count;
             function_index++) {
            const PorpoiseFunction *function =
                &source->functions[function_index];
            const PorpoiseFunctionPlanView *view =
                &plan->functions[view_index++];
            bool valid = false;

            if (view->source != source || view->function != function) {
                return plan_error(
                    diagnostics,
                    PORPOISE_EXIT_INTERNAL,
                    "translation plan function order is inconsistent");
            }
            switch (view->action) {
                case PORPOISE_PLAN_ACTION_LIFT:
                    valid = !function->skipped && !function->data_region &&
                            view->origin == PORPOISE_PLAN_ORIGIN_DEFAULT &&
                            view->binding == NULL &&
                            view->binding_alias == NULL &&
                            view->binding_address == function->start_address;
                    if (valid) lifted_count++;
                    break;
                case PORPOISE_PLAN_ACTION_DATA:
                    valid = function->data_region &&
                            view->origin == PORPOISE_PLAN_ORIGIN_INPUT_DATA &&
                            view->binding == NULL &&
                            view->binding_alias == NULL &&
                            view->binding_address == function->start_address;
                    break;
                case PORPOISE_PLAN_ACTION_OMIT:
                    valid = function->skipped && !function->data_region &&
                            view->origin == PORPOISE_PLAN_ORIGIN_SKIP_LIST &&
                            view->binding == NULL &&
                            view->binding_alias == NULL &&
                            view->binding_address == function->start_address &&
                            find_import_binding(&plan->analysis, function) == NULL;
                    break;
                case PORPOISE_PLAN_ACTION_IMPORT:
                    valid = function->skipped && !function->data_region &&
                            view->origin == PORPOISE_PLAN_ORIGIN_ABI_IMPORT &&
                            view->binding != NULL &&
                            binding_matches_view(&plan->analysis, view);
                    break;
                default:
                    valid = false;
                    break;
            }
            if (!valid) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    source->path,
                    0U,
                    function->start_address,
                    "translation plan action for %s is inconsistent with its input state",
                    function->name);
                return PORPOISE_EXIT_INTERNAL;
            }
        }
    }

    if (lifted_count != plan->analysis.translated_function_count) {
        return plan_error(
            diagnostics,
            PORPOISE_EXIT_INTERNAL,
            "translation plan and analysis function counts disagree");
    }
    if ((plan->analysis.entry == NULL) != (plan->entry == NULL) ||
        (plan->entry != NULL &&
         (plan->entry->function != plan->analysis.entry ||
          plan->entry->action != PORPOISE_PLAN_ACTION_LIFT))) {
        return plan_error(
            diagnostics,
            PORPOISE_EXIT_INTERNAL,
            "translation plan entry is inconsistent with its analysis");
    }
    return PORPOISE_EXIT_OK;
}

const PorpoiseSession *porpoise_plan_session(
    const PorpoiseTranslationPlan *plan) {
    return plan == NULL ? NULL : plan->session;
}

PorpoiseSdkPolicy porpoise_plan_sdk_policy(
    const PorpoiseTranslationPlan *plan) {
    return plan == NULL ? PORPOISE_SDK_POLICY_KEEP : plan->sdk_policy;
}

size_t porpoise_plan_function_count(
    const PorpoiseTranslationPlan *plan) {
    return plan == NULL ? 0U : plan->function_count;
}

const PorpoiseFunctionPlanView *porpoise_plan_function_at(
    const PorpoiseTranslationPlan *plan,
    size_t index) {
    if (plan == NULL || index >= plan->function_count) return NULL;
    return &plan->functions[index];
}

const PorpoiseFunctionPlanView *porpoise_plan_find_function(
    const PorpoiseTranslationPlan *plan,
    const char *name) {
    size_t index;
    if (plan == NULL || name == NULL) return NULL;
    for (index = 0U; index < plan->function_count; index++) {
        if (strcmp(plan->functions[index].function->name, name) == 0) {
            return &plan->functions[index];
        }
    }
    return NULL;
}

const PorpoiseFunctionPlanView *porpoise_plan_entry(
    const PorpoiseTranslationPlan *plan) {
    return plan == NULL ? NULL : plan->entry;
}

const PorpoiseAnalysis *porpoise_plan_analysis_snapshot(
    const PorpoiseTranslationPlan *plan) {
    return plan == NULL ? NULL : &plan->analysis;
}

const char *porpoise_sdk_policy_name(PorpoiseSdkPolicy policy) {
    switch (policy) {
        case PORPOISE_SDK_POLICY_KEEP: return "keep";
        case PORPOISE_SDK_POLICY_IMPORTED: return "imported";
        case PORPOISE_SDK_POLICY_OMIT: return "omit";
        default: return "unknown";
    }
}

const char *porpoise_plan_action_name(PorpoisePlanAction action) {
    switch (action) {
        case PORPOISE_PLAN_ACTION_LIFT: return "lift";
        case PORPOISE_PLAN_ACTION_DATA: return "data";
        case PORPOISE_PLAN_ACTION_OMIT: return "omit";
        case PORPOISE_PLAN_ACTION_IMPORT: return "import";
        default: return "unknown";
    }
}

const char *porpoise_plan_origin_name(PorpoisePlanOrigin origin) {
    switch (origin) {
        case PORPOISE_PLAN_ORIGIN_DEFAULT: return "default";
        case PORPOISE_PLAN_ORIGIN_INPUT_DATA: return "input-data";
        case PORPOISE_PLAN_ORIGIN_SKIP_LIST: return "skip-list";
        case PORPOISE_PLAN_ORIGIN_ABI_IMPORT: return "abi-import";
        default: return "unknown";
    }
}
