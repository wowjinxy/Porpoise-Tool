#include "porpoise/sdk_catalog.h"

#include "jsmn.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDK_CATALOG_JSON_INITIAL_CAPACITY 4096U
#define SDK_CATALOG_JSON_MAX_DEPTH 32U

typedef struct SdkCatalogParseContext {
    const char *path;
    const char *json;
    size_t json_length;
    const jsmntok_t *tokens;
    int token_count;
    PorpoiseDiagnostics *diagnostics;
    int result;
} SdkCatalogParseContext;

enum {
    SDK_ROOT_SCHEMA_VERSION = 1U << 0,
    SDK_ROOT_SIGNATURE_VERSION = 1U << 1,
    SDK_ROOT_ENTRIES = 1U << 2
};

enum {
    SDK_ENTRY_IDENTITY = 1U << 0,
    SDK_ENTRY_CATEGORY = 1U << 1,
    SDK_ENTRY_CONTRACT = 1U << 2,
    SDK_ENTRY_SIGNATURE = 1U << 3
};

enum {
    SDK_SIGNATURE_SHA256 = 1U << 0,
    SDK_SIGNATURE_FUNCTION_SIZE = 1U << 1,
    SDK_SIGNATURE_INSTRUCTION_COUNT = 1U << 2,
    SDK_SIGNATURE_FIXED_COUNT = 1U << 3,
    SDK_SIGNATURE_MEANINGFUL_FIXED_WORDS = 1U << 4,
    SDK_SIGNATURE_RELOCATION_COUNT = 1U << 5,
    SDK_SIGNATURE_INTERNAL_BRANCH_COUNT = 1U << 6,
    SDK_SIGNATURE_EXTERNAL_BRANCH_COUNT = 1U << 7,
    SDK_SIGNATURE_EXTERNAL_TARGET_COUNT = 1U << 8,
    SDK_SIGNATURE_ISSUE_FLAGS = 1U << 9
};

static size_t sdk_line_at_offset(
    const SdkCatalogParseContext *context,
    size_t offset) {
    size_t line = 1U;
    size_t index;

    if (offset > context->json_length) offset = context->json_length;
    for (index = 0U; index < offset; index++) {
        if (context->json[index] == '\n') line++;
    }
    return line;
}

static size_t sdk_token_line(
    const SdkCatalogParseContext *context,
    int token_index) {
    if (token_index < 0 || token_index >= context->token_count ||
        context->tokens[token_index].start < 0) {
        return 1U;
    }
    return sdk_line_at_offset(
        context, (size_t)context->tokens[token_index].start);
}

static bool sdk_parse_report(
    SdkCatalogParseContext *context,
    int result,
    int token_index,
    const char *format,
    ...) {
    char message[PORPOISE_MESSAGE_CAPACITY];
    va_list arguments;
    int written;

    if (context->result == PORPOISE_EXIT_INTERNAL) return false;
    va_start(arguments, format);
    written = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (written < 0) {
        context->result = PORPOISE_EXIT_INTERNAL;
        return false;
    }
    context->result = result;
    if (context->diagnostics != NULL &&
        !porpoise_diagnostics_add(
            context->diagnostics,
            PORPOISE_SEVERITY_ERROR,
            context->path == NULL ? "" : context->path,
            sdk_token_line(context, token_index),
            0U,
            "%s",
            message)) {
        context->result = PORPOISE_EXIT_INTERNAL;
    }
    return false;
}

static bool sdk_parse_report_offset(
    SdkCatalogParseContext *context,
    size_t offset,
    const char *message) {
    context->result = PORPOISE_EXIT_USAGE;
    if (context->diagnostics != NULL &&
        !porpoise_diagnostics_add(
            context->diagnostics,
            PORPOISE_SEVERITY_ERROR,
            context->path == NULL ? "" : context->path,
            sdk_line_at_offset(context, offset),
            0U,
            "%s",
            message)) {
        context->result = PORPOISE_EXIT_INTERNAL;
    }
    return false;
}

static int sdk_add_diagnostic(
    PorpoiseDiagnostics *diagnostics,
    int result,
    const char *path,
    size_t line,
    const char *format,
    ...) {
    char message[PORPOISE_MESSAGE_CAPACITY];
    va_list arguments;
    int written;

    if (diagnostics == NULL) return result;
    va_start(arguments, format);
    written = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (written < 0 ||
        !porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            path == NULL ? "" : path,
            line,
            0U,
            "%s",
            written < 0 ? "failed to format SDK catalog diagnostic" : message)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    return result;
}

static void sdk_skip_whitespace(
    const char *json,
    size_t length,
    size_t *position) {
    while (*position < length) {
        char value = json[*position];
        if (value != ' ' && value != '\t' &&
            value != '\r' && value != '\n') {
            break;
        }
        (*position)++;
    }
}

static bool sdk_json_number_is_valid(const char *text, size_t length) {
    size_t position = 0U;

    if (length == 0U) return false;
    if (text[position] == '-') {
        position++;
        if (position == length) return false;
    }
    if (text[position] == '0') {
        position++;
        if (position < length && text[position] >= '0' && text[position] <= '9')
            return false;
    } else {
        if (text[position] < '1' || text[position] > '9') return false;
        do {
            position++;
        } while (position < length &&
                 text[position] >= '0' && text[position] <= '9');
    }
    if (position < length && text[position] == '.') {
        position++;
        if (position == length || text[position] < '0' || text[position] > '9')
            return false;
        do {
            position++;
        } while (position < length &&
                 text[position] >= '0' && text[position] <= '9');
    }
    if (position < length &&
        (text[position] == 'e' || text[position] == 'E')) {
        position++;
        if (position < length &&
            (text[position] == '+' || text[position] == '-')) {
            position++;
        }
        if (position == length || text[position] < '0' || text[position] > '9')
            return false;
        do {
            position++;
        } while (position < length &&
                 text[position] >= '0' && text[position] <= '9');
    }
    return position == length;
}

static bool sdk_json_primitive_is_valid(const char *text, size_t length) {
    if ((length == 4U && memcmp(text, "true", 4U) == 0) ||
        (length == 5U && memcmp(text, "false", 5U) == 0) ||
        (length == 4U && memcmp(text, "null", 4U) == 0)) {
        return true;
    }
    return sdk_json_number_is_valid(text, length);
}

static bool sdk_validate_json_value(
    SdkCatalogParseContext *context,
    int token_index,
    int expected_parent,
    unsigned int depth,
    size_t *position,
    int *next_token) {
    const jsmntok_t *token;
    int child_index;
    int child;

    if (depth > SDK_CATALOG_JSON_MAX_DEPTH) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "SDK catalog JSON is nested too deeply");
    }
    if (token_index < 0 || token_index >= context->token_count) {
        return sdk_parse_report_offset(
            context, *position, "SDK catalog JSON is missing a value");
    }
    token = &context->tokens[token_index];
    if (token->parent != expected_parent || token->start < 0 ||
        token->end < token->start) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "SDK catalog has an invalid JSON token relationship");
    }

    sdk_skip_whitespace(context->json, context->json_length, position);
    if (token->type == JSMN_STRING) {
        if (token->size != 0 || *position >= context->json_length ||
            context->json[*position] != '"' ||
            token->start != (int)(*position + 1U) ||
            (size_t)token->end >= context->json_length ||
            context->json[token->end] != '"') {
            return sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "SDK catalog is not strict JSON near a string");
        }
        *position = (size_t)token->end + 1U;
        *next_token = token_index + 1;
        return true;
    }
    if (token->type == JSMN_PRIMITIVE) {
        size_t primitive_length;
        if (token->size != 0 || token->start != (int)*position ||
            token->end <= token->start) {
            return sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "SDK catalog is not strict JSON near a primitive");
        }
        primitive_length = (size_t)(token->end - token->start);
        if (!sdk_json_primitive_is_valid(
                context->json + token->start, primitive_length)) {
            return sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "SDK catalog contains an invalid JSON primitive");
        }
        *position = (size_t)token->end;
        *next_token = token_index + 1;
        return true;
    }
    if (token->type == JSMN_ARRAY) {
        if (token->start != (int)*position ||
            *position >= context->json_length ||
            context->json[*position] != '[') {
            return sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "SDK catalog is not strict JSON near an array");
        }
        (*position)++;
        child_index = token_index + 1;
        for (child = 0; child < token->size; child++) {
            sdk_skip_whitespace(context->json, context->json_length, position);
            if (!sdk_validate_json_value(
                    context, child_index, token_index, depth + 1U,
                    position, &child_index)) {
                return false;
            }
            sdk_skip_whitespace(context->json, context->json_length, position);
            if (child + 1 < token->size) {
                if (*position >= context->json_length ||
                    context->json[*position] != ',') {
                    return sdk_parse_report_offset(
                        context, *position,
                        "SDK catalog array elements must be comma-separated");
                }
                (*position)++;
            }
        }
        sdk_skip_whitespace(context->json, context->json_length, position);
        if (*position >= context->json_length ||
            context->json[*position] != ']') {
            return sdk_parse_report_offset(
                context, *position,
                "SDK catalog has an unterminated or trailing-comma array");
        }
        (*position)++;
        if (token->end != (int)*position) {
            return sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "SDK catalog has an invalid array boundary");
        }
        *next_token = child_index;
        return true;
    }
    if (token->type == JSMN_OBJECT) {
        if (token->start != (int)*position ||
            *position >= context->json_length ||
            context->json[*position] != '{') {
            return sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "SDK catalog is not strict JSON near an object");
        }
        (*position)++;
        child_index = token_index + 1;
        for (child = 0; child < token->size; child++) {
            const jsmntok_t *key;
            int key_index = child_index;

            sdk_skip_whitespace(context->json, context->json_length, position);
            if (key_index < 0 || key_index >= context->token_count) {
                return sdk_parse_report_offset(
                    context, *position,
                    "SDK catalog object is missing a key");
            }
            key = &context->tokens[key_index];
            if (key->type != JSMN_STRING || key->parent != token_index ||
                key->size != 1 || *position >= context->json_length ||
                context->json[*position] != '"' ||
                key->start != (int)(*position + 1U) ||
                (size_t)key->end >= context->json_length ||
                context->json[key->end] != '"') {
                return sdk_parse_report(
                    context, PORPOISE_EXIT_USAGE, key_index,
                    "SDK catalog object keys must be strict JSON strings");
            }
            *position = (size_t)key->end + 1U;
            sdk_skip_whitespace(context->json, context->json_length, position);
            if (*position >= context->json_length ||
                context->json[*position] != ':') {
                return sdk_parse_report_offset(
                    context, *position,
                    "SDK catalog object key is missing ':'");
            }
            (*position)++;
            if (!sdk_validate_json_value(
                    context, key_index + 1, key_index, depth + 1U,
                    position, &child_index)) {
                return false;
            }
            sdk_skip_whitespace(context->json, context->json_length, position);
            if (child + 1 < token->size) {
                if (*position >= context->json_length ||
                    context->json[*position] != ',') {
                    return sdk_parse_report_offset(
                        context, *position,
                        "SDK catalog object members must be comma-separated");
                }
                (*position)++;
            }
        }
        sdk_skip_whitespace(context->json, context->json_length, position);
        if (*position >= context->json_length ||
            context->json[*position] != '}') {
            return sdk_parse_report_offset(
                context, *position,
                "SDK catalog has an unterminated or trailing-comma object");
        }
        (*position)++;
        if (token->end != (int)*position) {
            return sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "SDK catalog has an invalid object boundary");
        }
        *next_token = child_index;
        return true;
    }
    return sdk_parse_report(
        context, PORPOISE_EXIT_USAGE, token_index,
        "SDK catalog contains an undefined JSON token");
}

static bool sdk_validate_json_document(SdkCatalogParseContext *context) {
    size_t position = 0U;
    int next_token = 0;

    if (context->token_count < 1) {
        return sdk_parse_report_offset(
            context, 0U, "SDK catalog JSON is empty");
    }
    if (!sdk_validate_json_value(
            context, 0, -1, 0U, &position, &next_token)) {
        return false;
    }
    sdk_skip_whitespace(context->json, context->json_length, &position);
    if (position != context->json_length ||
        next_token != context->token_count) {
        return sdk_parse_report_offset(
            context, position,
            "SDK catalog must contain exactly one JSON value");
    }
    return true;
}

static int sdk_hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool sdk_append_utf8(
    char *output,
    size_t capacity,
    size_t *length,
    uint32_t codepoint) {
    unsigned int count;

    if (codepoint == 0U || codepoint > UINT32_C(0x10ffff) ||
        (codepoint >= UINT32_C(0xd800) &&
         codepoint <= UINT32_C(0xdfff))) {
        return false;
    }
    if (codepoint <= UINT32_C(0x7f)) count = 1U;
    else if (codepoint <= UINT32_C(0x7ff)) count = 2U;
    else if (codepoint <= UINT32_C(0xffff)) count = 3U;
    else count = 4U;
    if (*length >= capacity || count > capacity - 1U - *length) return false;

    if (count == 1U) {
        output[(*length)++] = (char)codepoint;
    } else if (count == 2U) {
        output[(*length)++] = (char)(UINT32_C(0xc0) | (codepoint >> 6U));
        output[(*length)++] =
            (char)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
    } else if (count == 3U) {
        output[(*length)++] = (char)(UINT32_C(0xe0) | (codepoint >> 12U));
        output[(*length)++] = (char)(
            UINT32_C(0x80) | ((codepoint >> 6U) & UINT32_C(0x3f)));
        output[(*length)++] =
            (char)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
    } else {
        output[(*length)++] = (char)(UINT32_C(0xf0) | (codepoint >> 18U));
        output[(*length)++] = (char)(
            UINT32_C(0x80) | ((codepoint >> 12U) & UINT32_C(0x3f)));
        output[(*length)++] = (char)(
            UINT32_C(0x80) | ((codepoint >> 6U) & UINT32_C(0x3f)));
        output[(*length)++] =
            (char)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
    }
    return true;
}

static bool sdk_valid_utf8(const char *text, size_t length) {
    size_t position = 0U;

    while (position < length) {
        unsigned char first = (unsigned char)text[position++];
        uint32_t codepoint;
        uint32_t minimum;
        unsigned int remaining;

        if (first <= 0x7fU) {
            if (first == 0U) return false;
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            codepoint = (uint32_t)(first & 0x1fU);
            minimum = UINT32_C(0x80);
            remaining = 1U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            codepoint = (uint32_t)(first & 0x0fU);
            minimum = UINT32_C(0x800);
            remaining = 2U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            codepoint = (uint32_t)(first & 0x07U);
            minimum = UINT32_C(0x10000);
            remaining = 3U;
        } else {
            return false;
        }
        if (remaining > length - position) return false;
        while (remaining > 0U) {
            unsigned char next = (unsigned char)text[position++];
            if ((next & 0xc0U) != 0x80U) return false;
            codepoint = (codepoint << 6U) | (uint32_t)(next & 0x3fU);
            remaining--;
        }
        if (codepoint < minimum || codepoint > UINT32_C(0x10ffff) ||
            (codepoint >= UINT32_C(0xd800) &&
             codepoint <= UINT32_C(0xdfff))) {
            return false;
        }
    }
    return true;
}

static bool sdk_decode_json_string(
    const char *json,
    const jsmntok_t *token,
    char *output,
    size_t capacity) {
    size_t input_position;
    size_t input_end;
    size_t output_length = 0U;

    if (token->type != JSMN_STRING || token->start < 0 ||
        token->end < token->start || capacity == 0U) {
        return false;
    }
    input_position = (size_t)token->start;
    input_end = (size_t)token->end;
    while (input_position < input_end) {
        unsigned char value = (unsigned char)json[input_position++];
        if (value != '\\') {
            if (value == 0U || output_length + 1U >= capacity) return false;
            output[output_length++] = (char)value;
            continue;
        }
        if (input_position >= input_end) return false;
        value = (unsigned char)json[input_position++];
        if (value == '"' || value == '\\' || value == '/') {
            if (output_length + 1U >= capacity) return false;
            output[output_length++] = (char)value;
        } else if (value == 'b' || value == 'f' || value == 'n' ||
                   value == 'r' || value == 't') {
            char decoded = value == 'b' ? '\b' :
                           value == 'f' ? '\f' :
                           value == 'n' ? '\n' :
                           value == 'r' ? '\r' : '\t';
            if (output_length + 1U >= capacity) return false;
            output[output_length++] = decoded;
        } else if (value == 'u') {
            uint32_t codepoint = 0U;
            unsigned int digit;

            if (input_position + 4U > input_end) return false;
            for (digit = 0U; digit < 4U; digit++) {
                int hex = sdk_hex_value(json[input_position++]);
                if (hex < 0) return false;
                codepoint = (codepoint << 4U) | (uint32_t)hex;
            }
            if (codepoint >= UINT32_C(0xd800) &&
                codepoint <= UINT32_C(0xdbff)) {
                uint32_t low = 0U;
                if (input_position + 6U > input_end ||
                    json[input_position] != '\\' ||
                    json[input_position + 1U] != 'u') {
                    return false;
                }
                input_position += 2U;
                for (digit = 0U; digit < 4U; digit++) {
                    int hex = sdk_hex_value(json[input_position++]);
                    if (hex < 0) return false;
                    low = (low << 4U) | (uint32_t)hex;
                }
                if (low < UINT32_C(0xdc00) || low > UINT32_C(0xdfff))
                    return false;
                codepoint = UINT32_C(0x10000) +
                    ((codepoint - UINT32_C(0xd800)) << 10U) +
                    (low - UINT32_C(0xdc00));
            }
            if (!sdk_append_utf8(
                    output, capacity, &output_length, codepoint)) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (!sdk_valid_utf8(output, output_length)) return false;
    output[output_length] = '\0';
    return true;
}

static bool sdk_decode_owned_string(
    SdkCatalogParseContext *context,
    int token_index,
    const char *description,
    bool nonempty,
    char **output) {
    const jsmntok_t *token;
    size_t length;
    char *decoded;

    if (token_index < 0 || token_index >= context->token_count) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s is missing", description);
    }
    token = &context->tokens[token_index];
    if (token->type != JSMN_STRING) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s must be a JSON string", description);
    }
    length = (size_t)(token->end - token->start);
    if (length == SIZE_MAX) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_INTERNAL, token_index,
            "out of memory while decoding %s", description);
    }
    decoded = (char *)malloc(length + 1U);
    if (decoded == NULL) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_INTERNAL, token_index,
            "out of memory while decoding %s", description);
    }
    if (!sdk_decode_json_string(
            context->json, token, decoded, length + 1U)) {
        free(decoded);
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s contains invalid Unicode or an embedded null", description);
    }
    if (nonempty && decoded[0] == '\0') {
        free(decoded);
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s must not be empty", description);
    }
    *output = decoded;
    return true;
}

static int sdk_token_after(
    const SdkCatalogParseContext *context,
    int token_index) {
    const jsmntok_t *token;
    int next;
    int child;

    if (token_index < 0 || token_index >= context->token_count) return -1;
    token = &context->tokens[token_index];
    next = token_index + 1;
    for (child = 0; child < token->size; child++) {
        next = sdk_token_after(context, next);
        if (next < 0) return -1;
    }
    return next;
}

static bool sdk_parse_uint32(
    SdkCatalogParseContext *context,
    int token_index,
    const char *description,
    uint32_t *value_out) {
    const jsmntok_t *token;
    size_t length;
    size_t index;
    uint32_t value = 0U;

    if (token_index < 0 || token_index >= context->token_count) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s is missing", description);
    }
    token = &context->tokens[token_index];
    if (token->type != JSMN_PRIMITIVE || token->end <= token->start) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s must be an unsigned 32-bit integer", description);
    }
    length = (size_t)(token->end - token->start);
    if (length > 1U && context->json[token->start] == '0') {
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s must be an unsigned 32-bit integer", description);
    }
    for (index = 0U; index < length; index++) {
        unsigned int digit;
        char character = context->json[token->start + (int)index];
        if (character < '0' || character > '9') {
            return sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "%s must be an unsigned 32-bit integer", description);
        }
        digit = (unsigned int)(character - '0');
        if (value > (UINT32_MAX - digit) / 10U) {
            return sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "%s exceeds the unsigned 32-bit range", description);
        }
        value = value * 10U + (uint32_t)digit;
    }
    *value_out = value;
    return true;
}

static bool sdk_key(
    SdkCatalogParseContext *context,
    int key_index,
    char **key_out) {
    return sdk_decode_owned_string(
        context, key_index, "SDK catalog object key", false, key_out);
}

static bool sdk_signature_exact_equal(
    const PorpoiseFunctionSignature *left,
    const PorpoiseFunctionSignature *right) {
    return porpoise_signature_equal(left, right);
}

typedef struct SdkCatalogIdentityIndexSlot {
    uint64_t hash;
    size_t entry_plus_one;
} SdkCatalogIdentityIndexSlot;

typedef struct SdkCatalogSignatureIndexSlot {
    uint64_t hash;
    size_t representative_plus_one;
    size_t match_count;
} SdkCatalogSignatureIndexSlot;

struct PorpoiseSdkCatalogLookupIndex {
    size_t capacity;
    SdkCatalogIdentityIndexSlot *identities;
    SdkCatalogSignatureIndexSlot *signatures;
};

static uint64_t sdk_index_hash_bytes(
    uint64_t hash,
    const void *data,
    size_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    size_t index;
    for (index = 0U; index < size; index++) {
        hash ^= (uint64_t)bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t sdk_index_hash_u32(uint64_t hash, uint32_t value) {
    unsigned char bytes[4];
    bytes[0] = (unsigned char)(value >> 24U);
    bytes[1] = (unsigned char)(value >> 16U);
    bytes[2] = (unsigned char)(value >> 8U);
    bytes[3] = (unsigned char)value;
    return sdk_index_hash_bytes(hash, bytes, sizeof(bytes));
}

static uint64_t sdk_index_identity_hash(const char *identity) {
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = sdk_index_hash_bytes(hash, identity, strlen(identity));
    return hash == 0U ? UINT64_C(1) : hash;
}

static uint64_t sdk_index_signature_hash(
    const PorpoiseFunctionSignature *signature) {
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = sdk_index_hash_u32(hash, signature->algorithm_version);
    hash = sdk_index_hash_u32(hash, signature->function_size);
    hash = sdk_index_hash_u32(hash, signature->instruction_count);
    hash = sdk_index_hash_u32(hash, signature->fixed_instruction_count);
    hash = sdk_index_hash_u32(
        hash, signature->meaningful_fixed_instruction_count);
    hash = sdk_index_hash_u32(hash, signature->relocation_count);
    hash = sdk_index_hash_u32(hash, signature->internal_branch_count);
    hash = sdk_index_hash_u32(hash, signature->external_branch_count);
    hash = sdk_index_hash_u32(hash, signature->external_target_count);
    hash = sdk_index_hash_u32(hash, signature->issue_flags);
    hash = sdk_index_hash_bytes(
        hash, signature->digest, PORPOISE_SHA256_DIGEST_SIZE);
    return hash == 0U ? UINT64_C(1) : hash;
}

static void sdk_catalog_index_free(PorpoiseSdkCatalogLookupIndex *index) {
    if (index == NULL) return;
    free(index->identities);
    free(index->signatures);
    free(index);
}

static bool sdk_catalog_index_capacity(
    size_t entry_count,
    size_t *capacity_out) {
    size_t required;
    size_t capacity = 16U;
    if (entry_count > SIZE_MAX / 2U) return false;
    required = entry_count * 2U;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) return false;
        capacity *= 2U;
    }
    *capacity_out = capacity;
    return true;
}

static bool sdk_catalog_rebuild_index(PorpoiseSdkCatalog *catalog) {
    PorpoiseSdkCatalogLookupIndex *replacement;
    size_t capacity;
    size_t entry_index;
    if (catalog->entry_count == 0U) {
        sdk_catalog_index_free(catalog->lookup_index);
        catalog->lookup_index = NULL;
        return true;
    }
    if (!sdk_catalog_index_capacity(catalog->entry_count, &capacity)) {
        return false;
    }
    replacement = (PorpoiseSdkCatalogLookupIndex *)calloc(
        1U, sizeof(*replacement));
    if (replacement == NULL) return false;
    replacement->capacity = capacity;
    replacement->identities = (SdkCatalogIdentityIndexSlot *)calloc(
        capacity, sizeof(*replacement->identities));
    replacement->signatures = (SdkCatalogSignatureIndexSlot *)calloc(
        capacity, sizeof(*replacement->signatures));
    if (replacement->identities == NULL ||
        replacement->signatures == NULL) {
        sdk_catalog_index_free(replacement);
        return false;
    }
    for (entry_index = 0U;
         entry_index < catalog->entry_count;
         entry_index++) {
        const PorpoiseSdkCatalogEntry *entry =
            &catalog->entries[entry_index];
        uint64_t identity_hash = sdk_index_identity_hash(
            entry->canonical_identity);
        uint64_t signature_hash = sdk_index_signature_hash(
            &entry->signature);
        size_t slot = (size_t)identity_hash & (capacity - 1U);
        while (replacement->identities[slot].entry_plus_one != 0U) {
            slot = (slot + 1U) & (capacity - 1U);
        }
        replacement->identities[slot].hash = identity_hash;
        replacement->identities[slot].entry_plus_one = entry_index + 1U;

        slot = (size_t)signature_hash & (capacity - 1U);
        while (replacement->signatures[slot].representative_plus_one != 0U) {
            size_t representative =
                replacement->signatures[slot].representative_plus_one - 1U;
            if (replacement->signatures[slot].hash == signature_hash &&
                sdk_signature_exact_equal(
                    &catalog->entries[representative].signature,
                    &entry->signature)) {
                replacement->signatures[slot].match_count++;
                break;
            }
            slot = (slot + 1U) & (capacity - 1U);
        }
        if (replacement->signatures[slot].representative_plus_one == 0U) {
            replacement->signatures[slot].hash = signature_hash;
            replacement->signatures[slot].representative_plus_one =
                entry_index + 1U;
            replacement->signatures[slot].match_count = 1U;
        }
    }
    sdk_catalog_index_free(catalog->lookup_index);
    catalog->lookup_index = replacement;
    return true;
}

static bool sdk_catalog_lookup_indexed_signature(
    const PorpoiseSdkCatalog *catalog,
    const PorpoiseFunctionSignature *signature,
    PorpoiseSdkCatalogMatch *match_out) {
    const PorpoiseSdkCatalogLookupIndex *lookup_index;
    uint64_t hash;
    size_t slot;
    memset(match_out, 0, sizeof(*match_out));
    match_out->status = PORPOISE_SDK_CATALOG_MATCH_NONE;
    lookup_index = catalog == NULL ? NULL : catalog->lookup_index;
    if (lookup_index == NULL || lookup_index->capacity == 0U) return false;
    hash = sdk_index_signature_hash(signature);
    slot = (size_t)hash & (lookup_index->capacity - 1U);
    while (lookup_index->signatures[slot].representative_plus_one != 0U) {
        const SdkCatalogSignatureIndexSlot *candidate =
            &lookup_index->signatures[slot];
        size_t representative = candidate->representative_plus_one - 1U;
        if (candidate->hash == hash &&
            sdk_signature_exact_equal(
                &catalog->entries[representative].signature, signature)) {
            match_out->match_count = candidate->match_count;
            if (candidate->match_count == 1U) {
                match_out->status = PORPOISE_SDK_CATALOG_MATCH_UNIQUE;
                match_out->entry = &catalog->entries[representative];
            } else {
                match_out->status = PORPOISE_SDK_CATALOG_MATCH_AMBIGUOUS;
            }
            return true;
        }
        slot = (slot + 1U) & (lookup_index->capacity - 1U);
    }
    return true;
}

static bool sdk_optional_string_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL) return left == right;
    return strcmp(left, right) == 0;
}

static bool sdk_entry_semantic_equal(
    const PorpoiseSdkCatalogEntry *left,
    const PorpoiseSdkCatalogEntry *right) {
    return strcmp(left->canonical_identity, right->canonical_identity) == 0 &&
           left->category == right->category &&
           sdk_optional_string_equal(left->contract_name, right->contract_name) &&
           sdk_signature_exact_equal(&left->signature, &right->signature);
}

static bool sdk_category_valid(PorpoiseSdkCategory category) {
    return category >= PORPOISE_SDK_CATEGORY_NINTENDO_DOLPHIN &&
           category <= PORPOISE_SDK_CATEGORY_STUB;
}

static int sdk_validate_entry(
    const PorpoiseSdkCatalogEntry *entry,
    PorpoiseDiagnostics *diagnostics) {
    const char *path = entry == NULL ? "" : entry->provenance.path;
    size_t line = entry == NULL ? 0U : entry->provenance.line;

    if (entry == NULL || entry->canonical_identity == NULL ||
        entry->canonical_identity[0] == '\0' ||
        !sdk_category_valid(entry->category) ||
        (entry->contract_name != NULL && entry->contract_name[0] == '\0')) {
        return sdk_add_diagnostic(
            diagnostics, PORPOISE_EXIT_USAGE, path, line,
            "SDK catalog entry has invalid identity, category, or contract");
    }
    if (entry->signature.algorithm_version !=
            PORPOISE_SIGNATURE_ALGORITHM_VERSION ||
        entry->signature.meaningful_fixed_instruction_count >
            entry->signature.fixed_instruction_count ||
        entry->signature.fixed_instruction_count >
            entry->signature.instruction_count ||
        entry->signature.relocation_count >
            entry->signature.instruction_count ||
        entry->signature.internal_branch_count >
            entry->signature.instruction_count ||
        entry->signature.external_branch_count >
            entry->signature.instruction_count ||
        (uint64_t)entry->signature.external_target_count >
            (uint64_t)entry->signature.relocation_count +
            (uint64_t)entry->signature.external_branch_count) {
        return sdk_add_diagnostic(
            diagnostics, PORPOISE_EXIT_USAGE, path, line,
            "SDK catalog entry '%s' has inconsistent signature metadata",
            entry->canonical_identity);
    }
    return PORPOISE_EXIT_OK;
}

static char *sdk_duplicate_optional(const char *text) {
    size_t length;
    char *copy;
    if (text == NULL) return NULL;
    length = strlen(text);
    if (length == SIZE_MAX) return NULL;
    copy = (char *)malloc(length + 1U);
    if (copy == NULL) return NULL;
    memcpy(copy, text, length + 1U);
    return copy;
}

static void sdk_entry_free(PorpoiseSdkCatalogEntry *entry) {
    if (entry == NULL) return;
    free(entry->canonical_identity);
    free(entry->contract_name);
    free(entry->provenance.path);
    memset(entry, 0, sizeof(*entry));
}

void porpoise_sdk_catalog_init(PorpoiseSdkCatalog *catalog) {
    if (catalog != NULL) memset(catalog, 0, sizeof(*catalog));
}

void porpoise_sdk_catalog_free(PorpoiseSdkCatalog *catalog) {
    size_t index;
    if (catalog == NULL) return;
    for (index = 0U; index < catalog->entry_count; index++)
        sdk_entry_free(&catalog->entries[index]);
    free(catalog->entries);
    sdk_catalog_index_free(catalog->lookup_index);
    memset(catalog, 0, sizeof(*catalog));
}

int porpoise_sdk_catalog_add(
    PorpoiseSdkCatalog *catalog,
    const PorpoiseSdkCatalogEntry *entry,
    PorpoiseDiagnostics *diagnostics) {
    size_t index;
    PorpoiseSdkCatalogEntry copy;
    size_t new_capacity;
    PorpoiseSdkCatalogEntry *resized;
    int validation;

    if (catalog == NULL || entry == NULL) {
        return sdk_add_diagnostic(
            diagnostics, PORPOISE_EXIT_INTERNAL, "", 0U,
            "SDK catalog add arguments are invalid");
    }
    validation = sdk_validate_entry(entry, diagnostics);
    if (validation != PORPOISE_EXIT_OK) return validation;

    for (index = 0U; index < catalog->entry_count; index++) {
        const PorpoiseSdkCatalogEntry *existing = &catalog->entries[index];
        if (strcmp(existing->canonical_identity,
                   entry->canonical_identity) != 0) {
            continue;
        }
        if (sdk_entry_semantic_equal(existing, entry))
            return PORPOISE_EXIT_OK;
        return sdk_add_diagnostic(
            diagnostics, PORPOISE_EXIT_USAGE,
            entry->provenance.path, entry->provenance.line,
            "SDK identity '%s' conflicts with its existing definition at %s:%lu",
            entry->canonical_identity,
            existing->provenance.path == NULL ?
                porpoise_sdk_catalog_source_kind_name(
                    existing->provenance.source_kind) :
                existing->provenance.path,
            (unsigned long)existing->provenance.line);
    }

    if (catalog->entry_count == catalog->entry_capacity) {
        if (catalog->entry_capacity == 0U) new_capacity = 16U;
        else if (catalog->entry_capacity > SIZE_MAX / 2U)
            new_capacity = SIZE_MAX;
        else new_capacity = catalog->entry_capacity * 2U;
        if (new_capacity < catalog->entry_count + 1U ||
            new_capacity > SIZE_MAX / sizeof(*catalog->entries)) {
            return sdk_add_diagnostic(
                diagnostics, PORPOISE_EXIT_INTERNAL,
                entry->provenance.path, entry->provenance.line,
                "out of memory while growing SDK catalog");
        }
        resized = (PorpoiseSdkCatalogEntry *)realloc(
            catalog->entries, new_capacity * sizeof(*catalog->entries));
        if (resized == NULL) {
            return sdk_add_diagnostic(
                diagnostics, PORPOISE_EXIT_INTERNAL,
                entry->provenance.path, entry->provenance.line,
                "out of memory while growing SDK catalog");
        }
        catalog->entries = resized;
        catalog->entry_capacity = new_capacity;
    }

    memset(&copy, 0, sizeof(copy));
    copy.canonical_identity = sdk_duplicate_optional(entry->canonical_identity);
    copy.contract_name = sdk_duplicate_optional(entry->contract_name);
    copy.provenance.path = sdk_duplicate_optional(entry->provenance.path);
    if (copy.canonical_identity == NULL ||
        (entry->contract_name != NULL && copy.contract_name == NULL) ||
        (entry->provenance.path != NULL && copy.provenance.path == NULL)) {
        sdk_entry_free(&copy);
        return sdk_add_diagnostic(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            entry->provenance.path, entry->provenance.line,
            "out of memory while copying SDK catalog entry");
    }
    copy.category = entry->category;
    copy.signature = entry->signature;
    porpoise_sha256_hex(copy.signature.digest, copy.signature.digest_hex);
    copy.provenance.source_kind = entry->provenance.source_kind;
    copy.provenance.line = entry->provenance.line;
    catalog->entries[catalog->entry_count++] = copy;
    if (!sdk_catalog_rebuild_index(catalog)) {
        catalog->entry_count--;
        sdk_entry_free(&catalog->entries[catalog->entry_count]);
        return sdk_add_diagnostic(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            entry->provenance.path, entry->provenance.line,
            "out of memory while indexing SDK catalog entry");
    }
    return PORPOISE_EXIT_OK;
}

int porpoise_sdk_catalog_merge(
    PorpoiseSdkCatalog *destination,
    const PorpoiseSdkCatalog *source,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSdkCatalog merged;
    size_t index;
    int result;

    if (destination == NULL || source == NULL) {
        return sdk_add_diagnostic(
            diagnostics, PORPOISE_EXIT_INTERNAL, "", 0U,
            "SDK catalog merge arguments are invalid");
    }
    porpoise_sdk_catalog_init(&merged);
    for (index = 0U; index < destination->entry_count; index++) {
        result = porpoise_sdk_catalog_add(
            &merged, &destination->entries[index], diagnostics);
        if (result != PORPOISE_EXIT_OK) {
            porpoise_sdk_catalog_free(&merged);
            return result;
        }
    }
    for (index = 0U; index < source->entry_count; index++) {
        result = porpoise_sdk_catalog_add(
            &merged, &source->entries[index], diagnostics);
        if (result != PORPOISE_EXIT_OK) {
            porpoise_sdk_catalog_free(&merged);
            return result;
        }
    }
    porpoise_sdk_catalog_free(destination);
    *destination = merged;
    return PORPOISE_EXIT_OK;
}

static bool sdk_parse_sha256(
    SdkCatalogParseContext *context,
    int token_index,
    PorpoiseFunctionSignature *signature) {
    char *hex = NULL;
    size_t index;

    if (!sdk_decode_owned_string(
            context, token_index, "signature.sha256", true, &hex)) {
        return false;
    }
    if (strlen(hex) != PORPOISE_SHA256_DIGEST_SIZE * 2U) {
        free(hex);
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "signature.sha256 must contain exactly 64 hexadecimal digits");
    }
    for (index = 0U; index < PORPOISE_SHA256_DIGEST_SIZE; index++) {
        int high = sdk_hex_value(hex[index * 2U]);
        int low = sdk_hex_value(hex[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            free(hex);
            return sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "signature.sha256 must contain exactly 64 hexadecimal digits");
        }
        signature->digest[index] = (uint8_t)((high << 4) | low);
    }
    porpoise_sha256_hex(signature->digest, signature->digest_hex);
    free(hex);
    return true;
}

static bool sdk_parse_signature(
    SdkCatalogParseContext *context,
    int object_index,
    PorpoiseFunctionSignature *signature) {
    const jsmntok_t *object;
    int member_index;
    int member;
    unsigned int seen = 0U;

    if (object_index < 0 || object_index >= context->token_count) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "SDK catalog entry signature is missing");
    }
    object = &context->tokens[object_index];
    if (object->type != JSMN_OBJECT) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "SDK catalog entry signature must be an object");
    }
    memset(signature, 0, sizeof(*signature));
    signature->algorithm_version = PORPOISE_SIGNATURE_ALGORITHM_VERSION;
    member_index = object_index + 1;
    for (member = 0; member < object->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int bit = 0U;
        uint32_t *field = NULL;

        if (!sdk_key(context, key_index, &key)) return false;
        if (strcmp(key, "sha256") == 0) bit = SDK_SIGNATURE_SHA256;
        else if (strcmp(key, "function_size") == 0) {
            bit = SDK_SIGNATURE_FUNCTION_SIZE;
            field = &signature->function_size;
        } else if (strcmp(key, "instruction_count") == 0) {
            bit = SDK_SIGNATURE_INSTRUCTION_COUNT;
            field = &signature->instruction_count;
        } else if (strcmp(key, "fixed_instruction_count") == 0) {
            bit = SDK_SIGNATURE_FIXED_COUNT;
            field = &signature->fixed_instruction_count;
        } else if (strcmp(key, "meaningful_fixed_words") == 0) {
            bit = SDK_SIGNATURE_MEANINGFUL_FIXED_WORDS;
            field = &signature->meaningful_fixed_instruction_count;
        } else if (strcmp(key, "relocation_count") == 0) {
            bit = SDK_SIGNATURE_RELOCATION_COUNT;
            field = &signature->relocation_count;
        } else if (strcmp(key, "internal_branch_count") == 0) {
            bit = SDK_SIGNATURE_INTERNAL_BRANCH_COUNT;
            field = &signature->internal_branch_count;
        } else if (strcmp(key, "external_branch_count") == 0) {
            bit = SDK_SIGNATURE_EXTERNAL_BRANCH_COUNT;
            field = &signature->external_branch_count;
        } else if (strcmp(key, "external_target_count") == 0) {
            bit = SDK_SIGNATURE_EXTERNAL_TARGET_COUNT;
            field = &signature->external_target_count;
        } else if (strcmp(key, "issue_flags") == 0) {
            bit = SDK_SIGNATURE_ISSUE_FLAGS;
            field = &signature->issue_flags;
        } else {
            bool result = sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "unknown SDK signature key '%s'", key);
            free(key);
            return result;
        }
        if ((seen & bit) != 0U) {
            bool result = sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "duplicate SDK signature key '%s'", key);
            free(key);
            return result;
        }
        seen |= bit;
        free(key);
        if (bit == SDK_SIGNATURE_SHA256) {
            if (!sdk_parse_sha256(context, value_index, signature)) return false;
        } else if (!sdk_parse_uint32(
                       context, value_index, "SDK signature field", field)) {
            return false;
        }
        member_index = sdk_token_after(context, value_index);
        if (member_index < 0) {
            return sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "SDK signature object has invalid token structure");
        }
    }
    if (seen != (SDK_SIGNATURE_SHA256 |
                 SDK_SIGNATURE_FUNCTION_SIZE |
                 SDK_SIGNATURE_INSTRUCTION_COUNT |
                 SDK_SIGNATURE_FIXED_COUNT |
                 SDK_SIGNATURE_MEANINGFUL_FIXED_WORDS |
                 SDK_SIGNATURE_RELOCATION_COUNT |
                 SDK_SIGNATURE_INTERNAL_BRANCH_COUNT |
                 SDK_SIGNATURE_EXTERNAL_BRANCH_COUNT |
                 SDK_SIGNATURE_EXTERNAL_TARGET_COUNT |
                 SDK_SIGNATURE_ISSUE_FLAGS)) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "SDK signature object is missing required fields");
    }
    return true;
}

static bool sdk_parse_category(
    SdkCatalogParseContext *context,
    int token_index,
    PorpoiseSdkCategory *category_out) {
    char *category = NULL;
    bool valid;
    if (!sdk_decode_owned_string(
            context, token_index, "SDK category", true, &category)) {
        return false;
    }
    valid = porpoise_sdk_category_from_name(category, category_out);
    if (!valid) {
        bool result = sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "unknown SDK category '%s'", category);
        free(category);
        return result;
    }
    free(category);
    return true;
}

static bool sdk_parse_entry(
    SdkCatalogParseContext *context,
    int object_index,
    PorpoiseSdkCatalog *catalog) {
    const jsmntok_t *object;
    PorpoiseSdkCatalogEntry entry;
    int member_index;
    int member;
    unsigned int seen = 0U;
    int add_result;

    if (object_index < 0 || object_index >= context->token_count ||
        context->tokens[object_index].type != JSMN_OBJECT) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "SDK catalog entries must be objects");
    }
    object = &context->tokens[object_index];
    memset(&entry, 0, sizeof(entry));
    entry.provenance.source_kind = PORPOISE_SDK_CATALOG_SOURCE_JSON;
    entry.provenance.path = (char *)context->path;
    entry.provenance.line = sdk_token_line(context, object_index);
    member_index = object_index + 1;

    for (member = 0; member < object->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int bit;

        if (!sdk_key(context, key_index, &key)) goto failed;
        if (strcmp(key, "canonical_identity") == 0)
            bit = SDK_ENTRY_IDENTITY;
        else if (strcmp(key, "category") == 0)
            bit = SDK_ENTRY_CATEGORY;
        else if (strcmp(key, "contract") == 0)
            bit = SDK_ENTRY_CONTRACT;
        else if (strcmp(key, "signature") == 0)
            bit = SDK_ENTRY_SIGNATURE;
        else {
            (void)sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "unknown SDK catalog entry key '%s'", key);
            free(key);
            goto failed;
        }
        if ((seen & bit) != 0U) {
            (void)sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "duplicate SDK catalog entry key '%s'", key);
            free(key);
            goto failed;
        }
        seen |= bit;
        free(key);

        if (bit == SDK_ENTRY_IDENTITY) {
            if (!sdk_decode_owned_string(
                    context, value_index, "canonical_identity", true,
                    &entry.canonical_identity)) {
                goto failed;
            }
        } else if (bit == SDK_ENTRY_CATEGORY) {
            if (!sdk_parse_category(context, value_index, &entry.category))
                goto failed;
        } else if (bit == SDK_ENTRY_CONTRACT) {
            if (!sdk_decode_owned_string(
                    context, value_index, "contract", true,
                    &entry.contract_name)) {
                goto failed;
            }
        } else if (!sdk_parse_signature(
                       context, value_index, &entry.signature)) {
            goto failed;
        }
        member_index = sdk_token_after(context, value_index);
        if (member_index < 0) {
            (void)sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "SDK catalog entry has invalid token structure");
            goto failed;
        }
    }
    if ((seen & (SDK_ENTRY_IDENTITY |
                 SDK_ENTRY_CATEGORY |
                 SDK_ENTRY_SIGNATURE)) !=
        (SDK_ENTRY_IDENTITY |
         SDK_ENTRY_CATEGORY |
         SDK_ENTRY_SIGNATURE)) {
        (void)sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "SDK catalog entry is missing required fields");
        goto failed;
    }
    add_result = porpoise_sdk_catalog_add(
        catalog, &entry, context->diagnostics);
    if (add_result != PORPOISE_EXIT_OK) {
        context->result = add_result;
        goto failed;
    }
    free(entry.canonical_identity);
    free(entry.contract_name);
    return true;

failed:
    free(entry.canonical_identity);
    free(entry.contract_name);
    return false;
}

static bool sdk_parse_entries(
    SdkCatalogParseContext *context,
    int array_index,
    PorpoiseSdkCatalog *catalog) {
    const jsmntok_t *array;
    int entry_index;
    int entry;

    if (array_index < 0 || array_index >= context->token_count ||
        context->tokens[array_index].type != JSMN_ARRAY) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, array_index,
            "SDK catalog entries must be an array");
    }
    array = &context->tokens[array_index];
    entry_index = array_index + 1;
    for (entry = 0; entry < array->size; entry++) {
        if (!sdk_parse_entry(context, entry_index, catalog)) return false;
        entry_index = sdk_token_after(context, entry_index);
        if (entry_index < 0) {
            return sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, array_index,
                "SDK catalog entries have invalid token structure");
        }
    }
    return true;
}

static bool sdk_parse_root(
    SdkCatalogParseContext *context,
    PorpoiseSdkCatalog *catalog) {
    const jsmntok_t *root = &context->tokens[0];
    int member_index;
    int member;
    unsigned int seen = 0U;

    if (root->type != JSMN_OBJECT) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, 0,
            "SDK catalog root must be an object");
    }
    member_index = 1;
    for (member = 0; member < root->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int bit;

        if (!sdk_key(context, key_index, &key)) return false;
        if (strcmp(key, "schema_version") == 0)
            bit = SDK_ROOT_SCHEMA_VERSION;
        else if (strcmp(key, "signature_algorithm_version") == 0)
            bit = SDK_ROOT_SIGNATURE_VERSION;
        else if (strcmp(key, "entries") == 0)
            bit = SDK_ROOT_ENTRIES;
        else {
            bool result = sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "unknown SDK catalog root key '%s'", key);
            free(key);
            return result;
        }
        if ((seen & bit) != 0U) {
            bool result = sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "duplicate SDK catalog root key '%s'", key);
            free(key);
            return result;
        }
        seen |= bit;
        free(key);

        if (bit == SDK_ROOT_ENTRIES) {
            if (!sdk_parse_entries(context, value_index, catalog)) return false;
        } else {
            uint32_t version;
            if (!sdk_parse_uint32(
                    context, value_index,
                    bit == SDK_ROOT_SCHEMA_VERSION ?
                        "schema_version" : "signature_algorithm_version",
                    &version)) {
                return false;
            }
            if ((bit == SDK_ROOT_SCHEMA_VERSION &&
                 version != PORPOISE_SDK_CATALOG_SCHEMA_VERSION) ||
                (bit == SDK_ROOT_SIGNATURE_VERSION &&
                 version != PORPOISE_SIGNATURE_ALGORITHM_VERSION)) {
                return sdk_parse_report(
                    context, PORPOISE_EXIT_USAGE, value_index,
                    "%s must be %u",
                    bit == SDK_ROOT_SCHEMA_VERSION ?
                        "schema_version" : "signature_algorithm_version",
                    bit == SDK_ROOT_SCHEMA_VERSION ?
                        PORPOISE_SDK_CATALOG_SCHEMA_VERSION :
                        PORPOISE_SIGNATURE_ALGORITHM_VERSION);
            }
        }
        member_index = sdk_token_after(context, value_index);
        if (member_index < 0) {
            return sdk_parse_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "SDK catalog root has invalid token structure");
        }
    }
    if (seen != (SDK_ROOT_SCHEMA_VERSION |
                 SDK_ROOT_SIGNATURE_VERSION |
                 SDK_ROOT_ENTRIES)) {
        return sdk_parse_report(
            context, PORPOISE_EXIT_USAGE, 0,
            "SDK catalog root is missing required fields");
    }
    return true;
}

static int sdk_read_file(
    const char *path,
    char **text_out,
    size_t *length_out,
    PorpoiseDiagnostics *diagnostics) {
    FILE *file;
    char *text = NULL;
    size_t capacity = 0U;
    size_t length = 0U;

    file = fopen(path, "rb");
    if (file == NULL) {
        return sdk_add_diagnostic(
            diagnostics, PORPOISE_EXIT_IO, path, 0U,
            "failed to open SDK catalog");
    }
    for (;;) {
        size_t available;
        size_t count;
        if (capacity - length < 2048U) {
            size_t new_capacity = capacity == 0U ?
                SDK_CATALOG_JSON_INITIAL_CAPACITY : capacity * 2U;
            char *resized;
            if (new_capacity <= capacity || new_capacity == SIZE_MAX) {
                fclose(file);
                free(text);
                return sdk_add_diagnostic(
                    diagnostics, PORPOISE_EXIT_INTERNAL, path, 0U,
                    "SDK catalog is too large to read");
            }
            resized = (char *)realloc(text, new_capacity);
            if (resized == NULL) {
                fclose(file);
                free(text);
                return sdk_add_diagnostic(
                    diagnostics, PORPOISE_EXIT_INTERNAL, path, 0U,
                    "out of memory while reading SDK catalog");
            }
            text = resized;
            capacity = new_capacity;
        }
        available = capacity - length - 1U;
        count = fread(text + length, 1U, available, file);
        length += count;
        if (count < available) {
            if (ferror(file)) {
                fclose(file);
                free(text);
                return sdk_add_diagnostic(
                    diagnostics, PORPOISE_EXIT_IO, path, 0U,
                    "failed while reading SDK catalog");
            }
            break;
        }
    }
    if (fclose(file) != 0) {
        free(text);
        return sdk_add_diagnostic(
            diagnostics, PORPOISE_EXIT_IO, path, 0U,
            "failed to close SDK catalog after reading");
    }
    text[length] = '\0';
    *text_out = text;
    *length_out = length;
    return PORPOISE_EXIT_OK;
}

int porpoise_sdk_catalog_load_json(
    PorpoiseSdkCatalog *catalog,
    const char *path,
    PorpoiseDiagnostics *diagnostics) {
    char *json = NULL;
    size_t json_length = 0U;
    jsmn_parser parser;
    jsmntok_t *tokens = NULL;
    int token_count;
    int parsed_count;
    int result;
    SdkCatalogParseContext context;
    PorpoiseSdkCatalog parsed;

    if (catalog == NULL || path == NULL || path[0] == '\0') {
        return sdk_add_diagnostic(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            path == NULL ? "" : path, 0U,
            "SDK catalog load arguments are invalid");
    }
    result = sdk_read_file(path, &json, &json_length, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (json_length > UINT_MAX) {
        free(json);
        return sdk_add_diagnostic(
            diagnostics, PORPOISE_EXIT_USAGE, path, 0U,
            "SDK catalog JSON exceeds the supported parser size");
    }

    jsmn_init(&parser);
    token_count = jsmn_parse(&parser, json, json_length, NULL, 0U);
    if (token_count <= 0 ||
        (size_t)token_count > SIZE_MAX / sizeof(*tokens)) {
        free(json);
        return sdk_add_diagnostic(
            diagnostics,
            token_count == JSMN_ERROR_NOMEM ?
                PORPOISE_EXIT_INTERNAL : PORPOISE_EXIT_USAGE,
            path, 0U,
            token_count == JSMN_ERROR_NOMEM ?
                "out of memory while parsing SDK catalog" :
                "SDK catalog is not valid JSON");
    }
    tokens = (jsmntok_t *)calloc((size_t)token_count, sizeof(*tokens));
    if (tokens == NULL) {
        free(json);
        return sdk_add_diagnostic(
            diagnostics, PORPOISE_EXIT_INTERNAL, path, 0U,
            "out of memory while parsing SDK catalog");
    }
    jsmn_init(&parser);
    parsed_count = jsmn_parse(
        &parser, json, json_length, tokens, (unsigned int)token_count);
    if (parsed_count != token_count) {
        free(tokens);
        free(json);
        return sdk_add_diagnostic(
            diagnostics, PORPOISE_EXIT_USAGE, path, 0U,
            "SDK catalog is not valid JSON");
    }

    memset(&context, 0, sizeof(context));
    context.path = path;
    context.json = json;
    context.json_length = json_length;
    context.tokens = tokens;
    context.token_count = token_count;
    context.diagnostics = diagnostics;
    context.result = PORPOISE_EXIT_USAGE;
    porpoise_sdk_catalog_init(&parsed);
    if (!sdk_validate_json_document(&context) ||
        !sdk_parse_root(&context, &parsed)) {
        porpoise_sdk_catalog_free(&parsed);
        result = context.result;
    } else {
        result = porpoise_sdk_catalog_merge(catalog, &parsed, diagnostics);
        porpoise_sdk_catalog_free(&parsed);
    }
    free(tokens);
    free(json);
    return result;
}

int porpoise_sdk_catalog_load_builtin(
    PorpoiseSdkCatalog *catalog,
    PorpoiseDiagnostics *diagnostics) {
    if (catalog == NULL) {
        return sdk_add_diagnostic(
            diagnostics, PORPOISE_EXIT_INTERNAL, "builtin", 0U,
            "SDK built-in catalog load arguments are invalid");
    }
    return PORPOISE_EXIT_OK;
}

PorpoiseSdkCatalogMatch porpoise_sdk_catalog_lookup_exact(
    const PorpoiseSdkCatalog *catalog,
    const PorpoiseFunctionSignature *signature) {
    PorpoiseSdkCatalogMatch match;
    size_t index;

    memset(&match, 0, sizeof(match));
    match.status = PORPOISE_SDK_CATALOG_MATCH_NONE;
    if (catalog == NULL || signature == NULL) return match;
    if (sdk_catalog_lookup_indexed_signature(catalog, signature, &match))
        return match;
    for (index = 0U; index < catalog->entry_count; index++) {
        const PorpoiseSdkCatalogEntry *entry = &catalog->entries[index];
        if (!sdk_signature_exact_equal(&entry->signature, signature)) continue;
        if (match.match_count == 0U) match.entry = entry;
        match.match_count++;
    }
    if (match.match_count == 1U) {
        match.status = PORPOISE_SDK_CATALOG_MATCH_UNIQUE;
    } else if (match.match_count > 1U) {
        match.status = PORPOISE_SDK_CATALOG_MATCH_AMBIGUOUS;
        match.entry = NULL;
    }
    return match;
}

const PorpoiseSdkCatalogEntry *porpoise_sdk_catalog_find_identity(
    const PorpoiseSdkCatalog *catalog,
    const char *canonical_identity) {
    size_t index;
    if (catalog == NULL || canonical_identity == NULL ||
        canonical_identity[0] == '\0') {
        return NULL;
    }
    if (catalog->lookup_index != NULL &&
        catalog->lookup_index->capacity != 0U) {
        uint64_t hash = sdk_index_identity_hash(canonical_identity);
        size_t slot = (size_t)hash &
            (catalog->lookup_index->capacity - 1U);
        while (catalog->lookup_index->identities[slot].entry_plus_one != 0U) {
            const SdkCatalogIdentityIndexSlot *candidate =
                &catalog->lookup_index->identities[slot];
            size_t entry_index = candidate->entry_plus_one - 1U;
            if (candidate->hash == hash &&
                strcmp(catalog->entries[entry_index].canonical_identity,
                       canonical_identity) == 0) {
                return &catalog->entries[entry_index];
            }
            slot = (slot + 1U) &
                (catalog->lookup_index->capacity - 1U);
        }
        return NULL;
    }
    for (index = 0U; index < catalog->entry_count; index++) {
        if (catalog->entries[index].canonical_identity != NULL &&
            strcmp(catalog->entries[index].canonical_identity,
                   canonical_identity) == 0) {
            return &catalog->entries[index];
        }
    }
    return NULL;
}

PorpoiseSdkCatalogMatch porpoise_sdk_catalog_lookup_identity_exact(
    const PorpoiseSdkCatalog *catalog,
    const char *canonical_identity,
    const PorpoiseFunctionSignature *signature) {
    PorpoiseSdkCatalogMatch match;
    const PorpoiseSdkCatalogEntry *entry;
    memset(&match, 0, sizeof(match));
    match.status = PORPOISE_SDK_CATALOG_MATCH_NONE;
    if (catalog == NULL || signature == NULL) return match;
    entry = porpoise_sdk_catalog_find_identity(
        catalog, canonical_identity);
    if (entry == NULL ||
        !sdk_signature_exact_equal(&entry->signature, signature)) {
        return match;
    }
    if (!sdk_catalog_lookup_indexed_signature(catalog, signature, &match)) {
        match = porpoise_sdk_catalog_lookup_exact(catalog, signature);
    }
    if (match.status == PORPOISE_SDK_CATALOG_MATCH_UNIQUE &&
        match.entry != entry) {
        memset(&match, 0, sizeof(match));
        match.status = PORPOISE_SDK_CATALOG_MATCH_NONE;
    }
    return match;
}

const char *porpoise_sdk_category_name(PorpoiseSdkCategory category) {
    switch (category) {
    case PORPOISE_SDK_CATEGORY_NINTENDO_DOLPHIN:
        return "nintendo_dolphin";
    case PORPOISE_SDK_CATEGORY_DEMO:
        return "demo";
    case PORPOISE_SDK_CATEGORY_CRT_MSL:
        return "crt_msl";
    case PORPOISE_SDK_CATEGORY_RUNTIME:
        return "runtime";
    case PORPOISE_SDK_CATEGORY_METROTRK:
        return "metrotrk";
    case PORPOISE_SDK_CATEGORY_DEBUGGER:
        return "debugger";
    case PORPOISE_SDK_CATEGORY_STUB:
        return "stub";
    default:
        return "unknown";
    }
}

bool porpoise_sdk_category_from_name(
    const char *name,
    PorpoiseSdkCategory *category_out) {
    PorpoiseSdkCategory category;
    if (name == NULL || category_out == NULL) return false;
    for (category = PORPOISE_SDK_CATEGORY_NINTENDO_DOLPHIN;
         category <= PORPOISE_SDK_CATEGORY_STUB;
         category = (PorpoiseSdkCategory)((int)category + 1)) {
        if (strcmp(name, porpoise_sdk_category_name(category)) == 0) {
            *category_out = category;
            return true;
        }
    }
    return false;
}

bool porpoise_sdk_category_is_automatic(PorpoiseSdkCategory category) {
    return category == PORPOISE_SDK_CATEGORY_NINTENDO_DOLPHIN ||
           category == PORPOISE_SDK_CATEGORY_DEMO;
}

bool porpoise_sdk_category_is_report_only(PorpoiseSdkCategory category) {
    return sdk_category_valid(category) &&
           !porpoise_sdk_category_is_automatic(category);
}

const char *porpoise_sdk_catalog_source_kind_name(
    PorpoiseSdkCatalogSourceKind kind) {
    switch (kind) {
    case PORPOISE_SDK_CATALOG_SOURCE_BUILTIN:
        return "builtin";
    case PORPOISE_SDK_CATALOG_SOURCE_JSON:
        return "json";
    default:
        return "unknown";
    }
}
