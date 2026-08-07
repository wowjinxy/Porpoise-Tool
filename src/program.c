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
    if (!porpoise_grow_array((void **)&paths->items, &paths->capacity,
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
    free(program->files);
    memset(program, 0, sizeof(*program));
}

static PorpoiseSourceFile *program_add_file(
    PorpoiseProgram *program,
    const char *path,
    const char *relative_path) {
    PorpoiseSourceFile *file;
    char stem[PORPOISE_PATH_CAPACITY];
    char *dot;
    size_t component_start = 0U;
    size_t cursor;
    if (!porpoise_grow_array((void **)&program->files, &program->file_capacity,
                             sizeof(*program->files), program->file_count + 1U)) {
        return NULL;
    }
    file = &program->files[program->file_count++];
    memset(file, 0, sizeof(*file));
    file->path = porpoise_strdup(path);
    file->relative_path = porpoise_strdup(relative_path);
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
    file->output_stem = porpoise_strdup(stem);
    return file->path != NULL && file->relative_path != NULL && file->output_stem != NULL ? file : NULL;
}

static PorpoiseFunction *file_add_function(
    PorpoiseSourceFile *file,
    const char *name,
    bool is_global) {
    PorpoiseFunction *function;
    char c_name[PORPOISE_NAME_CAPACITY];
    if (!porpoise_grow_array((void **)&file->functions, &file->function_capacity,
                             sizeof(*file->functions), file->function_count + 1U)) {
        return NULL;
    }
    function = &file->functions[file->function_count++];
    memset(function, 0, sizeof(*function));
    function->name = porpoise_strdup(name);
    porpoise_sanitize_identifier(name, c_name, sizeof(c_name));
    function->c_name = porpoise_strdup(c_name);
    function->is_global = is_global;
    return function->name != NULL && function->c_name != NULL ? function : NULL;
}

static PorpoiseAsmItem *function_add_item(PorpoiseFunction *function) {
    PorpoiseAsmItem *item;
    if (!porpoise_grow_array((void **)&function->items, &function->item_capacity,
                             sizeof(*function->items), function->item_count + 1U)) {
        return NULL;
    }
    item = &function->items[function->item_count++];
    memset(item, 0, sizeof(*item));
    return item;
}

static PorpoiseDataWord *file_add_data_word(PorpoiseSourceFile *file) {
    PorpoiseDataWord *word;
    if (!porpoise_grow_array((void **)&file->data_words, &file->data_word_capacity,
                             sizeof(*file->data_words), file->data_word_count + 1U)) return NULL;
    word = &file->data_words[file->data_word_count++];
    memset(word, 0, sizeof(*word));
    return word;
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
    bool have_previous_instruction = false;
    uint32_t previous_instruction_address = 0U;
    bool in_data_section = false;
    bool ok = true;
    if (input == NULL) {
        *io_failure = true;
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path, 0U, 0U,
                                 "cannot open input: %s", strerror(errno));
        return false;
    }
    while (fgets(line, sizeof(line), input) != NULL) {
        char function_name[PORPOISE_NAME_CAPACITY];
        char function_end_name[PORPOISE_NAME_CAPACITY];
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
        if (line[0] == '\0' || line[0] == '#') continue;
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
            if (current == NULL) {
                porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                         line_number, 0U, ".endfn without .fn");
                ok = false;
                continue;
            }
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
                if (data_word->directive == NULL) { ok = false; break; }
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
            if (item->label == NULL) { ok = false; break; }
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
            if (item->mnemonic == NULL || item->operands == NULL) { ok = false; break; }
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
    if (fclose(input) != 0) {
        *io_failure = true;
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                 line_number, 0U, "failed while closing input");
        ok = false;
    }
    return ok;
}

static bool ensure_unique_symbols(PorpoiseProgram *program, PorpoiseDiagnostics *diagnostics) {
    size_t file_index;
    size_t function_index;
    size_t other_file;
    size_t other_function;
    bool ok = true;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        for (other_file = 0U; other_file < file_index; other_file++) {
            const char *left = program->files[file_index].output_stem;
            const char *right = program->files[other_file].output_stem;
            size_t cursor = 0U;
            while (left[cursor] != '\0' && right[cursor] != '\0' &&
                   tolower((unsigned char)left[cursor]) == tolower((unsigned char)right[cursor])) cursor++;
            if (left[cursor] == '\0' && right[cursor] == '\0') {
                porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR,
                                         program->files[file_index].path, 0U, 0U,
                                         "input filenames collide after portable output-name sanitization: %s and %s",
                                         program->files[other_file].relative_path,
                                         program->files[file_index].relative_path);
                ok = false;
            }
        }
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        PorpoiseSourceFile *file = &program->files[file_index];
        size_t data_index;
        for (data_index = 0U; data_index < file->data_word_count; data_index++) {
            const PorpoiseDataWord *word = &file->data_words[data_index];
            size_t prior_file;
            if (word->address > UINT32_MAX - 3U) {
                porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                         word->source_line, word->address,
                                         "annotated data word crosses the 32-bit address boundary");
                ok = false;
            }
            for (prior_file = 0U; prior_file <= file_index; prior_file++) {
                const PorpoiseSourceFile *candidate_file = &program->files[prior_file];
                size_t limit = prior_file == file_index ? data_index : candidate_file->data_word_count;
                size_t prior_data;
                for (prior_data = 0U; prior_data < limit; prior_data++) {
                    const PorpoiseDataWord *candidate = &candidate_file->data_words[prior_data];
                    uint64_t left_start = word->address;
                    uint64_t right_start = candidate->address;
                    if (left_start < right_start + 4U && right_start < left_start + 4U) {
                        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                                 word->source_line, word->address,
                                                 "annotated data overlaps %s:%lu",
                                                 candidate_file->relative_path,
                                                 (unsigned long)candidate->source_line);
                        ok = false;
                    }
                }
            }
        }
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        PorpoiseSourceFile *file = &program->files[file_index];
        for (function_index = 0U; function_index < file->function_count; function_index++) {
            PorpoiseFunction *function = &file->functions[function_index];
            for (other_file = 0U; other_file <= file_index; other_file++) {
                PorpoiseSourceFile *candidate_file = &program->files[other_file];
                size_t limit = other_file == file_index ? function_index : candidate_file->function_count;
                for (other_function = 0U; other_function < limit; other_function++) {
                    const PorpoiseFunction *candidate = &candidate_file->functions[other_function];
                    if (strcmp(function->name, candidate->name) == 0 ||
                        strcmp(function->c_name, candidate->c_name) == 0) {
                        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                                 0U, function->start_address,
                                                 "duplicate or colliding function symbol %s", function->name);
                        ok = false;
                    }
                    if (function->size != 0U && candidate->size != 0U) {
                        uint64_t function_start = function->start_address;
                        uint64_t function_end = function_start + function->size;
                        uint64_t candidate_start = candidate->start_address;
                        uint64_t candidate_end = candidate_start + candidate->size;
                        if (function_start < candidate_end && candidate_start < function_end) {
                            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR,
                                                     file->path, 0U, function->start_address,
                                                     "function address range overlaps %s in %s",
                                                     candidate->name,
                                                     candidate_file->relative_path);
                            ok = false;
                        }
                    }
                }
            }
        }
    }
    return ok;
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
    if (!ensure_unique_symbols(program, diagnostics)) ok = false;
    if (!ok) {
        if (io_failure) return PORPOISE_EXIT_IO;
        return porpoise_diagnostics_have_errors(diagnostics) ? PORPOISE_EXIT_TRANSLATION : PORPOISE_EXIT_INTERNAL;
    }
    return PORPOISE_EXIT_OK;
}

const PorpoiseFunction *porpoise_program_find_function(
    const PorpoiseProgram *program,
    const char *name) {
    size_t file_index;
    size_t function_index;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        for (function_index = 0U; function_index < program->files[file_index].function_count; function_index++) {
            const PorpoiseFunction *function = &program->files[file_index].functions[function_index];
            if (!function->skipped &&
                (strcmp(function->name, name) == 0 || strcmp(function->c_name, name) == 0)) {
                return function;
            }
        }
    }
    return NULL;
}

size_t porpoise_program_count_named_function(
    const PorpoiseProgram *program,
    const char *name) {
    size_t file_index;
    size_t function_index;
    size_t count = 0U;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        for (function_index = 0U; function_index < program->files[file_index].function_count; function_index++) {
            if (!program->files[file_index].functions[function_index].skipped &&
                strcmp(program->files[file_index].functions[function_index].name, name) == 0) count++;
        }
    }
    return count;
}

int porpoise_program_apply_skip_list(
    PorpoiseProgram *program,
    const char *path,
    PorpoiseDiagnostics *diagnostics) {
    FILE *input;
    char line[PORPOISE_NAME_CAPACITY + 16U];
    size_t line_number = 0U;
    bool ok = true;
    if (path == NULL || path[0] == '\0') return PORPOISE_EXIT_OK;
    input = fopen(path, "rb");
    if (input == NULL) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, 0U, 0U,
                                 "cannot open skip list: %s", strerror(errno));
        return PORPOISE_EXIT_IO;
    }
    while (fgets(line, sizeof(line), input) != NULL) {
        size_t file_index;
        size_t function_index;
        size_t matches = 0U;
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
        for (file_index = 0U; file_index < program->file_count; file_index++) {
            for (function_index = 0U; function_index < program->files[file_index].function_count; function_index++) {
                PorpoiseFunction *function = &program->files[file_index].functions[function_index];
                if (strcmp(function->name, line) == 0) {
                    function->skipped = true;
                    matches++;
                }
            }
        }
        if (matches == 0U) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, line_number, 0U,
                                     "skip-list symbol %s is not present in the input", line);
            ok = false;
        }
    }
    if (ferror(input) != 0) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, line_number, 0U,
                                 "failed while reading skip list");
        ok = false;
    }
    if (fclose(input) != 0) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, line_number, 0U,
                                 "failed while closing skip list");
        return PORPOISE_EXIT_IO;
    }
    return ok ? PORPOISE_EXIT_OK : PORPOISE_EXIT_USAGE;
}
