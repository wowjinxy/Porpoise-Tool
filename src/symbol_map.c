#include "porpoise/symbol_map.h"

#include "porpoise/util.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PorpoiseCwMetadata {
    char *name;
    char *object;
    char *library;
    PorpoiseSymbolKind kind;
    PorpoiseSymbolScope scope;
} PorpoiseCwMetadata;

typedef struct PorpoiseCwMetadataList {
    PorpoiseCwMetadata *items;
    size_t count;
    size_t capacity;
} PorpoiseCwMetadataList;

typedef struct PorpoiseSplitRange {
    char *section;
    char *object;
    char *library;
    uint32_t start;
    uint32_t end;
    size_t line;
} PorpoiseSplitRange;

typedef struct PorpoiseSplitRanges {
    PorpoiseSplitRange *items;
    size_t count;
    size_t capacity;
} PorpoiseSplitRanges;

typedef struct PorpoiseMapParser {
    bool strict;
    PorpoiseDiagnostics *diagnostics;
} PorpoiseMapParser;

static bool nullable_string_equal(const char *left, const char *right) {
    if (left == NULL || left[0] == '\0') left = NULL;
    if (right == NULL || right[0] == '\0') right = NULL;
    return left == right ||
           (left != NULL && right != NULL && strcmp(left, right) == 0);
}

static char *duplicate_optional(const char *value) {
    if (value == NULL || value[0] == '\0') return NULL;
    return porpoise_strdup(value);
}

static char *duplicate_trimmed_span(const char *start, size_t length) {
    char *result;
    while (length > 0U && isspace((unsigned char)*start)) {
        start++;
        length--;
    }
    while (length > 0U &&
           isspace((unsigned char)start[length - 1U])) {
        length--;
    }
    result = (char *)malloc(length + 1U);
    if (result == NULL) return NULL;
    if (length > 0U) memcpy(result, start, length);
    result[length] = '\0';
    return result;
}

static void free_symbol(PorpoiseSymbol *symbol) {
    if (symbol == NULL) return;
    free(symbol->name);
    free(symbol->section);
    free(symbol->module);
    free(symbol->object);
    free(symbol->library);
    free(symbol->provenance.path);
    free(symbol->provenance.auxiliary_path);
    memset(symbol, 0, sizeof(*symbol));
}

void porpoise_symbol_catalog_init(PorpoiseSymbolCatalog *catalog) {
    if (catalog != NULL) memset(catalog, 0, sizeof(*catalog));
}

void porpoise_symbol_catalog_free(PorpoiseSymbolCatalog *catalog) {
    size_t index;
    if (catalog == NULL) return;
    for (index = 0U; index < catalog->symbol_count; index++) {
        free_symbol(&catalog->symbols[index]);
    }
    free(catalog->symbols);
    memset(catalog, 0, sizeof(*catalog));
}

void porpoise_symbol_map_load_options_init(
    PorpoiseSymbolMapLoadOptions *options) {
    if (options == NULL) return;
    memset(options, 0, sizeof(*options));
    options->strict = true;
}

static bool parser_message(
    PorpoiseMapParser *parser,
    const char *path,
    size_t line,
    bool always_error,
    const char *format,
    ...) {
    char message[PORPOISE_MESSAGE_CAPACITY];
    va_list arguments;
    bool error = always_error || parser->strict;

    va_start(arguments, format);
    (void)vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    porpoise_diagnostics_add(
        parser->diagnostics,
        error ? PORPOISE_SEVERITY_ERROR : PORPOISE_SEVERITY_WARNING,
        path,
        line,
        0U,
        "%s",
        message);
    return !error;
}

static int invalid_arguments(
    PorpoiseDiagnostics *diagnostics,
    const char *message) {
    if (diagnostics != NULL) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "%s",
            message);
    }
    return PORPOISE_EXIT_INTERNAL;
}

static bool parse_unsigned(
    const char *text,
    int base,
    bool require_hex_prefix,
    uint32_t *value_out) {
    char *end;
    unsigned long long value;
    if (text == NULL || text[0] == '\0' || value_out == NULL) return false;
    if (require_hex_prefix &&
        !(text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))) {
        return false;
    }
    errno = 0;
    value = strtoull(text, &end, base);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        return false;
    }
    *value_out = (uint32_t)value;
    return true;
}

static bool parse_decimal(const char *text) {
    uint32_t ignored;
    return parse_unsigned(text, 10, false, &ignored);
}

static size_t split_tokens(char *text, char **tokens, size_t capacity) {
    size_t count = 0U;
    char *cursor = text;
    while (*cursor != '\0') {
        while (*cursor != '\0' && isspace((unsigned char)*cursor)) cursor++;
        if (*cursor == '\0') break;
        if (count == capacity) return count;
        tokens[count++] = cursor;
        while (*cursor != '\0' && !isspace((unsigned char)*cursor)) cursor++;
        if (*cursor != '\0') *cursor++ = '\0';
    }
    return count;
}

/* Return 1 for a line, 0 for EOF, and -1 for allocation or I/O failure. */
static int read_line(FILE *file, char **line, size_t *capacity) {
    size_t length = 0U;
    int character;
    if (*line == NULL) {
        *capacity = 256U;
        *line = (char *)malloc(*capacity);
        if (*line == NULL) return -1;
    }
    while ((character = fgetc(file)) != EOF) {
        if (length + 1U >= *capacity) {
            size_t next = *capacity > SIZE_MAX / 2U ? 0U : *capacity * 2U;
            char *replacement;
            if (next == 0U) return -1;
            replacement = (char *)realloc(*line, next);
            if (replacement == NULL) return -1;
            *line = replacement;
            *capacity = next;
        }
        if (character == '\n') break;
        (*line)[length++] = (char)character;
    }
    if (character == EOF && length == 0U) {
        return ferror(file) ? -1 : 0;
    }
    if (length > 0U && (*line)[length - 1U] == '\r') length--;
    (*line)[length] = '\0';
    return 1;
}

static bool symbol_identity_equal(
    const PorpoiseSymbol *left,
    const PorpoiseSymbol *right) {
    if (!nullable_string_equal(left->module, right->module) ||
        !nullable_string_equal(left->section, right->section) ||
        strcmp(left->name, right->name) != 0 ||
        left->has_address != right->has_address) {
        return false;
    }
    if (left->has_address) return left->address == right->address;
    return nullable_string_equal(left->object, right->object) &&
           nullable_string_equal(left->library, right->library);
}

static bool symbol_facts_conflict(
    const PorpoiseSymbol *left,
    const PorpoiseSymbol *right) {
    return (left->has_size && right->has_size && left->size != right->size) ||
           (left->kind != PORPOISE_SYMBOL_KIND_UNKNOWN &&
            right->kind != PORPOISE_SYMBOL_KIND_UNKNOWN &&
            left->kind != right->kind) ||
           (left->scope != PORPOISE_SYMBOL_SCOPE_UNKNOWN &&
            right->scope != PORPOISE_SYMBOL_SCOPE_UNKNOWN &&
            left->scope != right->scope) ||
           left->used != right->used ||
           (left->object != NULL && right->object != NULL &&
            strcmp(left->object, right->object) != 0) ||
           (left->library != NULL && right->library != NULL &&
            strcmp(left->library, right->library) != 0);
}

static bool symbol_same_origin(
    const PorpoiseSymbol *left,
    const PorpoiseSymbol *right) {
    return left->provenance.kind == right->provenance.kind &&
           left->provenance.line == right->provenance.line &&
           nullable_string_equal(
               left->provenance.path, right->provenance.path);
}

static bool catalog_accept_symbol(
    PorpoiseSymbolCatalog *catalog,
    PorpoiseSymbol *symbol,
    PorpoiseMapParser *parser) {
    size_t index;
    for (index = 0U; index < catalog->symbol_count; index++) {
        const PorpoiseSymbol *existing = &catalog->symbols[index];
        if (!symbol_identity_equal(existing, symbol)) continue;
        if (symbol_facts_conflict(existing, symbol)) {
            return parser_message(
                parser,
                symbol->provenance.path,
                symbol->provenance.line,
                true,
                "symbol '%s' conflicts with %s:%lu",
                symbol->name,
                existing->provenance.path != NULL
                    ? existing->provenance.path
                    : "an earlier source",
                (unsigned long)existing->provenance.line);
        }
        if (symbol_same_origin(existing, symbol)) {
            free_symbol(symbol);
            return true;
        }
    }
    if (!porpoise_grow_array(
            (void **)&catalog->symbols,
            &catalog->symbol_capacity,
            sizeof(*catalog->symbols),
            catalog->symbol_count + 1U)) {
        parser_message(
            parser,
            symbol->provenance.path,
            symbol->provenance.line,
            true,
            "out of memory while adding symbol '%s'",
            symbol->name);
        return false;
    }
    catalog->symbols[catalog->symbol_count++] = *symbol;
    memset(symbol, 0, sizeof(*symbol));
    return true;
}

static bool catalog_validate_merge(
    const PorpoiseSymbolCatalog *destination,
    const PorpoiseSymbolCatalog *source,
    PorpoiseMapParser *parser) {
    size_t source_index;
    for (source_index = 0U;
         source_index < source->symbol_count;
         source_index++) {
        const PorpoiseSymbol *candidate = &source->symbols[source_index];
        size_t destination_index;
        for (destination_index = 0U;
             destination_index < destination->symbol_count;
             destination_index++) {
            const PorpoiseSymbol *existing =
                &destination->symbols[destination_index];
            if (symbol_identity_equal(existing, candidate) &&
                symbol_facts_conflict(existing, candidate)) {
                return parser_message(
                    parser,
                    candidate->provenance.path,
                    candidate->provenance.line,
                    true,
                    "symbol '%s' conflicts with %s:%lu",
                    candidate->name,
                    existing->provenance.path != NULL
                        ? existing->provenance.path
                        : "an earlier source",
                    (unsigned long)existing->provenance.line);
            }
        }
    }
    return true;
}

static bool catalog_commit(
    PorpoiseSymbolCatalog *destination,
    PorpoiseSymbolCatalog *source,
    PorpoiseMapParser *parser) {
    size_t index;
    if (!catalog_validate_merge(destination, source, parser)) return false;
    if (!porpoise_grow_array(
            (void **)&destination->symbols,
            &destination->symbol_capacity,
            sizeof(*destination->symbols),
            destination->symbol_count + source->symbol_count)) {
        parser_message(
            parser, NULL, 0U, true,
            "out of memory while merging symbol catalogs");
        return false;
    }
    for (index = 0U; index < source->symbol_count; index++) {
        PorpoiseSymbol *candidate = &source->symbols[index];
        bool duplicate = false;
        size_t prior;
        for (prior = 0U; prior < destination->symbol_count; prior++) {
            if (symbol_identity_equal(
                    &destination->symbols[prior], candidate) &&
                symbol_same_origin(
                    &destination->symbols[prior], candidate)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            free_symbol(candidate);
        } else {
            destination->symbols[destination->symbol_count++] = *candidate;
            memset(candidate, 0, sizeof(*candidate));
        }
    }
    return true;
}

static PorpoiseSymbolKind parse_kind(const char *text) {
    if (strcmp(text, "func") == 0 || strcmp(text, "function") == 0)
        return PORPOISE_SYMBOL_KIND_FUNCTION;
    if (strcmp(text, "object") == 0) return PORPOISE_SYMBOL_KIND_OBJECT;
    if (strcmp(text, "section") == 0) return PORPOISE_SYMBOL_KIND_SECTION;
    if (strcmp(text, "label") == 0 || strcmp(text, "notype") == 0)
        return PORPOISE_SYMBOL_KIND_LABEL;
    return PORPOISE_SYMBOL_KIND_UNKNOWN;
}

static PorpoiseSymbolScope parse_scope(const char *text) {
    if (strcmp(text, "local") == 0) return PORPOISE_SYMBOL_SCOPE_LOCAL;
    if (strcmp(text, "global") == 0) return PORPOISE_SYMBOL_SCOPE_GLOBAL;
    if (strcmp(text, "weak") == 0) return PORPOISE_SYMBOL_SCOPE_WEAK;
    return PORPOISE_SYMBOL_SCOPE_UNKNOWN;
}

static void free_cw_metadata_list(PorpoiseCwMetadataList *metadata) {
    size_t index;
    for (index = 0U; index < metadata->count; index++) {
        free(metadata->items[index].name);
        free(metadata->items[index].object);
        free(metadata->items[index].library);
    }
    free(metadata->items);
    memset(metadata, 0, sizeof(*metadata));
}

static bool parse_owner_tokens(
    char **tokens,
    size_t token_count,
    size_t owner_index,
    char **library_out,
    char **object_out) {
    *library_out = NULL;
    *object_out = NULL;
    if (owner_index >= token_count) return true;
    if (token_count - owner_index == 1U) {
        *object_out = porpoise_strdup(tokens[owner_index]);
        return *object_out != NULL;
    } else {
        *library_out = porpoise_strdup(tokens[owner_index]);
        *object_out = porpoise_strdup(tokens[owner_index + 1U]);
    }
    return *library_out != NULL && *object_out != NULL;
}

static bool cw_collect_metadata(
    char *line,
    PorpoiseCwMetadataList *metadata) {
    char *found = strstr(line, ") found in ");
    char *open;
    char *name_start;
    char *owner;
    char *tokens[4];
    size_t token_count;
    PorpoiseCwMetadata item;
    char *comma;
    if (found == NULL) return true;
    open = found;
    while (open > line && *open != '(') open--;
    if (*open != '(' || open == line) return true;
    *open = '\0';
    name_start = strrchr(line, ']');
    name_start = name_start == NULL ? line : name_start + 1;
    porpoise_trim(name_start);
    if (name_start[0] == '\0' || strncmp(name_start, ">>>", 3U) == 0)
        return true;

    *found = '\0';
    comma = strchr(open + 1, ',');
    memset(&item, 0, sizeof(item));
    if (comma != NULL) {
        *comma = '\0';
        item.kind = parse_kind(open + 1);
        item.scope = parse_scope(comma + 1);
    } else {
        item.kind = parse_kind(open + 1);
    }
    owner = found + strlen(") found in ");
    token_count = split_tokens(owner, tokens, 4U);
    item.name = porpoise_strdup(name_start);
    if (token_count == 1U) {
        item.object = porpoise_strdup(tokens[0]);
    } else if (token_count >= 2U) {
        item.library = porpoise_strdup(tokens[0]);
        item.object = porpoise_strdup(tokens[1]);
    }
    if (item.name == NULL ||
        (token_count >= 1U && item.object == NULL) ||
        (token_count >= 2U && item.library == NULL)) {
        free(item.name);
        free(item.object);
        free(item.library);
        return false;
    }
    if (!porpoise_grow_array(
            (void **)&metadata->items,
            &metadata->capacity,
            sizeof(*metadata->items),
            metadata->count + 1U)) {
        free(item.name);
        free(item.object);
        free(item.library);
        return false;
    }
    metadata->items[metadata->count++] = item;
    return true;
}

static const PorpoiseCwMetadata *cw_find_metadata(
    const PorpoiseCwMetadataList *metadata,
    const char *name,
    const char *library,
    const char *object) {
    const PorpoiseCwMetadata *fallback = NULL;
    size_t index;
    for (index = 0U; index < metadata->count; index++) {
        const PorpoiseCwMetadata *item = &metadata->items[index];
        if (strcmp(item->name, name) != 0) continue;
        if (nullable_string_equal(item->library, library) &&
            nullable_string_equal(item->object, object)) {
            return item;
        }
        if (fallback == NULL) fallback = item;
        else fallback = NULL;
    }
    return fallback;
}

static PorpoiseSymbolKind infer_cw_kind(
    const char *section,
    const char *name,
    bool entry) {
    if (entry) return PORPOISE_SYMBOL_KIND_LABEL;
    if (strcmp(section, name) == 0) return PORPOISE_SYMBOL_KIND_SECTION;
    if (strcmp(section, ".init") == 0 ||
        strstr(section, "text") != NULL) {
        return PORPOISE_SYMBOL_KIND_FUNCTION;
    }
    return PORPOISE_SYMBOL_KIND_OBJECT;
}

static bool initialize_symbol_strings(
    PorpoiseSymbol *symbol,
    const char *name,
    const char *section,
    const char *module,
    const char *object,
    const char *library,
    const char *path) {
    symbol->name = porpoise_strdup(name);
    symbol->section = duplicate_optional(section);
    symbol->module = duplicate_optional(module);
    symbol->object = duplicate_optional(object);
    symbol->library = duplicate_optional(library);
    symbol->provenance.path = porpoise_strdup(path);
    return symbol->name != NULL && symbol->provenance.path != NULL &&
           (section == NULL || section[0] == '\0' || symbol->section != NULL) &&
           (module == NULL || module[0] == '\0' || symbol->module != NULL) &&
           (object == NULL || object[0] == '\0' || symbol->object != NULL) &&
           (library == NULL || library[0] == '\0' || symbol->library != NULL);
}

static bool cw_parse_layout_record(
    char *line,
    const char *section,
    const char *module,
    const char *path,
    size_t line_number,
    const PorpoiseCwMetadataList *metadata,
    PorpoiseSymbolCatalog *temporary,
    PorpoiseMapParser *parser) {
    char *tokens[16];
    size_t token_count = split_tokens(line, tokens, 16U);
    size_t name_index;
    size_t owner_index;
    bool used;
    bool entry = false;
    uint32_t address = 0U;
    uint32_t size;
    char *library = NULL;
    char *object = NULL;
    PorpoiseSymbol symbol;
    const PorpoiseCwMetadata *item_metadata;

    if (token_count == 0U || strcmp(tokens[0], "Starting") == 0 ||
        strcmp(tokens[0], "address") == 0 || tokens[0][0] == '-') {
        return true;
    }
    if (strcmp(tokens[0], "UNUSED") == 0) {
        used = false;
        if (token_count < 4U ||
            !parse_unsigned(tokens[1], 16, false, &size) ||
            strcmp(tokens[2], "........") != 0) {
            return parser_message(
                parser, path, line_number, false,
                "malformed CodeWarrior UNUSED layout record");
        }
        name_index = 3U;
        owner_index = 4U;
    } else {
        used = true;
        if (token_count < 5U ||
            !parse_unsigned(tokens[0], 16, false, &address) ||
            !parse_unsigned(tokens[1], 16, false, &size) ||
            !parse_unsigned(tokens[2], 16, false, &address)) {
            return parser_message(
                parser, path, line_number, false,
                "malformed CodeWarrior section-layout record");
        }
        if (parse_decimal(tokens[3])) {
            name_index = 4U;
            owner_index = 5U;
        } else {
            name_index = 3U;
            owner_index = 4U;
            if (owner_index < token_count &&
                strcmp(tokens[owner_index], "(entry") == 0) {
                size_t close = owner_index;
                while (close < token_count &&
                       strchr(tokens[close], ')') == NULL) {
                    close++;
                }
                if (close == token_count) {
                    return parser_message(
                        parser, path, line_number, false,
                        "unterminated CodeWarrior entry-of annotation");
                }
                entry = true;
                owner_index = close + 1U;
            } else {
                return parser_message(
                    parser, path, line_number, false,
                    "CodeWarrior layout record has no alignment or entry annotation");
            }
        }
        if ((uint64_t)address + (uint64_t)size >
            (uint64_t)UINT32_MAX + UINT64_C(1)) {
            return parser_message(
                parser, path, line_number, false,
                "CodeWarrior symbol '%s' extends past the 32-bit address space",
                tokens[name_index]);
        }
    }

    if (!parse_owner_tokens(
            tokens, token_count, owner_index, &library, &object)) {
        free(library);
        free(object);
        return parser_message(
            parser, path, line_number, true,
            "out of memory while reading CodeWarrior ownership");
    }
    memset(&symbol, 0, sizeof(symbol));
    if (!initialize_symbol_strings(
            &symbol, tokens[name_index], section, module,
            object, library, path)) {
        free(library);
        free(object);
        free_symbol(&symbol);
        return parser_message(
            parser, path, line_number, true,
            "out of memory while reading CodeWarrior symbol");
    }
    free(library);
    free(object);
    symbol.used = used;
    symbol.has_address = used;
    symbol.address = address;
    symbol.has_size = true;
    symbol.size = size;
    symbol.kind = infer_cw_kind(section, symbol.name, entry);
    symbol.scope = PORPOISE_SYMBOL_SCOPE_UNKNOWN;
    symbol.provenance.kind = PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP;
    symbol.provenance.line = line_number;

    item_metadata = cw_find_metadata(
        metadata, symbol.name, symbol.library, symbol.object);
    if (item_metadata != NULL) {
        if (!entry && item_metadata->kind != PORPOISE_SYMBOL_KIND_UNKNOWN)
            symbol.kind = item_metadata->kind;
        symbol.scope = item_metadata->scope;
    }
    if (!catalog_accept_symbol(temporary, &symbol, parser)) {
        free_symbol(&symbol);
        return !parser->strict && !porpoise_diagnostics_have_errors(
            parser->diagnostics);
    }
    return true;
}

static bool codewarrior_layout_header(
    char *line,
    char **section_out) {
    static const char suffix[] = " section layout";
    size_t length;
    size_t suffix_length = sizeof(suffix) - 1U;
    porpoise_trim(line);
    length = strlen(line);
    if (length <= suffix_length ||
        strcmp(line + length - suffix_length, suffix) != 0) {
        return false;
    }
    line[length - suffix_length] = '\0';
    porpoise_trim(line);
    if (line[0] == '\0') return false;
    *section_out = line;
    return true;
}

static bool cw_parse_linker_symbol(
    char *line,
    const char *module,
    const char *path,
    size_t line_number,
    PorpoiseSymbolCatalog *temporary,
    PorpoiseMapParser *parser) {
    char *tokens[3];
    size_t token_count = split_tokens(line, tokens, 3U);
    PorpoiseSymbol symbol;
    uint32_t address;
    if (token_count != 2U ||
        !parse_unsigned(tokens[1], 16, false, &address)) {
        return parser_message(
            parser, path, line_number, false,
            "malformed CodeWarrior linker-generated symbol record");
    }
    memset(&symbol, 0, sizeof(symbol));
    if (!initialize_symbol_strings(
            &symbol, tokens[0], NULL, module, NULL, NULL, path)) {
        free_symbol(&symbol);
        return parser_message(
            parser, path, line_number, true,
            "out of memory while reading CodeWarrior linker-generated symbol");
    }
    symbol.used = true;
    symbol.has_address = true;
    symbol.address = address;
    symbol.kind = PORPOISE_SYMBOL_KIND_LABEL;
    symbol.scope = PORPOISE_SYMBOL_SCOPE_GLOBAL;
    symbol.provenance.kind = PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP;
    symbol.provenance.line = line_number;
    if (!catalog_accept_symbol(temporary, &symbol, parser)) {
        free_symbol(&symbol);
        return !parser->strict &&
               !porpoise_diagnostics_have_errors(parser->diagnostics);
    }
    return true;
}

int porpoise_symbol_catalog_load_codewarrior(
    PorpoiseSymbolCatalog *catalog,
    const char *map_path,
    const PorpoiseSymbolMapLoadOptions *options,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSymbolMapLoadOptions defaults;
    PorpoiseMapParser parser;
    PorpoiseSymbolCatalog temporary;
    PorpoiseCwMetadataList metadata;
    FILE *file;
    char *line = NULL;
    size_t line_capacity = 0U;
    size_t line_number = 0U;
    char *section = NULL;
    bool in_layout = false;
    bool in_linker_symbols = false;
    bool layout_has_header = false;
    bool success = true;
    int read_result;

    if (catalog == NULL || map_path == NULL || map_path[0] == '\0' ||
        diagnostics == NULL) {
        return invalid_arguments(
            diagnostics, "CodeWarrior map load arguments are invalid");
    }
    porpoise_symbol_map_load_options_init(&defaults);
    if (options == NULL) options = &defaults;
    parser.strict = options->strict;
    parser.diagnostics = diagnostics;
    porpoise_symbol_catalog_init(&temporary);
    memset(&metadata, 0, sizeof(metadata));

    file = fopen(map_path, "rb");
    if (file == NULL) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            map_path, 0U, 0U,
            "failed to open CodeWarrior map");
        return PORPOISE_EXIT_IO;
    }
    while ((read_result = read_line(
                file, &line, &line_capacity)) == 1) {
        char *header_line;
        char *record_line;
        char *header_section = NULL;
        line_number++;
        header_line = porpoise_strdup(line);
        if (header_line == NULL) {
            success = parser_message(
                &parser, map_path, line_number, true,
                "out of memory while reading CodeWarrior map");
            break;
        }
        if (codewarrior_layout_header(header_line, &header_section)) {
            char *replacement = porpoise_strdup(header_section);
            free(header_line);
            if (replacement == NULL) {
                success = parser_message(
                    &parser, map_path, line_number, true,
                    "out of memory while reading section name");
                break;
            }
            free(section);
            section = replacement;
            in_layout = true;
            in_linker_symbols = false;
            layout_has_header = false;
            continue;
        }
        porpoise_trim(header_line);
        if (strcmp(header_line, "Linker generated symbols:") == 0) {
            free(header_line);
            in_layout = false;
            in_linker_symbols = true;
            continue;
        }
        free(header_line);
        if (in_linker_symbols) {
            record_line = porpoise_strdup(line);
            if (record_line == NULL) {
                success = parser_message(
                    &parser, map_path, line_number, true,
                    "out of memory while reading CodeWarrior linker-generated symbols");
                break;
            }
            porpoise_trim(record_line);
            if (record_line[0] == '\0') {
                free(record_line);
                in_linker_symbols = false;
                continue;
            }
            success = cw_parse_linker_symbol(
                record_line, options->module, map_path, line_number,
                &temporary, &parser);
            free(record_line);
            if (!success) break;
            continue;
        }
        if (!in_layout) {
            if (!cw_collect_metadata(line, &metadata)) {
                success = parser_message(
                    &parser, map_path, line_number, true,
                    "out of memory while reading CodeWarrior metadata");
                break;
            }
            continue;
        }
        record_line = porpoise_strdup(line);
        if (record_line == NULL) {
            success = parser_message(
                &parser, map_path, line_number, true,
                "out of memory while reading CodeWarrior layout");
            break;
        }
        porpoise_trim(record_line);
        if (record_line[0] == '\0') {
            free(record_line);
            in_layout = false;
            continue;
        }
        if (strncmp(record_line, "Starting", 8U) == 0 ||
            strncmp(record_line, "address", 7U) == 0 ||
            record_line[0] == '-') {
            layout_has_header = true;
            free(record_line);
            continue;
        }
        if (!layout_has_header) {
            free(record_line);
            success = parser_message(
                &parser, map_path, line_number, false,
                "CodeWarrior section layout is missing its column header");
            if (!success) break;
            continue;
        }
        success = cw_parse_layout_record(
            record_line, section, options->module, map_path, line_number,
            &metadata, &temporary, &parser);
        free(record_line);
        if (!success) break;
    }
    if (read_result < 0 && success) {
        success = parser_message(
            &parser, map_path, line_number, true,
            "failed while reading CodeWarrior map");
    }
    fclose(file);
    free(line);
    free(section);
    free_cw_metadata_list(&metadata);
    if (success) success = catalog_commit(catalog, &temporary, &parser);
    porpoise_symbol_catalog_free(&temporary);
    return success ? PORPOISE_EXIT_OK : PORPOISE_EXIT_TRANSLATION;
}

static void free_split_ranges(PorpoiseSplitRanges *ranges) {
    size_t index;
    for (index = 0U; index < ranges->count; index++) {
        free(ranges->items[index].section);
        free(ranges->items[index].object);
        free(ranges->items[index].library);
    }
    free(ranges->items);
    memset(ranges, 0, sizeof(*ranges));
}

static bool split_owner(
    const char *heading,
    char **library_out,
    char **object_out) {
    const char *open = strchr(heading, '(');
    size_t length = strlen(heading);
    const char *space = strchr(heading, ' ');
    *library_out = NULL;
    *object_out = NULL;
    if (open != NULL && open > heading && length > 0U &&
        heading[length - 1U] == ')' &&
        (size_t)(open - heading) >= 2U &&
        open[-2] == '.' && open[-1] == 'a') {
        *library_out = duplicate_trimmed_span(
            heading, (size_t)(open - heading));
        *object_out = duplicate_trimmed_span(
            open + 1, length - (size_t)(open - heading) - 2U);
    } else if (space != NULL && space > heading &&
               (size_t)(space - heading) >= 2U &&
               space[-2] == '.' && space[-1] == 'a') {
        *library_out = duplicate_trimmed_span(
            heading, (size_t)(space - heading));
        *object_out = duplicate_trimmed_span(
            space + 1, length - (size_t)(space + 1 - heading));
    } else {
        *object_out = porpoise_strdup(heading);
    }
    return *object_out != NULL &&
           (open == NULL || *library_out != NULL ||
            heading[length - 1U] != ')') &&
           (space == NULL || *library_out != NULL ||
            !((size_t)(space - heading) >= 2U &&
              space[-2] == '.' && space[-1] == 'a'));
}

static bool ranges_overlap(
    const PorpoiseSplitRange *left,
    const PorpoiseSplitRange *right) {
    return strcmp(left->section, right->section) == 0 &&
           left->start < right->end && right->start < left->end;
}

static int compare_split_ranges(const void *left_value, const void *right_value) {
    const PorpoiseSplitRange *left =
        (const PorpoiseSplitRange *)left_value;
    const PorpoiseSplitRange *right =
        (const PorpoiseSplitRange *)right_value;
    int section_order = strcmp(left->section, right->section);
    if (section_order != 0) return section_order;
    if (left->start < right->start) return -1;
    if (left->start > right->start) return 1;
    if (left->end < right->end) return -1;
    if (left->end > right->end) return 1;
    return 0;
}

static bool parse_split_record(
    char *line,
    const char *heading,
    const char *path,
    size_t line_number,
    PorpoiseSplitRanges *ranges,
    PorpoiseMapParser *parser) {
    char *tokens[12];
    size_t token_count = split_tokens(line, tokens, 12U);
    size_t index;
    const char *start_text = NULL;
    const char *end_text = NULL;
    PorpoiseSplitRange range;
    if (token_count == 0U) return true;
    for (index = 1U; index < token_count; index++) {
        if (strncmp(tokens[index], "start:", 6U) == 0)
            start_text = tokens[index] + 6U;
        else if (strncmp(tokens[index], "end:", 4U) == 0)
            end_text = tokens[index] + 4U;
    }
    if (start_text == NULL || end_text == NULL) {
        return parser_message(
            parser, path, line_number, false,
            "malformed DTK split range");
    }
    memset(&range, 0, sizeof(range));
    if (!parse_unsigned(start_text, 0, true, &range.start) ||
        !parse_unsigned(end_text, 0, true, &range.end) ||
        range.end < range.start) {
        return parser_message(
            parser, path, line_number, false,
            "invalid DTK split address range");
    }
    range.section = porpoise_strdup(tokens[0]);
    range.line = line_number;
    if (range.section == NULL ||
        !split_owner(heading, &range.library, &range.object)) {
        free(range.section);
        free(range.library);
        free(range.object);
        return parser_message(
            parser, path, line_number, true,
            "out of memory while reading DTK split range");
    }
    for (index = 0U; index < ranges->count; index++) {
        if (ranges_overlap(&ranges->items[index], &range)) {
            bool keep_going = parser_message(
                parser, path, line_number, false,
                "DTK split range overlaps line %lu in section '%s'",
                (unsigned long)ranges->items[index].line,
                range.section);
            free(range.section);
            free(range.library);
            free(range.object);
            return keep_going;
        }
    }
    if (!porpoise_grow_array(
            (void **)&ranges->items,
            &ranges->capacity,
            sizeof(*ranges->items),
            ranges->count + 1U)) {
        free(range.section);
        free(range.library);
        free(range.object);
        return parser_message(
            parser, path, line_number, true,
            "out of memory while adding DTK split range");
    }
    ranges->items[ranges->count++] = range;
    return true;
}

static bool load_dtk_splits(
    const char *path,
    PorpoiseSplitRanges *ranges,
    PorpoiseMapParser *parser) {
    FILE *file;
    char *line = NULL;
    size_t capacity = 0U;
    size_t line_number = 0U;
    char *heading = NULL;
    bool in_sections = false;
    bool success = true;
    int read_result;
    if (path == NULL || path[0] == '\0') return true;
    file = fopen(path, "rb");
    if (file == NULL) {
        parser_message(
            parser, path, 0U, true,
            "failed to open DTK splits file");
        return false;
    }
    while ((read_result = read_line(file, &line, &capacity)) == 1) {
        char *trimmed;
        char *heading_separator;
        size_t length;
        bool indented;
        line_number++;
        indented = line[0] != '\0' && isspace((unsigned char)line[0]);
        trimmed = porpoise_strdup(line);
        if (trimmed == NULL) {
            success = parser_message(
                parser, path, line_number, true,
                "out of memory while reading DTK splits");
            break;
        }
        porpoise_trim(trimmed);
        length = strlen(trimmed);
        if (length == 0U || trimmed[0] == '#') {
            free(trimmed);
            continue;
        }
        if (strcmp(trimmed, "Sections:") == 0) {
            free(heading);
            heading = NULL;
            in_sections = true;
            free(trimmed);
            continue;
        }
        heading_separator = indented ? NULL : strchr(trimmed, ':');
        if (heading_separator != NULL) {
            char *replacement;
            *heading_separator = '\0';
            porpoise_trim(trimmed);
            replacement = porpoise_strdup(trimmed);
            free(trimmed);
            if (replacement == NULL) {
                success = parser_message(
                    parser, path, line_number, true,
                    "out of memory while reading DTK split owner");
                break;
            }
            free(heading);
            heading = replacement;
            in_sections = false;
            continue;
        }
        if (in_sections && heading == NULL) {
            /* Section declarations carry no symbol ownership. */
            free(trimmed);
            continue;
        }
        if (heading == NULL) {
            success = parser_message(
                parser, path, line_number, false,
                "DTK split range appears before an object heading");
            free(trimmed);
            if (!success) break;
            continue;
        }
        success = parse_split_record(
            trimmed, heading, path, line_number, ranges, parser);
        free(trimmed);
        if (!success) break;
    }
    if (read_result < 0 && success) {
        success = parser_message(
            parser, path, line_number, true,
            "failed while reading DTK splits");
    }
    fclose(file);
    free(line);
    free(heading);
    if (success && ranges->count > 1U) {
        qsort(
            ranges->items,
            ranges->count,
            sizeof(*ranges->items),
            compare_split_ranges);
    }
    return success;
}

static bool dtk_parse_attributes(
    char *attributes,
    const char *path,
    size_t line_number,
    PorpoiseSymbol *symbol,
    PorpoiseMapParser *parser) {
    char *tokens[24];
    size_t token_count = split_tokens(attributes, tokens, 24U);
    size_t index;
    bool have_kind = false;
    bool have_scope = false;
    bool have_size = false;
    for (index = 0U; index < token_count; index++) {
        if (strncmp(tokens[index], "type:", 5U) == 0) {
            PorpoiseSymbolKind kind = parse_kind(tokens[index] + 5U);
            if (kind == PORPOISE_SYMBOL_KIND_UNKNOWN) {
                if (!parser_message(
                        parser, path, line_number, false,
                        "unknown DTK symbol type '%s'", tokens[index] + 5U))
                    return false;
                continue;
            }
            if (have_kind && symbol->kind != kind) {
                return parser_message(
                    parser, path, line_number, false,
                    "conflicting DTK symbol type attributes");
            }
            symbol->kind = kind;
            have_kind = true;
        } else if (strncmp(tokens[index], "scope:", 6U) == 0) {
            PorpoiseSymbolScope scope = parse_scope(tokens[index] + 6U);
            if (scope == PORPOISE_SYMBOL_SCOPE_UNKNOWN) {
                if (!parser_message(
                        parser, path, line_number, false,
                        "unknown DTK symbol scope '%s'", tokens[index] + 6U))
                    return false;
                continue;
            }
            if (have_scope && symbol->scope != scope) {
                return parser_message(
                    parser, path, line_number, false,
                    "conflicting DTK symbol scope attributes");
            }
            symbol->scope = scope;
            have_scope = true;
        } else if (strncmp(tokens[index], "size:", 5U) == 0) {
            uint32_t size;
            if (!parse_unsigned(tokens[index] + 5U, 0, true, &size)) {
                return parser_message(
                    parser, path, line_number, false,
                    "invalid DTK symbol size '%s'", tokens[index] + 5U);
            }
            if (have_size && symbol->size != size) {
                return parser_message(
                    parser, path, line_number, false,
                    "conflicting DTK symbol size attributes");
            }
            symbol->size = size;
            symbol->has_size = true;
            have_size = true;
        } else if (strncmp(tokens[index], "align:", 6U) == 0) {
            if (!parse_decimal(tokens[index] + 6U)) {
                return parser_message(
                    parser, path, line_number, false,
                    "invalid DTK symbol alignment '%s'", tokens[index] + 6U);
            }
        }
    }
    return true;
}

static bool assign_dtk_split(
    PorpoiseSymbol *symbol,
    const PorpoiseSplitRanges *ranges,
    const char *splits_path,
    PorpoiseMapParser *parser) {
    const PorpoiseSplitRange *match = NULL;
    size_t first = 0U;
    size_t last = ranges->count;
    size_t section_end;
    size_t low;
    size_t high;

    /* Ranges are sorted by section and start after splits.txt is loaded. */
    while (first < last) {
        size_t middle = first + (last - first) / 2U;
        int order = strcmp(ranges->items[middle].section, symbol->section);
        if (order < 0) first = middle + 1U;
        else last = middle;
    }
    if (first == ranges->count ||
        strcmp(ranges->items[first].section, symbol->section) != 0) {
        return true;
    }
    section_end = first;
    while (section_end < ranges->count &&
           strcmp(ranges->items[section_end].section, symbol->section) == 0) {
        section_end++;
    }
    low = first;
    high = section_end;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (ranges->items[middle].start <= symbol->address)
            low = middle + 1U;
        else
            high = middle;
    }
    if (low > first) {
        const PorpoiseSplitRange *candidate = &ranges->items[low - 1U];
        if (symbol->address < candidate->end) match = candidate;
    }
    if (match == NULL) return true;
    if (symbol->has_size &&
        (uint64_t)symbol->address + symbol->size > match->end) {
        return parser_message(
            parser, symbol->provenance.path, symbol->provenance.line, false,
            "symbol '%s' crosses the DTK split boundary at 0x%08x",
            symbol->name, match->end);
    }
    symbol->object = duplicate_optional(match->object);
    symbol->library = duplicate_optional(match->library);
    symbol->provenance.auxiliary_path = duplicate_optional(splits_path);
    symbol->provenance.auxiliary_line = match->line;
    if ((match->object != NULL && symbol->object == NULL) ||
        (match->library != NULL && symbol->library == NULL) ||
        (splits_path != NULL && splits_path[0] != '\0' &&
         symbol->provenance.auxiliary_path == NULL)) {
        return parser_message(
            parser, symbol->provenance.path, symbol->provenance.line, true,
            "out of memory while assigning DTK split ownership");
    }
    return true;
}

static bool parse_dtk_symbol(
    char *line,
    const char *symbols_path,
    const char *splits_path,
    const char *module,
    size_t line_number,
    const PorpoiseSplitRanges *ranges,
    PorpoiseSymbolCatalog *temporary,
    PorpoiseMapParser *parser) {
    char *comment = strstr(line, "//");
    char *equals;
    char *colon;
    char *semicolon;
    char *name = NULL;
    char *section = NULL;
    char *address_text = NULL;
    PorpoiseSymbol symbol;
    bool success = false;

    if (comment != NULL) {
        *comment = '\0';
        comment += 2;
    }
    porpoise_trim(line);
    if (line[0] == '\0' || line[0] == '#') return true;
    equals = strchr(line, '=');
    colon = equals == NULL ? NULL : strchr(equals + 1, ':');
    semicolon = colon == NULL ? NULL : strchr(colon + 1, ';');
    if (equals == NULL || colon == NULL || semicolon == NULL) {
        return parser_message(
            parser, symbols_path, line_number, false,
            "malformed DTK symbol record");
    }
    *equals = '\0';
    *colon = '\0';
    *semicolon = '\0';
    if (semicolon[1] != '\0') {
        char *trailing = semicolon + 1;
        porpoise_trim(trailing);
        if (trailing[0] != '\0') {
            return parser_message(
                parser, symbols_path, line_number, false,
                "unexpected text after DTK symbol record");
        }
    }
    name = duplicate_trimmed_span(line, strlen(line));
    section = duplicate_trimmed_span(equals + 1, strlen(equals + 1));
    address_text = duplicate_trimmed_span(colon + 1, strlen(colon + 1));
    if (name == NULL || section == NULL || address_text == NULL) {
        parser_message(
            parser, symbols_path, line_number, true,
            "out of memory while reading DTK symbol");
        goto cleanup;
    }
    if (name[0] == '\0' || section[0] == '\0') {
        parser_message(
            parser, symbols_path, line_number, false,
            "DTK symbol name and section must not be empty");
        goto cleanup;
    }
    memset(&symbol, 0, sizeof(symbol));
    if (!initialize_symbol_strings(
            &symbol, name, section, module, NULL, NULL, symbols_path)) {
        free_symbol(&symbol);
        parser_message(
            parser, symbols_path, line_number, true,
            "out of memory while reading DTK symbol");
        goto cleanup;
    }
    symbol.used = true;
    symbol.has_address = true;
    symbol.provenance.kind = PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS;
    symbol.provenance.line = line_number;
    if (!parse_unsigned(address_text, 0, true, &symbol.address)) {
        free_symbol(&symbol);
        parser_message(
            parser, symbols_path, line_number, false,
            "invalid DTK symbol address '%s'", address_text);
        goto cleanup;
    }
    if (comment != NULL &&
        !dtk_parse_attributes(
            comment, symbols_path, line_number, &symbol, parser)) {
        free_symbol(&symbol);
        goto cleanup;
    }
    if (symbol.has_size &&
        (uint64_t)symbol.address + symbol.size >
        (uint64_t)UINT32_MAX + UINT64_C(1)) {
        free_symbol(&symbol);
        parser_message(
            parser, symbols_path, line_number, false,
            "DTK symbol '%s' extends past the 32-bit address space", name);
        goto cleanup;
    }
    if (!assign_dtk_split(&symbol, ranges, splits_path, parser)) {
        free_symbol(&symbol);
        goto cleanup;
    }
    if (!catalog_accept_symbol(temporary, &symbol, parser)) {
        free_symbol(&symbol);
        goto cleanup;
    }
    success = true;

cleanup:
    free(name);
    free(section);
    free(address_text);
    return success || (!parser->strict &&
                       !porpoise_diagnostics_have_errors(
                           parser->diagnostics));
}

int porpoise_symbol_catalog_load_dtk(
    PorpoiseSymbolCatalog *catalog,
    const char *symbols_path,
    const char *splits_path,
    const PorpoiseSymbolMapLoadOptions *options,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSymbolMapLoadOptions defaults;
    PorpoiseMapParser parser;
    PorpoiseSymbolCatalog temporary;
    PorpoiseSplitRanges ranges;
    FILE *file;
    char *line = NULL;
    size_t capacity = 0U;
    size_t line_number = 0U;
    bool success;
    int read_result = 0;

    if (catalog == NULL || symbols_path == NULL ||
        symbols_path[0] == '\0' || diagnostics == NULL) {
        return invalid_arguments(
            diagnostics, "DTK symbol load arguments are invalid");
    }
    porpoise_symbol_map_load_options_init(&defaults);
    if (options == NULL) options = &defaults;
    parser.strict = options->strict;
    parser.diagnostics = diagnostics;
    porpoise_symbol_catalog_init(&temporary);
    memset(&ranges, 0, sizeof(ranges));

    success = load_dtk_splits(splits_path, &ranges, &parser);
    if (!success) {
        free_split_ranges(&ranges);
        return PORPOISE_EXIT_TRANSLATION;
    }
    file = fopen(symbols_path, "rb");
    if (file == NULL) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            symbols_path, 0U, 0U,
            "failed to open DTK symbols file");
        free_split_ranges(&ranges);
        return PORPOISE_EXIT_IO;
    }
    while ((read_result = read_line(file, &line, &capacity)) == 1) {
        line_number++;
        success = parse_dtk_symbol(
            line, symbols_path, splits_path, options->module,
            line_number, &ranges, &temporary, &parser);
        if (!success) break;
    }
    if (read_result < 0 && success) {
        success = parser_message(
            &parser, symbols_path, line_number, true,
            "failed while reading DTK symbols");
    }
    fclose(file);
    free(line);
    free_split_ranges(&ranges);
    if (success) success = catalog_commit(catalog, &temporary, &parser);
    porpoise_symbol_catalog_free(&temporary);
    return success ? PORPOISE_EXIT_OK : PORPOISE_EXIT_TRANSLATION;
}

size_t porpoise_symbol_catalog_find_name(
    const PorpoiseSymbolCatalog *catalog,
    const char *module,
    const char *name,
    size_t start_index) {
    size_t index;
    if (catalog == NULL || name == NULL) return SIZE_MAX;
    for (index = start_index; index < catalog->symbol_count; index++) {
        if (nullable_string_equal(catalog->symbols[index].module, module) &&
            strcmp(catalog->symbols[index].name, name) == 0) {
            return index;
        }
    }
    return SIZE_MAX;
}

size_t porpoise_symbol_catalog_find_address(
    const PorpoiseSymbolCatalog *catalog,
    const char *module,
    const char *section,
    uint32_t address,
    size_t start_index) {
    size_t index;
    if (catalog == NULL) return SIZE_MAX;
    for (index = start_index; index < catalog->symbol_count; index++) {
        const PorpoiseSymbol *symbol = &catalog->symbols[index];
        if (symbol->has_address && symbol->address == address &&
            nullable_string_equal(symbol->module, module) &&
            nullable_string_equal(symbol->section, section)) {
            return index;
        }
    }
    return SIZE_MAX;
}

const char *porpoise_symbol_kind_name(PorpoiseSymbolKind kind) {
    switch (kind) {
        case PORPOISE_SYMBOL_KIND_FUNCTION: return "function";
        case PORPOISE_SYMBOL_KIND_OBJECT: return "object";
        case PORPOISE_SYMBOL_KIND_SECTION: return "section";
        case PORPOISE_SYMBOL_KIND_LABEL: return "label";
        case PORPOISE_SYMBOL_KIND_UNKNOWN:
        default: return "unknown";
    }
}

const char *porpoise_symbol_scope_name(PorpoiseSymbolScope scope) {
    switch (scope) {
        case PORPOISE_SYMBOL_SCOPE_LOCAL: return "local";
        case PORPOISE_SYMBOL_SCOPE_GLOBAL: return "global";
        case PORPOISE_SYMBOL_SCOPE_WEAK: return "weak";
        case PORPOISE_SYMBOL_SCOPE_UNKNOWN:
        default: return "unknown";
    }
}

const char *porpoise_symbol_source_kind_name(
    PorpoiseSymbolSourceKind kind) {
    switch (kind) {
        case PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS: return "dtk-symbols";
        case PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP:
        default: return "codewarrior-map";
    }
}
