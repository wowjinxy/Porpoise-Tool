#ifndef PORPOISE_PROCESS_INTERNAL_H
#define PORPOISE_PROCESS_INTERNAL_H

#include "porpoise/build.h"

/*
 * Live log callbacks receive every byte synchronously.  The convenience
 * capture retains only the newest 1 MiB from each stream so an unattended
 * compiler or title cannot grow process memory without bound.  Callers that
 * consume captured machine-readable output must reject a true truncation flag.
 */
#define PORPOISE_PROCESS_CAPTURE_LIMIT_BYTES (1024U * 1024U)

typedef struct PorpoiseProcessCapture {
    int exit_code;
    char *standard_output;
    char *standard_error;
    bool standard_output_truncated;
    bool standard_error_truncated;
} PorpoiseProcessCapture;

void porpoise_process_capture_init(PorpoiseProcessCapture *capture);
void porpoise_process_capture_free(PorpoiseProcessCapture *capture);

int porpoise_process_run(
    const char *const *argv,
    const char *working_directory,
    const PorpoiseBuildEnvironmentEntry *environment,
    size_t environment_count,
    PorpoiseBuildPhase phase,
    const PorpoiseBuildCallbacks *callbacks,
    PorpoiseProcessCapture *capture,
    PorpoiseDiagnostics *diagnostics);

#endif
