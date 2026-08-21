#include "asm_data_internal.h"

#include "porpoise/util.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PorpoiseConcreteRange {
    uint32_t address;
    uint32_t size;
    size_t file_index;
    size_t object_index;
    size_t source_line;
} PorpoiseConcreteRange;

static const char *skip_space(const char *cursor) {
    while (isspace((unsigned char)*cursor)) cursor++;
    return cursor;
}

static bool is_directive(const char *line, const char *directive) {
    size_t length = strlen(directive);
    line = skip_space(line);
    return strncmp(line, directive, length) == 0 &&
           (line[length] == '\0' || line[length] == ',' ||
            isspace((unsigned char)line[length]));
}

static bool parse_u32_literal(const char *text, uint32_t *value_out) {
    char *end = NULL;
    unsigned long value;
    text = skip_space(text);
    if (*text == '\0' || *text == '-') return false;
    errno = 0;
    value = strtoul(text, &end, 0);
    if (errno == ERANGE || end == text) return false;
    end = (char *)skip_space(end);
    if (*end != '\0') return false;
#if ULONG_MAX > UINT32_MAX
    if (value > UINT32_MAX) return false;
#endif
    *value_out = (uint32_t)value;
    return true;
}

static bool parse_u64_literal(const char *text, uint64_t *value_out) {
    char *end = NULL;
    unsigned long long value;
    text = skip_space(text);
    if (*text == '\0' || *text == '-') return false;
    errno = 0;
    value = strtoull(text, &end, 0);
    if (errno == ERANGE || end == text || *skip_space(end) != '\0') {
        return false;
    }
    *value_out = (uint64_t)value;
    return true;
}

static bool copy_normalized_section(
    char *destination,
    size_t capacity,
    const char *start,
    size_t length) {
    size_t output_length = length + (start[0] == '.' ? 0U : 1U);
    if (length == 0U || output_length >= capacity) return false;
    if (start[0] != '.') {
        destination[0] = '.';
        memcpy(destination + 1U, start, length);
    } else {
        memcpy(destination, start, length);
    }
    destination[output_length] = '\0';
    return true;
}

void porpoise_asm_data_parser_init(PorpoiseAsmDataParser *parser) {
    memset(parser, 0, sizeof(*parser));
}

bool porpoise_asm_data_accepts_annotated_words(
    const PorpoiseAsmDataParser *parser) {
    return parser != NULL && parser->executable_symbol_data;
}

void porpoise_asm_data_begin_function(PorpoiseAsmDataParser *parser) {
    if (parser == NULL) return;
    parser->executable_symbol_data = false;
    parser->have_metadata = false;
}

PorpoiseAsmDataLineResult porpoise_asm_data_parse_metadata(
    PorpoiseAsmDataParser *parser,
    const PorpoiseSourceFile *file,
    const char *line,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics,
    bool *recognized) {
    char text[PORPOISE_MESSAGE_CAPACITY];
    char *cursor;
    char *section_start;
    char *section_end;
    char *separator;
    char *size_marker;
    uint32_t section_offset;
    uint32_t address;
    uint32_t size;
    uint64_t end_address;

    *recognized = false;
    line = skip_space(line);
    if (*line != '#') return PORPOISE_ASM_DATA_NOT_HANDLED;
    line = skip_space(line + 1U);
    if (strchr(line, '|') == NULL || strstr(line, "size:") == NULL) {
        return PORPOISE_ASM_DATA_NOT_HANDLED;
    }
    {
        const char *first_separator = strchr(line, '|');
        const char *first_colon = strchr(line, ':');
        const char *range_separator = strstr(line, "..");
        bool object_metadata =
            first_colon != NULL && first_colon < first_separator &&
            (*line == '.' || *line == '_' ||
             isalpha((unsigned char)*line));
        bool range_metadata =
            range_separator != NULL && range_separator < first_separator;
        if (!object_metadata && !range_metadata) {
            return PORPOISE_ASM_DATA_NOT_HANDLED;
        }
        if (range_metadata && !object_metadata) {
            char range_text[PORPOISE_MESSAGE_CAPACITY];
            char *range_cursor;
            char *dots;
            char *separator;
            char *range_size_marker;
            uint64_t start;
            uint64_t end;
            uint32_t declared_size;
            *recognized = true;
            if (!porpoise_copy_string(
                    range_text, sizeof(range_text), line)) goto malformed;
            range_cursor = range_text;
            dots = strstr(range_cursor, "..");
            separator = strchr(range_cursor, '|');
            if (dots == NULL || separator == NULL || dots > separator) {
                goto malformed;
            }
            *dots = '\0';
            *separator = '\0';
            if (!parse_u64_literal(range_cursor, &start) ||
                !parse_u64_literal(dots + 2U, &end)) goto malformed;
            range_size_marker = strstr(separator + 1U, "size:");
            if (range_size_marker == NULL ||
                !parse_u32_literal(
                    range_size_marker + 5U, &declared_size) ||
                start > UINT32_MAX || end < start ||
                end > UINT64_C(0x100000000) ||
                end - start != declared_size) {
                goto malformed;
            }
            if (parser->current_object != NULL) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                    source_line, (uint32_t)start,
                    "section contribution range appears inside data object %s",
                    parser->current_object->name);
                return PORPOISE_ASM_DATA_ERROR;
            }
            parser->range_address = (uint32_t)start;
            parser->range_size = declared_size;
            parser->range_source_line = source_line;
            parser->have_range = true;
            parser->active_anonymous = NULL;
            parser->contribution_offset = 0U;
            return PORPOISE_ASM_DATA_HANDLED;
        }
    }
    *recognized = true;
    if (!porpoise_copy_string(text, sizeof(text), line)) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line, 0U,
            "assembly contribution metadata is too long");
        return PORPOISE_ASM_DATA_ERROR;
    }
    cursor = text;
    section_start = cursor;
    section_end = strchr(section_start, ':');
    if (section_end == NULL) goto malformed;
    while (section_end > section_start &&
           isspace((unsigned char)section_end[-1])) section_end--;
    if (!copy_normalized_section(
            parser->metadata.section, sizeof(parser->metadata.section),
            section_start, (size_t)(section_end - section_start))) {
        goto malformed;
    }
    cursor = strchr(cursor, ':') + 1U;
    separator = strchr(cursor, '|');
    if (separator == NULL) goto malformed;
    *separator = '\0';
    if (!parse_u32_literal(cursor, &section_offset)) goto malformed;
    cursor = separator + 1U;
    separator = strchr(cursor, '|');
    if (separator == NULL) goto malformed;
    *separator = '\0';
    if (!parse_u32_literal(cursor, &address)) goto malformed;
    cursor = separator + 1U;
    size_marker = strstr(cursor, "size:");
    if (size_marker == NULL) goto malformed;
    {
        char *prefix = cursor;
        while (prefix < size_marker && isspace((unsigned char)*prefix)) prefix++;
        if (prefix != size_marker) goto malformed;
    }
    if (!parse_u32_literal(size_marker + 5U, &size)) goto malformed;
    if (address < section_offset) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
            address,
            "section offset 0x%08lX exceeds object address",
            (unsigned long)section_offset);
        return PORPOISE_ASM_DATA_ERROR;
    }
    end_address = (uint64_t)address + (uint64_t)size;
    if (end_address > UINT64_C(0x100000000)) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
            address, "data object crosses the 32-bit address boundary");
        return PORPOISE_ASM_DATA_ERROR;
    }
    if (parser->current_object != NULL) {
        const PorpoiseDataObject *object = parser->current_object;
        uint64_t expected_address =
            (uint64_t)object->address + parser->current_offset;
        uint64_t object_end = (uint64_t)object->address + object->size;

        /*
         * DTK emits a zero-size contribution immediately before a `.sym`
         * inside an owning `.obj`.  It is a location declaration, not a
         * nested object and not an attempt to emit bytes.
         */
        if (size != 0U || strcmp(parser->metadata.section, object->section) != 0 ||
            address < object->address || (uint64_t)address > object_end ||
            (uint64_t)address != expected_address) {
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                source_line, address,
                "contribution metadata appears inside data object %s",
                object->name);
            return PORPOISE_ASM_DATA_ERROR;
        }
    }
    parser->metadata.section_offset = section_offset;
    parser->metadata.address = address;
    parser->metadata.size = size;
    parser->metadata.source_line = source_line;
    parser->have_metadata = true;
    return PORPOISE_ASM_DATA_HANDLED;

malformed:
    porpoise_diagnostics_add(
        diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line, 0U,
        "malformed assembly contribution metadata");
    return PORPOISE_ASM_DATA_ERROR;
}

static bool parse_quoted_name(
    const char **cursor_io,
    char *name,
    size_t capacity) {
    const char *cursor = skip_space(*cursor_io);
    size_t length = 0U;
    if (*cursor == '"') {
        cursor++;
        while (*cursor != '\0' && *cursor != '"') {
            unsigned char value = (unsigned char)*cursor++;
            if (value == '\\') {
                if (*cursor != '\\' && *cursor != '"') return false;
                value = (unsigned char)*cursor++;
            }
            if (length + 1U >= capacity) return false;
            name[length++] = (char)value;
        }
        if (*cursor != '"') return false;
        cursor++;
    } else {
        while (*cursor != '\0' && *cursor != ',' && *cursor != ':' &&
               !isspace((unsigned char)*cursor)) {
            if (length + 1U >= capacity) return false;
            name[length++] = *cursor++;
        }
    }
    if (length == 0U) return false;
    name[length] = '\0';
    *cursor_io = cursor;
    return true;
}

static bool parse_object_declaration(
    const char *line,
    const char *directive,
    char *name,
    size_t capacity,
    bool *is_global) {
    const char *cursor = skip_space(line) + strlen(directive);
    if (!parse_quoted_name(&cursor, name, capacity)) return false;
    cursor = skip_space(cursor);
    if (*cursor != ',') return false;
    cursor = skip_space(cursor + 1U);
    if (strcmp(cursor, "global") == 0 || strcmp(cursor, "weak") == 0) {
        *is_global = true;
        return true;
    }
    if (strcmp(cursor, "local") == 0) {
        *is_global = false;
        return true;
    }
    return false;
}

static PorpoiseDataAlias *add_data_alias(
    PorpoiseSourceFile *file,
    const PorpoiseAsmDataMetadata *metadata,
    const char *name,
    bool is_global,
    size_t source_line) {
    PorpoiseDataAlias candidate;
    PorpoiseDataAlias *alias;

    memset(&candidate, 0, sizeof(candidate));
    candidate.name = porpoise_strdup(name);
    candidate.section = porpoise_strdup(metadata->section);
    candidate.is_global = is_global;
    candidate.source_line = source_line;
    candidate.address = metadata->address;
    if (candidate.name == NULL || candidate.section == NULL ||
        file->data_alias_count == SIZE_MAX ||
        !porpoise_grow_array(
            (void **)&file->data_aliases, &file->data_alias_capacity,
            sizeof(*file->data_aliases), file->data_alias_count + 1U)) {
        free(candidate.name);
        free(candidate.section);
        return NULL;
    }
    alias = &file->data_aliases[file->data_alias_count++];
    *alias = candidate;
    return alias;
}

static bool parse_end_object(
    const char *line,
    char *name,
    size_t capacity) {
    const char *cursor = skip_space(line) + strlen(".endobj");
    if (!parse_quoted_name(&cursor, name, capacity)) return false;
    return *skip_space(cursor) == '\0';
}

void porpoise_asm_data_free_object(PorpoiseDataObject *object) {
    size_t index;
    if (object == NULL) return;
    free(object->name);
    free(object->section);
    free(object->bytes);
    free(object->initialized);
    for (index = 0U; index < object->label_count; index++) {
        free(object->labels[index].name);
    }
    free(object->labels);
    for (index = 0U; index < object->fixup_count; index++) {
        free(object->fixups[index].target_symbol);
        free(object->fixups[index].base_symbol);
    }
    free(object->fixups);
    memset(object, 0, sizeof(*object));
}

static PorpoiseDataObject *add_object(
    PorpoiseSourceFile *file,
    const PorpoiseAsmDataMetadata *metadata,
    const char *name,
    bool is_global,
    size_t source_line) {
    PorpoiseDataObject candidate;
    PorpoiseDataObject *object;
    memset(&candidate, 0, sizeof(candidate));
    candidate.name = porpoise_strdup(name);
    candidate.section = porpoise_strdup(metadata->section);
    candidate.is_global = is_global;
    candidate.metadata_line = metadata->source_line;
    candidate.source_line = source_line;
    candidate.section_offset = metadata->section_offset;
    candidate.address = metadata->address;
    candidate.size = metadata->size;
    if (metadata->size != 0U) {
        candidate.bytes = (uint8_t *)calloc((size_t)metadata->size, 1U);
        candidate.initialized = (uint8_t *)calloc((size_t)metadata->size, 1U);
    }
    if (candidate.name == NULL || candidate.section == NULL ||
        (metadata->size != 0U &&
         (candidate.bytes == NULL || candidate.initialized == NULL)) ||
        file->data_object_count == SIZE_MAX ||
        !porpoise_grow_array(
            (void **)&file->data_objects, &file->data_object_capacity,
            sizeof(*file->data_objects), file->data_object_count + 1U)) {
        porpoise_asm_data_free_object(&candidate);
        return NULL;
    }
    object = &file->data_objects[file->data_object_count++];
    *object = candidate;
    return object;
}

static bool object_append_bytes(
    PorpoiseAsmDataParser *parser,
    const PorpoiseSourceFile *file,
    const uint8_t *bytes,
    size_t count,
    bool initialized,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseDataObject *object = parser->current_object;
    if (object == NULL) return false;
    if (parser->current_offset > object->size ||
        count > (size_t)object->size - parser->current_offset) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
            object->address,
            "data emitted by object %s exceeds its declared size of 0x%lX",
            object->name, (unsigned long)object->size);
        return false;
    }
    if (count != 0U) {
        if (bytes != NULL) {
            memcpy(object->bytes + parser->current_offset, bytes, count);
        } else {
            memset(object->bytes + parser->current_offset, 0, count);
        }
        memset(object->initialized + parser->current_offset,
               initialized ? 1 : 0, count);
        if (parser->current_presence != NULL) {
            memset(parser->current_presence + parser->current_offset, 1, count);
        }
    }
    parser->current_offset += count;
    return true;
}

static bool object_append_integer(
    PorpoiseAsmDataParser *parser,
    const PorpoiseSourceFile *file,
    uint64_t value,
    unsigned int width,
    bool initialized,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics) {
    uint8_t bytes[8];
    unsigned int index;
    if (width == 0U || width > sizeof(bytes)) return false;
    for (index = 0U; index < width; index++) {
        bytes[width - index - 1U] = (uint8_t)(value & UINT64_C(0xFF));
        value >>= 8U;
    }
    return object_append_bytes(
        parser, file, bytes, width, initialized, source_line, diagnostics);
}

static bool strip_comment_outside_quotes(char *text) {
    bool quoted = false;
    bool escaped = false;
    size_t index;
    for (index = 0U; text[index] != '\0'; index++) {
        char value = text[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (quoted && value == '\\') {
            escaped = true;
            continue;
        }
        if (value == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && value == '#') {
            text[index] = '\0';
            break;
        }
    }
    porpoise_trim(text);
    return !quoted && !escaped;
}

static bool split_operands(
    char *text,
    char **operands,
    size_t capacity,
    size_t *count_out) {
    char *cursor = text;
    size_t count = 0U;
    bool quoted = false;
    bool escaped = false;
    char *start;
    porpoise_trim(text);
    if (*text == '\0') {
        *count_out = 0U;
        return true;
    }
    start = cursor;
    for (;; cursor++) {
        char value = *cursor;
        if (escaped) {
            escaped = false;
        } else if (quoted && value == '\\') {
            escaped = true;
        } else if (value == '"') {
            quoted = !quoted;
        }
        if ((!quoted && value == ',') || value == '\0') {
            if (count == capacity) return false;
            if (value == ',') *cursor = '\0';
            porpoise_trim(start);
            if (*start == '\0') return false;
            operands[count++] = start;
            if (value == '\0') break;
            start = cursor + 1U;
        }
        if (value == '\0') break;
    }
    if (quoted || escaped) return false;
    *count_out = count;
    return true;
}

static bool parse_width_integer(
    const char *text,
    unsigned int width,
    uint64_t *value_out) {
    char *end = NULL;
    unsigned int bits;
    uint64_t maximum;
    if (width == 0U || width > sizeof(uint64_t)) return false;
    bits = width * CHAR_BIT;
    if (width == sizeof(uint64_t)) {
        maximum = UINT64_MAX;
    } else {
        maximum = (UINT64_C(1) << bits) - UINT64_C(1);
    }
    text = skip_space(text);
    if (*text == '-') {
        long long value;
        int64_t minimum;
        if (width == sizeof(uint64_t)) {
            minimum = INT64_MIN;
        } else {
            minimum = -(INT64_C(1) << (bits - 1U));
        }
        errno = 0;
        value = strtoll(text, &end, 0);
        if (errno == ERANGE || end == text ||
            (int64_t)value < minimum) return false;
        end = (char *)skip_space(end);
        if (*end != '\0') return false;
        *value_out = (uint64_t)value & maximum;
        return true;
    }
    {
        unsigned long long value;
        errno = 0;
        value = strtoull(text, &end, 0);
        if (errno == ERANGE || end == text || value > maximum) return false;
        end = (char *)skip_space(end);
        if (*end != '\0') return false;
        *value_out = (uint64_t)value;
    }
    return true;
}

static bool parse_expression(
    const char *text,
    char **symbol_out,
    int64_t *addend_out,
    uint32_t *literal_out,
    bool *is_literal_out) {
    char buffer[PORPOISE_MESSAGE_CAPACITY];
    char name[PORPOISE_NAME_CAPACITY];
    const char *name_cursor;
    size_t length;
    size_t index;
    size_t operator_index = SIZE_MAX;
    bool quoted = false;
    bool escaped = false;
    uint64_t literal;

    *symbol_out = NULL;
    *addend_out = 0;
    *literal_out = 0U;
    *is_literal_out = false;
    if (!porpoise_copy_string(buffer, sizeof(buffer), text)) return false;
    porpoise_trim(buffer);
    if (parse_width_integer(buffer, 4U, &literal)) {
        *literal_out = (uint32_t)literal;
        *is_literal_out = true;
        return true;
    }
    length = strlen(buffer);
    for (index = 0U; index < length; index++) {
        char value = buffer[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (quoted && value == '\\') {
            escaped = true;
            continue;
        }
        if (value == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && index != 0U && (value == '+' || value == '-')) {
            char *end = NULL;
            const char *addend_text = skip_space(buffer + index + 1U);
            unsigned long long magnitude;
            if (*addend_text == '\0' || *addend_text == '-') continue;
            errno = 0;
            magnitude = strtoull(addend_text, &end, 0);
            if (errno == 0 && end != addend_text &&
                *skip_space(end) == '\0' &&
                magnitude <= (unsigned long long)INT64_MAX) {
                operator_index = index;
            }
        }
    }
    if (quoted || escaped) return false;
    if (operator_index != SIZE_MAX) {
        char *end = NULL;
        unsigned long long magnitude;
        char operation = buffer[operator_index];
        char *addend_text = buffer + operator_index + 1U;
        buffer[operator_index] = '\0';
        porpoise_trim(buffer);
        addend_text = (char *)skip_space(addend_text);
        if (*addend_text == '\0' || *addend_text == '-') return false;
        errno = 0;
        magnitude = strtoull(addend_text, &end, 0);
        if (errno == ERANGE || end == addend_text ||
            *skip_space(end) != '\0' || magnitude > (unsigned long long)INT64_MAX) {
            return false;
        }
        *addend_out = operation == '-'
                          ? -(int64_t)magnitude
                          : (int64_t)magnitude;
    }
    name_cursor = buffer;
    if (!parse_quoted_name(&name_cursor, name, sizeof(name)) ||
        *skip_space(name_cursor) != '\0') {
        return false;
    }
    *symbol_out = porpoise_strdup(name);
    return *symbol_out != NULL;
}

static bool add_fixup(
    PorpoiseDataObject *object,
    PorpoiseDataFixupKind kind,
    size_t source_line,
    uint32_t offset,
    char *target_symbol,
    int64_t target_addend,
    char *base_symbol,
    int64_t base_addend) {
    PorpoiseDataFixup *fixup;
    if (object->fixup_count == SIZE_MAX ||
        !porpoise_grow_array(
            (void **)&object->fixups, &object->fixup_capacity,
            sizeof(*object->fixups), object->fixup_count + 1U)) {
        return false;
    }
    fixup = &object->fixups[object->fixup_count++];
    memset(fixup, 0, sizeof(*fixup));
    fixup->kind = kind;
    fixup->source_line = source_line;
    fixup->offset = offset;
    fixup->width = 4U;
    fixup->target_symbol = target_symbol;
    fixup->target_addend = target_addend;
    fixup->base_symbol = base_symbol;
    fixup->base_addend = base_addend;
    return true;
}

static PorpoiseAsmDataLineResult parse_integer_directive(
    PorpoiseAsmDataParser *parser,
    const PorpoiseSourceFile *file,
    char *operands_text,
    unsigned int width,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics) {
    char *operands[256];
    size_t operand_count;
    size_t index;
    if (!split_operands(
            operands_text, operands,
            sizeof(operands) / sizeof(operands[0]), &operand_count) ||
        operand_count == 0U) {
        goto malformed;
    }
    for (index = 0U; index < operand_count; index++) {
        uint64_t value;
        if (parse_width_integer(operands[index], width, &value)) {
            if (!object_append_integer(
                    parser, file, value, width, true, source_line,
                    diagnostics)) return PORPOISE_ASM_DATA_ERROR;
            continue;
        }
        if (width == 4U) {
            char *symbol = NULL;
            int64_t addend = 0;
            uint32_t literal = 0U;
            bool is_literal = false;
            uint32_t offset;
            if (!parse_expression(
                    operands[index], &symbol, &addend, &literal,
                    &is_literal) || is_literal || symbol == NULL) {
                free(symbol);
                goto malformed;
            }
            if (parser->current_offset > UINT32_MAX) {
                free(symbol);
                return PORPOISE_ASM_DATA_INTERNAL_ERROR;
            }
            offset = (uint32_t)parser->current_offset;
            if (!object_append_integer(
                    parser, file, 0U, 4U, true, source_line,
                    diagnostics)) {
                free(symbol);
                return PORPOISE_ASM_DATA_ERROR;
            }
            if (!add_fixup(
                    parser->current_object,
                    PORPOISE_DATA_FIXUP_ABSOLUTE_32, source_line, offset,
                    symbol, addend, NULL, 0)) {
                free(symbol);
                return PORPOISE_ASM_DATA_INTERNAL_ERROR;
            }
            continue;
        }
        goto malformed;
    }
    return PORPOISE_ASM_DATA_HANDLED;

malformed:
    porpoise_diagnostics_add(
        diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
        parser->current_object->address,
        "malformed or overflowing %u-byte data operand", width);
    return PORPOISE_ASM_DATA_ERROR;
}

static PorpoiseAsmDataLineResult parse_float_directive(
    PorpoiseAsmDataParser *parser,
    const PorpoiseSourceFile *file,
    char *operands_text,
    bool is_double,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics) {
    char *operands[256];
    size_t operand_count;
    size_t index;
    if (!split_operands(
            operands_text, operands,
            sizeof(operands) / sizeof(operands[0]), &operand_count) ||
        operand_count == 0U) goto malformed;
    for (index = 0U; index < operand_count; index++) {
        char *end = NULL;
        errno = 0;
        if (is_double) {
            double value = strtod(operands[index], &end);
            uint64_t bits;
            if (end == operands[index] || errno == ERANGE ||
                *skip_space(end) != '\0' || sizeof(value) != sizeof(bits)) {
                goto malformed;
            }
            memcpy(&bits, &value, sizeof(bits));
            if (!object_append_integer(
                    parser, file, bits, 8U, true, source_line,
                    diagnostics)) return PORPOISE_ASM_DATA_ERROR;
        } else {
            float value = strtof(operands[index], &end);
            uint32_t bits;
            if (end == operands[index] || errno == ERANGE ||
                (*skip_space(end) != '\0' &&
                 !((*skip_space(end) == 'f' || *skip_space(end) == 'F') &&
                   skip_space(end)[1] == '\0')) ||
                sizeof(value) != sizeof(bits)) {
                goto malformed;
            }
            memcpy(&bits, &value, sizeof(bits));
            if (!object_append_integer(
                    parser, file, bits, 4U, true, source_line,
                    diagnostics)) return PORPOISE_ASM_DATA_ERROR;
        }
    }
    return PORPOISE_ASM_DATA_HANDLED;

malformed:
    porpoise_diagnostics_add(
        diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
        parser->current_object->address, "malformed %s data operand",
        is_double ? ".double" : ".float");
    return PORPOISE_ASM_DATA_ERROR;
}

static bool decode_escape(
    const char **cursor_io,
    uint8_t *value_out) {
    const char *cursor = *cursor_io;
    unsigned int value;
    unsigned int digits;
    if (*cursor != '\\') {
        *value_out = (uint8_t)*cursor;
        *cursor_io = cursor + 1U;
        return *cursor != '\0';
    }
    cursor++;
    switch (*cursor) {
        case 'a': value = 7U; cursor++; break;
        case 'b': value = 8U; cursor++; break;
        case 't': value = 9U; cursor++; break;
        case 'n': value = 10U; cursor++; break;
        case 'v': value = 11U; cursor++; break;
        case 'f': value = 12U; cursor++; break;
        case 'r': value = 13U; cursor++; break;
        case '\\': value = '\\'; cursor++; break;
        case '"': value = '"'; cursor++; break;
        case '\'': value = '\''; cursor++; break;
        case '?': value = '?'; cursor++; break;
        case 'x':
            cursor++;
            value = 0U;
            digits = 0U;
            while (isxdigit((unsigned char)*cursor)) {
                unsigned int digit = isdigit((unsigned char)*cursor)
                                         ? (unsigned int)(*cursor - '0')
                                         : (unsigned int)(tolower(
                                               (unsigned char)*cursor) -
                                               'a' + 10);
                if (value > (UINT_MAX - digit) / 16U) return false;
                value = value * 16U + digit;
                digits++;
                cursor++;
            }
            if (digits == 0U || value > UINT8_MAX) return false;
            break;
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7':
            value = 0U;
            digits = 0U;
            while (digits < 3U && *cursor >= '0' && *cursor <= '7') {
                value = value * 8U + (unsigned int)(*cursor - '0');
                digits++;
                cursor++;
            }
            if (value > UINT8_MAX) return false;
            break;
        default:
            return false;
    }
    *value_out = (uint8_t)value;
    *cursor_io = cursor;
    return true;
}

static PorpoiseAsmDataLineResult parse_string_directive(
    PorpoiseAsmDataParser *parser,
    const PorpoiseSourceFile *file,
    char *operands_text,
    bool utf16,
    bool terminated,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics) {
    char *operands[256];
    size_t operand_count;
    size_t index;
    if (!split_operands(
            operands_text, operands,
            sizeof(operands) / sizeof(operands[0]), &operand_count) ||
        operand_count == 0U) goto malformed;
    for (index = 0U; index < operand_count; index++) {
        const char *cursor = skip_space(operands[index]);
        if (*cursor++ != '"') goto malformed;
        while (*cursor != '\0' && *cursor != '"') {
            uint8_t value;
            if (!decode_escape(&cursor, &value)) goto malformed;
            if (utf16) {
                if (!object_append_integer(
                        parser, file, value, 2U, true, source_line,
                        diagnostics)) return PORPOISE_ASM_DATA_ERROR;
            } else if (!object_append_bytes(
                           parser, file, &value, 1U, true, source_line,
                           diagnostics)) {
                return PORPOISE_ASM_DATA_ERROR;
            }
        }
        if (*cursor != '"' || *skip_space(cursor + 1U) != '\0') {
            goto malformed;
        }
        if (terminated &&
            !object_append_integer(
                parser, file, 0U, utf16 ? 2U : 1U, true, source_line,
                diagnostics)) {
            return PORPOISE_ASM_DATA_ERROR;
        }
    }
    return PORPOISE_ASM_DATA_HANDLED;

malformed:
    porpoise_diagnostics_add(
        diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
        parser->current_object->address,
        "malformed or unsupported %s string literal",
        utf16 ? "UTF-16" : "byte");
    return PORPOISE_ASM_DATA_ERROR;
}

static PorpoiseAsmDataLineResult parse_skip_directive(
    PorpoiseAsmDataParser *parser,
    const PorpoiseSourceFile *file,
    char *operands_text,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics) {
    char *operands[2];
    size_t operand_count;
    uint32_t count;
    uint64_t fill = 0U;
    uint8_t fill_byte;
    bool initialized;
    uint8_t buffer[256];
    size_t remaining;
    if (!split_operands(operands_text, operands, 2U, &operand_count) ||
        (operand_count != 1U && operand_count != 2U) ||
        !parse_u32_literal(operands[0], &count) ||
        (operand_count == 2U &&
         !parse_width_integer(operands[1], 1U, &fill))) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
            parser->current_object->address, "malformed .skip directive");
        return PORPOISE_ASM_DATA_ERROR;
    }
    fill_byte = (uint8_t)fill;
    initialized = operand_count == 2U && fill_byte != 0U;
    memset(buffer, fill_byte, sizeof(buffer));
    remaining = (size_t)count;
    while (remaining != 0U) {
        size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (!object_append_bytes(
                parser, file, buffer, chunk, initialized, source_line,
                diagnostics)) return PORPOISE_ASM_DATA_ERROR;
        remaining -= chunk;
    }
    return PORPOISE_ASM_DATA_HANDLED;
}

static PorpoiseAsmDataLineResult parse_rel_directive(
    PorpoiseAsmDataParser *parser,
    const PorpoiseSourceFile *file,
    char *operands_text,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics) {
    char *operands[2];
    size_t operand_count;
    char *base_symbol = NULL;
    char *target_symbol = NULL;
    int64_t base_addend = 0;
    int64_t target_addend = 0;
    uint32_t base_literal = 0U;
    uint32_t target_literal = 0U;
    bool base_is_literal = false;
    bool target_is_literal = false;
    uint32_t offset;
    if (!split_operands(operands_text, operands, 2U, &operand_count) ||
        operand_count != 2U ||
        !parse_expression(
            operands[0], &base_symbol, &base_addend, &base_literal,
            &base_is_literal) ||
        !parse_expression(
            operands[1], &target_symbol, &target_addend, &target_literal,
            &target_is_literal)) {
        free(base_symbol);
        free(target_symbol);
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
            parser->current_object->address, "malformed .rel directive");
        return PORPOISE_ASM_DATA_ERROR;
    }
    if (base_is_literal) base_addend = (int64_t)base_literal;
    if (target_is_literal) target_addend = (int64_t)target_literal;
    if (parser->current_offset > UINT32_MAX) {
        free(base_symbol);
        free(target_symbol);
        return PORPOISE_ASM_DATA_INTERNAL_ERROR;
    }
    offset = (uint32_t)parser->current_offset;
    if (!object_append_integer(
            parser, file, 0U, 4U, true, source_line, diagnostics)) {
        free(base_symbol);
        free(target_symbol);
        return PORPOISE_ASM_DATA_ERROR;
    }
    if (!add_fixup(
            parser->current_object, PORPOISE_DATA_FIXUP_REL_TARGET_32,
            source_line, offset, target_symbol, target_addend, base_symbol,
            base_addend)) {
        free(base_symbol);
        free(target_symbol);
        return PORPOISE_ASM_DATA_INTERNAL_ERROR;
    }
    return PORPOISE_ASM_DATA_HANDLED;
}

static bool parse_data_label(
    const char *line,
    char *name,
    size_t capacity) {
    const char *cursor = skip_space(line);
    if (!parse_quoted_name(&cursor, name, capacity)) return false;
    cursor = skip_space(cursor);
    if (*cursor != ':') return false;
    return *skip_space(cursor + 1U) == '\0';
}

static PorpoiseAsmDataLineResult add_data_label(
    PorpoiseAsmDataParser *parser,
    const PorpoiseSourceFile *file,
    const char *name,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseDataObject *object = parser->current_object;
    PorpoiseDataLocalLabel *label;
    char *copy;
    size_t index;
    if (parser->current_offset > UINT32_MAX) {
        return PORPOISE_ASM_DATA_INTERNAL_ERROR;
    }
    for (index = 0U; index < object->label_count; index++) {
        if (strcmp(object->labels[index].name, name) == 0) {
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                source_line, object->address,
                "duplicate local label %s in data object %s", name,
                object->name);
            return PORPOISE_ASM_DATA_ERROR;
        }
    }
    copy = porpoise_strdup(name);
    if (copy == NULL || object->label_count == SIZE_MAX ||
        !porpoise_grow_array(
            (void **)&object->labels, &object->label_capacity,
            sizeof(*object->labels), object->label_count + 1U)) {
        free(copy);
        return PORPOISE_ASM_DATA_INTERNAL_ERROR;
    }
    label = &object->labels[object->label_count++];
    label->name = copy;
    label->source_line = source_line;
    label->offset = (uint32_t)parser->current_offset;
    return PORPOISE_ASM_DATA_HANDLED;
}

static bool is_known_byte_emitter(const char *line) {
    static const char *const directives[] = {
        ".string", ".string16", ".ascii", ".asciz", ".byte",
        ".2byte", ".4byte", ".8byte", ".short", ".int", ".word", ".long",
        ".quad", ".float", ".double", ".skip", ".space", ".zero",
        ".rel", ".incbin", ".fill"
    };
    size_t index;
    for (index = 0U; index < sizeof(directives) / sizeof(directives[0]);
         index++) {
        if (is_directive(line, directives[index])) return true;
    }
    return false;
}

static PorpoiseAsmDataLineResult parse_object_body_line(
    PorpoiseAsmDataParser *parser,
    PorpoiseSourceFile *file,
    const char *line,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics) {
    char text[4096];
    char name[PORPOISE_NAME_CAPACITY];
    char *cursor;
    char *operands;
    char directive[32];
    size_t directive_length;

    if (*line == '\0' || *line == '#') return PORPOISE_ASM_DATA_HANDLED;
    if (is_directive(line, ".sym")) {
        bool is_global;
        if (!parse_object_declaration(
                line, ".sym", name, sizeof(name), &is_global)) {
            parser->have_metadata = false;
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                source_line, parser->current_object->address,
                "malformed .sym directive inside data object %s",
                parser->current_object->name);
            return PORPOISE_ASM_DATA_ERROR;
        }
        if (!parser->have_metadata || parser->metadata.size != 0U) {
            parser->have_metadata = false;
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                source_line, parser->current_object->address,
                "data symbol alias %s inside object %s is missing fresh zero-size contribution metadata",
                name, parser->current_object->name);
            return PORPOISE_ASM_DATA_ERROR;
        }
        if (add_data_alias(
                file, &parser->metadata, name, is_global,
                source_line) == NULL) {
            parser->have_metadata = false;
            return PORPOISE_ASM_DATA_INTERNAL_ERROR;
        }
        parser->have_metadata = false;
        return PORPOISE_ASM_DATA_HANDLED;
    }
    if (parser->have_metadata) {
        parser->have_metadata = false;
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
            source_line, parser->current_object->address,
            "zero-size contribution metadata inside data object %s is not followed by .sym",
            parser->current_object->name);
        return PORPOISE_ASM_DATA_ERROR;
    }
    if (is_directive(line, ".obj")) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
            parser->current_object->address,
            "nested .obj is not allowed (object %s is still open)",
            parser->current_object->name);
        return PORPOISE_ASM_DATA_ERROR;
    }
    if (is_directive(line, ".endobj")) {
        bool end_valid = parse_end_object(line, name, sizeof(name));
        PorpoiseDataObject *object = parser->current_object;
        bool valid = true;
        if (!end_valid) {
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                source_line, object->address, "malformed .endobj directive");
            valid = false;
        } else if (strcmp(name, object->name) != 0) {
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                source_line, object->address,
                ".endobj names %s but the open object is %s", name,
                object->name);
            valid = false;
        }
        if (parser->current_offset != object->size) {
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                source_line, object->address,
                "data object %s emitted 0x%lX bytes but metadata declares 0x%lX",
                object->name, (unsigned long)parser->current_offset,
                (unsigned long)object->size);
            valid = false;
        }
        object->end_source_line = source_line;
        if (parser->active_anonymous != NULL &&
            object != &parser->active_anonymous->storage) {
            uint64_t object_end = (uint64_t)object->address + object->size;
            uint64_t contribution_start =
                parser->active_anonymous->storage.address;
            if (object_end >= contribution_start &&
                object_end - contribution_start <=
                    parser->active_anonymous->storage.size) {
                parser->contribution_offset =
                    (size_t)(object_end - contribution_start);
            }
        }
        parser->current_object = NULL;
        parser->current_presence = NULL;
        parser->current_offset = 0U;
        return valid ? PORPOISE_ASM_DATA_HANDLED : PORPOISE_ASM_DATA_ERROR;
    }
    if (!porpoise_copy_string(text, sizeof(text), line)) {
        return PORPOISE_ASM_DATA_INTERNAL_ERROR;
    }
    if (!strip_comment_outside_quotes(text)) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
            parser->current_object->address,
            "unterminated quote or escape in data object %s",
            parser->current_object->name);
        return PORPOISE_ASM_DATA_ERROR;
    }
    if (*text == '\0') return PORPOISE_ASM_DATA_HANDLED;
    if (parse_data_label(text, name, sizeof(name))) {
        return add_data_label(
            parser, file, name, source_line, diagnostics);
    }
    cursor = text;
    if (*cursor != '.') {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
            parser->current_object->address,
            "unrecognized data-object line: %s", text);
        return PORPOISE_ASM_DATA_ERROR;
    }
    operands = cursor;
    while (*operands != '\0' && !isspace((unsigned char)*operands)) operands++;
    directive_length = (size_t)(operands - cursor);
    if (directive_length == 0U || directive_length >= sizeof(directive)) {
        goto unsupported;
    }
    memcpy(directive, cursor, directive_length);
    directive[directive_length] = '\0';
    if (*operands != '\0') *operands++ = '\0';
    operands = (char *)skip_space(operands);

    if (strcmp(directive, ".hidden") == 0) {
        return PORPOISE_ASM_DATA_HANDLED;
    }
    if (strcmp(directive, ".byte") == 0) {
        return parse_integer_directive(
            parser, file, operands, 1U, source_line, diagnostics);
    }
    if (strcmp(directive, ".2byte") == 0 ||
        strcmp(directive, ".short") == 0) {
        return parse_integer_directive(
            parser, file, operands, 2U, source_line, diagnostics);
    }
    if (strcmp(directive, ".4byte") == 0 ||
        strcmp(directive, ".int") == 0 ||
        strcmp(directive, ".word") == 0 ||
        strcmp(directive, ".long") == 0) {
        return parse_integer_directive(
            parser, file, operands, 4U, source_line, diagnostics);
    }
    if (strcmp(directive, ".8byte") == 0 ||
        strcmp(directive, ".quad") == 0) {
        return parse_integer_directive(
            parser, file, operands, 8U, source_line, diagnostics);
    }
    if (strcmp(directive, ".float") == 0) {
        return parse_float_directive(
            parser, file, operands, false, source_line, diagnostics);
    }
    if (strcmp(directive, ".double") == 0) {
        return parse_float_directive(
            parser, file, operands, true, source_line, diagnostics);
    }
    if (strcmp(directive, ".string") == 0) {
        return parse_string_directive(
            parser, file, operands, false, true, source_line, diagnostics);
    }
    if (strcmp(directive, ".string16") == 0) {
        return parse_string_directive(
            parser, file, operands, true, true, source_line, diagnostics);
    }
    if (strcmp(directive, ".ascii") == 0) {
        return parse_string_directive(
            parser, file, operands, false, false, source_line, diagnostics);
    }
    if (strcmp(directive, ".asciz") == 0) {
        return parse_string_directive(
            parser, file, operands, false, true, source_line, diagnostics);
    }
    if (strcmp(directive, ".skip") == 0 ||
        strcmp(directive, ".space") == 0 ||
        strcmp(directive, ".zero") == 0) {
        return parse_skip_directive(
            parser, file, operands, source_line, diagnostics);
    }
    if (strcmp(directive, ".rel") == 0) {
        return parse_rel_directive(
            parser, file, operands, source_line, diagnostics);
    }
    if (strcmp(directive, ".balign") == 0) {
        char *alignment_operands[2];
        size_t operand_count;
        uint32_t alignment;
        uint64_t fill = 0U;
        size_t padding;
        uint8_t buffer[256];
        if (!split_operands(
                operands, alignment_operands, 2U, &operand_count) ||
            (operand_count != 1U && operand_count != 2U) ||
            !parse_u32_literal(alignment_operands[0], &alignment) ||
            alignment == 0U ||
            (operand_count == 2U &&
             !parse_width_integer(alignment_operands[1], 1U, &fill))) {
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                source_line, parser->current_object->address,
                "malformed .balign directive");
            return PORPOISE_ASM_DATA_ERROR;
        }
        padding = (size_t)((alignment -
                            (((uint64_t)parser->current_object->address +
                              parser->current_offset) % alignment)) %
                           alignment);
        memset(buffer, (uint8_t)fill, sizeof(buffer));
        while (padding != 0U) {
            size_t chunk = padding < sizeof(buffer) ? padding : sizeof(buffer);
            if (!object_append_bytes(
                    parser, file, buffer, chunk,
                    operand_count == 2U && fill != 0U, source_line,
                    diagnostics)) return PORPOISE_ASM_DATA_ERROR;
            padding -= chunk;
        }
        return PORPOISE_ASM_DATA_HANDLED;
    }

unsupported:
    porpoise_diagnostics_add(
        diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
        parser->current_object->address,
        "unsupported data directive %s in object %s", directive,
        parser->current_object->name);
    return PORPOISE_ASM_DATA_ERROR;
}

static bool parse_section_selection(
    const char *line,
    char *section,
    size_t capacity,
    bool *executable) {
    const char *cursor = skip_space(line);
    const char *start;
    const char *end;
    size_t length;
    *executable = false;
    if (strncmp(cursor, ".section", 8U) == 0 &&
        isspace((unsigned char)cursor[8])) {
        cursor = skip_space(cursor + 8U);
        if (*cursor == '"') {
            start = ++cursor;
            end = strchr(cursor, '"');
            if (end == NULL) return false;
            cursor = end + 1U;
        } else {
            start = cursor;
            while (*cursor != '\0' && *cursor != ',' &&
                   !isspace((unsigned char)*cursor)) cursor++;
            end = cursor;
        }
        length = (size_t)(end - start);
        if (!copy_normalized_section(
                section, capacity, start, length)) return false;
        cursor = skip_space(cursor);
        if (*cursor == ',') {
            cursor = skip_space(cursor + 1U);
            if (*cursor == '"') {
                const char *flags_end = strchr(cursor + 1U, '"');
                const char *flag;
                if (flags_end == NULL) return false;
                for (flag = cursor + 1U; flag < flags_end; flag++) {
                    if (*flag == 'x') *executable = true;
                }
            }
        }
        return true;
    }
    {
        static const char *const selectors[] = {
            ".text", ".init", ".data", ".rodata", ".sdata", ".sdata2",
            ".bss", ".sbss", ".sbss2", ".ctors", ".dtors", ".extab",
            ".extabindex"
        };
        size_t index;
        for (index = 0U;
             index < sizeof(selectors) / sizeof(selectors[0]); index++) {
            if (strcmp(cursor, selectors[index]) == 0) {
                if (!porpoise_copy_string(
                        section, capacity, selectors[index])) return false;
                *executable = strcmp(selectors[index], ".text") == 0 ||
                              strcmp(selectors[index], ".init") == 0;
                return true;
            }
        }
    }
    return false;
}

static PorpoiseAnonymousData *add_anonymous_contribution(
    PorpoiseSourceFile *file,
    const char *section,
    uint32_t address,
    uint32_t size,
    size_t source_line) {
    PorpoiseAnonymousData candidate;
    PorpoiseAnonymousData *anonymous;
    memset(&candidate, 0, sizeof(candidate));
    candidate.storage.name = porpoise_strdup("@anonymous");
    candidate.storage.section = porpoise_strdup(section);
    candidate.storage.metadata_line = source_line;
    candidate.storage.source_line = source_line;
    candidate.storage.address = address;
    candidate.storage.size = size;
    if (size != 0U) {
        candidate.storage.bytes = (uint8_t *)calloc((size_t)size, 1U);
        candidate.storage.initialized =
            (uint8_t *)calloc((size_t)size, 1U);
        candidate.present = (uint8_t *)calloc((size_t)size, 1U);
    }
    if (candidate.storage.name == NULL ||
        candidate.storage.section == NULL ||
        (size != 0U &&
         (candidate.storage.bytes == NULL ||
          candidate.storage.initialized == NULL ||
          candidate.present == NULL)) ||
        file->anonymous_data_count == SIZE_MAX ||
        !porpoise_grow_array(
            (void **)&file->anonymous_data, &file->anonymous_data_capacity,
            sizeof(*file->anonymous_data),
            file->anonymous_data_count + 1U)) {
        porpoise_asm_data_free_object(&candidate.storage);
        free(candidate.present);
        return NULL;
    }
    anonymous = &file->anonymous_data[file->anonymous_data_count++];
    *anonymous = candidate;
    return anonymous;
}

PorpoiseAsmDataLineResult porpoise_asm_data_parse_line(
    PorpoiseAsmDataParser *parser,
    PorpoiseSourceFile *file,
    const char *line,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics) {
    char name[PORPOISE_NAME_CAPACITY];
    char section[PORPOISE_NAME_CAPACITY];
    bool is_global;
    bool executable;
    if (parser->current_object != NULL) {
        return parse_object_body_line(
            parser, file, line, source_line, diagnostics);
    }
    if (parse_section_selection(
            line, section, sizeof(section), &executable)) {
        parser->have_metadata = false;
        parser->active_anonymous = NULL;
        parser->contribution_offset = 0U;
        parser->executable_symbol_data = false;
        if (!porpoise_copy_string(
                parser->selected_section,
                sizeof(parser->selected_section), section)) {
            return PORPOISE_ASM_DATA_INTERNAL_ERROR;
        }
        parser->have_selected_section = true;
        parser->selected_section_executable = executable;
        if (parser->have_range) {
            parser->have_range = false;
            if (!executable) {
                parser->active_anonymous = add_anonymous_contribution(
                    file, section, parser->range_address,
                    parser->range_size, parser->range_source_line);
                if (parser->active_anonymous == NULL) {
                    return PORPOISE_ASM_DATA_INTERNAL_ERROR;
                }
            }
        }
        /* Let the main parser retain its section-selection behavior. */
        return PORPOISE_ASM_DATA_NOT_HANDLED;
    }
    if (is_directive(line, ".sym")) {
        bool alias_executable;
        if (!parser->have_metadata) {
            /* No data location evidence: retain instruction-alias parsing. */
            return PORPOISE_ASM_DATA_NOT_HANDLED;
        }
        if (!parse_object_declaration(
                line, ".sym", name, sizeof(name), &is_global)) {
            parser->have_metadata = false;
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                source_line, 0U,
                "malformed data .sym directive");
            return PORPOISE_ASM_DATA_ERROR;
        }
        if (parser->metadata.size != 0U) {
            parser->have_metadata = false;
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                source_line, parser->metadata.address,
                "data symbol alias %s requires zero-size contribution metadata",
                name);
            return PORPOISE_ASM_DATA_ERROR;
        }
        if (parser->have_selected_section &&
            strcmp(parser->selected_section, parser->metadata.section) != 0) {
            parser->have_metadata = false;
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                source_line, parser->metadata.address,
                "data symbol alias %s metadata section %s does not match selected section %s",
                name, parser->metadata.section,
                parser->selected_section);
            return PORPOISE_ASM_DATA_ERROR;
        }
        alias_executable = parser->have_selected_section
                               ? parser->selected_section_executable
                               : strcmp(parser->metadata.section, ".text") == 0 ||
                                     strcmp(parser->metadata.section, ".init") == 0;
        if (add_data_alias(
                file, &parser->metadata, name, is_global,
                source_line) == NULL) {
            parser->have_metadata = false;
            return PORPOISE_ASM_DATA_INTERNAL_ERROR;
        }
        parser->have_metadata = false;
        parser->executable_symbol_data = alias_executable;
        return PORPOISE_ASM_DATA_HANDLED;
    }
    if (is_directive(line, ".obj")) {
        PorpoiseDataObject *object;
        parser->executable_symbol_data = false;
        if (!parse_object_declaration(
                line, ".obj", name, sizeof(name), &is_global)) {
            parser->have_metadata = false;
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                source_line, 0U, "malformed .obj directive");
            return PORPOISE_ASM_DATA_ERROR;
        }
        if (!parser->have_metadata) {
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                source_line, 0U,
                "data object %s is missing fresh contribution metadata",
                name);
            return PORPOISE_ASM_DATA_ERROR;
        }
        if (parser->active_anonymous != NULL) {
            uint64_t contribution_start =
                parser->active_anonymous->storage.address;
            uint64_t contribution_end =
                contribution_start +
                parser->active_anonymous->storage.size;
            uint64_t object_end =
                (uint64_t)parser->metadata.address + parser->metadata.size;
            if (strcmp(
                    parser->active_anonymous->storage.section,
                    parser->metadata.section) != 0 ||
                parser->metadata.address < contribution_start ||
                object_end > contribution_end) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                    source_line, parser->metadata.address,
                    "data object %s falls outside the active %s contribution range",
                    name, parser->active_anonymous->storage.section);
                parser->have_metadata = false;
                return PORPOISE_ASM_DATA_ERROR;
            }
            parser->contribution_offset =
                (size_t)((uint64_t)parser->metadata.address -
                         contribution_start);
        }
        object = add_object(
            file, &parser->metadata, name, is_global, source_line);
        parser->have_metadata = false;
        if (object == NULL) return PORPOISE_ASM_DATA_INTERNAL_ERROR;
        parser->current_object = object;
        parser->current_presence = NULL;
        parser->current_offset = 0U;
        return PORPOISE_ASM_DATA_HANDLED;
    }
    if (is_directive(line, ".endobj")) {
        parser->have_metadata = false;
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
            0U, ".endobj without .obj");
        return PORPOISE_ASM_DATA_ERROR;
    }
    parser->have_metadata = false;
    if (is_directive(line, ".hidden")) {
        return PORPOISE_ASM_DATA_HANDLED;
    }
    if (parser->active_anonymous != NULL &&
        (is_known_byte_emitter(line) || is_directive(line, ".balign") ||
         parse_data_label(line, name, sizeof(name)))) {
        PorpoiseAsmDataLineResult result;
        parser->current_object = &parser->active_anonymous->storage;
        parser->current_presence = parser->active_anonymous->present;
        parser->current_offset = parser->contribution_offset;
        result = parse_object_body_line(
            parser, file, line, source_line, diagnostics);
        parser->contribution_offset = parser->current_offset;
        parser->current_object = NULL;
        parser->current_presence = NULL;
        parser->current_offset = 0U;
        return result;
    }
    if (is_directive(line, ".balign")) {
        return PORPOISE_ASM_DATA_HANDLED;
    }
    if (is_known_byte_emitter(line)) {
        char directive[32];
        const char *cursor = skip_space(line);
        size_t length = 0U;
        while (cursor[length] != '\0' &&
               !isspace((unsigned char)cursor[length])) length++;
        if (length >= sizeof(directive)) length = sizeof(directive) - 1U;
        memcpy(directive, cursor, length);
        directive[length] = '\0';
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
            0U,
            "byte-emitting directive %s appears outside an annotated .obj",
            directive);
        return PORPOISE_ASM_DATA_ERROR;
    }
    return PORPOISE_ASM_DATA_NOT_HANDLED;
}

bool porpoise_asm_data_finish_file(
    PorpoiseAsmDataParser *parser,
    const PorpoiseSourceFile *file,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics) {
    if (parser->current_object != NULL) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path, source_line,
            parser->current_object->address,
            "data object %s is missing .endobj",
            parser->current_object->name);
        parser->current_object = NULL;
        parser->current_offset = 0U;
        return false;
    }
    if (parser->have_range) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
            parser->range_source_line, parser->range_address,
            "section contribution range is not followed by a section selector");
        parser->have_range = false;
        return false;
    }
    parser->have_metadata = false;
    return true;
}

static void add_address_candidate(
    uint32_t address,
    bool *found,
    bool *ambiguous,
    uint32_t *value) {
    if (!*found) {
        *found = true;
        *value = address;
    } else if (*value != address) {
        *ambiguous = true;
    }
}

static int compare_data_symbol_entries(const void *left, const void *right) {
    const PorpoiseProgramDataSymbolIndexEntry *a =
        (const PorpoiseProgramDataSymbolIndexEntry *)left;
    const PorpoiseProgramDataSymbolIndexEntry *b =
        (const PorpoiseProgramDataSymbolIndexEntry *)right;
    int comparison = strcmp(a->name, b->name);
    uint32_t a_address;
    uint32_t b_address;
    if (comparison != 0) return comparison;
    comparison = strcmp(a->file->relative_path, b->file->relative_path);
    if (comparison != 0) return comparison;
    a_address = a->alias != NULL ? a->alias->address : a->object->address;
    b_address = b->alias != NULL ? b->alias->address : b->object->address;
    if (a_address != b_address) {
        return a_address < b_address ? -1 : 1;
    }
    if ((a->alias == NULL) != (b->alias == NULL)) {
        return a->alias == NULL ? -1 : 1;
    }
    if (a->alias != NULL) {
        comparison = strcmp(a->alias->section, b->alias->section);
        if (comparison != 0) return comparison;
        if (a->alias->source_line != b->alias->source_line) {
            return a->alias->source_line < b->alias->source_line ? -1 : 1;
        }
        if (a->alias->is_global != b->alias->is_global) {
            return a->alias->is_global ? -1 : 1;
        }
    }
    return 0;
}

static bool parse_dtk_section_base_name(
    const char *name,
    char *section,
    size_t capacity) {
    size_t name_length;
    size_t component_length;
    size_t index;

    if (name == NULL || section == NULL || capacity == 0U) return false;
    name_length = strlen(name);
    if (name_length < 7U || strncmp(name, "...", 3U) != 0 ||
        strcmp(name + name_length - 2U, ".0") != 0) {
        return false;
    }
    component_length = name_length - 5U;
    if (component_length == 0U || component_length + 2U > capacity) {
        return false;
    }
    for (index = 0U; index < component_length; index++) {
        unsigned char character = (unsigned char)name[index + 3U];
        if (!isalnum(character) && character != '_' && character != '.') {
            return false;
        }
    }
    section[0] = '.';
    memcpy(section + 1U, name + 3U, component_length);
    section[component_length + 1U] = '\0';
    return true;
}

static bool file_has_data_symbol(
    const PorpoiseSourceFile *file,
    const char *name) {
    size_t index;
    for (index = 0U; index < file->data_object_count; index++) {
        if (strcmp(file->data_objects[index].name, name) == 0) return true;
    }
    for (index = 0U; index < file->data_alias_count; index++) {
        if (strcmp(file->data_aliases[index].name, name) == 0) return true;
    }
    return false;
}

static void add_section_base_candidate(
    uint32_t candidate,
    bool *found,
    bool *ambiguous,
    uint32_t *base) {
    if (!*found) {
        *found = true;
        *base = candidate;
    } else if (*base != candidate) {
        *ambiguous = true;
    }
}

static bool find_file_section_base(
    const PorpoiseSourceFile *file,
    const char *section,
    uint32_t *base_out,
    bool *ambiguous_out) {
    bool found = false;
    bool ambiguous = false;
    uint32_t base = 0U;
    size_t index;

    for (index = 0U; index < file->data_object_count; index++) {
        const PorpoiseDataObject *object = &file->data_objects[index];
        if (strcmp(object->section, section) != 0 ||
            object->address < object->section_offset) {
            continue;
        }
        add_section_base_candidate(
            object->address - object->section_offset,
            &found, &ambiguous, &base);
    }
    /* A range-only contribution still provides an exact section start. */
    if (!found) {
        for (index = 0U; index < file->anonymous_data_count; index++) {
            const PorpoiseDataObject *storage =
                &file->anonymous_data[index].storage;
            if (strcmp(storage->section, section) != 0 ||
                storage->address < storage->section_offset) {
                continue;
            }
            add_section_base_candidate(
                storage->address - storage->section_offset,
                &found, &ambiguous, &base);
        }
    }
    *ambiguous_out = ambiguous;
    if (found && !ambiguous) *base_out = base;
    return found;
}

static bool synthesize_referenced_section_base(
    PorpoiseSourceFile *file,
    const char *symbol,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseAsmDataMetadata metadata;
    char section[PORPOISE_NAME_CAPACITY];
    uint32_t base = 0U;
    bool ambiguous = false;

    if (symbol == NULL ||
        !parse_dtk_section_base_name(symbol, section, sizeof(section)) ||
        file_has_data_symbol(file, symbol)) {
        return true;
    }
    if (!find_file_section_base(file, section, &base, &ambiguous)) {
        return true;
    }
    if (ambiguous) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
            source_line, 0U,
            "ambiguous DTK section base %s in one translation unit",
            symbol);
        return false;
    }
    memset(&metadata, 0, sizeof(metadata));
    if (!porpoise_copy_string(
            metadata.section, sizeof(metadata.section), section)) {
        return false;
    }
    metadata.address = base;
    metadata.source_line = source_line;
    return add_data_alias(
               file, &metadata, symbol, false, source_line) != NULL;
}

static bool synthesize_referenced_section_bases(
    PorpoiseProgram *program,
    PorpoiseDiagnostics *diagnostics) {
    size_t file_index;
    bool ok = true;

    for (file_index = 0U; file_index < program->file_count; file_index++) {
        PorpoiseSourceFile *file = &program->files[file_index];
        size_t object_index;
        for (object_index = 0U;
             object_index < file->data_object_count;
             object_index++) {
            PorpoiseDataObject *object = &file->data_objects[object_index];
            size_t fixup_index;
            for (fixup_index = 0U;
                 fixup_index < object->fixup_count;
                 fixup_index++) {
                const PorpoiseDataFixup *fixup =
                    &object->fixups[fixup_index];
                if (!synthesize_referenced_section_base(
                        file, fixup->base_symbol, fixup->source_line,
                        diagnostics) ||
                    !synthesize_referenced_section_base(
                        file, fixup->target_symbol, fixup->source_line,
                        diagnostics)) {
                    ok = false;
                }
            }
        }
        for (object_index = 0U;
             object_index < file->anonymous_data_count;
             object_index++) {
            PorpoiseDataObject *object =
                &file->anonymous_data[object_index].storage;
            size_t fixup_index;
            for (fixup_index = 0U;
                 fixup_index < object->fixup_count;
                 fixup_index++) {
                const PorpoiseDataFixup *fixup =
                    &object->fixups[fixup_index];
                if (!synthesize_referenced_section_base(
                        file, fixup->base_symbol, fixup->source_line,
                        diagnostics) ||
                    !synthesize_referenced_section_base(
                        file, fixup->target_symbol, fixup->source_line,
                        diagnostics)) {
                    ok = false;
                }
            }
        }
    }
    return ok;
}

static bool build_data_symbol_index(PorpoiseProgram *program) {
    PorpoiseProgramDataSymbolIndexEntry *entries = NULL;
    size_t count = 0U;
    size_t cursor = 0U;
    size_t file_index;
    if (program->data_symbol_index != NULL ||
        program->data_symbol_index_count != 0U ||
        program->data_symbol_index_capacity != 0U) return false;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        if (count > SIZE_MAX - file->data_object_count ||
            count + file->data_object_count >
                SIZE_MAX - file->data_alias_count) return false;
        count += file->data_object_count;
        count += file->data_alias_count;
    }
    if (count > SIZE_MAX / sizeof(*entries)) return false;
    if (count != 0U) {
        entries = (PorpoiseProgramDataSymbolIndexEntry *)malloc(
            count * sizeof(*entries));
        if (entries == NULL) return false;
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        size_t object_index;
        for (object_index = 0U; object_index < file->data_object_count;
             object_index++) {
            entries[cursor].name = file->data_objects[object_index].name;
            entries[cursor].file = file;
            entries[cursor].object = &file->data_objects[object_index];
            entries[cursor].alias = NULL;
            cursor++;
        }
        for (object_index = 0U; object_index < file->data_alias_count;
             object_index++) {
            entries[cursor].name = file->data_aliases[object_index].name;
            entries[cursor].file = file;
            entries[cursor].object = NULL;
            entries[cursor].alias = &file->data_aliases[object_index];
            cursor++;
        }
    }
    if (cursor != count) {
        free(entries);
        return false;
    }
    if (count > 1U) {
        qsort(entries, count, sizeof(*entries), compare_data_symbol_entries);
    }
    program->data_symbol_index = entries;
    program->data_symbol_index_count = count;
    program->data_symbol_index_capacity = count;
    return true;
}

static size_t data_symbol_lower_bound(
    const PorpoiseProgram *program,
    const char *name) {
    size_t first = 0U;
    size_t count = program->data_symbol_index_count;
    while (count != 0U) {
        size_t step = count / 2U;
        size_t index = first + step;
        if (strcmp(program->data_symbol_index[index].name, name) < 0) {
            first = index + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

static size_t raw_symbol_lower_bound(
    const PorpoiseProgram *program,
    const char *name) {
    size_t first = 0U;
    size_t count = program->symbol_index_count;
    while (count != 0U) {
        size_t step = count / 2U;
        size_t index = first + step;
        if (strcmp(program->symbol_index[index].name, name) < 0) {
            first = index + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

static size_t raw_label_lower_bound(
    const PorpoiseProgram *program,
    const char *name) {
    size_t first = 0U;
    size_t count = program->label_index_count;
    while (count != 0U) {
        size_t step = count / 2U;
        size_t index = first + step;
        if (strcmp(program->label_index[index].name, name) < 0) {
            first = index + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

static bool raw_symbol_entry_matches_exact(
    const PorpoiseProgramSymbolIndexEntry *entry,
    const char *name) {
    return entry->alias != NULL
               ? strcmp(entry->alias->name, name) == 0
               : strcmp(entry->function->name, name) == 0;
}

static bool raw_symbol_entry_belongs_to_file(
    const PorpoiseProgramSymbolIndexEntry *entry,
    const PorpoiseSourceFile *file) {
    return entry->file == file ||
           (file != NULL && entry->alias != NULL &&
            entry->alias->source_path != NULL &&
            strcmp(entry->alias->source_path, file->path) == 0);
}

static void collect_named_candidates(
    const PorpoiseProgram *program,
    const PorpoiseSourceFile *scope_file,
    const char *name,
    bool global,
    bool *found,
    bool *ambiguous,
    uint32_t *value) {
    size_t index = data_symbol_lower_bound(program, name);
    while (index < program->data_symbol_index_count &&
           strcmp(program->data_symbol_index[index].name, name) == 0) {
        const PorpoiseProgramDataSymbolIndexEntry *entry =
            &program->data_symbol_index[index++];
        bool entry_global = entry->alias != NULL
                                ? entry->alias->is_global
                                : entry->object->is_global;
        uint32_t entry_address = entry->alias != NULL
                                     ? entry->alias->address
                                     : entry->object->address;
        if ((!global && entry->file == scope_file) ||
            (global && entry_global)) {
            add_address_candidate(
                entry_address, found, ambiguous, value);
        }
    }
    index = raw_symbol_lower_bound(program, name);
    while (index < program->symbol_index_count &&
           strcmp(program->symbol_index[index].name, name) == 0) {
        const PorpoiseProgramSymbolIndexEntry *entry =
            &program->symbol_index[index++];
        uint32_t address;
        if (!raw_symbol_entry_matches_exact(entry, name) ||
            (!global &&
             !raw_symbol_entry_belongs_to_file(entry, scope_file))) {
            continue;
        }
        address = entry->alias != NULL
                      ? entry->alias->address
                      : entry->function->start_address;
        add_address_candidate(address, found, ambiguous, value);
    }
    index = raw_label_lower_bound(program, name);
    while (index < program->label_index_count &&
           strcmp(program->label_index[index].name, name) == 0) {
        const PorpoiseProgramLabelIndexEntry *entry =
            &program->label_index[index++];
        if (entry->instruction_item_index != SIZE_MAX &&
            (global || entry->file == scope_file)) {
            add_address_candidate(
                entry->address, found, ambiguous, value);
        }
    }
}

bool porpoise_program_resolve_raw_address(
    const PorpoiseProgram *program,
    const PorpoiseSourceFile *scope_file,
    const PorpoiseDataObject *scope_object,
    const char *name,
    uint32_t *address_out) {
    bool found = false;
    bool ambiguous = false;
    uint32_t value = 0U;
    size_t index;
    if (address_out != NULL) *address_out = 0U;
    if (program == NULL || name == NULL || address_out == NULL) return false;
    if (scope_object != NULL) {
        for (index = 0U; index < scope_object->label_count; index++) {
            const PorpoiseDataLocalLabel *label =
                &scope_object->labels[index];
            if (strcmp(label->name, name) == 0) {
                uint64_t address =
                    (uint64_t)scope_object->address + label->offset;
                if (address > UINT32_MAX) return false;
                *address_out = (uint32_t)address;
                return true;
            }
        }
    }
    if (scope_file != NULL) {
        collect_named_candidates(
            program, scope_file, name, false, &found, &ambiguous, &value);
        if (ambiguous) return false;
        if (found) {
            *address_out = value;
            return true;
        }
    }
    collect_named_candidates(
        program, NULL, name, true, &found, &ambiguous, &value);
    if (!found || ambiguous) return false;
    *address_out = value;
    return true;
}

static bool apply_addend(
    uint32_t address,
    int64_t addend,
    uint32_t *value_out) {
    if (addend < 0) {
        uint64_t magnitude = (uint64_t)(-(addend + 1)) + UINT64_C(1);
        if (magnitude > address) return false;
        *value_out = address - (uint32_t)magnitude;
    } else {
        uint64_t value = (uint64_t)address + (uint64_t)addend;
        if (value > UINT32_MAX) return false;
        *value_out = (uint32_t)value;
    }
    return true;
}

static void store_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
}

static bool resolve_object_fixups(
    PorpoiseProgram *program,
    PorpoiseSourceFile *file,
    PorpoiseDataObject *object,
    PorpoiseDiagnostics *diagnostics) {
    size_t fixup_index;
    bool ok = true;
    for (fixup_index = 0U; fixup_index < object->fixup_count;
         fixup_index++) {
        PorpoiseDataFixup *fixup = &object->fixups[fixup_index];
        uint32_t address;
        uint32_t value;
        if (fixup->base_symbol != NULL) {
            if (!porpoise_program_resolve_raw_address(
                    program, file, object, fixup->base_symbol, &address) ||
                !apply_addend(address, fixup->base_addend, &value)) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                    fixup->source_line, object->address,
                    "unresolved, ambiguous, or overflowing .rel base expression %s",
                    fixup->base_symbol);
                ok = false;
            }
        }
        if (fixup->target_symbol != NULL) {
            if (!porpoise_program_resolve_raw_address(
                    program, file, object, fixup->target_symbol, &address) ||
                !apply_addend(address, fixup->target_addend, &value)) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                    fixup->source_line, object->address,
                    "unresolved, ambiguous, or overflowing data expression %s",
                    fixup->target_symbol);
                ok = false;
                continue;
            }
        } else if (fixup->target_addend < 0 ||
                   (uint64_t)fixup->target_addend > UINT32_MAX) {
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                fixup->source_line, object->address,
                "overflowing literal .rel target");
            ok = false;
            continue;
        } else {
            value = (uint32_t)fixup->target_addend;
        }
        if ((uint64_t)fixup->offset + fixup->width > object->size ||
            fixup->width != 4U) {
            return false;
        }
        store_be32(object->bytes + fixup->offset, value);
    }
    return ok;
}

static bool resolve_fixups(
    PorpoiseProgram *program,
    PorpoiseDiagnostics *diagnostics) {
    size_t file_index;
    bool ok = true;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        PorpoiseSourceFile *file = &program->files[file_index];
        size_t object_index;
        for (object_index = 0U; object_index < file->data_object_count;
             object_index++) {
            if (!resolve_object_fixups(
                    program, file, &file->data_objects[object_index],
                    diagnostics)) ok = false;
        }
        for (object_index = 0U; object_index < file->anonymous_data_count;
             object_index++) {
            if (!resolve_object_fixups(
                    program, file,
                    &file->anonymous_data[object_index].storage,
                    diagnostics)) ok = false;
        }
    }
    return ok;
}

static int compare_ranges(const void *left, const void *right) {
    const PorpoiseConcreteRange *a = (const PorpoiseConcreteRange *)left;
    const PorpoiseConcreteRange *b = (const PorpoiseConcreteRange *)right;
    if (a->address != b->address) return a->address < b->address ? -1 : 1;
    if (a->size != b->size) return a->size < b->size ? -1 : 1;
    if (a->file_index != b->file_index) {
        return a->file_index < b->file_index ? -1 : 1;
    }
    return a->object_index < b->object_index ? -1 :
           a->object_index > b->object_index ? 1 : 0;
}

static int compare_spans(const void *left, const void *right) {
    const PorpoiseDataSpan *a = (const PorpoiseDataSpan *)left;
    const PorpoiseDataSpan *b = (const PorpoiseDataSpan *)right;
    if (a->address != b->address) return a->address < b->address ? -1 : 1;
    if (a->source_file_index != b->source_file_index) {
        return a->source_file_index < b->source_file_index ? -1 : 1;
    }
    if (a->data_object_index != b->data_object_index) {
        return a->data_object_index < b->data_object_index ? -1 : 1;
    }
    if (a->source_line != b->source_line) {
        return a->source_line < b->source_line ? -1 : 1;
    }
    if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
    return 0;
}

static bool add_span(
    PorpoiseProgram *program,
    PorpoiseDataSpanKind kind,
    uint32_t address,
    uint32_t size,
    const uint8_t *bytes,
    size_t file_index,
    size_t object_index,
    size_t source_line,
    bool contribution_padding) {
    PorpoiseDataSpan *span;
    uint8_t *copy = NULL;
    if (size == 0U) return true;
    if (kind == PORPOISE_DATA_SPAN_INITIALIZED) {
        if (bytes == NULL) return false;
        copy = (uint8_t *)malloc((size_t)size);
        if (copy == NULL) return false;
        memcpy(copy, bytes, (size_t)size);
    }
    if (program->data_span_count == SIZE_MAX ||
        !porpoise_grow_array(
            (void **)&program->data_spans, &program->data_span_capacity,
            sizeof(*program->data_spans), program->data_span_count + 1U)) {
        free(copy);
        return false;
    }
    span = &program->data_spans[program->data_span_count++];
    memset(span, 0, sizeof(*span));
    span->kind = kind;
    span->address = address;
    span->size = size;
    span->bytes = copy;
    span->source_file_index = file_index;
    span->data_object_index = object_index;
    span->source_line = source_line;
    span->contribution_padding = contribution_padding;
    return true;
}

static bool build_concrete_ranges(
    PorpoiseProgram *program,
    PorpoiseConcreteRange **ranges_out,
    size_t *range_count_out,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseConcreteRange *ranges = NULL;
    size_t count = 0U;
    size_t capacity = 0U;
    size_t file_index;
    bool ok = true;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        size_t object_index;
        size_t word_index;
        for (object_index = 0U; object_index < file->data_object_count;
             object_index++) {
            const PorpoiseDataObject *object =
                &file->data_objects[object_index];
            PorpoiseConcreteRange *range;
            if (object->size == 0U) continue;
            if (count == SIZE_MAX ||
                !porpoise_grow_array(
                    (void **)&ranges, &capacity, sizeof(*ranges), count + 1U)) {
                free(ranges);
                return false;
            }
            range = &ranges[count++];
            range->address = object->address;
            range->size = object->size;
            range->file_index = file_index;
            range->object_index = object_index;
            range->source_line = object->source_line;
        }
        for (object_index = 0U; object_index < file->anonymous_data_count;
             object_index++) {
            const PorpoiseAnonymousData *anonymous =
                &file->anonymous_data[object_index];
            size_t cursor = 0U;
            while (cursor < anonymous->storage.size) {
                size_t end;
                PorpoiseConcreteRange *range;
                if (anonymous->present[cursor] == 0U) {
                    cursor++;
                    continue;
                }
                end = cursor + 1U;
                while (end < anonymous->storage.size &&
                       anonymous->present[end] != 0U) end++;
                if (count == SIZE_MAX || end - cursor > UINT32_MAX ||
                    !porpoise_grow_array(
                        (void **)&ranges, &capacity, sizeof(*ranges),
                        count + 1U)) {
                    free(ranges);
                    return false;
                }
                range = &ranges[count++];
                range->address =
                    anonymous->storage.address + (uint32_t)cursor;
                range->size = (uint32_t)(end - cursor);
                range->file_index = file_index;
                range->object_index = SIZE_MAX - 1U;
                range->source_line = anonymous->storage.source_line;
                cursor = end;
            }
        }
        for (word_index = 0U; word_index < file->data_word_count;
             word_index++) {
            const PorpoiseDataWord *word = &file->data_words[word_index];
            PorpoiseConcreteRange *range;
            if (count == SIZE_MAX ||
                !porpoise_grow_array(
                    (void **)&ranges, &capacity, sizeof(*ranges), count + 1U)) {
                free(ranges);
                return false;
            }
            range = &ranges[count++];
            range->address = word->address;
            range->size = 4U;
            range->file_index = file_index;
            range->object_index = SIZE_MAX;
            range->source_line = word->source_line;
        }
    }
    if (count > 1U) {
        qsort(ranges, count, sizeof(*ranges), compare_ranges);
    }
    for (file_index = 1U; file_index < count; file_index++) {
        const PorpoiseConcreteRange *prior = &ranges[file_index - 1U];
        const PorpoiseConcreteRange *current = &ranges[file_index];
        uint64_t prior_end = (uint64_t)prior->address + prior->size;
        if (current->address < prior_end) {
            const PorpoiseSourceFile *file =
                &program->files[current->file_index];
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                current->source_line, current->address,
                "assembly-derived data overlaps materialized data at 0x%08lX",
                (unsigned long)prior->address);
            ok = false;
        }
    }
    if (!ok) {
        free(ranges);
        return true;
    }
    *ranges_out = ranges;
    *range_count_out = count;
    return true;
}

static bool validate_contribution_coverage(
    const PorpoiseProgram *program,
    const PorpoiseConcreteRange *ranges,
    size_t range_count,
    PorpoiseDiagnostics *diagnostics) {
    size_t file_index;
    bool ok = true;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        size_t anonymous_index;
        for (anonymous_index = 0U;
             anonymous_index < file->anonymous_data_count;
             anonymous_index++) {
            const PorpoiseAnonymousData *anonymous =
                &file->anonymous_data[anonymous_index];
            uint64_t cursor = anonymous->storage.address;
            uint64_t end = cursor + anonymous->storage.size;
            uint64_t hole_end = end;
            size_t range_index;
            for (range_index = 0U;
                 range_index < range_count && cursor < end;
                 range_index++) {
                uint64_t range_start = ranges[range_index].address;
                uint64_t range_end =
                    range_start + ranges[range_index].size;
                if (range_start >= end) break;
                if (ranges[range_index].file_index != file_index ||
                    range_end <= cursor) {
                    continue;
                }
                if (range_start > cursor) {
                    hole_end = range_start;
                    break;
                }
                if (range_end > cursor) cursor = range_end;
            }
            if (cursor < end) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                    anonymous->storage.source_line, (uint32_t)cursor,
                    "section contribution %s has unmaterialized bytes at 0x%08lX (0x%lX bytes remain); add an annotated object or explicit data emitter",
                    anonymous->storage.section, (unsigned long)cursor,
                    (unsigned long)(hole_end - cursor));
                ok = false;
            }
        }
    }
    return ok;
}

static bool validate_data_names(
    const PorpoiseProgram *program,
    PorpoiseDiagnostics *diagnostics) {
    size_t cursor = 0U;
    bool ok = true;
    while (cursor < program->data_symbol_index_count) {
        size_t group_end = cursor + 1U;
        size_t index;
        while (group_end < program->data_symbol_index_count &&
               strcmp(program->data_symbol_index[cursor].name,
                      program->data_symbol_index[group_end].name) == 0) {
            group_end++;
        }
        for (index = cursor; index < group_end; index++) {
            const PorpoiseProgramDataSymbolIndexEntry *entry =
                &program->data_symbol_index[index];
            uint32_t entry_address = entry->alias != NULL
                                         ? entry->alias->address
                                         : entry->object->address;
            size_t entry_line = entry->alias != NULL
                                    ? entry->alias->source_line
                                    : entry->object->source_line;
            bool entry_global = entry->alias != NULL
                                    ? entry->alias->is_global
                                    : entry->object->is_global;
            size_t prior;
            bool duplicate_reported = false;
            bool global_reported = false;
            for (prior = cursor; prior < index; prior++) {
                const PorpoiseProgramDataSymbolIndexEntry *candidate =
                    &program->data_symbol_index[prior];
                if (!duplicate_reported && candidate->file == entry->file) {
                    porpoise_diagnostics_add(
                        diagnostics, PORPOISE_SEVERITY_ERROR,
                        entry->file->path, entry_line,
                        entry_address,
                        "duplicate data symbol %s in one input file",
                        entry->name);
                    duplicate_reported = true;
                    ok = false;
                }
                if (!global_reported && entry_global &&
                    (candidate->alias != NULL
                         ? candidate->alias->is_global
                         : candidate->object->is_global) &&
                    (candidate->alias != NULL
                         ? candidate->alias->address
                         : candidate->object->address) != entry_address) {
                    porpoise_diagnostics_add(
                        diagnostics, PORPOISE_SEVERITY_ERROR,
                        entry->file->path, entry_line,
                        entry_address,
                        "ambiguous global data symbol %s",
                        entry->name);
                    global_reported = true;
                    ok = false;
                }
            }
        }
        cursor = group_end;
    }
    return ok;
}

static bool build_spans(
    PorpoiseProgram *program,
    PorpoiseDiagnostics *diagnostics) {
    size_t file_index;
    bool ok = true;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        size_t object_index;
        size_t word_index;
        for (object_index = 0U; object_index < file->data_object_count;
             object_index++) {
            const PorpoiseDataObject *object =
                &file->data_objects[object_index];
            size_t cursor = 0U;
            while (cursor < object->size) {
                bool initialized = object->initialized[cursor] != 0U;
                size_t end = cursor + 1U;
                uint64_t address;
                while (end < object->size &&
                       (object->initialized[end] != 0U) == initialized) end++;
                address = (uint64_t)object->address + cursor;
                if (address > UINT32_MAX || end - cursor > UINT32_MAX ||
                    !add_span(
                        program,
                        initialized ? PORPOISE_DATA_SPAN_INITIALIZED
                                    : PORPOISE_DATA_SPAN_ZERO_FILL,
                        (uint32_t)address, (uint32_t)(end - cursor),
                        initialized ? object->bytes + cursor : NULL,
                        file_index, object_index, object->source_line, false)) {
                    goto internal_failure;
                }
                cursor = end;
            }
        }
        for (object_index = 0U; object_index < file->anonymous_data_count;
             object_index++) {
            const PorpoiseAnonymousData *anonymous =
                &file->anonymous_data[object_index];
            size_t cursor = 0U;
            while (cursor < anonymous->storage.size) {
                bool initialized;
                size_t end;
                uint64_t address;
                if (anonymous->present[cursor] == 0U) {
                    cursor++;
                    continue;
                }
                initialized = anonymous->storage.initialized[cursor] != 0U;
                end = cursor + 1U;
                while (end < anonymous->storage.size &&
                       anonymous->present[end] != 0U &&
                       (anonymous->storage.initialized[end] != 0U) ==
                           initialized) {
                    end++;
                }
                address = (uint64_t)anonymous->storage.address + cursor;
                if (address > UINT32_MAX || end - cursor > UINT32_MAX ||
                    !add_span(
                        program,
                        initialized ? PORPOISE_DATA_SPAN_INITIALIZED
                                    : PORPOISE_DATA_SPAN_ZERO_FILL,
                        (uint32_t)address, (uint32_t)(end - cursor),
                        initialized
                            ? anonymous->storage.bytes + cursor
                            : NULL,
                        file_index, SIZE_MAX,
                        anonymous->storage.source_line, false)) {
                    goto internal_failure;
                }
                cursor = end;
            }
        }
        for (word_index = 0U; word_index < file->data_word_count;
             word_index++) {
            const PorpoiseDataWord *word = &file->data_words[word_index];
            uint8_t bytes[4];
            store_be32(bytes, word->word);
            if (!add_span(
                    program, PORPOISE_DATA_SPAN_INITIALIZED, word->address,
                    4U, bytes, file_index, SIZE_MAX, word->source_line,
                    false)) goto internal_failure;
        }
    }
    qsort(
        program->data_spans, program->data_span_count,
        sizeof(*program->data_spans), compare_spans);
    for (file_index = 1U; file_index < program->data_span_count; file_index++) {
        const PorpoiseDataSpan *prior = &program->data_spans[file_index - 1U];
        const PorpoiseDataSpan *current = &program->data_spans[file_index];
        uint64_t prior_end = (uint64_t)prior->address + prior->size;
        if (current->address < prior_end) {
            const PorpoiseSourceFile *file =
                &program->files[current->source_file_index];
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                current->source_line, current->address,
                "assembly-derived data span overlaps span at 0x%08lX",
                (unsigned long)prior->address);
            ok = false;
        }
    }
    return ok;

internal_failure:
    return false;
}

bool porpoise_asm_data_finalize(
    PorpoiseProgram *program,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseConcreteRange *ranges = NULL;
    size_t range_count = 0U;
    bool built_ranges;
    if (!synthesize_referenced_section_bases(program, diagnostics)) {
        return false;
    }
    if (!build_data_symbol_index(program)) return false;
    if (!validate_data_names(program, diagnostics)) return false;
    if (!resolve_fixups(program, diagnostics)) return false;
    built_ranges = build_concrete_ranges(
        program, &ranges, &range_count, diagnostics);
    if (!built_ranges) return false;
    if (porpoise_diagnostics_have_errors(diagnostics) ||
        !validate_contribution_coverage(
            program, ranges, range_count, diagnostics)) {
        free(ranges);
        return false;
    }
    if (!build_spans(program, diagnostics)) {
        free(ranges);
        return false;
    }
    free(ranges);
    return !porpoise_diagnostics_have_errors(diagnostics);
}
