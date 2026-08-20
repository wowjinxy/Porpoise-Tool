#include "porpoise/recovery_annotation.h"

#include <stdio.h>
#include <string.h>

static unsigned int failures = 0U;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #condition);                                                \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static const char fingerprint[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static bool diagnostics_contain(
    const PorpoiseDiagnostics *diagnostics,
    const char *text) {
    size_t index;
    for (index = 0U; index < diagnostics->count; index++) {
        if (strstr(diagnostics->items[index].message, text) != NULL) {
            return true;
        }
    }
    return false;
}

static void reset_diagnostics(PorpoiseDiagnostics *diagnostics) {
    porpoise_diagnostics_free(diagnostics);
    porpoise_diagnostics_init(diagnostics);
}

static void hash_bytes(
    const uint8_t *bytes,
    size_t size,
    char output[PORPOISE_SHA256_HEX_SIZE]) {
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    porpoise_sha256(bytes, size, digest);
    porpoise_sha256_hex(digest, output);
}

static void set_annotation(
    PorpoiseRecoveryAnnotation *annotation,
    uint32_t address,
    uint32_t size,
    PorpoiseRecoveryAnnotationInterpretation interpretation,
    uint32_t count,
    const char *encoding,
    char exact_hash[PORPOISE_SHA256_HEX_SIZE],
    const uint8_t *bytes) {
    memset(annotation, 0, sizeof(*annotation));
    annotation->target = (char *)"target";
    annotation->module = (char *)"main";
    annotation->address = address;
    annotation->size = size;
    annotation->normalized_fingerprint = (char *)fingerprint;
    hash_bytes(bytes, size, exact_hash);
    annotation->exact_bytes_sha256 = exact_hash;
    annotation->interpretation = interpretation;
    annotation->element_count = count;
    annotation->encoding = (char *)encoding;
}

static void check_annotation_opens(
    const PorpoiseProgram *program,
    const PorpoiseRecoveryAnnotation *annotation,
    size_t expected_width,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseRecoveryAnnotationView view;
    int result;
    porpoise_recovery_annotation_view_init(&view);
    result = porpoise_recovery_annotation_view_open(
        program, annotation, "target", "main", &view, diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(view.annotation == annotation);
    CHECK(view.byte_view.address == annotation->address);
    CHECK(view.byte_view.size == annotation->size);
    CHECK(view.element_width == expected_width);
    CHECK(view.decoded_element_count == annotation->element_count);
    CHECK(strcmp(view.exact_bytes_sha256,
                 annotation->exact_bytes_sha256) == 0);
    porpoise_recovery_annotation_view_free(&view);
}

static void test_valid_views_and_interpretations(void) {
    uint8_t data[96];
    uint8_t original[sizeof(data)];
    uint8_t zeros[8] = {0};
    uint8_t adjacent_zeros[4] = {0};
    PorpoiseDataSpan spans[4];
    PorpoiseAsmItem instruction;
    PorpoiseFunction function;
    PorpoiseSourceFile file;
    PorpoiseProgram program;
    PorpoiseRecoveryAnnotation annotations[15];
    char hashes[15][PORPOISE_SHA256_HEX_SIZE];
    PorpoiseDiagnostics diagnostics;
    PorpoiseRecoveryByteView bytes;
    size_t index;
    int result;

    for (index = 0U; index < sizeof(data); index++) {
        data[index] = (uint8_t)(index + 1U);
    }
    memcpy(data + 4U, "Test", 4U);
    {
        static const uint8_t utf8[] = {
            0x41U, 0xc2U, 0xa2U, 0xe2U, 0x82U,
            0xacU, 0xf0U, 0x90U, 0x8dU, 0x88U
        };
        static const uint8_t shift_jis[] = {
            0x82U, 0xa0U, 0xa6U, 0x41U
        };
        static const uint8_t utf16be[] = {
            0x00U, 0x41U, 0xd8U, 0x3dU, 0xdeU, 0x00U
        };
        memcpy(data + 8U, utf8, sizeof(utf8));
        memcpy(data + 18U, shift_jis, sizeof(shift_jis));
        memcpy(data + 22U, utf16be, sizeof(utf16be));
    }
    data[92] = UINT8_C(0xd8);
    data[93] = UINT8_C(0x00);
    memcpy(original, data, sizeof(data));

    memset(spans, 0, sizeof(spans));
    spans[0].kind = PORPOISE_DATA_SPAN_INITIALIZED;
    spans[0].address = UINT32_C(0x1000);
    spans[0].size = sizeof(data);
    spans[0].bytes = data;
    spans[1].kind = PORPOISE_DATA_SPAN_ZERO_FILL;
    spans[1].address = UINT32_C(0x1060);
    spans[1].size = sizeof(adjacent_zeros);
    spans[2].kind = PORPOISE_DATA_SPAN_ZERO_FILL;
    spans[2].address = UINT32_C(0x1100);
    spans[2].size = sizeof(zeros);

    memset(&instruction, 0, sizeof(instruction));
    instruction.kind = PORPOISE_ASM_INSTRUCTION;
    instruction.address = UINT32_C(0x1200);
    instruction.word = UINT32_C(0x12345678);
    memset(&function, 0, sizeof(function));
    function.items = &instruction;
    function.item_count = 1U;
    function.instruction_count = 1U;
    memset(&file, 0, sizeof(file));
    file.path = (char *)"memory.s";
    file.functions = &function;
    file.function_count = 1U;
    memset(&program, 0, sizeof(program));
    program.files = &file;
    program.file_count = 1U;
    program.data_spans = spans;
    program.data_span_count = 3U;

    set_annotation(&annotations[0], UINT32_C(0x1000), 4U,
                   PORPOISE_RECOVERY_ANNOTATION_RAW_BYTES, 4U, NULL,
                   hashes[0], data);
    set_annotation(&annotations[1], UINT32_C(0x1004), 4U,
                   PORPOISE_RECOVERY_ANNOTATION_ASCII, 4U, "ascii",
                   hashes[1], data + 4U);
    set_annotation(&annotations[2], UINT32_C(0x1008), 10U,
                   PORPOISE_RECOVERY_ANNOTATION_UTF8, 4U, "utf-8",
                   hashes[2], data + 8U);
    set_annotation(&annotations[3], UINT32_C(0x1012), 4U,
                   PORPOISE_RECOVERY_ANNOTATION_SHIFT_JIS, 3U,
                   "shift-jis", hashes[3], data + 18U);
    set_annotation(&annotations[4], UINT32_C(0x1016), 6U,
                   PORPOISE_RECOVERY_ANNOTATION_UTF16, 2U, "utf-16be",
                   hashes[4], data + 22U);
    set_annotation(&annotations[5], UINT32_C(0x101c), 4U,
                   PORPOISE_RECOVERY_ANNOTATION_S8_ARRAY, 4U, NULL,
                   hashes[5], data + 28U);
    set_annotation(&annotations[6], UINT32_C(0x1058), 4U,
                   PORPOISE_RECOVERY_ANNOTATION_U8_ARRAY, 4U, NULL,
                   hashes[6], data + 88U);
    set_annotation(&annotations[7], UINT32_C(0x1020), 4U,
                   PORPOISE_RECOVERY_ANNOTATION_S16_ARRAY, 2U,
                   "big-endian", hashes[7], data + 32U);
    set_annotation(&annotations[8], UINT32_C(0x1024), 4U,
                   PORPOISE_RECOVERY_ANNOTATION_U16_ARRAY, 2U,
                   "little-endian", hashes[8], data + 36U);
    set_annotation(&annotations[9], UINT32_C(0x1028), 8U,
                   PORPOISE_RECOVERY_ANNOTATION_S32_ARRAY, 2U,
                   "big-endian", hashes[9], data + 40U);
    set_annotation(&annotations[10], UINT32_C(0x1030), 8U,
                   PORPOISE_RECOVERY_ANNOTATION_U32_ARRAY, 2U,
                   "little-endian", hashes[10], data + 48U);
    set_annotation(&annotations[11], UINT32_C(0x1038), 8U,
                   PORPOISE_RECOVERY_ANNOTATION_F32_ARRAY, 2U,
                   "big-endian", hashes[11], data + 56U);
    set_annotation(&annotations[12], UINT32_C(0x1040), 16U,
                   PORPOISE_RECOVERY_ANNOTATION_F64_ARRAY, 2U,
                   "little-endian", hashes[12], data + 64U);
    set_annotation(&annotations[13], UINT32_C(0x1050), 8U,
                   PORPOISE_RECOVERY_ANNOTATION_POINTER32_ARRAY, 2U,
                   "big-endian", hashes[13], data + 80U);
    set_annotation(&annotations[14], UINT32_C(0x1100), 8U,
                   PORPOISE_RECOVERY_ANNOTATION_ZERO_FILL, 8U, NULL,
                   hashes[14], zeros);

    porpoise_diagnostics_init(&diagnostics);
    check_annotation_opens(&program, &annotations[0], 1U, &diagnostics);
    check_annotation_opens(&program, &annotations[1], 1U, &diagnostics);
    check_annotation_opens(&program, &annotations[2], 0U, &diagnostics);
    check_annotation_opens(&program, &annotations[3], 0U, &diagnostics);
    check_annotation_opens(&program, &annotations[4], 2U, &diagnostics);
    check_annotation_opens(&program, &annotations[5], 1U, &diagnostics);
    check_annotation_opens(&program, &annotations[6], 1U, &diagnostics);
    check_annotation_opens(&program, &annotations[7], 2U, &diagnostics);
    check_annotation_opens(&program, &annotations[8], 2U, &diagnostics);
    check_annotation_opens(&program, &annotations[9], 4U, &diagnostics);
    check_annotation_opens(&program, &annotations[10], 4U, &diagnostics);
    check_annotation_opens(&program, &annotations[11], 4U, &diagnostics);
    check_annotation_opens(&program, &annotations[12], 8U, &diagnostics);
    check_annotation_opens(&program, &annotations[13], 4U, &diagnostics);
    check_annotation_opens(&program, &annotations[14], 1U, &diagnostics);
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));

    result = porpoise_recovery_annotations_validate(
        &program, annotations, 15U, "target", "main", &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);

    porpoise_recovery_byte_view_init(&bytes);
    result = porpoise_recovery_byte_view_extract(
        &program, UINT32_C(0x1200), 4U, &bytes, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(bytes.size == 4U);
    if (bytes.size == 4U) {
        CHECK(bytes.bytes[0] == UINT8_C(0x12));
        CHECK(bytes.bytes[1] == UINT8_C(0x34));
        CHECK(bytes.bytes[2] == UINT8_C(0x56));
        CHECK(bytes.bytes[3] == UINT8_C(0x78));
    }
    porpoise_recovery_byte_view_free(&bytes);

    porpoise_recovery_byte_view_init(&bytes);
    result = porpoise_recovery_byte_view_extract(
        &program, UINT32_C(0x105e), 4U, &bytes, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(bytes.size == 4U && bytes.bytes[2] == 0U && bytes.bytes[3] == 0U);
    porpoise_recovery_byte_view_free(&bytes);

    CHECK(memcmp(data, original, sizeof(data)) == 0);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_invalid_annotations_and_coverage(void) {
    uint8_t data[16] = {
        0x01U, 0x02U, 0x03U, 0x04U,
        0x41U, 0xc2U, 0xa2U, 0x00U,
        0xd8U, 0x00U, 0x11U, 0x22U,
        0x82U, 0xa0U, 0x33U, 0x44U
    };
    uint8_t original[sizeof(data)];
    PorpoiseDataSpan spans[2];
    PorpoiseProgram program;
    PorpoiseRecoveryAnnotation annotation;
    PorpoiseRecoveryAnnotation overlap[2];
    PorpoiseRecoveryAnnotationView view;
    PorpoiseRecoveryByteView byte_view;
    PorpoiseDiagnostics diagnostics;
    char hash[PORPOISE_SHA256_HEX_SIZE];
    char overlap_hashes[2][PORPOISE_SHA256_HEX_SIZE];
    const uint8_t *retained_view_bytes;
    int result;

    memcpy(original, data, sizeof(data));
    memset(spans, 0, sizeof(spans));
    spans[0].kind = PORPOISE_DATA_SPAN_INITIALIZED;
    spans[0].address = UINT32_C(0x2000);
    spans[0].size = sizeof(data);
    spans[0].bytes = data;
    memset(&program, 0, sizeof(program));
    program.data_spans = spans;
    program.data_span_count = 1U;
    porpoise_diagnostics_init(&diagnostics);
    porpoise_recovery_annotation_view_init(&view);

    set_annotation(&annotation, UINT32_C(0x2000), 4U,
                   PORPOISE_RECOVERY_ANNOTATION_RAW_BYTES, 4U, NULL,
                   hash, data);
    result = porpoise_recovery_annotation_view_open(
        &program, &annotation, "target", "main", &view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    retained_view_bytes = view.byte_view.bytes;
    reset_diagnostics(&diagnostics);

    annotation.target = (char *)"wrong";
    result = porpoise_recovery_annotation_view_open(
        &program, &annotation, "target", "main", &view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "target"));
    reset_diagnostics(&diagnostics);

    annotation.target = (char *)"target";
    annotation.module = (char *)"wrong";
    result = porpoise_recovery_annotation_view_open(
        &program, &annotation, "target", "main", &view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "module"));
    reset_diagnostics(&diagnostics);

    annotation.module = (char *)"main";
    annotation.normalized_fingerprint = (char *)"ABC";
    result = porpoise_recovery_annotation_view_open(
        &program, &annotation, "target", "main", &view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "fingerprint"));
    reset_diagnostics(&diagnostics);

    annotation.normalized_fingerprint = (char *)fingerprint;
    annotation.exact_bytes_sha256 = (char *)"ABC";
    result = porpoise_recovery_annotation_view_open(
        &program, &annotation, "target", "main", &view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "exact-byte hash"));
    reset_diagnostics(&diagnostics);

    annotation.exact_bytes_sha256 = (char *)fingerprint;
    result = porpoise_recovery_annotation_view_open(
        &program, &annotation, "target", "main", &view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_TRANSLATION);
    CHECK(diagnostics_contain(&diagnostics, "stale"));
    CHECK(view.byte_view.bytes == retained_view_bytes);
    reset_diagnostics(&diagnostics);

    set_annotation(&annotation, UINT32_C(0x2001), 2U,
                   PORPOISE_RECOVERY_ANNOTATION_U16_ARRAY, 1U,
                   "big-endian", hash, data + 1U);
    result = porpoise_recovery_annotation_view_open(
        &program, &annotation, "target", "main", &view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "unaligned"));
    reset_diagnostics(&diagnostics);

    set_annotation(&annotation, UINT32_C(0x2000), 4U,
                   PORPOISE_RECOVERY_ANNOTATION_RAW_BYTES, 3U, NULL,
                   hash, data);
    result = porpoise_recovery_annotation_view_open(
        &program, &annotation, "target", "main", &view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "count"));
    reset_diagnostics(&diagnostics);

    set_annotation(&annotation, UINT32_C(0x2000), 4U,
                   PORPOISE_RECOVERY_ANNOTATION_ASCII, 4U, "utf-8",
                   hash, data);
    result = porpoise_recovery_annotation_view_open(
        &program, &annotation, "target", "main", &view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "encoding"));
    reset_diagnostics(&diagnostics);

    set_annotation(&annotation, UINT32_C(0x2005), 1U,
                   PORPOISE_RECOVERY_ANNOTATION_ASCII, 1U, "ascii",
                   hash, data + 5U);
    result = porpoise_recovery_annotation_view_open(
        &program, &annotation, "target", "main", &view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "not valid"));
    reset_diagnostics(&diagnostics);

    set_annotation(&annotation, UINT32_C(0x2000), 4U,
                   PORPOISE_RECOVERY_ANNOTATION_ZERO_FILL, 4U, NULL,
                   hash, data);
    result = porpoise_recovery_annotation_view_open(
        &program, &annotation, "target", "main", &view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_TRANSLATION);
    CHECK(diagnostics_contain(&diagnostics, "nonzero"));
    reset_diagnostics(&diagnostics);

    set_annotation(&annotation, UINT32_C(0x2006), 2U,
                   PORPOISE_RECOVERY_ANNOTATION_UTF8, 1U, "utf-8",
                   hash, data + 6U);
    result = porpoise_recovery_annotation_view_open(
        &program, &annotation, "target", "main", &view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "not valid"));
    reset_diagnostics(&diagnostics);

    set_annotation(&annotation, UINT32_C(0x200d), 1U,
                   PORPOISE_RECOVERY_ANNOTATION_SHIFT_JIS, 1U,
                   "shift-jis", hash, data + 13U);
    result = porpoise_recovery_annotation_view_open(
        &program, &annotation, "target", "main", &view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "not valid"));
    reset_diagnostics(&diagnostics);

    set_annotation(&annotation, UINT32_C(0x2008), 2U,
                   PORPOISE_RECOVERY_ANNOTATION_UTF16, 1U, "utf-16be",
                   hash, data + 8U);
    result = porpoise_recovery_annotation_view_open(
        &program, &annotation, "target", "main", &view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "not valid"));
    reset_diagnostics(&diagnostics);

    set_annotation(&overlap[0], UINT32_C(0x2000), 4U,
                   PORPOISE_RECOVERY_ANNOTATION_RAW_BYTES, 4U, NULL,
                   overlap_hashes[0], data);
    set_annotation(&overlap[1], UINT32_C(0x2002), 4U,
                   PORPOISE_RECOVERY_ANNOTATION_RAW_BYTES, 4U, NULL,
                   overlap_hashes[1], data + 2U);
    result = porpoise_recovery_annotations_validate(
        &program, overlap, 2U, "target", "main", &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "overlapping"));
    reset_diagnostics(&diagnostics);

    porpoise_recovery_byte_view_init(&byte_view);
    result = porpoise_recovery_byte_view_extract(
        &program, UINT32_C(0x200f), 2U, &byte_view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_TRANSLATION);
    CHECK(diagnostics_contain(&diagnostics, "not fully covered"));
    reset_diagnostics(&diagnostics);

    spans[1].kind = PORPOISE_DATA_SPAN_INITIALIZED;
    spans[1].address = UINT32_C(0x2002);
    spans[1].size = 2U;
    spans[1].bytes = data + 2U;
    program.data_span_count = 2U;
    result = porpoise_recovery_byte_view_extract(
        &program, UINT32_C(0x2000), 4U, &byte_view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_TRANSLATION);
    CHECK(diagnostics_contain(&diagnostics, "overlapping Program"));
    program.data_span_count = 1U;
    reset_diagnostics(&diagnostics);

    result = porpoise_recovery_byte_view_extract(
        &program, UINT32_MAX, 2U, &byte_view, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "32-bit"));
    reset_diagnostics(&diagnostics);

    result = porpoise_recovery_annotations_validate(
        &program, NULL, 0U, "target", "main", &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(memcmp(data, original, sizeof(data)) == 0);

    porpoise_recovery_byte_view_free(&byte_view);
    porpoise_recovery_annotation_view_free(&view);
    porpoise_diagnostics_free(&diagnostics);
}

int main(void) {
    test_valid_views_and_interpretations();
    test_invalid_annotations_and_coverage();
    if (failures != 0U) {
        fprintf(stderr, "%u recovery annotation test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
