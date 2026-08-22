#ifndef PORPOISE_RECOVERY_TITLE_HOST_H
#define PORPOISE_RECOVERY_TITLE_HOST_H

#include "porpoise/recovery_project.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PORPOISE_RECOVERY_TITLE_HOST_DVD_ROOT_ENV "PORPOISE_DVD_ROOT"

void porpoise_recovery_title_host_profile_init(
    PorpoiseRecoveryTitleHostProfile *profile);
void porpoise_recovery_title_host_profile_free(
    PorpoiseRecoveryTitleHostProfile *profile);

/*
 * Infer the standard CodeWarrior direct-main bootstrap only when the current
 * plan and symbol catalog uniquely provide _stack_addr, _SDA2_BASE_,
 * _SDA_BASE_, __ArenaLo, __ArenaHi, __OSThreadInit, and __init_user. The
 * conventional direct-main r1/initial words are then derived from
 * _stack_addr. Native DVD initialization remains false for explicit review.
 * Successful inference records path-independent canonical identities for the
 * loaded symbol and SDK-catalog evidence. A later semantic evidence change is
 * therefore a stale-profile Build/Run blocker, while absent optional evidence
 * remains represented by a NULL provenance digest.
 *
 * profile_out must be initialized. It is replaced transactionally on success
 * and remains unchanged when evidence is incomplete or ambiguous.
 */
int porpoise_recovery_title_host_infer(
    const PorpoiseRecoveryTarget *target,
    const PorpoiseTranslationPlan *plan,
    PorpoiseRecoveryTitleHostProfile *profile_out,
    PorpoiseDiagnostics *diagnostics);

/*
 * Validate a reviewed profile against the current immutable plan. This is a
 * Build/Run gate, not an Analyze gate: missing and stale profiles are rejected
 * without changing the session or translation plan.
 */
int porpoise_recovery_title_host_validate(
    const PorpoiseRecoveryTarget *target,
    const PorpoiseTranslationPlan *plan,
    PorpoiseDiagnostics *diagnostics);

/*
 * Generate a Meson subproject providing the `porpoise-title-host` dependency.
 * output_directory is expected to be build staging owned by the caller. The
 * generated source contains no absolute host path and reads PORPOISE_DVD_ROOT
 * only when initialize_dvd is enabled.
 */
int porpoise_recovery_title_host_generate(
    const PorpoiseRecoveryTarget *target,
    const PorpoiseTranslationPlan *plan,
    const char *output_directory,
    PorpoiseDiagnostics *diagnostics);

#ifdef __cplusplus
}
#endif

#endif
