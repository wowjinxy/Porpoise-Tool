#include "porpoise/recovery_annotation.h"
#include "porpoise/signature.h"

#include <stdlib.h>
#include <string.h>

static int annotation_error(
    PorpoiseDiagnostics *diagnostics,
    int result,
    const char *file,
    uint32_t address,
    const char *message) {
    if (diagnostics != NULL &&
        !porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file, 0U, address,
            "%s", message)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    return result;
}

static int annotation_named_error(
    PorpoiseDiagnostics *diagnostics,
    int result,
    const char *file,
    uint32_t address,
    const char *format,
    const char *name) {
    if (diagnostics != NULL &&
        !porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file, 0U, address,
            format, name)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    return result;
}

static bool lowercase_sha256_valid(const char *value) {
    size_t index;
    if (value == NULL || strlen(value) != 64U) return false;
    for (index = 0U; index < 64U; index++) {
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'a' && value[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static const char *annotation_interpretation_name(
    PorpoiseRecoveryAnnotationInterpretation interpretation) {
    static const char *const names[] = {
        "raw_bytes", "zero_fill", "ascii", "utf8", "shift_jis",
        "utf16", "s8_array", "u8_array", "s16_array", "u16_array",
        "s32_array", "u32_array", "f32_array", "f64_array",
        "pointer32_array"
    };
    if ((unsigned int)interpretation >=
        sizeof(names) / sizeof(names[0])) {
        return "unknown";
    }
    return names[(unsigned int)interpretation];
}

static bool range_valid(uint32_t address, uint32_t size) {
    return size != 0U &&
           (uint64_t)address + (uint64_t)size <= UINT64_C(0x100000000);
}

static int write_extracted_byte(
    uint8_t *bytes,
    uint8_t *coverage,
    uint32_t request_address,
    uint32_t request_size,
    uint32_t byte_address,
    uint8_t value,
    PorpoiseDiagnostics *diagnostics) {
    size_t offset;
    if (byte_address < request_address ||
        (uint64_t)byte_address >=
            (uint64_t)request_address + request_size) {
        return PORPOISE_EXIT_OK;
    }
    offset = (size_t)(byte_address - request_address);
    if (coverage[offset] != 0U) {
        return annotation_error(
            diagnostics, PORPOISE_EXIT_TRANSLATION, NULL, byte_address,
            "annotation bytes are covered by overlapping Program sources");
    }
    bytes[offset] = value;
    coverage[offset] = 1U;
    return PORPOISE_EXIT_OK;
}

void porpoise_recovery_byte_view_init(PorpoiseRecoveryByteView *view) {
    if (view != NULL) memset(view, 0, sizeof(*view));
}

void porpoise_recovery_byte_view_free(PorpoiseRecoveryByteView *view) {
    if (view == NULL) return;
    free((void *)view->bytes);
    memset(view, 0, sizeof(*view));
}

int porpoise_recovery_byte_view_extract(
    const PorpoiseProgram *program,
    uint32_t address,
    uint32_t size,
    PorpoiseRecoveryByteView *view,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseRecoveryByteView candidate;
    uint8_t *bytes;
    uint8_t *coverage;
    size_t span_index;
    size_t file_index;
    size_t offset;
    int result = PORPOISE_EXIT_OK;

    if (program == NULL || view == NULL || diagnostics == NULL) {
        return PORPOISE_EXIT_INTERNAL;
    }
    if (!range_valid(address, size)) {
        return annotation_error(
            diagnostics, PORPOISE_EXIT_USAGE, NULL, address,
            "annotation byte range is empty or crosses the 32-bit address boundary");
    }
    porpoise_recovery_byte_view_init(&candidate);
    bytes = (uint8_t *)malloc((size_t)size);
    coverage = (uint8_t *)calloc((size_t)size, 1U);
    if (bytes == NULL || coverage == NULL) {
        free(bytes);
        free(coverage);
        return PORPOISE_EXIT_INTERNAL;
    }

    for (span_index = 0U;
         span_index < program->data_span_count && result == PORPOISE_EXIT_OK;
         span_index++) {
        const PorpoiseDataSpan *span = &program->data_spans[span_index];
        uint64_t span_end = (uint64_t)span->address + span->size;
        uint64_t request_end = (uint64_t)address + size;
        uint64_t begin;
        uint64_t end;
        uint64_t cursor;
        if (span_end > UINT64_C(0x100000000)) {
            result = annotation_error(
                diagnostics, PORPOISE_EXIT_INTERNAL, NULL, span->address,
                "Program data span crosses the 32-bit address boundary");
            break;
        }
        if (span->kind != PORPOISE_DATA_SPAN_INITIALIZED &&
            span->kind != PORPOISE_DATA_SPAN_ZERO_FILL) {
            result = annotation_error(
                diagnostics, PORPOISE_EXIT_INTERNAL, NULL, span->address,
                "Program data span has an invalid storage kind");
            break;
        }
        if (span->kind == PORPOISE_DATA_SPAN_INITIALIZED &&
            span->size != 0U && span->bytes == NULL) {
            result = annotation_error(
                diagnostics, PORPOISE_EXIT_INTERNAL, NULL, span->address,
                "initialized Program data span has no byte storage");
            break;
        }
        begin = span->address > address ? span->address : address;
        end = span_end < request_end ? span_end : request_end;
        for (cursor = begin; cursor < end; cursor++) {
            uint8_t value = span->kind == PORPOISE_DATA_SPAN_ZERO_FILL
                ? 0U
                : span->bytes[(size_t)(cursor - span->address)];
            result = write_extracted_byte(
                bytes, coverage, address, size, (uint32_t)cursor, value,
                diagnostics);
            if (result != PORPOISE_EXIT_OK) break;
        }
    }

    for (file_index = 0U;
         file_index < program->file_count && result == PORPOISE_EXIT_OK;
         file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U;
             function_index < file->function_count &&
             result == PORPOISE_EXIT_OK;
             function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            size_t item_index;
            if (function->data_region) continue;
            for (item_index = 0U;
                 item_index < function->item_count;
                 item_index++) {
                const PorpoiseAsmItem *item = &function->items[item_index];
                unsigned int byte_index;
                if (item->kind != PORPOISE_ASM_INSTRUCTION) continue;
                if (item->address > UINT32_MAX - 3U) {
                    result = annotation_error(
                        diagnostics, PORPOISE_EXIT_INTERNAL, file->path,
                        item->address,
                        "Program instruction crosses the 32-bit address boundary");
                    break;
                }
                for (byte_index = 0U; byte_index < 4U; byte_index++) {
                    uint8_t value = (uint8_t)(
                        item->word >> (24U - byte_index * 8U));
                    result = write_extracted_byte(
                        bytes, coverage, address, size,
                        item->address + byte_index, value, diagnostics);
                    if (result != PORPOISE_EXIT_OK) break;
                }
                if (result != PORPOISE_EXIT_OK) break;
            }
        }
    }

    if (result == PORPOISE_EXIT_OK) {
        for (offset = 0U; offset < (size_t)size; offset++) {
            if (coverage[offset] == 0U) {
                result = annotation_error(
                    diagnostics, PORPOISE_EXIT_TRANSLATION, NULL,
                    address + (uint32_t)offset,
                    "annotation byte range is not fully covered by Program data or instructions");
                break;
            }
        }
    }
    free(coverage);
    if (result != PORPOISE_EXIT_OK) {
        free(bytes);
        return result;
    }
    candidate.address = address;
    candidate.size = (size_t)size;
    candidate.bytes = bytes;
    porpoise_recovery_byte_view_free(view);
    *view = candidate;
    return PORPOISE_EXIT_OK;
}

static const PorpoiseFunction *exact_code_function(
    const PorpoiseProgram *program,
    uint32_t address,
    uint32_t size) {
    size_t file_index;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            const PorpoiseFunction *function =
                &file->functions[function_index];
            if (!function->data_region &&
                function->start_address == address &&
                function->size == size) {
                return function;
            }
        }
    }
    return NULL;
}

static int normalized_fingerprint_from_view(
    const PorpoiseProgram *program,
    uint32_t address,
    uint32_t size,
    const PorpoiseRecoveryByteView *byte_view,
    char output[PORPOISE_SHA256_HEX_SIZE],
    PorpoiseDiagnostics *diagnostics) {
    const PorpoiseFunction *function =
        exact_code_function(program, address, size);
    if (function != NULL) {
        PorpoiseFunctionSignature signature;
        if (!porpoise_signature_compute(program, function, &signature)) {
            return annotation_error(
                diagnostics, PORPOISE_EXIT_TRANSLATION, NULL, address,
                "could not recompute the annotation's normalized code fingerprint");
        }
        memcpy(output, signature.digest_hex, sizeof(signature.digest_hex));
    } else {
        uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
        porpoise_sha256(byte_view->bytes, byte_view->size, digest);
        porpoise_sha256_hex(digest, output);
    }
    return PORPOISE_EXIT_OK;
}

int porpoise_recovery_normalized_fingerprint_compute(
    const PorpoiseProgram *program,
    uint32_t address,
    uint32_t size,
    char output[PORPOISE_SHA256_HEX_SIZE],
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseRecoveryByteView byte_view;
    int result;
    if (program == NULL || output == NULL || diagnostics == NULL) {
        return PORPOISE_EXIT_INTERNAL;
    }
    porpoise_recovery_byte_view_init(&byte_view);
    result = porpoise_recovery_byte_view_extract(
        program, address, size, &byte_view, diagnostics);
    if (result == PORPOISE_EXIT_OK) {
        result = normalized_fingerprint_from_view(
            program, address, size, &byte_view, output, diagnostics);
    }
    porpoise_recovery_byte_view_free(&byte_view);
    return result;
}

static bool encoding_equals(const char *encoding, const char *expected) {
    return encoding != NULL && strcmp(encoding, expected) == 0;
}

static bool endian_encoding_valid(const char *encoding) {
    return encoding_equals(encoding, "big-endian") ||
           encoding_equals(encoding, "little-endian");
}

static bool utf8_count(
    const uint8_t *bytes,
    size_t size,
    size_t *count_out) {
    size_t cursor = 0U;
    size_t count = 0U;
    while (cursor < size) {
        uint8_t first = bytes[cursor++];
        size_t following;
        if (first <= UINT8_C(0x7f)) {
            count++;
            continue;
        }
        if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf)) {
            following = 1U;
        } else if (first >= UINT8_C(0xe0) && first <= UINT8_C(0xef)) {
            following = 2U;
        } else if (first >= UINT8_C(0xf0) && first <= UINT8_C(0xf4)) {
            following = 3U;
        } else {
            return false;
        }
        if (size - cursor < following) return false;
        if (following >= 1U &&
            (bytes[cursor] < UINT8_C(0x80) ||
             bytes[cursor] > UINT8_C(0xbf))) return false;
        if (following == 2U) {
            if ((first == UINT8_C(0xe0) &&
                 bytes[cursor] < UINT8_C(0xa0)) ||
                (first == UINT8_C(0xed) &&
                 bytes[cursor] > UINT8_C(0x9f))) return false;
        } else if (following == 3U) {
            if ((first == UINT8_C(0xf0) &&
                 bytes[cursor] < UINT8_C(0x90)) ||
                (first == UINT8_C(0xf4) &&
                 bytes[cursor] > UINT8_C(0x8f))) return false;
        }
        while (following-- > 1U) {
            cursor++;
            if (bytes[cursor] < UINT8_C(0x80) ||
                bytes[cursor] > UINT8_C(0xbf)) return false;
        }
        cursor++;
        count++;
    }
    *count_out = count;
    return true;
}

static bool shift_jis_count(
    const uint8_t *bytes,
    size_t size,
    size_t *count_out) {
    size_t cursor = 0U;
    size_t count = 0U;
    while (cursor < size) {
        uint8_t first = bytes[cursor++];
        if (first <= UINT8_C(0x7f) ||
            (first >= UINT8_C(0xa1) && first <= UINT8_C(0xdf))) {
            count++;
            continue;
        }
        if (!((first >= UINT8_C(0x81) && first <= UINT8_C(0x9f)) ||
              (first >= UINT8_C(0xe0) && first <= UINT8_C(0xfc))) ||
            cursor == size) {
            return false;
        }
        if (!((bytes[cursor] >= UINT8_C(0x40) &&
               bytes[cursor] <= UINT8_C(0x7e)) ||
              (bytes[cursor] >= UINT8_C(0x80) &&
               bytes[cursor] <= UINT8_C(0xfc)))) {
            return false;
        }
        cursor++;
        count++;
    }
    *count_out = count;
    return true;
}

static uint16_t load_utf16_unit(const uint8_t *bytes, bool little_endian) {
    if (little_endian) {
        return (uint16_t)((uint16_t)bytes[0] |
                          ((uint16_t)bytes[1] << 8U));
    }
    return (uint16_t)(((uint16_t)bytes[0] << 8U) |
                      (uint16_t)bytes[1]);
}

static bool utf16_count(
    const uint8_t *bytes,
    size_t size,
    bool little_endian,
    size_t *count_out) {
    size_t cursor = 0U;
    size_t count = 0U;
    while (cursor < size) {
        uint16_t unit = load_utf16_unit(bytes + cursor, little_endian);
        cursor += 2U;
        if (unit >= UINT16_C(0xd800) && unit <= UINT16_C(0xdbff)) {
            uint16_t low;
            if (size - cursor < 2U) return false;
            low = load_utf16_unit(bytes + cursor, little_endian);
            if (low < UINT16_C(0xdc00) || low > UINT16_C(0xdfff)) {
                return false;
            }
            cursor += 2U;
        } else if (unit >= UINT16_C(0xdc00) &&
                   unit <= UINT16_C(0xdfff)) {
            return false;
        }
        count++;
    }
    *count_out = count;
    return true;
}

static int validate_annotation_locator(
    const PorpoiseRecoveryAnnotation *annotation,
    const char *target_id,
    const char *expected_module,
    PorpoiseDiagnostics *diagnostics) {
    const char *module = expected_module != NULL ? expected_module : "";
    if (annotation == NULL || target_id == NULL || target_id[0] == '\0') {
        return PORPOISE_EXIT_INTERNAL;
    }
    if (annotation->target == NULL || annotation->target[0] == '\0' ||
        strcmp(annotation->target, target_id) != 0) {
        return annotation_error(
            diagnostics, PORPOISE_EXIT_USAGE, target_id,
            annotation->address,
            "annotation target does not match the owning recovery target");
    }
    if (annotation->module == NULL ||
        strcmp(annotation->module, module) != 0) {
        return annotation_error(
            diagnostics, PORPOISE_EXIT_USAGE, target_id,
            annotation->address,
            "annotation module does not match the active target module");
    }
    if (!range_valid(annotation->address, annotation->size)) {
        return annotation_error(
            diagnostics, PORPOISE_EXIT_USAGE, target_id,
            annotation->address,
            "annotation size is zero or its range crosses the 32-bit address boundary");
    }
    if (!lowercase_sha256_valid(annotation->normalized_fingerprint)) {
        return annotation_error(
            diagnostics, PORPOISE_EXIT_USAGE, target_id,
            annotation->address,
            "annotation fingerprint must contain 64 lowercase hexadecimal digits");
    }
    if (!lowercase_sha256_valid(annotation->exact_bytes_sha256)) {
        return annotation_error(
            diagnostics, PORPOISE_EXIT_USAGE, target_id,
            annotation->address,
            "annotation exact-byte hash must contain 64 lowercase hexadecimal digits");
    }
    if ((unsigned int)annotation->interpretation >
        (unsigned int)PORPOISE_RECOVERY_ANNOTATION_POINTER32_ARRAY) {
        return annotation_error(
            diagnostics, PORPOISE_EXIT_USAGE, target_id,
            annotation->address,
            "annotation interpretation is outside the supported range");
    }
    if (annotation->element_count == 0U) {
        return annotation_error(
            diagnostics, PORPOISE_EXIT_USAGE, target_id,
            annotation->address,
            "annotation element count must be greater than zero");
    }
    return PORPOISE_EXIT_OK;
}

static int validate_annotation_interpretation(
    const PorpoiseRecoveryAnnotation *annotation,
    const uint8_t *bytes,
    size_t *element_width_out,
    size_t *decoded_count_out,
    PorpoiseDiagnostics *diagnostics) {
    size_t width = 0U;
    size_t decoded_count = 0U;
    uint64_t expected_size;
    size_t index;
    bool valid_encoding = false;
    bool valid_contents = true;
    const char *name = annotation_interpretation_name(
        annotation->interpretation);

    switch (annotation->interpretation) {
        case PORPOISE_RECOVERY_ANNOTATION_RAW_BYTES:
        case PORPOISE_RECOVERY_ANNOTATION_ZERO_FILL:
        case PORPOISE_RECOVERY_ANNOTATION_S8_ARRAY:
        case PORPOISE_RECOVERY_ANNOTATION_U8_ARRAY:
            width = 1U;
            decoded_count = annotation->size;
            valid_encoding = annotation->encoding == NULL;
            break;
        case PORPOISE_RECOVERY_ANNOTATION_ASCII:
            width = 1U;
            decoded_count = annotation->size;
            valid_encoding = encoding_equals(annotation->encoding, "ascii");
            for (index = 0U; index < annotation->size; index++) {
                if (bytes[index] > UINT8_C(0x7f)) valid_contents = false;
            }
            break;
        case PORPOISE_RECOVERY_ANNOTATION_UTF8:
            valid_encoding = encoding_equals(annotation->encoding, "utf-8");
            valid_contents = utf8_count(
                bytes, annotation->size, &decoded_count);
            break;
        case PORPOISE_RECOVERY_ANNOTATION_SHIFT_JIS:
            valid_encoding = encoding_equals(
                annotation->encoding, "shift-jis");
            valid_contents = shift_jis_count(
                bytes, annotation->size, &decoded_count);
            break;
        case PORPOISE_RECOVERY_ANNOTATION_UTF16:
            width = 2U;
            valid_encoding =
                encoding_equals(annotation->encoding, "utf-16be") ||
                encoding_equals(annotation->encoding, "utf-16le");
            if ((annotation->address & UINT32_C(1)) != 0U ||
                (annotation->size & UINT32_C(1)) != 0U) {
                valid_contents = false;
            } else if (valid_encoding) {
                valid_contents = utf16_count(
                    bytes, annotation->size,
                    encoding_equals(annotation->encoding, "utf-16le"),
                    &decoded_count);
            }
            break;
        case PORPOISE_RECOVERY_ANNOTATION_S16_ARRAY:
        case PORPOISE_RECOVERY_ANNOTATION_U16_ARRAY:
            width = 2U;
            valid_encoding = endian_encoding_valid(annotation->encoding);
            break;
        case PORPOISE_RECOVERY_ANNOTATION_S32_ARRAY:
        case PORPOISE_RECOVERY_ANNOTATION_U32_ARRAY:
        case PORPOISE_RECOVERY_ANNOTATION_F32_ARRAY:
        case PORPOISE_RECOVERY_ANNOTATION_POINTER32_ARRAY:
            width = 4U;
            valid_encoding = endian_encoding_valid(annotation->encoding);
            break;
        case PORPOISE_RECOVERY_ANNOTATION_F64_ARRAY:
            width = 8U;
            valid_encoding = endian_encoding_valid(annotation->encoding);
            break;
    }

    if (!valid_encoding) {
        return annotation_named_error(
            diagnostics, PORPOISE_EXIT_USAGE, annotation->target,
            annotation->address,
            "annotation interpretation %s has a missing or unsupported encoding",
            name);
    }
    if (width > 1U &&
        ((annotation->address % (uint32_t)width) != 0U ||
         (annotation->size % (uint32_t)width) != 0U)) {
        return annotation_named_error(
            diagnostics, PORPOISE_EXIT_USAGE, annotation->target,
            annotation->address,
            "annotation interpretation %s has an unaligned address or size",
            name);
    }
    if (!valid_contents) {
        return annotation_named_error(
            diagnostics, PORPOISE_EXIT_USAGE, annotation->target,
            annotation->address,
            "annotation bytes are not valid for interpretation %s",
            name);
    }
    if (annotation->interpretation ==
        PORPOISE_RECOVERY_ANNOTATION_ZERO_FILL) {
        for (index = 0U; index < annotation->size; index++) {
            if (bytes[index] != 0U) {
                return annotation_error(
                    diagnostics, PORPOISE_EXIT_TRANSLATION,
                    annotation->target, annotation->address,
                    "zero-fill annotation contains a nonzero existing byte");
            }
        }
    }
    if (width != 0U &&
        annotation->interpretation != PORPOISE_RECOVERY_ANNOTATION_ASCII &&
        annotation->interpretation != PORPOISE_RECOVERY_ANNOTATION_UTF16) {
        expected_size =
            (uint64_t)annotation->element_count * (uint64_t)width;
        decoded_count = annotation->element_count;
        if (expected_size != annotation->size) {
            return annotation_named_error(
                diagnostics, PORPOISE_EXIT_USAGE, annotation->target,
                annotation->address,
                "annotation size and element count disagree for interpretation %s",
                name);
        }
    } else if (decoded_count != annotation->element_count) {
        return annotation_named_error(
            diagnostics, PORPOISE_EXIT_USAGE, annotation->target,
            annotation->address,
            "annotation decoded element count disagrees for interpretation %s",
            name);
    }
    *element_width_out = width;
    *decoded_count_out = decoded_count;
    return PORPOISE_EXIT_OK;
}

void porpoise_recovery_annotation_view_init(
    PorpoiseRecoveryAnnotationView *view) {
    if (view != NULL) memset(view, 0, sizeof(*view));
}

void porpoise_recovery_annotation_view_free(
    PorpoiseRecoveryAnnotationView *view) {
    if (view == NULL) return;
    porpoise_recovery_byte_view_free(&view->byte_view);
    memset(view, 0, sizeof(*view));
}

int porpoise_recovery_annotation_view_open(
    const PorpoiseProgram *program,
    const PorpoiseRecoveryAnnotation *annotation,
    const char *target_id,
    const char *expected_module,
    PorpoiseRecoveryAnnotationView *view,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseRecoveryAnnotationView candidate;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    int result;
    if (program == NULL || annotation == NULL || view == NULL ||
        diagnostics == NULL) {
        return PORPOISE_EXIT_INTERNAL;
    }
    result = validate_annotation_locator(
        annotation, target_id, expected_module, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    porpoise_recovery_annotation_view_init(&candidate);
    result = porpoise_recovery_byte_view_extract(
        program, annotation->address, annotation->size,
        &candidate.byte_view, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    porpoise_sha256(
        candidate.byte_view.bytes, candidate.byte_view.size, digest);
    porpoise_sha256_hex(digest, candidate.exact_bytes_sha256);
    result = normalized_fingerprint_from_view(
        program, annotation->address, annotation->size,
        &candidate.byte_view, candidate.normalized_fingerprint,
        diagnostics);
    if (result != PORPOISE_EXIT_OK) {
        porpoise_recovery_annotation_view_free(&candidate);
        return result;
    }
    if (strcmp(candidate.normalized_fingerprint,
               annotation->normalized_fingerprint) != 0) {
        porpoise_recovery_annotation_view_free(&candidate);
        return annotation_error(
            diagnostics, PORPOISE_EXIT_TRANSLATION, annotation->target,
            annotation->address,
            "annotation normalized fingerprint is stale for the current Program code or data");
    }
    if (strcmp(candidate.exact_bytes_sha256,
               annotation->exact_bytes_sha256) != 0) {
        porpoise_recovery_annotation_view_free(&candidate);
        return annotation_error(
            diagnostics, PORPOISE_EXIT_TRANSLATION, annotation->target,
            annotation->address,
            "annotation exact-byte hash is stale for the current Program bytes");
    }
    result = validate_annotation_interpretation(
        annotation, candidate.byte_view.bytes, &candidate.element_width,
        &candidate.decoded_element_count, diagnostics);
    if (result != PORPOISE_EXIT_OK) {
        porpoise_recovery_annotation_view_free(&candidate);
        return result;
    }
    candidate.annotation = annotation;
    porpoise_recovery_annotation_view_free(view);
    *view = candidate;
    return PORPOISE_EXIT_OK;
}

int porpoise_recovery_annotations_validate(
    const PorpoiseProgram *program,
    const PorpoiseRecoveryAnnotation *annotations,
    size_t annotation_count,
    const char *target_id,
    const char *expected_module,
    PorpoiseDiagnostics *diagnostics) {
    size_t left;
    if (program == NULL || target_id == NULL || target_id[0] == '\0' ||
        diagnostics == NULL ||
        (annotation_count != 0U && annotations == NULL)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    for (left = 0U; left < annotation_count; left++) {
        int result = validate_annotation_locator(
            &annotations[left], target_id, expected_module, diagnostics);
        if (result != PORPOISE_EXIT_OK) return result;
    }
    for (left = 0U; left < annotation_count; left++) {
        size_t right;
        for (right = left + 1U; right < annotation_count; right++) {
            uint64_t left_end =
                (uint64_t)annotations[left].address + annotations[left].size;
            uint64_t right_end =
                (uint64_t)annotations[right].address + annotations[right].size;
            if ((uint64_t)annotations[left].address < right_end &&
                (uint64_t)annotations[right].address < left_end) {
                return annotation_error(
                    diagnostics, PORPOISE_EXIT_USAGE, target_id,
                    annotations[right].address,
                    "recovery annotations contain overlapping byte ranges");
            }
        }
    }
    for (left = 0U; left < annotation_count; left++) {
        PorpoiseRecoveryAnnotationView view;
        int result;
        porpoise_recovery_annotation_view_init(&view);
        result = porpoise_recovery_annotation_view_open(
            program, &annotations[left], target_id, expected_module,
            &view, diagnostics);
        porpoise_recovery_annotation_view_free(&view);
        if (result != PORPOISE_EXIT_OK) return result;
    }
    return PORPOISE_EXIT_OK;
}
