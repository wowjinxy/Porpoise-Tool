#ifndef PORPOISE_PLAN_H
#define PORPOISE_PLAN_H

#include "porpoise/session.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PorpoiseTranslationPlan PorpoiseTranslationPlan;

typedef enum PorpoiseSdkPolicy {
    PORPOISE_SDK_POLICY_KEEP = 0,
    PORPOISE_SDK_POLICY_IMPORTED,
    PORPOISE_SDK_POLICY_OMIT
} PorpoiseSdkPolicy;

typedef enum PorpoisePlanAction {
    PORPOISE_PLAN_ACTION_LIFT = 0,
    PORPOISE_PLAN_ACTION_DATA,
    PORPOISE_PLAN_ACTION_OMIT,
    PORPOISE_PLAN_ACTION_IMPORT
} PorpoisePlanAction;

typedef enum PorpoisePlanOrigin {
    PORPOISE_PLAN_ORIGIN_DEFAULT = 0,
    PORPOISE_PLAN_ORIGIN_INPUT_DATA,
    PORPOISE_PLAN_ORIGIN_SKIP_LIST,
    PORPOISE_PLAN_ORIGIN_ABI_IMPORT
} PorpoisePlanOrigin;

typedef struct PorpoisePlanOptions {
    const char *entry_symbol;
    PorpoiseSdkPolicy sdk_policy;
} PorpoisePlanOptions;

/*
 * A view is immutable and borrows all pointer members from the plan's session.
 * binding_address is the ABI binding address for IMPORT and the function start
 * address for every other action.
 */
typedef struct PorpoiseFunctionPlanView {
    const PorpoiseSourceFile *source;
    const PorpoiseFunction *function;
    PorpoisePlanAction action;
    PorpoisePlanOrigin origin;
    const PorpoiseAbiFunction *binding;
    const PorpoiseAddressAlias *binding_alias;
    uint32_t binding_address;
} PorpoiseFunctionPlanView;

void porpoise_plan_options_init(PorpoisePlanOptions *options);

/*
 * Build an immutable snapshot. The session must outlive the returned plan.
 * Views are ordered first by Program file order and then by function order.
 */
int porpoise_plan_build(
    const PorpoiseSession *session,
    const PorpoisePlanOptions *options,
    PorpoiseTranslationPlan **plan_out,
    PorpoiseDiagnostics *diagnostics);
void porpoise_plan_free(PorpoiseTranslationPlan *plan);

/* Recheck the structural invariants of an already built plan. */
int porpoise_plan_validate(
    const PorpoiseTranslationPlan *plan,
    PorpoiseDiagnostics *diagnostics);

const PorpoiseSession *porpoise_plan_session(
    const PorpoiseTranslationPlan *plan);
PorpoiseSdkPolicy porpoise_plan_sdk_policy(
    const PorpoiseTranslationPlan *plan);
size_t porpoise_plan_function_count(
    const PorpoiseTranslationPlan *plan);
const PorpoiseFunctionPlanView *porpoise_plan_function_at(
    const PorpoiseTranslationPlan *plan,
    size_t index);
const PorpoiseFunctionPlanView *porpoise_plan_find_function(
    const PorpoiseTranslationPlan *plan,
    const char *name);
const PorpoiseFunctionPlanView *porpoise_plan_entry(
    const PorpoiseTranslationPlan *plan);

const char *porpoise_sdk_policy_name(PorpoiseSdkPolicy policy);
const char *porpoise_plan_action_name(PorpoisePlanAction action);
const char *porpoise_plan_origin_name(PorpoisePlanOrigin origin);

#ifdef __cplusplus
}
#endif

#endif
