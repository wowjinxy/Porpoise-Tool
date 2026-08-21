#ifndef PORPOISE_PLAN_H
#define PORPOISE_PLAN_H

#include "porpoise/session.h"
#include "porpoise/signature.h"

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
    PORPOISE_PLAN_ORIGIN_ABI_IMPORT,
    PORPOISE_PLAN_ORIGIN_SDK_POLICY,
    PORPOISE_PLAN_ORIGIN_MANUAL_OVERRIDE
} PorpoisePlanOrigin;

typedef enum PorpoiseOverrideAction {
    PORPOISE_OVERRIDE_AUTO = 0,
    PORPOISE_OVERRIDE_LIFT,
    PORPOISE_OVERRIDE_IMPORT,
    PORPOISE_OVERRIDE_OMIT,
    PORPOISE_OVERRIDE_TREAT_AS_DATA
} PorpoiseOverrideAction;

typedef enum PorpoiseMatchConfidence {
    PORPOISE_MATCH_CONFIDENCE_NONE = 0,
    PORPOISE_MATCH_CONFIDENCE_MAP_ONLY,
    PORPOISE_MATCH_CONFIDENCE_EXACT
} PorpoiseMatchConfidence;

typedef enum PorpoisePlanEvidenceFlag {
    PORPOISE_PLAN_EVIDENCE_NONE = 0U,
    PORPOISE_PLAN_EVIDENCE_MAP = 1U << 0,
    PORPOISE_PLAN_EVIDENCE_SIGNATURE = 1U << 1,
    PORPOISE_PLAN_EVIDENCE_AMBIGUOUS_SIGNATURE = 1U << 2,
    PORPOISE_PLAN_EVIDENCE_CONFLICT = 1U << 3,
    PORPOISE_PLAN_EVIDENCE_OVERRIDE = 1U << 4
} PorpoisePlanEvidenceFlag;

typedef struct PorpoiseFunctionOverride {
    const char *module;
    uint32_t address;
    uint32_t size;
    /* Lowercase canonical signature hex; required for a stable locator. */
    const char *normalized_fingerprint;
    PorpoiseOverrideAction action;
    /* Required for IMPORT; ignored by other actions. */
    const char *contract_name;
    bool acknowledge_conflict;
} PorpoiseFunctionOverride;

/*
 * Non-authoritative exact-match hint. Every field is revalidated against the
 * loaded function and current SDK catalog; stale or malformed hints are
 * ignored and ordinary exact catalog matching proceeds unchanged.
 */
typedef struct PorpoisePlanMatchHint {
    const char *target_id;
    const char *module;
    uint32_t address;
    uint32_t size;
    const char *normalized_fingerprint;
    const char *canonical_identity;
    const char *contract_name;
} PorpoisePlanMatchHint;

typedef struct PorpoisePlanOptions {
    const char *entry_symbol;
    const char *target_id;
    const char *module;
    PorpoiseSdkPolicy sdk_policy;
    const PorpoiseFunctionOverride *overrides;
    size_t override_count;
    const PorpoisePlanMatchHint *match_hints;
    size_t match_hint_count;
    /* Optional operational counter; reset and filled by plan_build(). */
    size_t *match_hint_used_count_out;
    const PorpoiseOperationCallbacks *operation;
} PorpoisePlanOptions;

/*
 * A view is immutable. Pointer members are owned by either the plan or its
 * session and remain valid until the plan is freed (the session must outlive
 * the plan). binding_address is the ABI binding address for IMPORT and the
 * function start address for every other action.
 */
typedef struct PorpoiseFunctionPlanView {
    const PorpoiseSourceFile *source;
    const PorpoiseFunction *function;
    PorpoisePlanAction action;
    PorpoisePlanAction requested_action;
    PorpoisePlanOrigin origin;
    const PorpoiseAbiFunction *binding;
    const PorpoiseAddressAlias *binding_alias;
    uint32_t binding_address;
    PorpoiseFunctionSignature signature;
    const PorpoiseSymbol *map_symbol;
    const PorpoiseSdkCatalogEntry *sdk_entry;
    const char *canonical_sdk_identity;
    const char *contract_name;
    PorpoiseSdkCategory sdk_category;
    PorpoiseMatchConfidence confidence;
    unsigned int evidence_flags;
    PorpoiseOverrideAction override_action;
    bool has_sdk_category;
    bool overridden;
    bool blocked;
    const char *blocking_reason;
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

/*
 * Recheck structural invariants and the plan's session/settings binding.
 * Validation rejects a plan when any generation-relevant session input or
 * snapshotted plan field has changed since porpoise_plan_build().
 */
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
const char *porpoise_plan_target_id(
    const PorpoiseTranslationPlan *plan);
const char *porpoise_plan_module(
    const PorpoiseTranslationPlan *plan);
const char *porpoise_plan_digest(
    const PorpoiseTranslationPlan *plan);

const char *porpoise_sdk_policy_name(PorpoiseSdkPolicy policy);
const char *porpoise_plan_action_name(PorpoisePlanAction action);
const char *porpoise_plan_origin_name(PorpoisePlanOrigin origin);
const char *porpoise_override_action_name(PorpoiseOverrideAction action);
const char *porpoise_match_confidence_name(PorpoiseMatchConfidence confidence);

#ifdef __cplusplus
}
#endif

#endif
