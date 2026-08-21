#include "porpoise/abi.h"

#include "jsmn.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ABI_JSON_INITIAL_CAPACITY 4096U
#define ABI_JSON_MAX_DEPTH 32U

typedef enum AbiParseStatus {
    ABI_PARSE_OK = 0,
    ABI_PARSE_SCHEMA_ERROR,
    ABI_PARSE_OUT_OF_MEMORY
} AbiParseStatus;

typedef struct AbiParseContext {
    const char *path;
    const char *json;
    size_t json_length;
    const jsmntok_t *tokens;
    int token_count;
    PorpoiseDiagnostics *diagnostics;
    AbiParseStatus status;
} AbiParseContext;

enum {
    ABI_ROOT_SCHEMA_VERSION = 1U << 0,
    ABI_ROOT_FUNCTIONS = 1U << 1
};

enum {
    ABI_FUNCTION_KIND = 1U << 0,
    ABI_FUNCTION_SYMBOL = 1U << 1,
    ABI_FUNCTION_WRAPPER = 1U << 2,
    ABI_FUNCTION_HEADER = 1U << 3,
    ABI_FUNCTION_ADAPTER = 1U << 4,
    ABI_FUNCTION_RETURN = 1U << 5,
    ABI_FUNCTION_ARGUMENTS = 1U << 6
};

enum {
    ABI_VALUE_TYPE = 1U << 0,
    ABI_VALUE_REGISTER = 1U << 1,
    ABI_VALUE_NAME = 1U << 2
};

static char *abi_duplicate_string(const char *value);

static size_t abi_line_at_offset(const AbiParseContext *context,
                                 size_t offset) {
    size_t line = 1U;
    size_t index;

    if (offset > context->json_length) {
        offset = context->json_length;
    }
    for (index = 0U; index < offset; index++) {
        if (context->json[index] == '\n') {
            line++;
        }
    }
    return line;
}

static size_t abi_token_line(const AbiParseContext *context,
                             int token_index) {
    if (token_index < 0 || token_index >= context->token_count ||
        context->tokens[token_index].start < 0) {
        return 1U;
    }
    return abi_line_at_offset(
        context,
        (size_t)context->tokens[token_index].start);
}

static bool abi_report_at_offset(AbiParseContext *context,
                                 AbiParseStatus status,
                                 size_t offset,
                                 const char *format,
                                 ...) {
    char message[PORPOISE_MESSAGE_CAPACITY];
    va_list arguments;
    int written;

    if (context->status == ABI_PARSE_OUT_OF_MEMORY) {
        return false;
    }

    va_start(arguments, format);
    written = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (written < 0) {
        context->status = ABI_PARSE_OUT_OF_MEMORY;
        return false;
    }

    context->status = status;
    if (context->diagnostics != NULL &&
        !porpoise_diagnostics_add(
            context->diagnostics,
            PORPOISE_SEVERITY_ERROR,
            context->path,
            abi_line_at_offset(context, offset),
            0U,
            "%s",
            message)) {
        context->status = ABI_PARSE_OUT_OF_MEMORY;
    }
    return false;
}

static bool abi_report(AbiParseContext *context,
                       AbiParseStatus status,
                       int token_index,
                       const char *format,
                       ...) {
    char message[PORPOISE_MESSAGE_CAPACITY];
    va_list arguments;
    int written;
    size_t line;

    if (context->status == ABI_PARSE_OUT_OF_MEMORY) {
        return false;
    }

    va_start(arguments, format);
    written = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (written < 0) {
        context->status = ABI_PARSE_OUT_OF_MEMORY;
        return false;
    }

    context->status = status;
    line = abi_token_line(context, token_index);
    if (context->diagnostics != NULL &&
        !porpoise_diagnostics_add(
            context->diagnostics,
            PORPOISE_SEVERITY_ERROR,
            context->path,
            line,
            0U,
            "%s",
            message)) {
        context->status = ABI_PARSE_OUT_OF_MEMORY;
    }
    return false;
}

static bool abi_schema_error(AbiParseContext *context,
                             int token_index,
                             const char *format,
                             ...) {
    char message[PORPOISE_MESSAGE_CAPACITY];
    va_list arguments;
    int written;

    va_start(arguments, format);
    written = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (written < 0) {
        context->status = ABI_PARSE_OUT_OF_MEMORY;
        return false;
    }
    return abi_report(
        context,
        ABI_PARSE_SCHEMA_ERROR,
        token_index,
        "%s",
        message);
}

static bool abi_out_of_memory(AbiParseContext *context,
                              int token_index,
                              const char *operation) {
    return abi_report(
        context,
        ABI_PARSE_OUT_OF_MEMORY,
        token_index,
        "out of memory while %s",
        operation);
}

static void abi_skip_whitespace(const char *json,
                                size_t length,
                                size_t *position) {
    while (*position < length) {
        char value = json[*position];
        if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
            break;
        }
        (*position)++;
    }
}

static bool abi_json_number_is_valid(const char *text, size_t length) {
    size_t position = 0U;

    if (position < length && text[position] == '-') {
        position++;
    }
    if (position >= length) {
        return false;
    }

    if (text[position] == '0') {
        position++;
        if (position < length && isdigit((unsigned char)text[position]) != 0) {
            return false;
        }
    } else {
        if (text[position] < '1' || text[position] > '9') {
            return false;
        }
        do {
            position++;
        } while (position < length &&
                 isdigit((unsigned char)text[position]) != 0);
    }

    if (position < length && text[position] == '.') {
        position++;
        if (position >= length ||
            isdigit((unsigned char)text[position]) == 0) {
            return false;
        }
        do {
            position++;
        } while (position < length &&
                 isdigit((unsigned char)text[position]) != 0);
    }

    if (position < length &&
        (text[position] == 'e' || text[position] == 'E')) {
        position++;
        if (position < length &&
            (text[position] == '+' || text[position] == '-')) {
            position++;
        }
        if (position >= length ||
            isdigit((unsigned char)text[position]) == 0) {
            return false;
        }
        do {
            position++;
        } while (position < length &&
                 isdigit((unsigned char)text[position]) != 0);
    }

    return position == length;
}

static bool abi_json_primitive_is_valid(const char *text, size_t length) {
    if ((length == 4U && memcmp(text, "true", 4U) == 0) ||
        (length == 5U && memcmp(text, "false", 5U) == 0) ||
        (length == 4U && memcmp(text, "null", 4U) == 0)) {
        return true;
    }
    return abi_json_number_is_valid(text, length);
}

static bool abi_validate_json_value(AbiParseContext *context,
                                    int token_index,
                                    int expected_parent,
                                    unsigned int depth,
                                    size_t *position,
                                    int *next_token) {
    const jsmntok_t *token;
    int child_index;
    int child;

    if (depth > ABI_JSON_MAX_DEPTH) {
        return abi_report_at_offset(
            context,
            ABI_PARSE_SCHEMA_ERROR,
            *position,
            "ABI manifest exceeds the maximum JSON nesting depth");
    }
    if (token_index < 0 || token_index >= context->token_count) {
        return abi_report_at_offset(
            context,
            ABI_PARSE_SCHEMA_ERROR,
            *position,
            "ABI manifest has an invalid JSON token structure");
    }

    token = &context->tokens[token_index];
    if (token->parent != expected_parent || token->start < 0 ||
        token->end < token->start || token->size < 0) {
        return abi_schema_error(
            context,
            token_index,
            "ABI manifest has an invalid JSON token structure");
    }

    abi_skip_whitespace(context->json, context->json_length, position);
    if (token->type == JSMN_STRING) {
        if (token->size != 0 || *position >= context->json_length ||
            context->json[*position] != '"' ||
            token->start != (int)(*position + 1U) ||
            (size_t)token->end >= context->json_length ||
            context->json[token->end] != '"') {
            return abi_schema_error(
                context,
                token_index,
                "ABI manifest is not strict JSON near a string value");
        }
        *position = (size_t)token->end + 1U;
        *next_token = token_index + 1;
        return true;
    }

    if (token->type == JSMN_PRIMITIVE) {
        size_t primitive_length;

        if (token->size != 0 || token->start != (int)*position ||
            token->end <= token->start) {
            return abi_schema_error(
                context,
                token_index,
                "ABI manifest is not strict JSON near a primitive value");
        }
        primitive_length = (size_t)(token->end - token->start);
        if (!abi_json_primitive_is_valid(
                context->json + token->start,
                primitive_length)) {
            return abi_schema_error(
                context,
                token_index,
                "ABI manifest contains an invalid JSON primitive");
        }
        *position = (size_t)token->end;
        *next_token = token_index + 1;
        return true;
    }

    if (token->type == JSMN_ARRAY) {
        if (token->start != (int)*position ||
            *position >= context->json_length ||
            context->json[*position] != '[') {
            return abi_schema_error(
                context,
                token_index,
                "ABI manifest is not strict JSON near an array");
        }
        (*position)++;
        child_index = token_index + 1;
        for (child = 0; child < token->size; child++) {
            abi_skip_whitespace(
                context->json,
                context->json_length,
                position);
            if (!abi_validate_json_value(
                    context,
                    child_index,
                    token_index,
                    depth + 1U,
                    position,
                    &child_index)) {
                return false;
            }
            abi_skip_whitespace(
                context->json,
                context->json_length,
                position);
            if (child + 1 < token->size) {
                if (*position >= context->json_length ||
                    context->json[*position] != ',') {
                    return abi_report_at_offset(
                        context,
                        ABI_PARSE_SCHEMA_ERROR,
                        *position,
                        "ABI manifest array elements must be comma-separated");
                }
                (*position)++;
            }
        }
        abi_skip_whitespace(context->json, context->json_length, position);
        if (*position >= context->json_length ||
            context->json[*position] != ']') {
            return abi_report_at_offset(
                context,
                ABI_PARSE_SCHEMA_ERROR,
                *position,
                "ABI manifest has an unterminated or trailing-comma array");
        }
        (*position)++;
        if (token->end != (int)*position) {
            return abi_schema_error(
                context,
                token_index,
                "ABI manifest has an invalid array token boundary");
        }
        *next_token = child_index;
        return true;
    }

    if (token->type == JSMN_OBJECT) {
        if (token->start != (int)*position ||
            *position >= context->json_length ||
            context->json[*position] != '{') {
            return abi_schema_error(
                context,
                token_index,
                "ABI manifest is not strict JSON near an object");
        }
        (*position)++;
        child_index = token_index + 1;
        for (child = 0; child < token->size; child++) {
            const jsmntok_t *key;
            int value_index;

            abi_skip_whitespace(
                context->json,
                context->json_length,
                position);
            if (child_index < 0 || child_index >= context->token_count) {
                return abi_report_at_offset(
                    context,
                    ABI_PARSE_SCHEMA_ERROR,
                    *position,
                    "ABI manifest object is missing a key");
            }
            key = &context->tokens[child_index];
            if (key->type != JSMN_STRING || key->parent != token_index ||
                key->size != 1 || *position >= context->json_length ||
                context->json[*position] != '"' ||
                key->start != (int)(*position + 1U) ||
                key->end < key->start ||
                (size_t)key->end >= context->json_length ||
                context->json[key->end] != '"') {
                return abi_schema_error(
                    context,
                    child_index,
                    "ABI manifest object keys must be strict JSON strings");
            }
            *position = (size_t)key->end + 1U;
            abi_skip_whitespace(
                context->json,
                context->json_length,
                position);
            if (*position >= context->json_length ||
                context->json[*position] != ':') {
                return abi_report_at_offset(
                    context,
                    ABI_PARSE_SCHEMA_ERROR,
                    *position,
                    "ABI manifest object key is missing ':'");
            }
            (*position)++;
            value_index = child_index + 1;
            if (!abi_validate_json_value(
                    context,
                    value_index,
                    child_index,
                    depth + 1U,
                    position,
                    &child_index)) {
                return false;
            }
            abi_skip_whitespace(
                context->json,
                context->json_length,
                position);
            if (child + 1 < token->size) {
                if (*position >= context->json_length ||
                    context->json[*position] != ',') {
                    return abi_report_at_offset(
                        context,
                        ABI_PARSE_SCHEMA_ERROR,
                        *position,
                        "ABI manifest object members must be comma-separated");
                }
                (*position)++;
            }
        }
        abi_skip_whitespace(context->json, context->json_length, position);
        if (*position >= context->json_length ||
            context->json[*position] != '}') {
            return abi_report_at_offset(
                context,
                ABI_PARSE_SCHEMA_ERROR,
                *position,
                "ABI manifest has an unterminated or trailing-comma object");
        }
        (*position)++;
        if (token->end != (int)*position) {
            return abi_schema_error(
                context,
                token_index,
                "ABI manifest has an invalid object token boundary");
        }
        *next_token = child_index;
        return true;
    }

    return abi_schema_error(
        context,
        token_index,
        "ABI manifest contains an undefined JSON token");
}

static bool abi_validate_json_document(AbiParseContext *context) {
    size_t position = 0U;
    int next_token = 0;

    if (context->token_count < 1) {
        return abi_report_at_offset(
            context,
            ABI_PARSE_SCHEMA_ERROR,
            0U,
            "ABI manifest is empty");
    }
    if (!abi_validate_json_value(
            context,
            0,
            -1,
            0U,
            &position,
            &next_token)) {
        return false;
    }
    abi_skip_whitespace(context->json, context->json_length, &position);
    if (position != context->json_length ||
        next_token != context->token_count) {
        return abi_report_at_offset(
            context,
            ABI_PARSE_SCHEMA_ERROR,
            position,
            "ABI manifest must contain exactly one JSON value");
    }
    return true;
}

static int abi_hex_value(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool abi_append_utf8(char *output,
                            size_t capacity,
                            size_t *length,
                            uint32_t codepoint) {
    unsigned int count;

    if (codepoint == 0U || codepoint > UINT32_C(0x10FFFF) ||
        (codepoint >= UINT32_C(0xD800) &&
         codepoint <= UINT32_C(0xDFFF))) {
        return false;
    }
    if (codepoint <= UINT32_C(0x7F)) {
        count = 1U;
    } else if (codepoint <= UINT32_C(0x7FF)) {
        count = 2U;
    } else if (codepoint <= UINT32_C(0xFFFF)) {
        count = 3U;
    } else {
        count = 4U;
    }
    if (*length > capacity - 1U ||
        count > capacity - 1U - *length) {
        return false;
    }

    if (count == 1U) {
        output[(*length)++] = (char)codepoint;
    } else if (count == 2U) {
        output[(*length)++] = (char)(UINT32_C(0xC0) | (codepoint >> 6U));
        output[(*length)++] =
            (char)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3F)));
    } else if (count == 3U) {
        output[(*length)++] = (char)(UINT32_C(0xE0) | (codepoint >> 12U));
        output[(*length)++] = (char)(
            UINT32_C(0x80) |
            ((codepoint >> 6U) & UINT32_C(0x3F)));
        output[(*length)++] =
            (char)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3F)));
    } else {
        output[(*length)++] = (char)(UINT32_C(0xF0) | (codepoint >> 18U));
        output[(*length)++] = (char)(
            UINT32_C(0x80) |
            ((codepoint >> 12U) & UINT32_C(0x3F)));
        output[(*length)++] = (char)(
            UINT32_C(0x80) |
            ((codepoint >> 6U) & UINT32_C(0x3F)));
        output[(*length)++] =
            (char)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3F)));
    }
    return true;
}

static bool abi_valid_utf8(const char *text, size_t length) {
    size_t position = 0U;

    while (position < length) {
        unsigned char first = (unsigned char)text[position++];
        uint32_t codepoint;
        uint32_t minimum;
        unsigned int remaining;

        if (first <= 0x7FU) {
            if (first == 0U) {
                return false;
            }
            continue;
        }
        if (first >= 0xC2U && first <= 0xDFU) {
            codepoint = (uint32_t)(first & 0x1FU);
            minimum = UINT32_C(0x80);
            remaining = 1U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            codepoint = (uint32_t)(first & 0x0FU);
            minimum = UINT32_C(0x800);
            remaining = 2U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            codepoint = (uint32_t)(first & 0x07U);
            minimum = UINT32_C(0x10000);
            remaining = 3U;
        } else {
            return false;
        }

        if (remaining > length - position) {
            return false;
        }
        while (remaining > 0U) {
            unsigned char next = (unsigned char)text[position++];
            if ((next & 0xC0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (uint32_t)(next & 0x3FU);
            remaining--;
        }
        if (codepoint < minimum || codepoint > UINT32_C(0x10FFFF) ||
            (codepoint >= UINT32_C(0xD800) &&
             codepoint <= UINT32_C(0xDFFF))) {
            return false;
        }
    }
    return true;
}

static bool abi_decode_json_string(const char *json,
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
            if (value == 0U || output_length + 1U >= capacity) {
                return false;
            }
            output[output_length++] = (char)value;
            continue;
        }

        if (input_position >= input_end) {
            return false;
        }
        value = (unsigned char)json[input_position++];
        if (value == '"' || value == '\\' || value == '/') {
            if (output_length + 1U >= capacity) {
                return false;
            }
            output[output_length++] = (char)value;
        } else if (value == 'b' || value == 'f' || value == 'n' ||
                   value == 'r' || value == 't') {
            char decoded;

            if (value == 'b') {
                decoded = '\b';
            } else if (value == 'f') {
                decoded = '\f';
            } else if (value == 'n') {
                decoded = '\n';
            } else if (value == 'r') {
                decoded = '\r';
            } else {
                decoded = '\t';
            }
            if (output_length + 1U >= capacity) {
                return false;
            }
            output[output_length++] = decoded;
        } else if (value == 'u') {
            uint32_t codepoint = 0U;
            unsigned int digit;

            if (input_position + 4U > input_end) {
                return false;
            }
            for (digit = 0U; digit < 4U; digit++) {
                int hex = abi_hex_value(json[input_position++]);
                if (hex < 0) {
                    return false;
                }
                codepoint = (codepoint << 4U) | (uint32_t)hex;
            }

            if (codepoint >= UINT32_C(0xD800) &&
                codepoint <= UINT32_C(0xDBFF)) {
                uint32_t low = 0U;

                if (input_position + 6U > input_end ||
                    json[input_position] != '\\' ||
                    json[input_position + 1U] != 'u') {
                    return false;
                }
                input_position += 2U;
                for (digit = 0U; digit < 4U; digit++) {
                    int hex = abi_hex_value(json[input_position++]);
                    if (hex < 0) {
                        return false;
                    }
                    low = (low << 4U) | (uint32_t)hex;
                }
                if (low < UINT32_C(0xDC00) ||
                    low > UINT32_C(0xDFFF)) {
                    return false;
                }
                codepoint = UINT32_C(0x10000) +
                            ((codepoint - UINT32_C(0xD800)) << 10U) +
                            (low - UINT32_C(0xDC00));
            }
            if (!abi_append_utf8(
                    output,
                    capacity,
                    &output_length,
                    codepoint)) {
                return false;
            }
        } else {
            return false;
        }
    }

    if (!abi_valid_utf8(output, output_length)) {
        return false;
    }
    output[output_length] = '\0';
    return true;
}

static bool abi_decode_owned_string(AbiParseContext *context,
                                    int token_index,
                                    const char *description,
                                    char **output) {
    const jsmntok_t *token;
    size_t length;
    char *decoded;

    if (token_index < 0 || token_index >= context->token_count) {
        return abi_schema_error(
            context,
            token_index,
            "%s is missing",
            description);
    }
    token = &context->tokens[token_index];
    if (token->type != JSMN_STRING) {
        return abi_schema_error(
            context,
            token_index,
            "%s must be a JSON string",
            description);
    }

    length = (size_t)(token->end - token->start);
    if (length == SIZE_MAX) {
        return abi_out_of_memory(context, token_index, "decoding an ABI string");
    }
    decoded = (char *)malloc(length + 1U);
    if (decoded == NULL) {
        return abi_out_of_memory(context, token_index, "decoding an ABI string");
    }
    if (!abi_decode_json_string(
            context->json,
            token,
            decoded,
            length + 1U)) {
        free(decoded);
        return abi_schema_error(
            context,
            token_index,
            "%s contains invalid Unicode or an embedded null",
            description);
    }
    *output = decoded;
    return true;
}

static bool abi_token_is(const AbiParseContext *context,
                         int token_index,
                         const char *expected) {
    const jsmntok_t *token;
    size_t expected_length;
    size_t token_length;

    if (token_index < 0 || token_index >= context->token_count) {
        return false;
    }
    token = &context->tokens[token_index];
    expected_length = strlen(expected);
    token_length = (size_t)(token->end - token->start);
    return token_length == expected_length &&
           memcmp(
               context->json + token->start,
               expected,
               expected_length) == 0;
}

static int abi_token_after(const AbiParseContext *context, int token_index) {
    const jsmntok_t *token;
    int next;
    int child;

    if (token_index < 0 || token_index >= context->token_count) {
        return -1;
    }
    token = &context->tokens[token_index];
    next = token_index + 1;
    for (child = 0; child < token->size; child++) {
        next = abi_token_after(context, next);
        if (next < 0) {
            return -1;
        }
    }
    return next;
}

static bool abi_nonempty_string(AbiParseContext *context,
                                int token_index,
                                const char *description,
                                char **output) {
    if (!abi_decode_owned_string(
            context,
            token_index,
            description,
            output)) {
        return false;
    }
    if ((*output)[0] == '\0') {
        free(*output);
        *output = NULL;
        return abi_schema_error(
            context,
            token_index,
            "%s must not be empty",
            description);
    }
    return true;
}

static bool abi_is_c_keyword(const char *value) {
    static const char *const keywords[] = {
        "auto", "break", "case", "char", "const", "continue", "default",
        "do", "double", "else", "enum", "extern", "float", "for", "goto",
        "if", "inline", "int", "long", "register", "restrict", "return",
        "short", "signed", "sizeof", "static", "struct", "switch", "typedef",
        "union", "unsigned", "void", "volatile", "while", "_Bool",
        "_Complex", "_Imaginary"
    };
    size_t index;

    for (index = 0U; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        if (strcmp(value, keywords[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool abi_is_c_identifier(const char *value) {
    const unsigned char *cursor = (const unsigned char *)value;

    if (!(cursor[0] == '_' ||
          (cursor[0] >= 'A' && cursor[0] <= 'Z') ||
          (cursor[0] >= 'a' && cursor[0] <= 'z'))) {
        return false;
    }
    cursor++;
    while (*cursor != 0U) {
        if (!(*cursor == '_' ||
              (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9'))) {
            return false;
        }
        cursor++;
    }
    return !abi_is_c_keyword(value);
}

static bool abi_symbol_is_valid(const char *value) {
    const unsigned char *cursor = (const unsigned char *)value;

    while (*cursor != 0U) {
        if (iscntrl(*cursor) != 0 || isspace(*cursor) != 0) {
            return false;
        }
        cursor++;
    }
    return true;
}

static bool abi_header_is_valid(const char *value) {
    const unsigned char *cursor = (const unsigned char *)value;

    while (*cursor != 0U) {
        if (iscntrl(*cursor) != 0 || *cursor == '"' ||
            *cursor == '<' || *cursor == '>') {
            return false;
        }
        cursor++;
    }
    return true;
}

static bool abi_parse_identifier_string(AbiParseContext *context,
                                        int token_index,
                                        const char *description,
                                        char **output) {
    if (!abi_nonempty_string(
            context,
            token_index,
            description,
            output)) {
        return false;
    }
    if (!abi_is_c_identifier(*output)) {
        free(*output);
        *output = NULL;
        return abi_schema_error(
            context,
            token_index,
            "%s must be a non-keyword C99 identifier",
            description);
    }
    return true;
}

static bool abi_parse_kind(AbiParseContext *context,
                           int token_index,
                           PorpoiseAbiKind *kind) {
    char *value = NULL;
    bool success = false;

    if (!abi_nonempty_string(
            context,
            token_index,
            "function kind",
            &value)) {
        return false;
    }
    if (strcmp(value, "import") == 0) {
        *kind = PORPOISE_ABI_IMPORT;
        success = true;
    } else if (strcmp(value, "export") == 0) {
        *kind = PORPOISE_ABI_EXPORT;
        success = true;
    } else {
        abi_schema_error(
            context,
            token_index,
            "function kind must be 'import' or 'export'");
    }
    free(value);
    return success;
}

static bool abi_parse_type(AbiParseContext *context,
                           int token_index,
                           PorpoiseAbiType *type) {
    static const struct AbiTypeName {
        const char *name;
        PorpoiseAbiType type;
    } names[] = {
        {"void", PORPOISE_ABI_VOID},
        {"u8", PORPOISE_ABI_U8},
        {"u16", PORPOISE_ABI_U16},
        {"u32", PORPOISE_ABI_U32},
        {"s8", PORPOISE_ABI_S8},
        {"s16", PORPOISE_ABI_S16},
        {"s32", PORPOISE_ABI_S32},
        {"f32", PORPOISE_ABI_F32},
        {"f64", PORPOISE_ABI_F64},
        {"pointer", PORPOISE_ABI_POINTER}
    };
    char *value = NULL;
    size_t index;

    if (!abi_nonempty_string(
            context,
            token_index,
            "ABI value type",
            &value)) {
        return false;
    }
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(value, names[index].name) == 0) {
            *type = names[index].type;
            free(value);
            return true;
        }
    }
    free(value);
    return abi_schema_error(
        context,
        token_index,
        "unknown ABI value type; expected void, u8/u16/u32, s8/s16/s32, f32/f64, or pointer");
}

static bool abi_parse_register(AbiParseContext *context,
                               int token_index,
                               PorpoiseAbiRegisterClass *register_class,
                               unsigned int *register_index) {
    char *value = NULL;
    char prefix;
    unsigned long parsed_index;
    char *end = NULL;
    const char *digit;

    if (!abi_nonempty_string(
            context,
            token_index,
            "ABI register",
            &value)) {
        return false;
    }
    if (strcmp(value, "none") == 0) {
        *register_class = PORPOISE_ABI_REGISTER_NONE;
        *register_index = 0U;
        free(value);
        return true;
    }

    prefix = value[0];
    if ((prefix != 'r' && prefix != 'f') || value[1] == '\0' ||
        (value[1] == '0' && value[2] != '\0')) {
        free(value);
        return abi_schema_error(
            context,
            token_index,
            "ABI register must be 'none', r3..r10, or f1..f8");
    }
    for (digit = value + 1; *digit != '\0'; digit++) {
        if (*digit < '0' || *digit > '9') {
            free(value);
            return abi_schema_error(
                context,
                token_index,
                "ABI register must be 'none', r3..r10, or f1..f8");
        }
    }
    errno = 0;
    parsed_index = strtoul(value + 1, &end, 10);
    if (errno == ERANGE || end == value + 1 || *end != '\0' ||
        parsed_index > (unsigned long)UINT_MAX) {
        free(value);
        return abi_schema_error(
            context,
            token_index,
            "ABI register must be 'none', r3..r10, or f1..f8");
    }

    *register_class = prefix == 'r'
        ? PORPOISE_ABI_REGISTER_GPR
        : PORPOISE_ABI_REGISTER_FPR;
    *register_index = (unsigned int)parsed_index;
    free(value);
    return true;
}

static bool abi_type_uses_gpr(PorpoiseAbiType type) {
    return type == PORPOISE_ABI_U8 || type == PORPOISE_ABI_U16 ||
           type == PORPOISE_ABI_U32 || type == PORPOISE_ABI_S8 ||
           type == PORPOISE_ABI_S16 || type == PORPOISE_ABI_S32 ||
           type == PORPOISE_ABI_POINTER;
}

static bool abi_type_uses_fpr(PorpoiseAbiType type) {
    return type == PORPOISE_ABI_F32 || type == PORPOISE_ABI_F64;
}

static bool abi_parse_value(AbiParseContext *context,
                            int object_index,
                            bool is_result,
                            PorpoiseAbiValue *value,
                            const char *location) {
    const jsmntok_t *object = &context->tokens[object_index];
    unsigned int seen = 0U;
    int key_index;
    int pair;

    if (object->type != JSMN_OBJECT) {
        return abi_schema_error(
            context,
            object_index,
            "%s must be an object",
            location);
    }

    key_index = object_index + 1;
    for (pair = 0; pair < object->size; pair++) {
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int key_flag;

        if (!abi_decode_owned_string(
                context,
                key_index,
                "ABI value key",
                &key)) {
            return false;
        }
        if (strcmp(key, "type") == 0) {
            key_flag = ABI_VALUE_TYPE;
        } else if (strcmp(key, "register") == 0) {
            key_flag = ABI_VALUE_REGISTER;
        } else if (strcmp(key, "name") == 0) {
            key_flag = ABI_VALUE_NAME;
        } else {
            bool result = abi_schema_error(
                context,
                key_index,
                "unknown key '%s' in %s",
                key,
                location);
            free(key);
            return result;
        }

        if ((seen & key_flag) != 0U) {
            bool result = abi_schema_error(
                context,
                key_index,
                "duplicate key '%s' in %s",
                key,
                location);
            free(key);
            return result;
        }
        seen |= key_flag;
        free(key);

        if (key_flag == ABI_VALUE_TYPE) {
            if (!abi_parse_type(context, value_index, &value->type)) {
                return false;
            }
        } else if (key_flag == ABI_VALUE_REGISTER) {
            if (!abi_parse_register(
                    context,
                    value_index,
                    &value->register_class,
                    &value->register_index)) {
                return false;
            }
        } else if (!abi_parse_identifier_string(
                       context,
                       value_index,
                       "ABI value name",
                       &value->name)) {
            return false;
        }

        key_index = abi_token_after(context, key_index);
        if (key_index < 0) {
            return abi_schema_error(
                context,
                object_index,
                "%s has an invalid token structure",
                location);
        }
    }

    if ((seen & ABI_VALUE_TYPE) == 0U) {
        return abi_schema_error(
            context,
            object_index,
            "%s is missing required key 'type'",
            location);
    }
    if (value->type == PORPOISE_ABI_VOID) {
        if (!is_result) {
            return abi_schema_error(
                context,
                object_index,
                "%s cannot have type 'void'",
                location);
        }
        if ((seen & ABI_VALUE_REGISTER) != 0U &&
            value->register_class != PORPOISE_ABI_REGISTER_NONE) {
            return abi_schema_error(
                context,
                object_index,
                "void return must omit register or use register 'none'");
        }
        value->register_class = PORPOISE_ABI_REGISTER_NONE;
        value->register_index = 0U;
        if (value->name != NULL) {
            return abi_schema_error(
                context,
                object_index,
                "void return must not declare a name");
        }
        return true;
    }

    if ((seen & ABI_VALUE_REGISTER) == 0U) {
        return abi_schema_error(
            context,
            object_index,
            "%s is missing required key 'register'",
            location);
    }
    if (abi_type_uses_gpr(value->type)) {
        if (value->register_class != PORPOISE_ABI_REGISTER_GPR) {
            return abi_schema_error(
                context,
                object_index,
                "%s type '%s' must map to a GPR",
                location,
                porpoise_abi_type_name(value->type));
        }
    } else if (abi_type_uses_fpr(value->type)) {
        if (value->register_class != PORPOISE_ABI_REGISTER_FPR) {
            return abi_schema_error(
                context,
                object_index,
                "%s type '%s' must map to an FPR",
                location,
                porpoise_abi_type_name(value->type));
        }
    } else {
        return abi_schema_error(
            context,
            object_index,
            "%s uses an unsupported type",
            location);
    }

    if (is_result) {
        if ((value->register_class == PORPOISE_ABI_REGISTER_GPR &&
             value->register_index != 3U) ||
            (value->register_class == PORPOISE_ABI_REGISTER_FPR &&
             value->register_index != 1U)) {
            return abi_schema_error(
                context,
                object_index,
                "%s must map integer/pointer returns to r3 and floating returns to f1",
                location);
        }
    } else if ((value->register_class == PORPOISE_ABI_REGISTER_GPR &&
                (value->register_index < 3U ||
                 value->register_index > 10U)) ||
               (value->register_class == PORPOISE_ABI_REGISTER_FPR &&
                (value->register_index < 1U ||
                 value->register_index > 8U))) {
        return abi_schema_error(
            context,
            object_index,
            "%s must map arguments to r3..r10 or f1..f8",
            location);
    }
    return true;
}

static bool abi_parse_arguments(AbiParseContext *context,
                                int array_index,
                                PorpoiseAbiFunction *function,
                                size_t function_index) {
    const jsmntok_t *array = &context->tokens[array_index];
    bool used_gprs[32] = {false};
    bool used_fprs[32] = {false};
    unsigned int previous_gpr = 0U;
    unsigned int previous_fpr = 0U;
    bool have_gpr = false;
    bool have_fpr = false;
    int argument_index;
    int child_index;

    if (array->type != JSMN_ARRAY) {
        return abi_schema_error(
            context,
            array_index,
            "functions[%lu].arguments must be an array",
            (unsigned long)function_index);
    }
    if ((size_t)array->size > SIZE_MAX / sizeof(*function->arguments)) {
        return abi_out_of_memory(
            context,
            array_index,
            "allocating ABI arguments");
    }

    function->argument_count = (size_t)array->size;
    if (function->argument_count != 0U) {
        function->arguments = (PorpoiseAbiValue *)calloc(
            function->argument_count,
            sizeof(*function->arguments));
        if (function->arguments == NULL) {
            return abi_out_of_memory(
                context,
                array_index,
                "allocating ABI arguments");
        }
    }

    child_index = array_index + 1;
    for (argument_index = 0; argument_index < array->size; argument_index++) {
        PorpoiseAbiValue *argument =
            &function->arguments[(size_t)argument_index];
        char location[96];

        (void)snprintf(
            location,
            sizeof(location),
            "functions[%lu].arguments[%d]",
            (unsigned long)function_index,
            argument_index);
        if (!abi_parse_value(
                context,
                child_index,
                false,
                argument,
                location)) {
            return false;
        }

        if (argument->name == NULL) {
            char generated_name[48];

            (void)snprintf(
                generated_name,
                sizeof(generated_name),
                "argument%lu",
                (unsigned long)argument_index);
            argument->name = abi_duplicate_string(generated_name);
            if (argument->name == NULL) {
                return abi_out_of_memory(
                    context,
                    child_index,
                    "creating a default ABI argument name");
            }
        }

        if (strcmp(argument->name, "state") == 0 ||
            strcmp(argument->name, "result") == 0) {
            return abi_schema_error(
                context,
                child_index,
                "%s uses reserved generated-local name '%s'",
                location,
                argument->name);
        }

        if (argument->register_class == PORPOISE_ABI_REGISTER_GPR) {
            if (used_gprs[argument->register_index]) {
                return abi_schema_error(
                    context,
                    child_index,
                    "%s duplicates register r%u",
                    location,
                    argument->register_index);
            }
            if (have_gpr && argument->register_index <= previous_gpr) {
                return abi_schema_error(
                    context,
                    child_index,
                    "%s has an out-of-order GPR mapping",
                    location);
            }
            used_gprs[argument->register_index] = true;
            previous_gpr = argument->register_index;
            have_gpr = true;
        } else {
            if (used_fprs[argument->register_index]) {
                return abi_schema_error(
                    context,
                    child_index,
                    "%s duplicates register f%u",
                    location,
                    argument->register_index);
            }
            if (have_fpr && argument->register_index <= previous_fpr) {
                return abi_schema_error(
                    context,
                    child_index,
                    "%s has an out-of-order FPR mapping",
                    location);
            }
            used_fprs[argument->register_index] = true;
            previous_fpr = argument->register_index;
            have_fpr = true;
        }

        if (argument->name != NULL) {
            size_t earlier;
            for (earlier = 0U; earlier < (size_t)argument_index; earlier++) {
                const char *previous_name = function->arguments[earlier].name;
                if (previous_name != NULL &&
                    strcmp(previous_name, argument->name) == 0) {
                    return abi_schema_error(
                        context,
                        child_index,
                        "%s duplicates argument name '%s'",
                        location,
                        argument->name);
                }
            }
        }

        child_index = abi_token_after(context, child_index);
        if (child_index < 0) {
            return abi_schema_error(
                context,
                array_index,
                "functions[%lu].arguments has an invalid token structure",
                (unsigned long)function_index);
        }
    }
    return true;
}

static char *abi_duplicate_string(const char *value) {
    size_t length = strlen(value) + 1U;
    char *copy = (char *)malloc(length);

    if (copy != NULL) {
        memcpy(copy, value, length);
    }
    return copy;
}

static bool abi_parse_function(AbiParseContext *context,
                               int object_index,
                               PorpoiseAbiFunction *function,
                               size_t function_index) {
    const jsmntok_t *object = &context->tokens[object_index];
    unsigned int seen = 0U;
    int key_index;
    int pair;

    if (object->type != JSMN_OBJECT) {
        return abi_schema_error(
            context,
            object_index,
            "functions[%lu] must be an object",
            (unsigned long)function_index);
    }

    key_index = object_index + 1;
    for (pair = 0; pair < object->size; pair++) {
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int key_flag;

        if (!abi_decode_owned_string(
                context,
                key_index,
                "function key",
                &key)) {
            return false;
        }
        if (strcmp(key, "kind") == 0) {
            key_flag = ABI_FUNCTION_KIND;
        } else if (strcmp(key, "symbol") == 0) {
            key_flag = ABI_FUNCTION_SYMBOL;
        } else if (strcmp(key, "wrapper") == 0) {
            key_flag = ABI_FUNCTION_WRAPPER;
        } else if (strcmp(key, "header") == 0) {
            key_flag = ABI_FUNCTION_HEADER;
        } else if (strcmp(key, "adapter") == 0) {
            key_flag = ABI_FUNCTION_ADAPTER;
        } else if (strcmp(key, "return") == 0) {
            key_flag = ABI_FUNCTION_RETURN;
        } else if (strcmp(key, "arguments") == 0) {
            key_flag = ABI_FUNCTION_ARGUMENTS;
        } else {
            bool result = abi_schema_error(
                context,
                key_index,
                "unknown key '%s' in functions[%lu]",
                key,
                (unsigned long)function_index);
            free(key);
            return result;
        }

        if ((seen & key_flag) != 0U) {
            bool result = abi_schema_error(
                context,
                key_index,
                "duplicate key '%s' in functions[%lu]",
                key,
                (unsigned long)function_index);
            free(key);
            return result;
        }
        seen |= key_flag;
        free(key);

        if (key_flag == ABI_FUNCTION_KIND) {
            if (!abi_parse_kind(context, value_index, &function->kind)) {
                return false;
            }
        } else if (key_flag == ABI_FUNCTION_SYMBOL) {
            if (!abi_nonempty_string(
                    context,
                    value_index,
                    "function symbol",
                    &function->symbol)) {
                return false;
            }
            if (!abi_symbol_is_valid(function->symbol)) {
                return abi_schema_error(
                    context,
                    value_index,
                    "function symbol must not contain whitespace or control characters");
            }
        } else if (key_flag == ABI_FUNCTION_WRAPPER) {
            if (!abi_parse_identifier_string(
                    context,
                    value_index,
                    "function wrapper",
                    &function->wrapper)) {
                return false;
            }
        } else if (key_flag == ABI_FUNCTION_HEADER) {
            if (!abi_nonempty_string(
                    context,
                    value_index,
                    "function header",
                    &function->header)) {
                return false;
            }
            if (!abi_header_is_valid(function->header)) {
                return abi_schema_error(
                    context,
                    value_index,
                    "function header must not contain quotes, angle brackets, or control characters");
            }
        } else if (key_flag == ABI_FUNCTION_ADAPTER) {
            if (!abi_parse_identifier_string(
                    context,
                    value_index,
                    "function adapter",
                    &function->adapter)) {
                return false;
            }
        } else if (key_flag == ABI_FUNCTION_RETURN) {
            char location[64];

            (void)snprintf(
                location,
                sizeof(location),
                "functions[%lu].return",
                (unsigned long)function_index);
            if (!abi_parse_value(
                    context,
                    value_index,
                    true,
                    &function->result,
                    location)) {
                return false;
            }
        } else if (key_flag == ABI_FUNCTION_ARGUMENTS) {
            if (!abi_parse_arguments(
                    context,
                    value_index,
                    function,
                    function_index)) {
                return false;
            }
        }

        key_index = abi_token_after(context, key_index);
        if (key_index < 0) {
            return abi_schema_error(
                context,
                object_index,
                "functions[%lu] has an invalid token structure",
                (unsigned long)function_index);
        }
    }

    if ((seen & ABI_FUNCTION_KIND) == 0U) {
        return abi_schema_error(
            context,
            object_index,
            "functions[%lu] is missing required key 'kind'",
            (unsigned long)function_index);
    }
    if ((seen & ABI_FUNCTION_SYMBOL) == 0U) {
        return abi_schema_error(
            context,
            object_index,
            "functions[%lu] is missing required key 'symbol'",
            (unsigned long)function_index);
    }
    if ((seen & ABI_FUNCTION_HEADER) == 0U) {
        return abi_schema_error(
            context,
            object_index,
            "functions[%lu] is missing required key 'header'",
            (unsigned long)function_index);
    }
    if ((seen & ABI_FUNCTION_RETURN) == 0U) {
        return abi_schema_error(
            context,
            object_index,
            "functions[%lu] is missing required key 'return'",
            (unsigned long)function_index);
    }
    if ((seen & ABI_FUNCTION_ARGUMENTS) == 0U) {
        return abi_schema_error(
            context,
            object_index,
            "functions[%lu] is missing required key 'arguments'",
            (unsigned long)function_index);
    }

    if (function->kind == PORPOISE_ABI_IMPORT &&
        (seen & ABI_FUNCTION_ADAPTER) != 0U) {
        if ((seen & ABI_FUNCTION_WRAPPER) != 0U) {
            return abi_schema_error(
                context,
                object_index,
                "adapter import functions[%lu] must omit 'wrapper'",
                (unsigned long)function_index);
        }
        return true;
    }

    if (function->kind == PORPOISE_ABI_IMPORT) {
        if (function->wrapper == NULL) {
            if (!abi_is_c_identifier(function->symbol)) {
                return abi_schema_error(
                    context,
                    object_index,
                    "direct import functions[%lu] must provide 'wrapper' when its symbol is not a C99 identifier",
                    (unsigned long)function_index);
            }
            function->wrapper = abi_duplicate_string(function->symbol);
            if (function->wrapper == NULL) {
                return abi_out_of_memory(
                    context,
                    object_index,
                    "defaulting an import wrapper to its symbol");
            }
        }
        return true;
    }

    if ((seen & ABI_FUNCTION_ADAPTER) != 0U) {
        return abi_schema_error(
            context,
            object_index,
            "export functions[%lu] must not declare an adapter",
            (unsigned long)function_index);
    }
    if ((seen & ABI_FUNCTION_WRAPPER) == 0U) {
        return abi_schema_error(
            context,
            object_index,
            "export functions[%lu] is missing required key 'wrapper'",
            (unsigned long)function_index);
    }
    return true;
}

static bool abi_validate_unique_functions(AbiParseContext *context,
                                          PorpoiseAbiManifest *manifest,
                                          const int *function_tokens) {
    size_t index;

    for (index = 0U; index < manifest->function_count; index++) {
        size_t previous;

        for (previous = 0U; previous < index; previous++) {
            if (strcmp(
                    manifest->functions[previous].symbol,
                    manifest->functions[index].symbol) == 0) {
                return abi_schema_error(
                    context,
                    function_tokens[index],
                    "duplicate ABI function symbol '%s'",
                    manifest->functions[index].symbol);
            }
            if (manifest->functions[index].kind == PORPOISE_ABI_EXPORT &&
                manifest->functions[previous].kind == PORPOISE_ABI_EXPORT &&
                strcmp(
                    manifest->functions[previous].wrapper,
                    manifest->functions[index].wrapper) == 0) {
                return abi_schema_error(
                    context,
                    function_tokens[index],
                    "duplicate ABI export wrapper '%s'",
                    manifest->functions[index].wrapper);
            }
        }
    }
    return true;
}

static bool abi_parse_functions(AbiParseContext *context,
                                int array_index,
                                PorpoiseAbiManifest *manifest) {
    const jsmntok_t *array = &context->tokens[array_index];
    int *function_tokens = NULL;
    int function_index;
    int child_index;
    bool success = false;

    if (array->type != JSMN_ARRAY) {
        return abi_schema_error(
            context,
            array_index,
            "ABI manifest key 'functions' must be an array");
    }
    if ((size_t)array->size > SIZE_MAX / sizeof(*manifest->functions) ||
        (size_t)array->size > SIZE_MAX / sizeof(*function_tokens)) {
        return abi_out_of_memory(
            context,
            array_index,
            "allocating ABI functions");
    }

    manifest->function_count = (size_t)array->size;
    if (manifest->function_count != 0U) {
        manifest->functions = (PorpoiseAbiFunction *)calloc(
            manifest->function_count,
            sizeof(*manifest->functions));
        function_tokens = (int *)malloc(
            manifest->function_count * sizeof(*function_tokens));
        if (manifest->functions == NULL || function_tokens == NULL) {
            abi_out_of_memory(
                context,
                array_index,
                "allocating ABI functions");
            goto finished;
        }
    }

    child_index = array_index + 1;
    for (function_index = 0; function_index < array->size; function_index++) {
        function_tokens[(size_t)function_index] = child_index;
        if (!abi_parse_function(
                context,
                child_index,
                &manifest->functions[(size_t)function_index],
                (size_t)function_index)) {
            goto finished;
        }
        child_index = abi_token_after(context, child_index);
        if (child_index < 0) {
            abi_schema_error(
                context,
                array_index,
                "ABI functions array has an invalid token structure");
            goto finished;
        }
    }

    if (!abi_validate_unique_functions(
            context,
            manifest,
            function_tokens)) {
        goto finished;
    }
    success = true;

finished:
    free(function_tokens);
    return success;
}

static bool abi_parse_manifest(AbiParseContext *context,
                               PorpoiseAbiManifest *manifest) {
    const jsmntok_t *root = &context->tokens[0];
    unsigned int seen = 0U;
    int key_index;
    int pair;

    if (root->type != JSMN_OBJECT) {
        return abi_schema_error(
            context,
            0,
            "ABI manifest root must be an object");
    }

    key_index = 1;
    for (pair = 0; pair < root->size; pair++) {
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int key_flag;

        if (!abi_decode_owned_string(
                context,
                key_index,
                "ABI manifest key",
                &key)) {
            return false;
        }
        if (strcmp(key, "schema_version") == 0) {
            key_flag = ABI_ROOT_SCHEMA_VERSION;
        } else if (strcmp(key, "functions") == 0) {
            key_flag = ABI_ROOT_FUNCTIONS;
        } else {
            bool result = abi_schema_error(
                context,
                key_index,
                "unknown key '%s' in ABI manifest root",
                key);
            free(key);
            return result;
        }

        if ((seen & key_flag) != 0U) {
            bool result = abi_schema_error(
                context,
                key_index,
                "duplicate key '%s' in ABI manifest root",
                key);
            free(key);
            return result;
        }
        seen |= key_flag;
        free(key);

        if (key_flag == ABI_ROOT_SCHEMA_VERSION) {
            if (context->tokens[value_index].type != JSMN_PRIMITIVE ||
                !abi_token_is(context, value_index, "1")) {
                return abi_schema_error(
                    context,
                    value_index,
                    "ABI manifest requires numeric schema_version 1");
            }
        } else if (!abi_parse_functions(
                       context,
                       value_index,
                       manifest)) {
            return false;
        }

        key_index = abi_token_after(context, key_index);
        if (key_index < 0) {
            return abi_schema_error(
                context,
                0,
                "ABI manifest root has an invalid token structure");
        }
    }

    if ((seen & ABI_ROOT_SCHEMA_VERSION) == 0U) {
        return abi_schema_error(
            context,
            0,
            "ABI manifest is missing required key 'schema_version'");
    }
    if ((seen & ABI_ROOT_FUNCTIONS) == 0U) {
        return abi_schema_error(
            context,
            0,
            "ABI manifest is missing required key 'functions'");
    }
    return true;
}

static bool abi_add_file_diagnostic(PorpoiseDiagnostics *diagnostics,
                                    const char *path,
                                    const char *format,
                                    ...) {
    char message[PORPOISE_MESSAGE_CAPACITY];
    va_list arguments;
    int written;

    if (diagnostics == NULL) {
        return true;
    }
    va_start(arguments, format);
    written = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (written < 0) {
        return false;
    }
    return porpoise_diagnostics_add(
        diagnostics,
        PORPOISE_SEVERITY_ERROR,
        path,
        0U,
        0U,
        "%s",
        message);
}

static int abi_read_file(const char *path,
                         PorpoiseDiagnostics *diagnostics,
                         char **contents_out,
                         size_t *length_out) {
    FILE *file;
    char *contents;
    size_t length = 0U;
    size_t capacity = ABI_JSON_INITIAL_CAPACITY;

    file = fopen(path, "rb");
    if (file == NULL) {
        int saved_errno = errno;
        if (!abi_add_file_diagnostic(
                diagnostics,
                path,
                "cannot open ABI manifest: %s",
                strerror(saved_errno))) {
            return PORPOISE_EXIT_INTERNAL;
        }
        return PORPOISE_EXIT_IO;
    }

    contents = (char *)malloc(capacity + 1U);
    if (contents == NULL) {
        fclose(file);
        (void)abi_add_file_diagnostic(
            diagnostics,
            path,
            "out of memory while reading ABI manifest");
        return PORPOISE_EXIT_INTERNAL;
    }

    for (;;) {
        size_t amount;

        if (length == capacity) {
            size_t next_capacity;
            char *larger;

            if (capacity >= (size_t)INT_MAX) {
                free(contents);
                fclose(file);
                if (!abi_add_file_diagnostic(
                        diagnostics,
                        path,
                        "ABI manifest exceeds the JSON parser size limit")) {
                    return PORPOISE_EXIT_INTERNAL;
                }
                return PORPOISE_EXIT_USAGE;
            }
            next_capacity = capacity > (size_t)INT_MAX / 2U
                ? (size_t)INT_MAX
                : capacity * 2U;
            larger = (char *)realloc(contents, next_capacity + 1U);
            if (larger == NULL) {
                free(contents);
                fclose(file);
                (void)abi_add_file_diagnostic(
                    diagnostics,
                    path,
                    "out of memory while reading ABI manifest");
                return PORPOISE_EXIT_INTERNAL;
            }
            contents = larger;
            capacity = next_capacity;
        }

        amount = fread(contents + length, 1U, capacity - length, file);
        length += amount;
        if (amount == 0U) {
            if (ferror(file)) {
                int saved_errno = errno;
                free(contents);
                fclose(file);
                if (!abi_add_file_diagnostic(
                        diagnostics,
                        path,
                        "failed to read ABI manifest: %s",
                        strerror(saved_errno))) {
                    return PORPOISE_EXIT_INTERNAL;
                }
                return PORPOISE_EXIT_IO;
            }
            break;
        }
    }

    if (fclose(file) != 0) {
        int saved_errno = errno;
        free(contents);
        if (!abi_add_file_diagnostic(
                diagnostics,
                path,
                "failed to close ABI manifest: %s",
                strerror(saved_errno))) {
            return PORPOISE_EXIT_INTERNAL;
        }
        return PORPOISE_EXIT_IO;
    }

    contents[length] = '\0';
    *contents_out = contents;
    *length_out = length;
    return PORPOISE_EXIT_OK;
}

void porpoise_abi_init(PorpoiseAbiManifest *manifest) {
    if (manifest != NULL) {
        memset(manifest, 0, sizeof(*manifest));
    }
}

void porpoise_abi_free(PorpoiseAbiManifest *manifest) {
    if (manifest == NULL) {
        return;
    }
    if (manifest->functions != NULL) {
        size_t function_index;
        for (function_index = 0U;
             function_index < manifest->function_count;
            function_index++) {
            PorpoiseAbiFunction *function = &manifest->functions[function_index];

            free(function->symbol);
            free(function->wrapper);
            free(function->header);
            free(function->adapter);
            free(function->result.name);
            if (function->arguments != NULL) {
                size_t argument_index;
                for (argument_index = 0U;
                     argument_index < function->argument_count;
                     argument_index++) {
                    free(function->arguments[argument_index].name);
                }
            }
            free(function->arguments);
        }
    }
    free(manifest->functions);
    memset(manifest, 0, sizeof(*manifest));
}

int porpoise_abi_load(PorpoiseAbiManifest *manifest,
                      const char *path,
                      PorpoiseDiagnostics *diagnostics) {
    char *json = NULL;
    size_t json_length = 0U;
    jsmn_parser parser;
    jsmntok_t *tokens = NULL;
    int token_count;
    int parsed_count;
    int result;
    AbiParseContext context;
    PorpoiseAbiManifest parsed_manifest;

    if (manifest == NULL || path == NULL) {
        if (!abi_add_file_diagnostic(
                diagnostics,
                path == NULL ? "" : path,
                "internal error: invalid ABI load arguments")) {
            return PORPOISE_EXIT_INTERNAL;
        }
        return PORPOISE_EXIT_INTERNAL;
    }

    result = abi_read_file(path, diagnostics, &json, &json_length);
    if (result != PORPOISE_EXIT_OK) {
        return result;
    }

    jsmn_init(&parser);
    token_count = jsmn_parse(&parser, json, json_length, NULL, 0U);
    if (token_count <= 0) {
        if (!abi_add_file_diagnostic(
                diagnostics,
                path,
                "ABI manifest is not valid JSON")) {
            result = PORPOISE_EXIT_INTERNAL;
        } else {
            result = PORPOISE_EXIT_USAGE;
        }
        goto finished;
    }
    if ((size_t)token_count > SIZE_MAX / sizeof(*tokens)) {
        (void)abi_add_file_diagnostic(
            diagnostics,
            path,
            "out of memory while parsing ABI manifest");
        result = PORPOISE_EXIT_INTERNAL;
        goto finished;
    }

    tokens = (jsmntok_t *)calloc((size_t)token_count, sizeof(*tokens));
    if (tokens == NULL) {
        (void)abi_add_file_diagnostic(
            diagnostics,
            path,
            "out of memory while parsing ABI manifest");
        result = PORPOISE_EXIT_INTERNAL;
        goto finished;
    }

    jsmn_init(&parser);
    parsed_count = jsmn_parse(
        &parser,
        json,
        json_length,
        tokens,
        (unsigned int)token_count);
    if (parsed_count != token_count) {
        if (!abi_add_file_diagnostic(
                diagnostics,
                path,
                "ABI manifest is not valid JSON")) {
            result = PORPOISE_EXIT_INTERNAL;
        } else {
            result = PORPOISE_EXIT_USAGE;
        }
        goto finished;
    }

    memset(&context, 0, sizeof(context));
    context.path = path;
    context.json = json;
    context.json_length = json_length;
    context.tokens = tokens;
    context.token_count = token_count;
    context.diagnostics = diagnostics;
    porpoise_abi_init(&parsed_manifest);

    if (!abi_validate_json_document(&context) ||
        !abi_parse_manifest(&context, &parsed_manifest)) {
        porpoise_abi_free(&parsed_manifest);
        result = context.status == ABI_PARSE_OUT_OF_MEMORY
            ? PORPOISE_EXIT_INTERNAL
            : PORPOISE_EXIT_USAGE;
        goto finished;
    }

    porpoise_abi_free(manifest);
    *manifest = parsed_manifest;
    result = PORPOISE_EXIT_OK;

finished:
    free(tokens);
    free(json);
    return result;
}

static bool abi_nullable_string_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL) return left == right;
    return strcmp(left, right) == 0;
}

static bool abi_value_equal(
    const PorpoiseAbiValue *left,
    const PorpoiseAbiValue *right) {
    return left->type == right->type &&
           left->register_class == right->register_class &&
           left->register_index == right->register_index &&
           abi_nullable_string_equal(left->name, right->name);
}

static bool abi_function_equal(
    const PorpoiseAbiFunction *left,
    const PorpoiseAbiFunction *right) {
    size_t index;
    if (left->kind != right->kind ||
        !abi_nullable_string_equal(left->symbol, right->symbol) ||
        !abi_nullable_string_equal(left->wrapper, right->wrapper) ||
        !abi_nullable_string_equal(left->header, right->header) ||
        !abi_nullable_string_equal(left->adapter, right->adapter) ||
        !abi_value_equal(&left->result, &right->result) ||
        left->argument_count != right->argument_count) {
        return false;
    }
    for (index = 0U; index < left->argument_count; index++) {
        if (!abi_value_equal(
                &left->arguments[index], &right->arguments[index])) {
            return false;
        }
    }
    return true;
}

static bool abi_clone_value(
    PorpoiseAbiValue *destination,
    const PorpoiseAbiValue *source) {
    *destination = *source;
    destination->name = NULL;
    if (source->name != NULL) {
        destination->name = abi_duplicate_string(source->name);
        if (destination->name == NULL) return false;
    }
    return true;
}

static bool abi_clone_function(
    PorpoiseAbiFunction *destination,
    const PorpoiseAbiFunction *source) {
    size_t index;
    memset(destination, 0, sizeof(*destination));
    destination->kind = source->kind;
    destination->symbol = source->symbol == NULL
        ? NULL : abi_duplicate_string(source->symbol);
    destination->wrapper = source->wrapper == NULL
        ? NULL : abi_duplicate_string(source->wrapper);
    destination->header = source->header == NULL
        ? NULL : abi_duplicate_string(source->header);
    destination->adapter = source->adapter == NULL
        ? NULL : abi_duplicate_string(source->adapter);
    if ((source->symbol != NULL && destination->symbol == NULL) ||
        (source->wrapper != NULL && destination->wrapper == NULL) ||
        (source->header != NULL && destination->header == NULL) ||
        (source->adapter != NULL && destination->adapter == NULL) ||
        !abi_clone_value(&destination->result, &source->result)) {
        return false;
    }
    destination->argument_count = source->argument_count;
    if (source->argument_count == 0U) return true;
    if (source->argument_count > SIZE_MAX / sizeof(*destination->arguments)) {
        return false;
    }
    destination->arguments = (PorpoiseAbiValue *)calloc(
        source->argument_count, sizeof(*destination->arguments));
    if (destination->arguments == NULL) return false;
    for (index = 0U; index < source->argument_count; index++) {
        if (!abi_clone_value(
                &destination->arguments[index], &source->arguments[index])) {
            return false;
        }
    }
    return true;
}

int porpoise_abi_merge(
    PorpoiseAbiManifest *destination,
    const PorpoiseAbiManifest *source,
    const char *source_identity,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseAbiManifest merged;
    size_t source_index;
    size_t capacity;
    if (destination == NULL || source == NULL) {
        (void)abi_add_file_diagnostic(
            diagnostics, source_identity == NULL ? "" : source_identity,
            "internal error: invalid ABI merge arguments");
        return PORPOISE_EXIT_INTERNAL;
    }
    if (destination->function_count >
        SIZE_MAX - source->function_count) {
        return PORPOISE_EXIT_INTERNAL;
    }
    capacity = destination->function_count + source->function_count;
    porpoise_abi_init(&merged);
    if (capacity != 0U) {
        merged.functions = (PorpoiseAbiFunction *)calloc(
            capacity, sizeof(*merged.functions));
        if (merged.functions == NULL) return PORPOISE_EXIT_INTERNAL;
    }
    for (source_index = 0U;
         source_index < destination->function_count;
         source_index++) {
        PorpoiseAbiFunction *slot =
            &merged.functions[merged.function_count++];
        if (!abi_clone_function(
                slot,
                &destination->functions[source_index])) {
            porpoise_abi_free(&merged);
            (void)abi_add_file_diagnostic(
                diagnostics,
                source_identity == NULL ? "" : source_identity,
                "out of memory while merging ABI contracts");
            return PORPOISE_EXIT_INTERNAL;
        }
    }
    for (source_index = 0U;
         source_index < source->function_count;
         source_index++) {
        const PorpoiseAbiFunction *incoming =
            &source->functions[source_index];
        size_t existing_index;
        const PorpoiseAbiFunction *existing = NULL;
        for (existing_index = 0U;
             existing_index < merged.function_count;
             existing_index++) {
            if (strcmp(
                    merged.functions[existing_index].symbol,
                    incoming->symbol) == 0) {
                existing = &merged.functions[existing_index];
                break;
            }
        }
        if (existing != NULL) {
            if (abi_function_equal(existing, incoming)) continue;
            porpoise_abi_free(&merged);
            (void)abi_add_file_diagnostic(
                diagnostics,
                source_identity == NULL ? "" : source_identity,
                "ABI contract for symbol '%s' conflicts with an earlier contract",
                incoming->symbol);
            return PORPOISE_EXIT_USAGE;
        }
        {
            PorpoiseAbiFunction *slot =
                &merged.functions[merged.function_count++];
            if (abi_clone_function(slot, incoming)) continue;
            porpoise_abi_free(&merged);
            (void)abi_add_file_diagnostic(
                diagnostics,
                source_identity == NULL ? "" : source_identity,
                "out of memory while merging ABI contracts");
            return PORPOISE_EXIT_INTERNAL;
        }
    }
    porpoise_abi_free(destination);
    *destination = merged;
    return PORPOISE_EXIT_OK;
}

int porpoise_abi_load_additive(
    PorpoiseAbiManifest *manifest,
    const char *path,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseAbiManifest loaded;
    int result;
    if (manifest == NULL || path == NULL) return PORPOISE_EXIT_INTERNAL;
    porpoise_abi_init(&loaded);
    result = porpoise_abi_load(&loaded, path, diagnostics);
    if (result == PORPOISE_EXIT_OK) {
        result = porpoise_abi_merge(
            manifest, &loaded, path, diagnostics);
    }
    porpoise_abi_free(&loaded);
    return result;
}

const PorpoiseAbiFunction *porpoise_abi_find_import(
    const PorpoiseAbiManifest *manifest,
    const char *symbol) {
    size_t index;

    if (manifest == NULL || symbol == NULL) {
        return NULL;
    }
    for (index = 0U; index < manifest->function_count; index++) {
        const PorpoiseAbiFunction *function = &manifest->functions[index];
        if (function->kind == PORPOISE_ABI_IMPORT &&
            strcmp(function->symbol, symbol) == 0) {
            return function;
        }
    }
    return NULL;
}

const char *porpoise_abi_type_name(PorpoiseAbiType type) {
    switch (type) {
        case PORPOISE_ABI_VOID:
            return "void";
        case PORPOISE_ABI_U8:
            return "u8";
        case PORPOISE_ABI_U16:
            return "u16";
        case PORPOISE_ABI_U32:
            return "u32";
        case PORPOISE_ABI_S8:
            return "s8";
        case PORPOISE_ABI_S16:
            return "s16";
        case PORPOISE_ABI_S32:
            return "s32";
        case PORPOISE_ABI_F32:
            return "f32";
        case PORPOISE_ABI_F64:
            return "f64";
        case PORPOISE_ABI_POINTER:
            return "pointer";
        default:
            return "unknown";
    }
}
