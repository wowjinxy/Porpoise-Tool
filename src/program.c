#include "porpoise/program.h"
#include "porpoise/util.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct PorpoisePathList {
    char **items;
    size_t count;
    size_t capacity;
} PorpoisePathList;

typedef struct PorpoisePendingAlias {
    char *name;
    char *c_name;
    bool is_global;
    size_t source_line;
} PorpoisePendingAlias;

typedef struct PorpoisePendingAliasList {
    PorpoisePendingAlias *items;
    size_t count;
    size_t capacity;
} PorpoisePendingAliasList;

static bool has_assembly_extension(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot != NULL && (dot[1] == 's' || dot[1] == 'S') && dot[2] == '\0';
}

static int compare_paths(const void *left, const void *right) {
    const char *const *left_path = (const char *const *)left;
    const char *const *right_path = (const char *const *)right;
    return strcmp(*left_path, *right_path);
}

static bool path_list_add(PorpoisePathList *paths, const char *path) {
    char *copy;
    if (paths->count == SIZE_MAX ||
        !porpoise_grow_array((void **)&paths->items, &paths->capacity,
                             sizeof(*paths->items), paths->count + 1U)) {
        return false;
    }
    copy = porpoise_strdup(path);
    if (copy == NULL) {
        return false;
    }
    paths->items[paths->count++] = copy;
    return true;
}

static void path_list_free(PorpoisePathList *paths) {
    size_t index;
    for (index = 0U; index < paths->count; index++) {
        free(paths->items[index]);
    }
    free(paths->items);
    memset(paths, 0, sizeof(*paths));
}

static bool collect_paths_recursive(
    const char *root,
    const char *relative,
    unsigned int depth,
    PorpoisePathList *paths,
    PorpoiseDiagnostics *diagnostics) {
    char directory_path[PORPOISE_PATH_CAPACITY];
    DIR *directory;
    const struct dirent *entry;
    if (depth > 64U) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, root, 0U, 0U,
                                 "input directory nesting exceeds 64 levels (possible symlink cycle)");
        return false;
    }
    if (relative[0] == '\0') {
        if (!porpoise_copy_string(directory_path, sizeof(directory_path), root)) {
            return false;
        }
    } else if (!porpoise_path_join(directory_path, sizeof(directory_path), root, relative)) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, root, 0U, 0U,
                                 "input path is too long");
        return false;
    }
    directory = opendir(directory_path);
    if (directory == NULL) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, directory_path, 0U, 0U,
                                 "cannot open input directory: %s", strerror(errno));
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        char child_relative[PORPOISE_PATH_CAPACITY];
        char child_path[PORPOISE_PATH_CAPACITY];
        struct stat status;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (relative[0] == '\0') {
            if (!porpoise_copy_string(child_relative, sizeof(child_relative), entry->d_name)) {
                closedir(directory);
                return false;
            }
        } else if (!porpoise_path_join(child_relative, sizeof(child_relative), relative, entry->d_name)) {
            closedir(directory);
            return false;
        }
        if (!porpoise_path_join(child_path, sizeof(child_path), root, child_relative)) {
            closedir(directory);
            return false;
        }
        if (stat(child_path, &status) != 0) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, child_path, 0U, 0U,
                                     "cannot inspect input path: %s", strerror(errno));
            closedir(directory);
            return false;
        }
        if (S_ISDIR(status.st_mode)) {
            if (!collect_paths_recursive(root, child_relative, depth + 1U, paths, diagnostics)) {
                closedir(directory);
                return false;
            }
        } else if (S_ISREG(status.st_mode) && has_assembly_extension(child_path)) {
            if (!path_list_add(paths, child_relative)) {
                closedir(directory);
                return false;
            }
        }
    }
    closedir(directory);
    return true;
}

static void free_item(PorpoiseAsmItem *item) {
    free(item->mnemonic);
    free(item->operands);
    free(item->label);
}

static void free_function(PorpoiseFunction *function) {
    size_t index;
    free(function->name);
    free(function->c_name);
    for (index = 0U; index < function->item_count; index++) {
        free_item(&function->items[index]);
    }
    free(function->items);
    for (index = 0U; index < function->alias_count; index++) {
        free(function->aliases[index].name);
        free(function->aliases[index].c_name);
    }
    free(function->aliases);
}

static void free_file(PorpoiseSourceFile *file) {
    size_t index;
    free(file->path);
    free(file->relative_path);
    free(file->output_stem);
    for (index = 0U; index < file->function_count; index++) {
        free_function(&file->functions[index]);
    }
    free(file->functions);
    for (index = 0U; index < file->data_word_count; index++) {
        free(file->data_words[index].directive);
    }
    free(file->data_words);
}

void porpoise_program_init(PorpoiseProgram *program) {
    memset(program, 0, sizeof(*program));
}

void porpoise_program_free(PorpoiseProgram *program) {
    size_t index;
    if (program == NULL) {
        return;
    }
    for (index = 0U; index < program->file_count; index++) {
        free_file(&program->files[index]);
    }
    free(program->symbol_index);
    free(program->label_index);
    free(program->files);
    memset(program, 0, sizeof(*program));
}

static PorpoiseSourceFile *program_add_file(
    PorpoiseProgram *program,
    const char *path,
    const char *relative_path) {
    PorpoiseSourceFile candidate = {0};
    PorpoiseSourceFile *file;
    char stem[PORPOISE_PATH_CAPACITY];
    char *dot;
    size_t component_start = 0U;
    size_t cursor;
    if (!porpoise_copy_string(stem, sizeof(stem), relative_path)) {
        return NULL;
    }
    dot = strrchr(stem, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    for (cursor = 0U;; cursor++) {
        if (stem[cursor] == '/' || stem[cursor] == '\\' || stem[cursor] == '\0') {
            char component[PORPOISE_NAME_CAPACITY];
            char sanitized[PORPOISE_NAME_CAPACITY];
            size_t length = cursor - component_start;
            if (length == 0U || length >= sizeof(component)) return NULL;
            memcpy(component, stem + component_start, length);
            component[length] = '\0';
            porpoise_sanitize_identifier(component, sanitized, sizeof(sanitized));
            if (strlen(sanitized) != length) {
                size_t old_length = strlen(stem);
                size_t new_length = strlen(sanitized);
                if (old_length - length + new_length >= sizeof(stem)) return NULL;
                memmove(stem + component_start + new_length, stem + cursor,
                        old_length - cursor + 1U);
                memcpy(stem + component_start, sanitized, new_length);
                cursor = component_start + new_length;
            } else {
                memcpy(stem + component_start, sanitized, length);
            }
            if (stem[cursor] == '\0') break;
            stem[cursor] = '/';
            component_start = cursor + 1U;
        }
    }
    candidate.path = porpoise_strdup(path);
    candidate.relative_path = porpoise_strdup(relative_path);
    candidate.output_stem = porpoise_strdup(stem);
    if (candidate.path == NULL || candidate.relative_path == NULL ||
        candidate.output_stem == NULL || program->file_count == SIZE_MAX ||
        !porpoise_grow_array((void **)&program->files, &program->file_capacity,
                             sizeof(*program->files), program->file_count + 1U)) {
        free_file(&candidate);
        return NULL;
    }
    file = &program->files[program->file_count++];
    *file = candidate;
    return file;
}

static PorpoiseFunction *file_add_function(
    PorpoiseSourceFile *file,
    const char *name,
    bool is_global) {
    PorpoiseFunction candidate = {0};
    PorpoiseFunction *function;
    char c_name[PORPOISE_NAME_CAPACITY];
    porpoise_sanitize_identifier(name, c_name, sizeof(c_name));
    candidate.name = porpoise_strdup(name);
    candidate.c_name = porpoise_strdup(c_name);
    candidate.is_global = is_global;
    if (candidate.name == NULL || candidate.c_name == NULL ||
        file->function_count == SIZE_MAX ||
        !porpoise_grow_array((void **)&file->functions, &file->function_capacity,
                             sizeof(*file->functions), file->function_count + 1U)) {
        free_function(&candidate);
        return NULL;
    }
    function = &file->functions[file->function_count++];
    *function = candidate;
    return function;
}

static PorpoiseAsmItem *function_add_item(PorpoiseFunction *function) {
    PorpoiseAsmItem *item;
    if (function->item_count == SIZE_MAX ||
        !porpoise_grow_array((void **)&function->items, &function->item_capacity,
                             sizeof(*function->items), function->item_count + 1U)) {
        return NULL;
    }
    item = &function->items[function->item_count++];
    memset(item, 0, sizeof(*item));
    return item;
}

static void function_rollback_last_item(
    PorpoiseFunction *function,
    PorpoiseAsmItem *item) {
    if (function->item_count == 0U ||
        item != &function->items[function->item_count - 1U]) {
        return;
    }
    free_item(item);
    memset(item, 0, sizeof(*item));
    function->item_count--;
}

static void pending_alias_list_clear(PorpoisePendingAliasList *aliases) {
    size_t index;
    for (index = 0U; index < aliases->count; index++) {
        free(aliases->items[index].name);
        free(aliases->items[index].c_name);
    }
    aliases->count = 0U;
}

static void pending_alias_list_free(PorpoisePendingAliasList *aliases) {
    pending_alias_list_clear(aliases);
    free(aliases->items);
    memset(aliases, 0, sizeof(*aliases));
}

static bool pending_alias_list_add(
    PorpoisePendingAliasList *aliases,
    const char *name,
    bool is_global,
    size_t source_line) {
    PorpoisePendingAlias *alias;
    char c_name[PORPOISE_NAME_CAPACITY];
    char *name_copy;
    char *c_name_copy;
    porpoise_sanitize_identifier(name, c_name, sizeof(c_name));
    name_copy = porpoise_strdup(name);
    c_name_copy = porpoise_strdup(c_name);
    if (name_copy == NULL || c_name_copy == NULL) {
        free(name_copy);
        free(c_name_copy);
        return false;
    }
    if (aliases->count == SIZE_MAX ||
        !porpoise_grow_array((void **)&aliases->items, &aliases->capacity,
                             sizeof(*aliases->items), aliases->count + 1U)) {
        free(name_copy);
        free(c_name_copy);
        return false;
    }
    alias = &aliases->items[aliases->count++];
    alias->name = name_copy;
    alias->c_name = c_name_copy;
    alias->is_global = is_global;
    alias->source_line = source_line;
    return true;
}

static bool function_bind_pending_aliases(
    PorpoiseFunction *function,
    PorpoisePendingAliasList *pending,
    uint32_t address,
    size_t instruction_item_index) {
    size_t index;
    if (pending->count == 0U) return true;
    if (function->alias_count > SIZE_MAX - pending->count ||
        !porpoise_grow_array((void **)&function->aliases, &function->alias_capacity,
                             sizeof(*function->aliases),
                             function->alias_count + pending->count)) {
        return false;
    }
    for (index = 0U; index < pending->count; index++) {
        PorpoisePendingAlias *source = &pending->items[index];
        PorpoiseAddressAlias *destination = &function->aliases[function->alias_count++];
        destination->name = source->name;
        destination->c_name = source->c_name;
        destination->is_global = source->is_global;
        destination->is_function_name = false;
        destination->source_line = source->source_line;
        destination->source_path = NULL;
        destination->address = address;
        destination->instruction_item_index = instruction_item_index;
        source->name = NULL;
        source->c_name = NULL;
    }
    pending->count = 0U;
    return true;
}

static PorpoiseDataWord *file_add_data_word(PorpoiseSourceFile *file) {
    PorpoiseDataWord *word;
    if (file->data_word_count == SIZE_MAX ||
        !porpoise_grow_array((void **)&file->data_words, &file->data_word_capacity,
                             sizeof(*file->data_words), file->data_word_count + 1U)) return NULL;
    word = &file->data_words[file->data_word_count++];
    memset(word, 0, sizeof(*word));
    return word;
}

static void file_rollback_last_data_word(
    PorpoiseSourceFile *file,
    PorpoiseDataWord *word) {
    if (file->data_word_count == 0U ||
        word != &file->data_words[file->data_word_count - 1U]) {
        return;
    }
    free(word->directive);
    memset(word, 0, sizeof(*word));
    file->data_word_count--;
}

static bool directive_selects_data(const char *line, bool *is_data) {
    const char *cursor = line;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (strcmp(cursor, ".data") == 0 || strcmp(cursor, ".rodata") == 0 ||
        strcmp(cursor, ".sdata") == 0 || strcmp(cursor, ".sdata2") == 0 ||
        strcmp(cursor, ".bss") == 0 || strcmp(cursor, ".sbss") == 0 ||
        strcmp(cursor, ".sbss2") == 0) {
        *is_data = true;
        return true;
    }
    if (strcmp(cursor, ".text") == 0) {
        *is_data = false;
        return true;
    }
    if (strncmp(cursor, ".section", 8U) == 0 && isspace((unsigned char)cursor[8])) {
        cursor += 8;
        while (isspace((unsigned char)*cursor)) cursor++;
        *is_data = strstr(cursor, ".data") != NULL || strstr(cursor, ".rodata") != NULL ||
                   strstr(cursor, ".sdata") != NULL || strstr(cursor, ".bss") != NULL ||
                   strstr(cursor, ".sbss") != NULL;
        return true;
    }
    return false;
}

static bool parse_function_start(const char *line, char *name, size_t capacity, bool *is_global) {
    const char *cursor = line;
    const char *end;
    size_t length;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (strncmp(cursor, ".fn", 3U) != 0 || !isspace((unsigned char)cursor[3])) {
        return false;
    }
    cursor += 3;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor == '"') {
        cursor++;
        end = strchr(cursor, '"');
    } else {
        end = cursor;
        while (*end != '\0' && *end != ',' && !isspace((unsigned char)*end)) end++;
    }
    if (end == NULL || end == cursor) {
        return false;
    }
    length = (size_t)(end - cursor);
    if (length >= capacity) {
        return false;
    }
    memcpy(name, cursor, length);
    name[length] = '\0';
    cursor = *end == '"' ? end + 1 : end;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor == '\0') {
        *is_global = false;
        return true;
    }
    if (*cursor != ',') return false;
    cursor++;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (strcmp(cursor, "global") == 0) {
        *is_global = true;
        return true;
    }
    if (strcmp(cursor, "weak") == 0) {
        *is_global = true;
        return true;
    }
    if (strcmp(cursor, "local") == 0) {
        *is_global = false;
        return true;
    }
    return false;
}

static bool is_named_directive(const char *line, const char *directive) {
    size_t length;
    while (isspace((unsigned char)*line)) line++;
    length = strlen(directive);
    return strncmp(line, directive, length) == 0 &&
           (line[length] == '\0' || line[length] == ',' ||
            isspace((unsigned char)line[length]));
}

static bool parse_symbol_alias(
    const char *line,
    char *name,
    size_t capacity,
    bool *is_global) {
    const char *cursor = line;
    const char *start;
    const char *end;
    size_t length;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (strncmp(cursor, ".sym", 4U) != 0 ||
        !isspace((unsigned char)cursor[4])) {
        return false;
    }
    cursor += 4;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor == '"') {
        start = ++cursor;
        end = strchr(cursor, '"');
        if (end == NULL) return false;
        cursor = end + 1;
    } else {
        start = cursor;
        while (*cursor != '\0' && *cursor != ',' &&
               !isspace((unsigned char)*cursor)) cursor++;
        end = cursor;
    }
    length = (size_t)(end - start);
    if (length == 0U || length >= capacity) return false;
    memcpy(name, start, length);
    name[length] = '\0';
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor != ',') return false;
    cursor++;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (strcmp(cursor, "global") == 0) {
        *is_global = true;
        return true;
    }
    if (strcmp(cursor, "local") == 0) {
        *is_global = false;
        return true;
    }
    return false;
}

static bool parse_function_end(const char *line, char *name, size_t capacity) {
    const char *start;
    const char *end;
    size_t length;
    while (isspace((unsigned char)*line)) line++;
    if (strncmp(line, ".endfn", 6U) != 0 ||
        (line[6] != '\0' && !isspace((unsigned char)line[6]))) return false;
    line += 6;
    while (isspace((unsigned char)*line)) line++;
    if (*line == '"') {
        start = ++line;
        end = strchr(line, '"');
        if (end == NULL) return false;
        line = end + 1;
    } else {
        start = line;
        while (*line != '\0' && !isspace((unsigned char)*line)) line++;
        end = line;
    }
    length = (size_t)(end - start);
    if (length >= capacity) return false;
    memcpy(name, start, length);
    name[length] = '\0';
    while (isspace((unsigned char)*line)) line++;
    return *line == '\0';
}

static bool parse_label(const char *line, char *label, size_t capacity) {
    const char *cursor = line;
    const char *colon;
    size_t length;
    while (isspace((unsigned char)*cursor)) cursor++;
    colon = strchr(cursor, ':');
    if (colon == NULL || colon[1] != '\0') {
        return false;
    }
    length = (size_t)(colon - cursor);
    if (length == 0U || length >= capacity) {
        return false;
    }
    while (length > 0U && isspace((unsigned char)cursor[length - 1U])) length--;
    if (length == 0U || (cursor[0] != '.' && cursor[0] != '_' && !isalpha((unsigned char)cursor[0]))) {
        return false;
    }
    memcpy(label, cursor, length);
    label[length] = '\0';
    return true;
}

static bool is_comment_only(const char *line) {
    const char *end;
    if (strncmp(line, "/*", 2U) != 0) return false;
    end = strstr(line + 2, "*/");
    if (end == NULL) return false;
    end += 2;
    while (isspace((unsigned char)*end)) end++;
    return *end == '\0';
}

static bool is_hex_token(const char *token) {
    size_t index;
    if (token[0] == '\0') return false;
    for (index = 0U; token[index] != '\0'; index++) {
        if (!isxdigit((unsigned char)token[index])) return false;
    }
    return true;
}

static bool parse_instruction(
    const char *line,
    uint32_t *address,
    uint32_t *word,
    char *mnemonic,
    size_t mnemonic_capacity,
    char *operands,
    size_t operands_capacity) {
    const char *comment = strstr(line, "/*");
    const char *comment_end;
    char metadata[256];
    char *tokens[16];
    size_t token_count = 0U;
    char *cursor;
    size_t metadata_length;
    unsigned long parsed_address;
    unsigned long parsed_word = 0UL;
    if (comment == NULL) return false;
    comment_end = strstr(comment + 2, "*/");
    if (comment_end == NULL) return false;
    metadata_length = (size_t)(comment_end - (comment + 2));
    if (metadata_length >= sizeof(metadata)) return false;
    memcpy(metadata, comment + 2, metadata_length);
    metadata[metadata_length] = '\0';
    cursor = metadata;
    while (*cursor != '\0' && token_count < sizeof(tokens) / sizeof(tokens[0])) {
        while (isspace((unsigned char)*cursor)) cursor++;
        if (*cursor == '\0') break;
        tokens[token_count++] = cursor;
        while (*cursor != '\0' && !isspace((unsigned char)*cursor)) cursor++;
        if (*cursor != '\0') *cursor++ = '\0';
    }
    if ((token_count != 3U && token_count != 6U) ||
        strlen(tokens[0]) != 8U || strlen(tokens[1]) != 8U ||
        !is_hex_token(tokens[0]) || !is_hex_token(tokens[1])) return false;
    parsed_address = strtoul(tokens[0], NULL, 16);
#if ULONG_MAX > UINT32_MAX
    if (parsed_address > UINT32_MAX) return false;
#endif
    if (token_count == 6U && strlen(tokens[2]) == 2U && strlen(tokens[3]) == 2U &&
        strlen(tokens[4]) == 2U && strlen(tokens[5]) == 2U) {
        size_t index;
        for (index = 2U; index < 6U; index++) {
            if (!is_hex_token(tokens[index])) return false;
            parsed_word = (parsed_word << 8U) | strtoul(tokens[index], NULL, 16);
        }
    } else if (token_count == 3U && strlen(tokens[2]) == 8U && is_hex_token(tokens[2])) {
        parsed_word = strtoul(tokens[2], NULL, 16);
    } else {
        return false;
    }
    cursor = (char *)(comment_end + 2);
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor == '\0') return false;
    {
        char *mnemonic_end = cursor;
        size_t length;
        while (*mnemonic_end != '\0' && !isspace((unsigned char)*mnemonic_end)) mnemonic_end++;
        length = (size_t)(mnemonic_end - cursor);
        if (length == 0U || length >= mnemonic_capacity) return false;
        memcpy(mnemonic, cursor, length);
        mnemonic[length] = '\0';
        cursor = mnemonic_end;
    }
    while (isspace((unsigned char)*cursor)) cursor++;
    if (!porpoise_copy_string(operands, operands_capacity, cursor)) return false;
    {
        char *line_comment = strchr(operands, '#');
        if (line_comment != NULL) *line_comment = '\0';
    }
    porpoise_trim(operands);
    *address = (uint32_t)parsed_address;
    *word = (uint32_t)parsed_word;
    return true;
}

static bool parse_file(
    PorpoiseSourceFile *file,
    PorpoiseDiagnostics *diagnostics,
    bool *io_failure) {
    FILE *input = fopen(file->path, "rb");
    char line[4096];
    size_t line_number = 0U;
    PorpoiseFunction *current = NULL;
    PorpoisePendingAliasList pending_aliases = {0};
    bool have_previous_instruction = false;
    uint32_t previous_instruction_address = 0U;
    bool in_data_section = false;
    bool ok = true;
    bool in_block_comment = false;
    size_t block_comment_start_line = 0U;
    if (input == NULL) {
        *io_failure = true;
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path, 0U, 0U,
                                 "cannot open input: %s", strerror(errno));
        return false;
    }
    while (fgets(line, sizeof(line), input) != NULL) {
        char function_name[PORPOISE_NAME_CAPACITY];
        char function_end_name[PORPOISE_NAME_CAPACITY];
        char alias_name[PORPOISE_NAME_CAPACITY];
        bool is_global;
        char label[PORPOISE_NAME_CAPACITY];
        uint32_t address;
        uint32_t word;
        char mnemonic[64];
        char operands[1024];
        line_number++;
        if (strchr(line, '\n') == NULL && !feof(input)) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                     line_number, 0U, "line exceeds 4095 bytes");
            ok = false;
            break;
        }
        porpoise_trim(line);

        /*
         * Ignore ordinary multi-line C block comments.
         *
         * Annotated instruction comments remain untouched because
         * their opening and closing delimiters occur on the same line.
         */
        if (in_block_comment) {
            char *comment_end = strstr(line, "*/");

            if (comment_end == NULL) {
                continue;
            }

            in_block_comment = false;
            block_comment_start_line = 0U;

            /*
             * Preserve any text following the end of the comment.
             */
            memmove(line, comment_end + 2, strlen(comment_end + 2) + 1U);
            porpoise_trim(line);

            if (line[0] == '\0') {
                continue;
            }
        }

        /*
         * If a block comment starts here but does not terminate on this
         * physical line, enter multi-line-comment mode.
         */
        if (strncmp(line, "/*", 2U) == 0 &&
            strstr(line + 2, "*/") == NULL) {
            in_block_comment = true;
            block_comment_start_line = line_number;
            continue;
        }
        if (line[0] == '\0' || line[0] == '#') continue;
        if (parse_symbol_alias(line, alias_name, sizeof(alias_name), &is_global)) {
            if (!pending_alias_list_add(&pending_aliases, alias_name, is_global, line_number)) {
                ok = false;
                break;
            }
            continue;
        }
        if (is_named_directive(line, ".sym")) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                     line_number, 0U, "malformed .sym directive");
            ok = false;
            continue;
        }
        if (directive_selects_data(line, &in_data_section)) continue;
        if (parse_function_start(line, function_name, sizeof(function_name), &is_global)) {
            if (current != NULL) {
                porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                         line_number, 0U, "nested .fn is not allowed");
                ok = false;
                continue;
            }
            current = file_add_function(file, function_name, is_global);
            if (current == NULL) {
                ok = false;
                break;
            }
            have_previous_instruction = false;
            in_data_section = false;
            continue;
        }
        if (strncmp(line, ".fn", 3U) == 0 &&
            (line[3] == '\0' || isspace((unsigned char)line[3]))) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                     line_number, 0U, "malformed .fn directive");
            ok = false;
            continue;
        }
        if (parse_function_end(line, function_end_name, sizeof(function_end_name))) {
            size_t pending_index;
            if (current == NULL) {
                porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                         line_number, 0U, ".endfn without .fn");
                ok = false;
                continue;
            }
            for (pending_index = 0U; pending_index < pending_aliases.count; pending_index++) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    file->path,
                    pending_aliases.items[pending_index].source_line,
                    0U,
                    "symbol alias %s is not followed by an annotated instruction before .endfn",
                    pending_aliases.items[pending_index].name);
                ok = false;
            }
            pending_alias_list_clear(&pending_aliases);
            if (current->instruction_count == 0U) {
                porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                         line_number, 0U, "function %s contains no instructions", current->name);
                ok = false;
            }
            if (function_end_name[0] != '\0' && strcmp(function_end_name, current->name) != 0) {
                porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                         line_number, 0U,
                                         ".endfn names %s but the open function is %s",
                                         function_end_name, current->name);
                ok = false;
            }
            current = NULL;
            have_previous_instruction = false;
            continue;
        }
        if (strncmp(line, ".endfn", 6U) == 0 &&
            (line[6] == '\0' || isspace((unsigned char)line[6]))) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                     line_number, 0U, "malformed .endfn directive");
            ok = false;
            continue;
        }
        if (current == NULL) {
            if (in_data_section &&
                parse_instruction(line, &address, &word, mnemonic, sizeof(mnemonic), operands, sizeof(operands)) &&
                mnemonic[0] == '.') {
                PorpoiseDataWord *data_word = file_add_data_word(file);
                if (data_word == NULL) { ok = false; break; }
                data_word->source_line = line_number;
                data_word->address = address;
                data_word->word = word;
                data_word->directive = porpoise_strdup(mnemonic);
                if (data_word->directive == NULL) {
                    file_rollback_last_data_word(file, data_word);
                    ok = false;
                    break;
                }
                continue;
            }
            if (line[0] == '.' || parse_label(line, label, sizeof(label)) || is_comment_only(line)) continue;
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                     line_number, 0U,
                                     strncmp(line, "/*", 2U) == 0
                                         ? "instruction outside a .fn block"
                                         : "unrecognized line outside a .fn block: %s",
                                     line);
            ok = false;
            continue;
        }
        if (parse_label(line, label, sizeof(label))) {
            size_t prior;
            char c_label[PORPOISE_NAME_CAPACITY];
            porpoise_sanitize_identifier(label, c_label, sizeof(c_label));
            for (prior = 0U; prior < current->item_count; prior++) {
                const PorpoiseAsmItem *candidate = &current->items[prior];
                if (candidate->kind == PORPOISE_ASM_LABEL) {
                    char candidate_c_label[PORPOISE_NAME_CAPACITY];
                    porpoise_sanitize_identifier(candidate->label, candidate_c_label, sizeof(candidate_c_label));
                    if (strcmp(candidate->label, label) == 0 || strcmp(candidate_c_label, c_label) == 0) {
                        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                                 line_number, 0U,
                                                 "duplicate or colliding label %s in function %s",
                                                 label, current->name);
                        ok = false;
                    }
                }
            }
            PorpoiseAsmItem *item = function_add_item(current);
            if (item == NULL) { ok = false; break; }
            item->kind = PORPOISE_ASM_LABEL;
            item->source_line = line_number;
            item->label = porpoise_strdup(label);
            if (item->label == NULL) {
                function_rollback_last_item(current, item);
                ok = false;
                break;
            }
            continue;
        }
        if (parse_instruction(line, &address, &word, mnemonic, sizeof(mnemonic), operands, sizeof(operands))) {
            uint64_t span;
            PorpoiseAsmItem *item = function_add_item(current);
            if (item == NULL) { ok = false; break; }
            item->kind = PORPOISE_ASM_INSTRUCTION;
            item->source_line = line_number;
            item->address = address;
            item->word = word;
            item->mnemonic = porpoise_strdup(mnemonic);
            item->operands = porpoise_strdup(operands);
            if (item->mnemonic == NULL || item->operands == NULL) {
                function_rollback_last_item(current, item);
                ok = false;
                break;
            }
            if (!function_bind_pending_aliases(current, &pending_aliases, address,
                                               current->item_count - 1U)) {
                function_rollback_last_item(current, item);
                ok = false;
                break;
            }
            if ((address & UINT32_C(3)) != 0U) {
                porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                         line_number, address,
                                         "instruction address is not 4-byte aligned");
                ok = false;
            }
            if (have_previous_instruction && address <= previous_instruction_address) {
                porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                         line_number, address,
                                         "instruction addresses in function %s must be strictly increasing",
                                         current->name);
                ok = false;
            }
            if (current->instruction_count == 0U) current->start_address = address;
            span = (uint64_t)address - (uint64_t)current->start_address + UINT64_C(4);
            if (address >= current->start_address && span <= UINT32_MAX) {
                current->size = (uint32_t)span;
            } else if (address >= current->start_address) {
                porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                         line_number, address,
                                         "function %s address span exceeds 32-bit metadata",
                                         current->name);
                ok = false;
            }
            previous_instruction_address = address;
            have_previous_instruction = true;
            current->instruction_count++;
            continue;
        }
        if (line[0] != '.' && !is_comment_only(line)) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                     line_number, 0U, "unrecognized line inside function: %s", line);
            ok = false;
        }
    }
    if (in_block_comment) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            file->path,
            block_comment_start_line,
            0U,
            "unterminated block comment"
        );
        ok = false;
    }
    if (ferror(input) != 0) {
        *io_failure = true;
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                 line_number, 0U, "failed while reading input");
        ok = false;
    }
    if (current != NULL) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                 line_number, 0U, "function %s is missing .endfn", current->name);
        ok = false;
    }
    if (pending_aliases.count != 0U) {
        size_t pending_index;
        for (pending_index = 0U; pending_index < pending_aliases.count; pending_index++) {
            porpoise_diagnostics_add(
                diagnostics,
                PORPOISE_SEVERITY_ERROR,
                file->path,
                pending_aliases.items[pending_index].source_line,
                0U,
                "symbol alias %s is not followed by an annotated instruction",
                pending_aliases.items[pending_index].name);
        }
        ok = false;
    }
    if (fclose(input) != 0) {
        *io_failure = true;
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                 line_number, 0U, "failed while closing input");
        ok = false;
    }
    pending_alias_list_free(&pending_aliases);
    return ok;
}

static bool checked_size_add(size_t *value, size_t increment) {
    if (*value > SIZE_MAX - increment) return false;
    *value += increment;
    return true;
}

static int compare_symbol_index_entries(const void *left, const void *right)
{
    const PorpoiseProgramSymbolIndexEntry *left_entry =
        (const PorpoiseProgramSymbolIndexEntry *)left;
    const PorpoiseProgramSymbolIndexEntry *right_entry =
        (const PorpoiseProgramSymbolIndexEntry *)right;
    int comparison = strcmp(left_entry->name, right_entry->name);

    if (comparison != 0) return comparison;
    comparison = strcmp(
        left_entry->file->relative_path,
        right_entry->file->relative_path);
    if (comparison != 0) return comparison;
    comparison = strcmp(
        left_entry->function->c_name,
        right_entry->function->c_name);
    if (comparison != 0) return comparison;
    if (left_entry->alias == right_entry->alias) return 0;
    if (left_entry->alias == NULL) return -1;
    if (right_entry->alias == NULL) return 1;
    comparison = strcmp(left_entry->alias->c_name, right_entry->alias->c_name);
    if (comparison != 0) return comparison;
    if (left_entry->alias->address < right_entry->alias->address) return -1;
    if (left_entry->alias->address > right_entry->alias->address) return 1;
    return 0;
}

static int compare_label_index_entries(const void *left, const void *right)
{
    const PorpoiseProgramLabelIndexEntry *left_entry =
        (const PorpoiseProgramLabelIndexEntry *)left;
    const PorpoiseProgramLabelIndexEntry *right_entry =
        (const PorpoiseProgramLabelIndexEntry *)right;
    int comparison = strcmp(left_entry->name, right_entry->name);

    if (comparison != 0) return comparison;
    comparison = strcmp(
        left_entry->function->c_name,
        right_entry->function->c_name);
    if (comparison != 0) return comparison;
    if (left_entry->address < right_entry->address) return -1;
    if (left_entry->address > right_entry->address) return 1;
    if (left_entry->instruction_item_index < right_entry->instruction_item_index)
        return -1;
    if (left_entry->instruction_item_index > right_entry->instruction_item_index)
        return 1;
    return 0;
}

static bool program_build_lookup_indices(PorpoiseProgram *program)
{
    PorpoiseProgramSymbolIndexEntry *symbol_index = NULL;
    PorpoiseProgramLabelIndexEntry *label_index = NULL;
    size_t symbol_count = 0U;
    size_t label_count = 0U;
    size_t symbol_cursor = 0U;
    size_t label_cursor = 0U;
    size_t file_index;

    if (program->symbol_index != NULL || program->symbol_index_count != 0U ||
        program->symbol_index_capacity != 0U || program->label_index != NULL ||
        program->label_index_count != 0U || program->label_index_capacity != 0U) {
        return false;
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            size_t alias_index;
            size_t item_index;
            size_t function_symbols = strcmp(function->name, function->c_name) == 0
                ? 1U
                : 2U;
            if (!checked_size_add(&symbol_count, function_symbols) ||
                !checked_size_add(&symbol_count, function->alias_count)) {
                return false;
            }
            for (alias_index = 0U;
                 alias_index < function->alias_count;
                 alias_index++) {
                const PorpoiseAddressAlias *alias =
                    &function->aliases[alias_index];
                if (strcmp(alias->name, alias->c_name) != 0 &&
                    !checked_size_add(&symbol_count, 1U)) return false;
            }
            for (item_index = 0U; item_index < function->item_count; item_index++) {
                const PorpoiseAsmItem *item = &function->items[item_index];
                if (item->kind == PORPOISE_ASM_LABEL &&
                    !checked_size_add(&label_count, 1U)) return false;
            }
        }
    }
    if (symbol_count > SIZE_MAX / sizeof(*symbol_index) ||
        label_count > SIZE_MAX / sizeof(*label_index)) return false;
    if (symbol_count != 0U) {
        symbol_index = (PorpoiseProgramSymbolIndexEntry *)malloc(
            symbol_count * sizeof(*symbol_index));
        if (symbol_index == NULL) return false;
    }
    if (label_count != 0U) {
        label_index = (PorpoiseProgramLabelIndexEntry *)malloc(
            label_count * sizeof(*label_index));
        if (label_index == NULL) {
            free(symbol_index);
            return false;
        }
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            size_t alias_index;
            size_t item_index;
            size_t next_instruction = SIZE_MAX;
            PorpoiseProgramSymbolIndexEntry *entry =
                &symbol_index[symbol_cursor++];
            entry->name = function->name;
            entry->file = file;
            entry->function = function;
            entry->alias = NULL;
            if (strcmp(function->name, function->c_name) != 0) {
                entry = &symbol_index[symbol_cursor++];
                entry->name = function->c_name;
                entry->file = file;
                entry->function = function;
                entry->alias = NULL;
            }
            for (alias_index = 0U;
                 alias_index < function->alias_count;
                 alias_index++) {
                const PorpoiseAddressAlias *alias = &function->aliases[alias_index];
                entry = &symbol_index[symbol_cursor++];
                entry->name = alias->name;
                entry->file = file;
                entry->function = function;
                entry->alias = alias;
                if (strcmp(alias->name, alias->c_name) != 0) {
                    entry = &symbol_index[symbol_cursor++];
                    entry->name = alias->c_name;
                    entry->file = file;
                    entry->function = function;
                    entry->alias = alias;
                }
            }
            for (item_index = function->item_count; item_index-- > 0U;) {
                const PorpoiseAsmItem *item = &function->items[item_index];
                PorpoiseProgramLabelIndexEntry *label_entry;
                if (item->kind == PORPOISE_ASM_INSTRUCTION) {
                    next_instruction = item_index;
                    continue;
                }
                label_entry = &label_index[label_cursor++];
                label_entry->name = item->label;
                label_entry->function = function;
                label_entry->address = next_instruction == SIZE_MAX
                    ? 0U
                    : function->items[next_instruction].address;
                label_entry->instruction_item_index = next_instruction;
            }
        }
    }
    if (symbol_cursor != symbol_count || label_cursor != label_count) {
        free(symbol_index);
        free(label_index);
        return false;
    }
    if (symbol_count > 1U) {
        qsort(
            symbol_index,
            symbol_count,
            sizeof(*symbol_index),
            compare_symbol_index_entries);
    }
    if (label_count > 1U) {
        qsort(
            label_index,
            label_count,
            sizeof(*label_index),
            compare_label_index_entries);
    }
    program->symbol_index = symbol_index;
    program->symbol_index_count = symbol_count;
    program->symbol_index_capacity = symbol_count;
    program->label_index = label_index;
    program->label_index_count = label_count;
    program->label_index_capacity = label_count;
    return true;
}

static size_t program_symbol_lower_bound(
    const PorpoiseProgram *program,
    const char *name)
{
    size_t first = 0U;
    size_t count = program->symbol_index_count;

    while (count != 0U) {
        size_t step = count / 2U;
        size_t middle = first + step;

        if (strcmp(program->symbol_index[middle].name, name) < 0) {
            first = middle + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

static size_t program_label_lower_bound(
    const PorpoiseProgram *program,
    const char *name)
{
    size_t first = 0U;
    size_t count = program->label_index_count;

    while (count != 0U) {
        size_t step = count / 2U;
        size_t middle = first + step;

        if (strcmp(program->label_index[middle].name, name) < 0) {
            first = middle + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

typedef struct PorpoiseFileValidationRef {
    const PorpoiseSourceFile *file;
    size_t order;
} PorpoiseFileValidationRef;

typedef struct PorpoiseDataValidationRef {
    const PorpoiseSourceFile *file;
    const PorpoiseDataWord *word;
    size_t order;
} PorpoiseDataValidationRef;

typedef struct PorpoiseFunctionValidationRef {
    const PorpoiseSourceFile *file;
    const PorpoiseFunction *function;
    size_t order;
} PorpoiseFunctionValidationRef;

typedef struct PorpoiseFunctionCoalesceRef {
    const PorpoiseSourceFile *file;
    PorpoiseFunction *function;
    size_t order;
} PorpoiseFunctionCoalesceRef;

typedef enum PorpoiseNameValidationKind {
    PORPOISE_VALIDATION_FUNCTION = 0,
    PORPOISE_VALIDATION_ALIAS,
    PORPOISE_VALIDATION_LABEL
} PorpoiseNameValidationKind;

typedef struct PorpoiseNameValidationRef {
    const char *name;
    const char *c_name;
    const PorpoiseSourceFile *file;
    const PorpoiseFunction *function;
    const PorpoiseAddressAlias *alias;
    const PorpoiseAsmItem *label;
    uint64_t key_hash;
    size_t order;
    PorpoiseNameValidationKind kind;
} PorpoiseNameValidationRef;

static int compare_portable_names(const char *left, const char *right) {
    size_t cursor = 0U;
    while (left[cursor] != '\0' && right[cursor] != '\0') {
        int left_value = tolower((unsigned char)left[cursor]);
        int right_value = tolower((unsigned char)right[cursor]);
        if (left_value < right_value) return -1;
        if (left_value > right_value) return 1;
        cursor++;
    }
    if (left[cursor] == '\0' && right[cursor] == '\0') return 0;
    return left[cursor] == '\0' ? -1 : 1;
}

static int compare_file_validation_refs(const void *left, const void *right) {
    const PorpoiseFileValidationRef *left_ref =
        (const PorpoiseFileValidationRef *)left;
    const PorpoiseFileValidationRef *right_ref =
        (const PorpoiseFileValidationRef *)right;
    int comparison = compare_portable_names(
        left_ref->file->output_stem,
        right_ref->file->output_stem);
    if (comparison != 0) return comparison;
    if (left_ref->order < right_ref->order) return -1;
    if (left_ref->order > right_ref->order) return 1;
    return 0;
}

static int compare_data_validation_refs(const void *left, const void *right) {
    const PorpoiseDataValidationRef *left_ref =
        (const PorpoiseDataValidationRef *)left;
    const PorpoiseDataValidationRef *right_ref =
        (const PorpoiseDataValidationRef *)right;
    if (left_ref->word->address < right_ref->word->address) return -1;
    if (left_ref->word->address > right_ref->word->address) return 1;
    if (left_ref->order < right_ref->order) return -1;
    if (left_ref->order > right_ref->order) return 1;
    return 0;
}

static int compare_function_validation_refs(const void *left, const void *right) {
    const PorpoiseFunctionValidationRef *left_ref =
        (const PorpoiseFunctionValidationRef *)left;
    const PorpoiseFunctionValidationRef *right_ref =
        (const PorpoiseFunctionValidationRef *)right;
    if (left_ref->function->start_address < right_ref->function->start_address)
        return -1;
    if (left_ref->function->start_address > right_ref->function->start_address)
        return 1;
    if (left_ref->order < right_ref->order) return -1;
    if (left_ref->order > right_ref->order) return 1;
    return 0;
}

static int compare_function_coalesce_refs(const void *left, const void *right) {
    const PorpoiseFunctionCoalesceRef *left_ref =
        (const PorpoiseFunctionCoalesceRef *)left;
    const PorpoiseFunctionCoalesceRef *right_ref =
        (const PorpoiseFunctionCoalesceRef *)right;
    if (left_ref->function->start_address < right_ref->function->start_address)
        return -1;
    if (left_ref->function->start_address > right_ref->function->start_address)
        return 1;
    if (left_ref->function->size < right_ref->function->size) return -1;
    if (left_ref->function->size > right_ref->function->size) return 1;
    if (left_ref->order < right_ref->order) return -1;
    if (left_ref->order > right_ref->order) return 1;
    return 0;
}

static const char *name_validation_key(
    const PorpoiseNameValidationRef *reference,
    char *buffer,
    size_t capacity) {
    if (reference->c_name != NULL) return reference->c_name;
    porpoise_sanitize_identifier(reference->name, buffer, capacity);
    return buffer;
}

static uint64_t hash_identifier(const char *identifier) {
    const unsigned char *cursor = (const unsigned char *)identifier;
    uint64_t hash = UINT64_C(14695981039346656037);
    while (*cursor != '\0') {
        hash ^= *cursor++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t name_validation_hash(
    const char *name,
    const char *c_name) {
    char buffer[PORPOISE_NAME_CAPACITY];
    if (c_name != NULL) return hash_identifier(c_name);
    porpoise_sanitize_identifier(name, buffer, sizeof(buffer));
    return hash_identifier(buffer);
}

static int compare_name_validation_refs(const void *left, const void *right) {
    const PorpoiseNameValidationRef *left_ref =
        (const PorpoiseNameValidationRef *)left;
    const PorpoiseNameValidationRef *right_ref =
        (const PorpoiseNameValidationRef *)right;
    char left_buffer[PORPOISE_NAME_CAPACITY];
    char right_buffer[PORPOISE_NAME_CAPACITY];
    int comparison;
    if (left_ref->key_hash < right_ref->key_hash) return -1;
    if (left_ref->key_hash > right_ref->key_hash) return 1;
    comparison = strcmp(
        name_validation_key(left_ref, left_buffer, sizeof(left_buffer)),
        name_validation_key(right_ref, right_buffer, sizeof(right_buffer)));
    if (comparison != 0) return comparison;
    if (left_ref->order < right_ref->order) return -1;
    if (left_ref->order > right_ref->order) return 1;
    return 0;
}

static bool name_validation_keys_equal(
    const PorpoiseNameValidationRef *left,
    const PorpoiseNameValidationRef *right) {
    char left_buffer[PORPOISE_NAME_CAPACITY];
    char right_buffer[PORPOISE_NAME_CAPACITY];
    if (left->key_hash != right->key_hash) return false;
    return strcmp(
        name_validation_key(left, left_buffer, sizeof(left_buffer)),
        name_validation_key(right, right_buffer, sizeof(right_buffer))) == 0;
}

static bool allocate_validation_array(
    void **items,
    size_t count,
    size_t item_size) {
    *items = NULL;
    if (count == 0U) return true;
    if (item_size == 0U || count > SIZE_MAX / item_size) return false;
    *items = malloc(count * item_size);
    return *items != NULL;
}

static bool operand_token_delimiter(char character) {
    return isspace((unsigned char)character) || character == ',' ||
           character == '(' || character == ')';
}

enum {
    PORPOISE_DUPLICATE_RELOCATION_LOW = 1U << 0U,
    PORPOISE_DUPLICATE_RELOCATION_HIGH = 1U << 1U,
    PORPOISE_DUPLICATE_RELOCATION_HIGH_ADJUSTED = 1U << 2U,
    PORPOISE_DUPLICATE_RELOCATION_SDA21 = 1U << 3U
};

static unsigned int duplicate_relocation_suffix_mask(
    const char *suffix,
    size_t length) {
    if (length == 2U && memcmp(suffix, "@l", 2U) == 0)
        return PORPOISE_DUPLICATE_RELOCATION_LOW;
    if (length == 2U && memcmp(suffix, "@h", 2U) == 0)
        return PORPOISE_DUPLICATE_RELOCATION_HIGH;
    if (length == 3U && memcmp(suffix, "@ha", 3U) == 0)
        return PORPOISE_DUPLICATE_RELOCATION_HIGH_ADJUSTED;
    if (length == 6U && memcmp(suffix, "@sda21", 6U) == 0)
        return PORPOISE_DUPLICATE_RELOCATION_SDA21;
    return 0U;
}

static bool duplicate_memory_relocation_mnemonic(const char *mnemonic) {
    static const char *const mnemonics[] = {
        "lbz", "lbzu", "lhz", "lhzu", "lha", "lhau",
        "lwz", "lwzu", "lfs", "lfsu", "lfd", "lfdu",
        "stb", "stbu", "sth", "sthu", "stw", "stwu",
        "stfs", "stfsu", "stfd", "stfdu", "lmw", "stmw"
    };
    size_t index;
    for (index = 0U;
         index < sizeof(mnemonics) / sizeof(mnemonics[0]);
         index++) {
        if (strcmp(mnemonic, mnemonics[index]) == 0) return true;
    }
    return false;
}

static bool duplicate_relocation_context_supported(
    const char *mnemonic,
    size_t operand_index,
    bool memory_offset,
    unsigned int suffix_mask) {
    unsigned int allowed = 0U;

    if (memory_offset) {
        if (operand_index == 1U &&
            duplicate_memory_relocation_mnemonic(mnemonic)) {
            allowed = PORPOISE_DUPLICATE_RELOCATION_LOW |
                      PORPOISE_DUPLICATE_RELOCATION_SDA21;
        }
    } else if (strcmp(mnemonic, "li") == 0 && operand_index == 1U) {
        allowed = PORPOISE_DUPLICATE_RELOCATION_LOW |
                  PORPOISE_DUPLICATE_RELOCATION_SDA21;
    } else if (strcmp(mnemonic, "lis") == 0 && operand_index == 1U) {
        allowed = PORPOISE_DUPLICATE_RELOCATION_HIGH |
                  PORPOISE_DUPLICATE_RELOCATION_HIGH_ADJUSTED;
    } else if (strcmp(mnemonic, "addi") == 0 && operand_index == 2U) {
        allowed = PORPOISE_DUPLICATE_RELOCATION_LOW |
                  PORPOISE_DUPLICATE_RELOCATION_SDA21;
    } else if (strcmp(mnemonic, "addis") == 0 && operand_index == 2U) {
        allowed = PORPOISE_DUPLICATE_RELOCATION_HIGH |
                  PORPOISE_DUPLICATE_RELOCATION_HIGH_ADJUSTED;
    } else if ((strcmp(mnemonic, "addic") == 0 ||
                strcmp(mnemonic, "addic.") == 0 ||
                strcmp(mnemonic, "ori") == 0) &&
               operand_index == 2U) {
        allowed = PORPOISE_DUPLICATE_RELOCATION_LOW;
    }
    return (allowed & suffix_mask) != 0U;
}

static bool relocated_operand_tokens_equal(
    const char *mnemonic,
    size_t operand_index,
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length,
    bool memory_offset) {
    const char *left_at = NULL;
    const char *right_at = NULL;
    size_t index;
    size_t left_suffix_length;
    size_t right_suffix_length;
    unsigned int suffix_mask;

    if (left_length == right_length &&
        memcmp(left, right, left_length) == 0) {
        return true;
    }
    for (index = 0U; index < left_length; index++) {
        if (left[index] == '@') left_at = left + index;
    }
    for (index = 0U; index < right_length; index++) {
        if (right[index] == '@') right_at = right + index;
    }
    if (left_at == NULL || right_at == NULL ||
        left_at == left || right_at == right) {
        return false;
    }
    left_suffix_length = left_length - (size_t)(left_at - left);
    right_suffix_length = right_length - (size_t)(right_at - right);
    if (left_suffix_length != right_suffix_length ||
        memcmp(left_at, right_at, left_suffix_length) != 0) {
        return false;
    }
    suffix_mask = duplicate_relocation_suffix_mask(
        left_at, left_suffix_length);
    return suffix_mask != 0U &&
           duplicate_relocation_context_supported(
               mnemonic, operand_index, memory_offset, suffix_mask);
}

static bool duplicate_operands_equal(
    const char *mnemonic,
    const char *left,
    const char *right) {
    size_t operand_index = 0U;
    if (left == NULL || right == NULL) return left == right;
    if (strcmp(left, right) == 0) return true;

    while (*left != '\0' && *right != '\0') {
        bool left_delimiter = operand_token_delimiter(*left);
        bool right_delimiter = operand_token_delimiter(*right);
        const char *left_end;
        const char *right_end;

        if (left_delimiter || right_delimiter) {
            if (!left_delimiter || !right_delimiter || *left != *right)
                return false;
            if (*left == ',') operand_index++;
            left++;
            right++;
            continue;
        }
        left_end = left;
        while (*left_end != '\0' && !operand_token_delimiter(*left_end))
            left_end++;
        right_end = right;
        while (*right_end != '\0' && !operand_token_delimiter(*right_end))
            right_end++;
        if (!relocated_operand_tokens_equal(
                mnemonic, operand_index,
                left, (size_t)(left_end - left),
                right, (size_t)(right_end - right),
                *left_end == '(' && *right_end == '(')) {
            return false;
        }
        left = left_end;
        right = right_end;
    }
    return *left == *right;
}

static bool duplicate_function_items_equal(
    const PorpoiseFunction *left,
    const PorpoiseFunction *right) {
    size_t index;

    if (left->start_address != right->start_address ||
        left->size != right->size ||
        left->item_count != right->item_count ||
        left->instruction_count != right->instruction_count) {
        return false;
    }
    for (index = 0U; index < left->item_count; index++) {
        const PorpoiseAsmItem *left_item = &left->items[index];
        const PorpoiseAsmItem *right_item = &right->items[index];

        if (left_item->kind != right_item->kind) return false;
        if (left_item->kind == PORPOISE_ASM_LABEL) {
            if (left_item->label == NULL || right_item->label == NULL ||
                strcmp(left_item->label, right_item->label) != 0) {
                return false;
            }
            continue;
        }
        if (left_item->address != right_item->address ||
            left_item->word != right_item->word ||
            left_item->mnemonic == NULL || right_item->mnemonic == NULL ||
            strcmp(left_item->mnemonic, right_item->mnemonic) != 0 ||
            !duplicate_operands_equal(
                left_item->mnemonic,
                left_item->operands, right_item->operands)) {
            return false;
        }
    }
    return true;
}

static size_t function_first_instruction_item_index(
    const PorpoiseFunction *function) {
    size_t index;
    for (index = 0U; index < function->item_count; index++) {
        if (function->items[index].kind == PORPOISE_ASM_INSTRUCTION)
            return index;
    }
    return SIZE_MAX;
}

static bool function_merge_alias(
    PorpoiseFunction *function,
    const char *name,
    const char *c_name,
    bool is_global,
    bool is_function_name,
    size_t source_line,
    const char *source_path,
    uint32_t address,
    size_t instruction_item_index) {
    size_t index;
    char *name_copy;
    char *c_name_copy;
    PorpoiseAddressAlias *alias;

    for (index = 0U; index < function->alias_count; index++) {
        alias = &function->aliases[index];
        if (strcmp(alias->name, name) == 0 &&
            strcmp(alias->c_name, c_name) == 0 &&
            alias->address == address &&
            alias->instruction_item_index == instruction_item_index) {
            alias->is_global = alias->is_global || is_global;
            alias->is_function_name =
                alias->is_function_name || is_function_name;
            return true;
        }
    }

    name_copy = porpoise_strdup(name);
    c_name_copy = porpoise_strdup(c_name);
    if (name_copy == NULL || c_name_copy == NULL) {
        free(name_copy);
        free(c_name_copy);
        return false;
    }
    if (function->alias_count == SIZE_MAX ||
        !porpoise_grow_array(
            (void **)&function->aliases, &function->alias_capacity,
            sizeof(*function->aliases), function->alias_count + 1U)) {
        free(name_copy);
        free(c_name_copy);
        return false;
    }
    alias = &function->aliases[function->alias_count++];
    alias->name = name_copy;
    alias->c_name = c_name_copy;
    alias->is_global = is_global;
    alias->is_function_name = is_function_name;
    alias->source_line = source_line;
    alias->source_path = source_path;
    alias->address = address;
    alias->instruction_item_index = instruction_item_index;
    return true;
}

static bool merge_duplicate_function(
    PorpoiseFunction *canonical,
    const PorpoiseFunction *duplicate,
    const PorpoiseSourceFile *duplicate_file) {
    size_t first_instruction_item_index =
        function_first_instruction_item_index(canonical);
    size_t duplicate_first_instruction_item_index =
        function_first_instruction_item_index(duplicate);
    size_t alias_index;
    size_t source_line;

    if (first_instruction_item_index == SIZE_MAX ||
        duplicate_first_instruction_item_index == SIZE_MAX) {
        return false;
    }
    source_line =
        duplicate->items[duplicate_first_instruction_item_index].source_line;
    if (strcmp(canonical->name, duplicate->name) == 0) {
        canonical->is_global = canonical->is_global || duplicate->is_global;
    } else if (!function_merge_alias(
                   canonical, duplicate->name, duplicate->c_name,
                   duplicate->is_global, true, source_line,
                   duplicate_file->path,
                   canonical->start_address, first_instruction_item_index)) {
        return false;
    }

    for (alias_index = 0U;
         alias_index < duplicate->alias_count;
         alias_index++) {
        const PorpoiseAddressAlias *alias = &duplicate->aliases[alias_index];
        if (!function_merge_alias(
                canonical, alias->name, alias->c_name, alias->is_global,
                alias->is_function_name, alias->source_line,
                alias->source_path != NULL
                    ? alias->source_path
                    : duplicate_file->path,
                alias->address,
                alias->instruction_item_index)) {
            return false;
        }
    }
    return true;
}

static void compact_coalesced_functions(
    PorpoiseProgram *program,
    const bool *remove) {
    size_t file_index;
    size_t order = 0U;

    for (file_index = 0U; file_index < program->file_count; file_index++) {
        PorpoiseSourceFile *file = &program->files[file_index];
        size_t read_index;
        size_t write_index = 0U;

        for (read_index = 0U;
             read_index < file->function_count;
             read_index++, order++) {
            if (remove[order]) {
                free_function(&file->functions[read_index]);
                memset(&file->functions[read_index], 0,
                       sizeof(file->functions[read_index]));
                continue;
            }
            if (write_index != read_index) {
                file->functions[write_index] = file->functions[read_index];
                memset(&file->functions[read_index], 0,
                       sizeof(file->functions[read_index]));
            }
            write_index++;
        }
        file->function_count = write_index;
    }
}

static bool coalesce_exact_duplicate_functions(PorpoiseProgram *program) {
    PorpoiseFunctionCoalesceRef *references = NULL;
    bool *remove = NULL;
    size_t function_count = 0U;
    size_t cursor = 0U;
    size_t file_index;

    for (file_index = 0U; file_index < program->file_count; file_index++) {
        if (!checked_size_add(
                &function_count,
                program->files[file_index].function_count)) {
            return false;
        }
    }
    if (function_count < 2U) return true;
    if (!allocate_validation_array(
            (void **)&references, function_count, sizeof(*references))) {
        return false;
    }
    remove = calloc(function_count, sizeof(*remove));
    if (remove == NULL) {
        free(references);
        return false;
    }

    for (file_index = 0U; file_index < program->file_count; file_index++) {
        PorpoiseSourceFile *file = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            references[cursor].file = file;
            references[cursor].function = &file->functions[function_index];
            references[cursor].order = cursor;
            cursor++;
        }
    }
    qsort(references, function_count, sizeof(*references),
          compare_function_coalesce_refs);

    for (cursor = 0U; cursor < function_count;) {
        size_t group_end = cursor + 1U;
        size_t canonical_index;

        while (group_end < function_count &&
               references[group_end].function->start_address ==
                   references[cursor].function->start_address &&
               references[group_end].function->size ==
                   references[cursor].function->size) {
            group_end++;
        }
        for (canonical_index = cursor;
             canonical_index < group_end;
             canonical_index++) {
            size_t duplicate_index;
            PorpoiseFunction *canonical;

            if (remove[references[canonical_index].order]) continue;
            canonical = references[canonical_index].function;
            for (duplicate_index = canonical_index + 1U;
                 duplicate_index < group_end;
                 duplicate_index++) {
                const PorpoiseFunction *duplicate;
                if (remove[references[duplicate_index].order]) continue;
                duplicate = references[duplicate_index].function;
                if (!duplicate_function_items_equal(canonical, duplicate))
                    continue;
                if (!merge_duplicate_function(
                        canonical, duplicate,
                        references[duplicate_index].file)) {
                    free(remove);
                    free(references);
                    return false;
                }
                remove[references[duplicate_index].order] = true;
            }
        }
        cursor = group_end;
    }

    compact_coalesced_functions(program, remove);
    free(remove);
    free(references);
    return true;
}

static void initialize_name_validation_ref(
    PorpoiseNameValidationRef *reference,
    const char *name,
    const char *c_name,
    const PorpoiseSourceFile *file,
    const PorpoiseFunction *function,
    const PorpoiseAddressAlias *alias,
    const PorpoiseAsmItem *label,
    PorpoiseNameValidationKind kind,
    size_t order) {
    reference->name = name;
    reference->c_name = c_name;
    reference->file = file;
    reference->function = function;
    reference->alias = alias;
    reference->label = label;
    reference->key_hash = name_validation_hash(name, c_name);
    reference->order = order;
    reference->kind = kind;
}

static const char *name_validation_source_path(
    const PorpoiseNameValidationRef *reference) {
    if (reference->alias != NULL &&
        reference->alias->source_path != NULL) {
        return reference->alias->source_path;
    }
    return reference->file->path;
}

static bool ensure_unique_symbols(
    const PorpoiseProgram *program,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseFileValidationRef *file_refs = NULL;
    PorpoiseDataValidationRef *data_refs = NULL;
    PorpoiseFunctionValidationRef *function_refs = NULL;
    PorpoiseNameValidationRef *name_refs = NULL;
    size_t data_count = 0U;
    size_t function_count = 0U;
    size_t name_count = 0U;
    size_t data_cursor = 0U;
    size_t function_cursor = 0U;
    size_t name_cursor = 0U;
    size_t file_index;
    bool ok = true;

    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        size_t function_index;
        if (!checked_size_add(&data_count, file->data_word_count) ||
            !checked_size_add(&function_count, file->function_count) ||
            !checked_size_add(&name_count, file->function_count)) goto allocation_failure;
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            size_t item_index;
            if (!checked_size_add(&name_count, function->alias_count))
                goto allocation_failure;
            for (item_index = 0U; item_index < function->item_count; item_index++) {
                if (function->items[item_index].kind == PORPOISE_ASM_LABEL &&
                    !checked_size_add(&name_count, 1U)) goto allocation_failure;
            }
        }
    }
    if (!allocate_validation_array(
            (void **)&file_refs,
            program->file_count,
            sizeof(*file_refs)) ||
        !allocate_validation_array(
            (void **)&data_refs,
            data_count,
            sizeof(*data_refs)) ||
        !allocate_validation_array(
            (void **)&function_refs,
            function_count,
            sizeof(*function_refs)) ||
        !allocate_validation_array(
            (void **)&name_refs,
            name_count,
            sizeof(*name_refs))) goto allocation_failure;

    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        size_t data_index;
        size_t function_index;
        file_refs[file_index].file = file;
        file_refs[file_index].order = file_index;
        for (data_index = 0U; data_index < file->data_word_count; data_index++) {
            data_refs[data_cursor].file = file;
            data_refs[data_cursor].word = &file->data_words[data_index];
            data_refs[data_cursor].order = data_cursor;
            data_cursor++;
        }
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            size_t alias_index;
            size_t item_index;
            function_refs[function_cursor].file = file;
            function_refs[function_cursor].function = function;
            function_refs[function_cursor].order = function_cursor;
            function_cursor++;
            initialize_name_validation_ref(
                &name_refs[name_cursor], function->name, function->c_name,
                file, function, NULL, NULL, PORPOISE_VALIDATION_FUNCTION,
                name_cursor);
            name_cursor++;
            for (alias_index = 0U;
                 alias_index < function->alias_count;
                 alias_index++) {
                const PorpoiseAddressAlias *alias = &function->aliases[alias_index];
                initialize_name_validation_ref(
                    &name_refs[name_cursor], alias->name, alias->c_name,
                    file, function, alias, NULL, PORPOISE_VALIDATION_ALIAS,
                    name_cursor);
                name_cursor++;
            }
            for (item_index = 0U; item_index < function->item_count; item_index++) {
                const PorpoiseAsmItem *item = &function->items[item_index];
                if (item->kind != PORPOISE_ASM_LABEL) continue;
                initialize_name_validation_ref(
                    &name_refs[name_cursor], item->label, NULL,
                    file, function, NULL, item, PORPOISE_VALIDATION_LABEL,
                    name_cursor);
                name_cursor++;
            }
        }
    }
    if (data_cursor != data_count || function_cursor != function_count ||
        name_cursor != name_count) goto allocation_failure;

    if (program->file_count > 1U) {
        qsort(file_refs, program->file_count, sizeof(*file_refs),
              compare_file_validation_refs);
    }
    if (data_count > 1U) {
        qsort(data_refs, data_count, sizeof(*data_refs),
              compare_data_validation_refs);
    }
    if (function_count > 1U) {
        qsort(function_refs, function_count, sizeof(*function_refs),
              compare_function_validation_refs);
    }
    if (name_count > 1U) {
        qsort(name_refs, name_count, sizeof(*name_refs),
              compare_name_validation_refs);
    }

    for (file_index = 1U; file_index < program->file_count; file_index++) {
        const PorpoiseFileValidationRef *current = &file_refs[file_index];
        const PorpoiseFileValidationRef *prior = &file_refs[file_index - 1U];
        if (compare_portable_names(current->file->output_stem,
                                   prior->file->output_stem) == 0) {
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, current->file->path, 0U, 0U,
                "input filenames collide after portable output-name sanitization: %s and %s",
                prior->file->relative_path, current->file->relative_path);
            ok = false;
        }
    }
    for (data_cursor = 0U; data_cursor < data_count; data_cursor++) {
        const PorpoiseDataValidationRef *current = &data_refs[data_cursor];
        if (current->word->address > UINT32_MAX - 3U) {
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR, current->file->path,
                current->word->source_line, current->word->address,
                "annotated data word crosses the 32-bit address boundary");
            ok = false;
        }
        if (data_cursor != 0U) {
            const PorpoiseDataValidationRef *prior = &data_refs[data_cursor - 1U];
            uint64_t current_start = current->word->address;
            uint64_t prior_start = prior->word->address;
            if (current_start < prior_start + UINT64_C(4)) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_ERROR, current->file->path,
                    current->word->source_line, current->word->address,
                    "annotated data overlaps %s:%lu",
                    prior->file->relative_path,
                    (unsigned long)prior->word->source_line);
                ok = false;
            }
        }
    }
    if (function_count != 0U) {
        size_t active_index = 0U;
        uint64_t active_end =
            (uint64_t)function_refs[0].function->start_address +
            function_refs[0].function->size;
        for (function_cursor = 1U;
             function_cursor < function_count;
             function_cursor++) {
            const PorpoiseFunctionValidationRef *current =
                &function_refs[function_cursor];
            uint64_t current_start = current->function->start_address;
            uint64_t current_end = current_start + current->function->size;
            if (current->function->size != 0U &&
                function_refs[active_index].function->size != 0U &&
                current_start < active_end) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_ERROR, current->file->path,
                    0U, current->function->start_address,
                    "function address range overlaps %s in %s",
                    function_refs[active_index].function->name,
                    function_refs[active_index].file->relative_path);
                ok = false;
            }
            if (current_start >= active_end || current_end > active_end) {
                active_index = function_cursor;
                active_end = current_end;
            }
        }
    }
    for (name_cursor = 0U; name_cursor < name_count;) {
        size_t group_end = name_cursor + 1U;
        const PorpoiseNameValidationRef *first_function = NULL;
        const PorpoiseNameValidationRef *first_alias = NULL;
        const PorpoiseNameValidationRef *first_label = NULL;
        size_t cursor;
        while (group_end < name_count &&
               name_validation_keys_equal(
                   &name_refs[name_cursor], &name_refs[group_end])) {
            group_end++;
        }
        for (cursor = name_cursor; cursor < group_end; cursor++) {
            const PorpoiseNameValidationRef *reference = &name_refs[cursor];
            if (reference->kind == PORPOISE_VALIDATION_FUNCTION &&
                first_function == NULL) first_function = reference;
            else if (reference->kind == PORPOISE_VALIDATION_ALIAS &&
                     first_alias == NULL) first_alias = reference;
            else if (reference->kind == PORPOISE_VALIDATION_LABEL &&
                     first_label == NULL) first_label = reference;
        }
        for (cursor = name_cursor; cursor < group_end; cursor++) {
            const PorpoiseNameValidationRef *reference = &name_refs[cursor];
            if (reference->kind == PORPOISE_VALIDATION_FUNCTION) {
                if (reference != first_function) {
                    porpoise_diagnostics_add(
                        diagnostics, PORPOISE_SEVERITY_ERROR,
                        reference->file->path, 0U,
                        reference->function->start_address,
                        "duplicate or colliding function symbol %s",
                        reference->function->name);
                    ok = false;
                }
                continue;
            }
            if (reference->kind != PORPOISE_VALIDATION_ALIAS) continue;
            if (first_function != NULL) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_ERROR,
                    name_validation_source_path(reference),
                    reference->alias->source_line,
                    reference->alias->address,
                    "symbol alias %s conflicts with function symbol %s",
                    reference->alias->name, first_function->function->name);
                ok = false;
            }
            if (reference != first_alias) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_ERROR,
                    name_validation_source_path(reference),
                    reference->alias->source_line,
                    reference->alias->address,
                    "duplicate or colliding symbol alias %s",
                    reference->alias->name);
                ok = false;
            }
            if (first_label != NULL) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_ERROR,
                    name_validation_source_path(reference),
                    reference->alias->source_line,
                    reference->alias->address,
                    "symbol alias %s conflicts with label %s in function %s",
                    reference->alias->name, first_label->label->label,
                    first_label->function->name);
                ok = false;
            }
        }
        name_cursor = group_end;
    }

    free(file_refs);
    free(data_refs);
    free(function_refs);
    free(name_refs);
    return ok;

allocation_failure:
    free(file_refs);
    free(data_refs);
    free(function_refs);
    free(name_refs);
    return false;
}

int porpoise_program_load(
    PorpoiseProgram *program,
    const char *input_path,
    PorpoiseDiagnostics *diagnostics) {
    PorpoisePathList paths = {0};
    char input_root[PORPOISE_PATH_CAPACITY];
    size_t index;
    bool ok = true;
    bool io_failure = false;
    if (program == NULL || input_path == NULL || diagnostics == NULL) {
        return PORPOISE_EXIT_INTERNAL;
    }
    if (program->file_count != 0U || program->symbol_index_count != 0U ||
        program->label_index_count != 0U) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, input_path, 0U, 0U,
            "program already contains parsed input; initialize an empty program before loading");
        return PORPOISE_EXIT_INTERNAL;
    }
    if (!porpoise_path_exists(input_path)) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, input_path, 0U, 0U,
                                 "input path does not exist");
        return PORPOISE_EXIT_IO;
    }
    if (porpoise_path_is_directory(input_path)) {
        if (!porpoise_copy_string(input_root, sizeof(input_root), input_path) ||
            !collect_paths_recursive(input_root, "", 0U, &paths, diagnostics)) {
            path_list_free(&paths);
            return PORPOISE_EXIT_IO;
        }
    } else {
        char parent[PORPOISE_PATH_CAPACITY];
        char base[PORPOISE_PATH_CAPACITY];
        if (!has_assembly_extension(input_path)) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, input_path, 0U, 0U,
                                     "input file must have a .s extension");
            return PORPOISE_EXIT_USAGE;
        }
        if (!porpoise_path_parent(parent, sizeof(parent), input_path) ||
            !porpoise_path_basename(base, sizeof(base), input_path) ||
            !porpoise_copy_string(input_root, sizeof(input_root), parent) ||
            !path_list_add(&paths, base)) {
            path_list_free(&paths);
            return PORPOISE_EXIT_INTERNAL;
        }
    }
    if (paths.count == 0U) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, input_path, 0U, 0U,
                                 "input contains no assembly files");
        path_list_free(&paths);
        return PORPOISE_EXIT_TRANSLATION;
    }
    qsort(paths.items, paths.count, sizeof(*paths.items), compare_paths);
    for (index = 0U; index < paths.count; index++) {
        char full_path[PORPOISE_PATH_CAPACITY];
        PorpoiseSourceFile *file;
        if (!porpoise_path_join(full_path, sizeof(full_path), input_root, paths.items[index])) {
            ok = false;
            break;
        }
        file = program_add_file(program, full_path, paths.items[index]);
        if (file == NULL) {
            ok = false;
            break;
        }
        if (!parse_file(file, diagnostics, &io_failure)) {
            ok = false;
        }
    }
    path_list_free(&paths);
    if (ok && !coalesce_exact_duplicate_functions(program)) ok = false;
    if (ok && !ensure_unique_symbols(program, diagnostics)) ok = false;
    if (ok && !program_build_lookup_indices(program)) ok = false;
    if (!ok) {
        if (io_failure) return PORPOISE_EXIT_IO;
        return porpoise_diagnostics_have_errors(diagnostics) ? PORPOISE_EXIT_TRANSLATION : PORPOISE_EXIT_INTERNAL;
    }
    return PORPOISE_EXIT_OK;
}

const PorpoiseFunction *porpoise_program_find_function(
    const PorpoiseProgram *program,
    const char *name) {
    size_t index;

    if (program == NULL || name == NULL) return NULL;
    index = program_symbol_lower_bound(program, name);
    while (index < program->symbol_index_count &&
           strcmp(program->symbol_index[index].name, name) == 0) {
        const PorpoiseProgramSymbolIndexEntry *entry =
            &program->symbol_index[index++];

        if ((entry->alias == NULL || entry->alias->is_function_name) &&
            !entry->function->skipped) {
            return entry->function;
        }
    }
    return NULL;
}

const PorpoiseAddressAlias *porpoise_program_find_alias(
    const PorpoiseProgram *program,
    const char *name,
    const PorpoiseFunction **function_out) {
    size_t index;

    if (function_out != NULL) *function_out = NULL;
    if (program == NULL || name == NULL) return NULL;
    index = program_symbol_lower_bound(program, name);
    while (index < program->symbol_index_count &&
           strcmp(program->symbol_index[index].name, name) == 0) {
        const PorpoiseProgramSymbolIndexEntry *entry =
            &program->symbol_index[index++];

        if (entry->alias != NULL && !entry->function->skipped) {
            if (function_out != NULL) *function_out = entry->function;
            return entry->alias;
        }
    }
    return NULL;
}

size_t porpoise_program_count_aliases(const PorpoiseProgram *program) {
    size_t file_index;
    size_t count = 0U;
    if (program == NULL) return 0U;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        size_t function_index;
        for (function_index = 0U;
             function_index < program->files[file_index].function_count;
             function_index++) {
            count += program->files[file_index].functions[function_index].alias_count;
        }
    }
    return count;
}

const PorpoiseAddressAlias *porpoise_program_alias_at(
    const PorpoiseProgram *program,
    size_t index,
    const PorpoiseSourceFile **file_out,
    const PorpoiseFunction **function_out) {
    size_t file_index;
    if (file_out != NULL) *file_out = NULL;
    if (function_out != NULL) *function_out = NULL;
    if (program == NULL) return NULL;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U; function_index < file->function_count; function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            if (index < function->alias_count) {
                if (file_out != NULL) *file_out = file;
                if (function_out != NULL) *function_out = function;
                return &function->aliases[index];
            }
            index -= function->alias_count;
        }
    }
    return NULL;
}

bool porpoise_program_resolve_symbol(
    const PorpoiseProgram *program,
    const char *name,
    const PorpoiseFunction **function_out,
    const PorpoiseAddressAlias **alias_out,
    uint32_t *address_out) {
    const PorpoiseFunction *function;
    const PorpoiseAddressAlias *alias;
    if (function_out != NULL) *function_out = NULL;
    if (alias_out != NULL) *alias_out = NULL;
    if (address_out != NULL) *address_out = 0U;
    if (program == NULL || name == NULL) return false;
    function = porpoise_program_find_function(program, name);
    if (function != NULL) {
        const PorpoiseFunction *alias_owner = NULL;
        alias = porpoise_program_find_alias(program, name, &alias_owner);
        if (alias != NULL && alias->is_function_name) {
            if (function_out != NULL) *function_out = alias_owner;
            if (alias_out != NULL) *alias_out = alias;
            if (address_out != NULL) *address_out = alias->address;
            return true;
        }
        if (function_out != NULL) *function_out = function;
        if (address_out != NULL) *address_out = function->start_address;
        return true;
    }
    alias = porpoise_program_find_alias(program, name, &function);
    if (alias == NULL) return false;
    if (function_out != NULL) *function_out = function;
    if (alias_out != NULL) *alias_out = alias;
    if (address_out != NULL) *address_out = alias->address;
    return true;
}

bool porpoise_program_resolve_unique_label(
    const PorpoiseProgram *program,
    const char *name,
    const PorpoiseFunction **function_out,
    uint32_t *address_out,
    size_t *instruction_item_index_out) {
    const PorpoiseProgramLabelIndexEntry *matched = NULL;
    size_t match_count = 0U;
    size_t index;
    if (function_out != NULL) *function_out = NULL;
    if (address_out != NULL) *address_out = 0U;
    if (instruction_item_index_out != NULL) {
        *instruction_item_index_out = SIZE_MAX;
    }
    if (program == NULL || name == NULL) return false;
    index = program_label_lower_bound(program, name);
    while (index < program->label_index_count &&
           strcmp(program->label_index[index].name, name) == 0) {
        const PorpoiseProgramLabelIndexEntry *entry =
            &program->label_index[index++];

        if (entry->function->skipped) continue;
        match_count++;
        if (match_count > 1U) return false;
        matched = entry;
    }
    if (match_count != 1U || matched == NULL ||
        matched->instruction_item_index == SIZE_MAX) {
        return false;
    }
    if (function_out != NULL) *function_out = matched->function;
    if (address_out != NULL) *address_out = matched->address;
    if (instruction_item_index_out != NULL) {
        *instruction_item_index_out = matched->instruction_item_index;
    }
    return true;
}

size_t porpoise_program_count_named_function(
    const PorpoiseProgram *program,
    const char *name) {
    size_t index;
    size_t count = 0U;

    if (program == NULL || name == NULL) return 0U;
    index = program_symbol_lower_bound(program, name);
    while (index < program->symbol_index_count &&
           strcmp(program->symbol_index[index].name, name) == 0) {
        const PorpoiseProgramSymbolIndexEntry *entry =
            &program->symbol_index[index++];

        if (!entry->function->skipped &&
            ((entry->alias == NULL &&
              strcmp(entry->function->name, name) == 0) ||
             (entry->alias != NULL && entry->alias->is_function_name &&
              strcmp(entry->alias->name, name) == 0))) {
            count++;
        }
    }
    return count;
}

static PorpoiseFunction *program_find_function_name_for_skip(
    PorpoiseProgram *program,
    const char *name) {
    size_t index = program_symbol_lower_bound(program, name);
    while (index < program->symbol_index_count &&
           strcmp(program->symbol_index[index].name, name) == 0) {
        const PorpoiseProgramSymbolIndexEntry *entry =
            &program->symbol_index[index++];
        if ((entry->alias == NULL &&
             strcmp(entry->function->name, name) == 0) ||
            (entry->alias != NULL && entry->alias->is_function_name &&
             strcmp(entry->alias->name, name) == 0)) {
            return (PorpoiseFunction *)entry->function;
        }
    }
    return NULL;
}

int porpoise_program_apply_skip_list(
    PorpoiseProgram *program,
    const char *path,
    PorpoiseDiagnostics *diagnostics) {
    FILE *input;
    PorpoiseFunction **targets = NULL;
    size_t target_count = 0U;
    size_t target_capacity = 0U;
    char line[PORPOISE_NAME_CAPACITY + 16U];
    size_t line_number = 0U;
    bool ok = true;
    bool io_failure = false;
    bool internal_failure = false;
    if (path == NULL || path[0] == '\0') return PORPOISE_EXIT_OK;
    if (program == NULL || diagnostics == NULL) return PORPOISE_EXIT_INTERNAL;
    input = fopen(path, "rb");
    if (input == NULL) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, 0U, 0U,
                                 "cannot open skip list: %s", strerror(errno));
        return PORPOISE_EXIT_IO;
    }
    while (fgets(line, sizeof(line), input) != NULL) {
        PorpoiseFunction *function;
        char *comment;
        line_number++;
        if (strchr(line, '\n') == NULL && !feof(input)) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, line_number, 0U,
                                     "skip-list line exceeds the supported length");
            ok = false;
            break;
        }
        comment = strchr(line, '#');
        if (comment != NULL) *comment = '\0';
        porpoise_trim(line);
        if (line[0] == '\0') continue;
        function = program_find_function_name_for_skip(program, line);
        if (function == NULL) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, line_number, 0U,
                                     "skip-list symbol %s is not present in the input", line);
            ok = false;
            continue;
        }
        if (target_count == SIZE_MAX ||
            !porpoise_grow_array((void **)&targets, &target_capacity,
                                 sizeof(*targets), target_count + 1U)) {
            internal_failure = true;
            break;
        }
        targets[target_count++] = function;
    }
    if (ferror(input) != 0) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, line_number, 0U,
                                 "failed while reading skip list");
        io_failure = true;
    }
    if (fclose(input) != 0) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, line_number, 0U,
                                 "failed while closing skip list");
        io_failure = true;
    }
    if (!internal_failure && !io_failure && ok) {
        size_t index;
        for (index = 0U; index < target_count; index++) {
            targets[index]->skipped = true;
        }
    }
    free(targets);
    if (internal_failure) return PORPOISE_EXIT_INTERNAL;
    if (io_failure) return PORPOISE_EXIT_IO;
    return ok ? PORPOISE_EXIT_OK : PORPOISE_EXIT_USAGE;
}
