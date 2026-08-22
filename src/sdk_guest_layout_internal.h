#ifndef PORPOISE_SDK_GUEST_LAYOUT_INTERNAL_H
#define PORPOISE_SDK_GUEST_LAYOUT_INTERNAL_H

#include "porpoise/plan.h"

typedef struct PorpoiseSdkGuestOsLayout {
    uint32_t arena_lo;
    uint32_t arena_hi;
    uint32_t initialized;
    uint32_t boot_info;
    uint32_t bi2_debug_flag;
    uint32_t dvd_long_file_name_flag;
} PorpoiseSdkGuestOsLayout;

typedef enum PorpoiseSdkGuestLayoutResolution {
    PORPOISE_SDK_GUEST_LAYOUT_NOT_REQUIRED = 0,
    PORPOISE_SDK_GUEST_LAYOUT_RESOLVED,
    PORPOISE_SDK_GUEST_LAYOUT_MISSING,
    PORPOISE_SDK_GUEST_LAYOUT_AMBIGUOUS,
    PORPOISE_SDK_GUEST_LAYOUT_INVALID
} PorpoiseSdkGuestLayoutResolution;

/*
 * Return whether this immutable plan row represents the exact, audited
 * os.a/OS.c/OSInit contract. Merely naming a function or adapter OSInit is
 * deliberately insufficient.
 */
bool porpoise_sdk_guest_os_init_requires_layout(
    const PorpoiseFunctionPlanView *view);

/*
 * Resolve the guest globals whose writes are elided when exact OSInit is
 * imported. Exact names and canonical DTK NAME_XXXXXXXX spellings are
 * accepted; the suffix must equal the symbol's actual guest address.
 * Symbols in OSInit's translation unit take precedence, matching ordinary
 * C/assembly locality, and otherwise only unique global data symbols qualify.
 */
PorpoiseSdkGuestLayoutResolution porpoise_sdk_guest_os_layout_resolve(
    const PorpoiseProgram *program,
    const PorpoiseSourceFile *os_init_source,
    PorpoiseSdkGuestOsLayout *layout_out,
    const char **problem_symbol_out);

/* Resolve the layout only when a plan imports the exact audited OSInit. */
PorpoiseSdkGuestLayoutResolution porpoise_sdk_guest_os_layout_for_plan(
    const PorpoiseTranslationPlan *plan,
    PorpoiseSdkGuestOsLayout *layout_out,
    const PorpoiseSourceFile **os_init_source_out,
    const char **problem_symbol_out);

#endif
