#ifndef PORPOISE_RECOVERY_ANNOTATION_H
#define PORPOISE_RECOVERY_ANNOTATION_H

#include "porpoise/program.h"
#include "porpoise/recovery_project.h"
#include "porpoise/sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * An immutable snapshot of guest bytes. `bytes` is read-only to callers and
 * never aliases Program storage. Initialize before extraction and release with
 * porpoise_recovery_byte_view_free().
 */
typedef struct PorpoiseRecoveryByteView {
    uint32_t address;
    size_t size;
    const uint8_t *bytes;
} PorpoiseRecoveryByteView;

/*
 * A validated annotation plus the immutable bytes shown by a GUI/editor.
 * `annotation` is borrowed from the caller. Variable-width encodings report an
 * element_width of zero; decoded_element_count still records the verified
 * Unicode/Shift-JIS character count.
 */
typedef struct PorpoiseRecoveryAnnotationView {
    const PorpoiseRecoveryAnnotation *annotation;
    PorpoiseRecoveryByteView byte_view;
    size_t element_width;
    size_t decoded_element_count;
    char exact_bytes_sha256[PORPOISE_SHA256_HEX_SIZE];
} PorpoiseRecoveryAnnotationView;

void porpoise_recovery_byte_view_init(PorpoiseRecoveryByteView *view);
void porpoise_recovery_byte_view_free(PorpoiseRecoveryByteView *view);

/*
 * Copy a fully covered guest range from immutable Program data spans or
 * annotated instruction words. Instruction words are exposed in guest
 * big-endian byte order. Gaps and overlapping Program sources are rejected.
 * The destination view is replaced only after successful extraction.
 */
int porpoise_recovery_byte_view_extract(
    const PorpoiseProgram *program,
    uint32_t address,
    uint32_t size,
    PorpoiseRecoveryByteView *view,
    PorpoiseDiagnostics *diagnostics);

void porpoise_recovery_annotation_view_init(
    PorpoiseRecoveryAnnotationView *view);
void porpoise_recovery_annotation_view_free(
    PorpoiseRecoveryAnnotationView *view);

/*
 * Validate one annotation and build its read-only byte view. Canonical
 * encoding values are:
 *
 *   raw_bytes, zero_fill, s8_array, u8_array: encoding == NULL
 *   ascii:       "ascii"
 *   utf8:        "utf-8"
 *   shift_jis:   "shift-jis"
 *   utf16:       "utf-16be" or "utf-16le"
 *   16/32-bit integer, f32/f64 and pointer32 arrays:
 *                "big-endian" or "little-endian"
 *
 * target_id is required. A NULL expected_module denotes the empty module.
 * The destination view is replaced only after all checks succeed.
 */
int porpoise_recovery_annotation_view_open(
    const PorpoiseProgram *program,
    const PorpoiseRecoveryAnnotation *annotation,
    const char *target_id,
    const char *expected_module,
    PorpoiseRecoveryAnnotationView *view,
    PorpoiseDiagnostics *diagnostics);

/* Validate a target's complete annotation set, including non-overlap. */
int porpoise_recovery_annotations_validate(
    const PorpoiseProgram *program,
    const PorpoiseRecoveryAnnotation *annotations,
    size_t annotation_count,
    const char *target_id,
    const char *expected_module,
    PorpoiseDiagnostics *diagnostics);

#ifdef __cplusplus
}
#endif

#endif
