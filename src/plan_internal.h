#ifndef PORPOISE_PLAN_INTERNAL_H
#define PORPOISE_PLAN_INTERNAL_H

#include "porpoise/analysis.h"
#include "porpoise/plan.h"

struct PorpoiseTranslationPlan {
    const PorpoiseSession *session;
    PorpoiseSdkPolicy sdk_policy;
    PorpoiseAnalysis analysis;
    PorpoiseFunctionPlanView *functions;
    size_t function_count;
    const PorpoiseFunctionPlanView *entry;
};

/* Internal bridge for the legacy project generator. */
const PorpoiseAnalysis *porpoise_plan_analysis_snapshot(
    const PorpoiseTranslationPlan *plan);

#endif
