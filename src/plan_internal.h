#ifndef PORPOISE_PLAN_INTERNAL_H
#define PORPOISE_PLAN_INTERNAL_H

#include "porpoise/analysis.h"
#include "porpoise/plan.h"

struct PorpoiseTranslationPlan {
    const PorpoiseSession *session;
    PorpoiseSdkPolicy sdk_policy;
    char *target_id;
    char *module;
    char digest_hex[PORPOISE_SHA256_HEX_SIZE];
    PorpoiseAnalysis analysis;
    PorpoiseAbiManifest effective_abi;
    size_t effective_abi_capacity;
    PorpoiseFunctionPlanView *functions;
    char **owned_sdk_identities;
    size_t function_count;
    const PorpoiseFunctionPlanView *entry;
    bool blocked;
    const char *blocking_reason;
};

/* Deterministic digest of every session and plan field consumed by generation. */
bool porpoise_plan_compute_binding_digest(
    const PorpoiseTranslationPlan *plan,
    char digest_hex[PORPOISE_SHA256_HEX_SIZE]);

/* Internal bridge for the legacy project generator. */
const PorpoiseAnalysis *porpoise_plan_analysis_snapshot(
    const PorpoiseTranslationPlan *plan);
const PorpoiseAbiManifest *porpoise_plan_effective_abi(
    const PorpoiseTranslationPlan *plan);

#endif
