#ifndef PORPOISE_LIBPORPOISE_PRESENTATION_PRIVATE_H
#define PORPOISE_LIBPORPOISE_PRESENTATION_PRIVATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Snapshot libPorpoise's successful host-XFB presentation counter.  The
 * implementation is a tiny C++ bridge because libPorpoise intentionally
 * exposes these read-only statistics from its C++ simulator namespace.
 * Returning zero means the selected checkout does not provide that exact
 * observation API; callers must fail closed instead of treating a queued XFB
 * or a retrace as a presented frame.
 */
int porpoise_libporpoise_presentation_snapshot(
    uint64_t *presentation_count_out,
    uint32_t *guest_frame_buffer_out);

#ifdef __cplusplus
}
#endif

#endif
