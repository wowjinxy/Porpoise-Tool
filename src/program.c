#include "porpoise/program.h"
#include "porpoise/relocation.h"
#include "porpoise/util.h"

#include "asm_data_internal.h"

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
    char *section;
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
    free(function->section);
    for (index = 0U; index < function->item_count; index++) {
        free_item(&function->items[index]);
    }
    free(function->items);
    for (index = 0U; index < function->alias_count; index++) {
        free(function->aliases[index].name);
        free(function->aliases[index].c_name);
        free(function->aliases[index].section);
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
    for (index = 0U; index < file->data_alias_count; index++) {
        free(file->data_aliases[index].name);
        free(file->data_aliases[index].section);
    }
    free(file->data_aliases);
    for (index = 0U; index < file->data_object_count; index++) {
        porpoise_asm_data_free_object(&file->data_objects[index]);
    }
    free(file->data_objects);
    for (index = 0U; index < file->anonymous_data_count; index++) {
        porpoise_asm_data_free_object(
            &file->anonymous_data[index].storage);
        free(file->anonymous_data[index].present);
    }
    free(file->anonymous_data);
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
    for (index = 0U; index < program->data_span_count; index++) {
        free(program->data_spans[index].bytes);
    }
    free(program->data_spans);
    free(program->symbol_index);
    free(program->label_index);
    free(program->data_symbol_index);
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
    bool is_global,
    const char *section) {
    PorpoiseFunction candidate = {0};
    PorpoiseFunction *function;
    char c_name[PORPOISE_NAME_CAPACITY];
    porpoise_sanitize_identifier(name, c_name, sizeof(c_name));
    candidate.name = porpoise_strdup(name);
    candidate.c_name = porpoise_strdup(c_name);
    candidate.section = porpoise_strdup(section);
    candidate.is_global = is_global;
    if (candidate.name == NULL || candidate.c_name == NULL ||
        candidate.section == NULL ||
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
        free(aliases->items[index].section);
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
    size_t source_line,
    const char *section) {
    PorpoisePendingAlias *alias;
    char c_name[PORPOISE_NAME_CAPACITY];
    char *name_copy;
    char *c_name_copy;
    char *section_copy;
    porpoise_sanitize_identifier(name, c_name, sizeof(c_name));
    name_copy = porpoise_strdup(name);
    c_name_copy = porpoise_strdup(c_name);
    section_copy = porpoise_strdup(section);
    if (name_copy == NULL || c_name_copy == NULL || section_copy == NULL) {
        free(name_copy);
        free(c_name_copy);
        free(section_copy);
        return false;
    }
    if (aliases->count == SIZE_MAX ||
        !porpoise_grow_array((void **)&aliases->items, &aliases->capacity,
                             sizeof(*aliases->items), aliases->count + 1U)) {
        free(name_copy);
        free(c_name_copy);
        free(section_copy);
        return false;
    }
    alias = &aliases->items[aliases->count++];
    alias->name = name_copy;
    alias->c_name = c_name_copy;
    alias->section = section_copy;
    alias->is_global = is_global;
    alias->source_line = source_line;
    return true;
}

static bool function_bind_pending_aliases(
    PorpoiseFunction *function,
    PorpoisePendingAliasList *pending,
    uint32_t address,
    size_t instruction_item_index,
    const char *source_path) {
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
        destination->section = source->section;
        destination->is_global = source->is_global;
        destination->is_function_name = false;
        destination->source_line = source->source_line;
        destination->source_path = source_path;
        destination->address = address;
        destination->instruction_item_index = instruction_item_index;
        source->name = NULL;
        source->c_name = NULL;
        source->section = NULL;
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

/*
 * decomp-toolkit represents otherwise unclaimed section bytes with a strict
 * gap_SS_AAAAAAAA_section symbol. Although emitted with .fn/.endfn markers,
 * those records are byte containers rather than inferred executable code.
 */
static bool is_annotated_gap_data_name(
    const char *name,
    uint32_t *address_out) {
    size_t index;
    uint32_t address = 0U;

    if (name == NULL || strlen(name) < 17U ||
        strncmp(name, "gap_", 4U) != 0 ||
        !isxdigit((unsigned char)name[4]) ||
        !isxdigit((unsigned char)name[5]) ||
        name[6] != '_') {
        return false;
    }
    for (index = 7U; index < 15U; index++) {
        unsigned char character = (unsigned char)name[index];
        uint32_t digit;

        if (!isxdigit(character)) return false;
        if (character >= '0' && character <= '9') {
            digit = (uint32_t)(character - '0');
        } else {
            digit = (uint32_t)(toupper(character) - 'A' + 10);
        }
        address = (address << 4U) | digit;
    }
    if (name[15] != '_' || name[16] == '\0') return false;
    for (index = 16U; name[index] != '\0'; index++) {
        if (!isalnum((unsigned char)name[index]) && name[index] != '_') {
            return false;
        }
    }
    if (address_out != NULL) *address_out = address;
    return true;
}

static bool materialize_gap_data_regions(PorpoiseProgram *program) {
    size_t file_index;

    for (file_index = 0U; file_index < program->file_count; file_index++) {
        PorpoiseSourceFile *file = &program->files[file_index];
        size_t function_index;

        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            size_t item_index;

            if (!function->data_region) continue;
            for (item_index = 0U;
                 item_index < function->item_count;
                 item_index++) {
                const PorpoiseAsmItem *item = &function->items[item_index];
                PorpoiseDataWord *word;

                if (item->kind != PORPOISE_ASM_INSTRUCTION) continue;
                word = file_add_data_word(file);
                if (word == NULL) return false;
                word->source_line = item->source_line;
                word->address = item->address;
                word->word = item->word;
                word->directive = porpoise_strdup(".4byte");
                if (word->directive == NULL) {
                    file_rollback_last_data_word(file, word);
                    return false;
                }
            }
        }
    }
    return true;
}

static bool section_is_data(const char *section) {
    return strstr(section, ".data") != NULL ||
           strstr(section, ".rodata") != NULL ||
           strstr(section, ".sdata") != NULL ||
           strstr(section, ".bss") != NULL ||
           strstr(section, ".sbss") != NULL;
}

static bool parse_section_directive(
    const char *line,
    char *section,
    size_t section_capacity,
    bool *is_data) {
    const char *cursor = line;
    const char *start;
    const char *end;
    size_t length;
    static const char *const selectors[] = {
        ".text", ".init", ".data", ".rodata", ".sdata", ".sdata2",
        ".bss", ".sbss", ".sbss2", ".ctors", ".dtors", ".extab",
        ".extabindex"
    };
    size_t selector_index;
    while (isspace((unsigned char)*cursor)) cursor++;
    for (selector_index = 0U;
         selector_index < sizeof(selectors) / sizeof(selectors[0]);
         selector_index++) {
        if (strcmp(cursor, selectors[selector_index]) == 0) {
            if (!porpoise_copy_string(
                    section, section_capacity, selectors[selector_index])) {
                return false;
            }
            *is_data = strcmp(section, ".text") != 0 &&
                       strcmp(section, ".init") != 0;
            return true;
        }
    }
    if (strncmp(cursor, ".section", 8U) == 0 && isspace((unsigned char)cursor[8])) {
        cursor += 8;
        while (isspace((unsigned char)*cursor)) cursor++;
        if (*cursor == '"') {
            start = ++cursor;
            end = strchr(cursor, '"');
            if (end == NULL) return false;
            cursor = end + 1U;
        } else {
            start = cursor;
            while (*cursor != '\0' && *cursor != ',' &&
                   !isspace((unsigned char)*cursor)) {
                cursor++;
            }
            end = cursor;
        }
        length = (size_t)(end - start);
        if (length == 0U ||
            length + (start[0] == '.' ? 0U : 1U) >= section_capacity) {
            return false;
        }
        if (start[0] != '.') {
            section[0] = '.';
            memcpy(section + 1U, start, length);
            length++;
        } else {
            memcpy(section, start, length);
        }
        section[length] = '\0';
        while (isspace((unsigned char)*cursor)) cursor++;
        if (*cursor == ',') {
            const char *flags_end;
            const char *flag;
            cursor++;
            while (isspace((unsigned char)*cursor)) cursor++;
            flags_end = *cursor == '"' ? strchr(cursor + 1U, '"') : NULL;
            if (flags_end != NULL) {
                *is_data = true;
                for (flag = cursor + 1U; flag < flags_end; flag++) {
                    if (*flag == 'x') *is_data = false;
                }
                return true;
            }
        }
        *is_data = section_is_data(section) ||
                   strcmp(section, ".ctors") == 0 ||
                   strcmp(section, ".dtors") == 0 ||
                   strcmp(section, ".extab") == 0 ||
                   strcmp(section, ".extabindex") == 0;
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
    char current_section[PORPOISE_PATH_CAPACITY] = ".text";
    bool ok = true;
    bool in_block_comment = false;
    size_t block_comment_start_line = 0U;
    PorpoiseAsmDataParser data_parser;
    porpoise_asm_data_parser_init(&data_parser);
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
        if (current == NULL) {
            bool metadata_recognized = false;
            PorpoiseAsmDataLineResult metadata_result =
                porpoise_asm_data_parse_metadata(
                    &data_parser, file, line, line_number, diagnostics,
                    &metadata_recognized);
            if (metadata_recognized) {
                if (metadata_result == PORPOISE_ASM_DATA_INTERNAL_ERROR) {
                    ok = false;
                    break;
                }
                if (metadata_result == PORPOISE_ASM_DATA_ERROR) ok = false;
                continue;
            }
        }
        if (line[0] == '\0' || line[0] == '#') continue;
        if (current == NULL) {
            PorpoiseAsmDataLineResult data_result =
                porpoise_asm_data_parse_line(
                    &data_parser, file, line, line_number, diagnostics);
            if (data_result == PORPOISE_ASM_DATA_INTERNAL_ERROR) {
                ok = false;
                break;
            }
            if (data_result == PORPOISE_ASM_DATA_ERROR) {
                ok = false;
                continue;
            }
            if (data_result == PORPOISE_ASM_DATA_HANDLED) continue;
        }
        if (parse_symbol_alias(line, alias_name, sizeof(alias_name), &is_global)) {
            if (!pending_alias_list_add(
                    &pending_aliases, alias_name, is_global, line_number,
                    current_section)) {
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
        {
            char selected_section[PORPOISE_PATH_CAPACITY];
            bool selected_is_data = false;
            if (parse_section_directive(
                    line, selected_section, sizeof(selected_section),
                    &selected_is_data)) {
                if (pending_aliases.count != 0U &&
                    strcmp(current_section, selected_section) != 0) {
                    size_t pending_index;
                    for (pending_index = 0U;
                         pending_index < pending_aliases.count;
                         pending_index++) {
                        porpoise_diagnostics_add(
                            diagnostics, PORPOISE_SEVERITY_ERROR,
                            file->path,
                            pending_aliases.items[pending_index].source_line,
                            0U,
                            "symbol alias %s crosses section boundary from %s to %s before an annotated instruction",
                            pending_aliases.items[pending_index].name,
                            current_section, selected_section);
                    }
                    pending_alias_list_clear(&pending_aliases);
                    ok = false;
                }
                if (!porpoise_copy_string(
                        current_section, sizeof(current_section),
                        selected_section)) {
                    ok = false;
                    break;
                }
                in_data_section = selected_is_data;
                continue;
            }
        }
        if (parse_function_start(line, function_name, sizeof(function_name), &is_global)) {
            if (current != NULL) {
                porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, file->path,
                                         line_number, 0U, "nested .fn is not allowed");
                ok = false;
                continue;
            }
            current = file_add_function(
                file, function_name, is_global, current_section);
            if (current == NULL) {
                ok = false;
                break;
            }
            porpoise_asm_data_begin_function(&data_parser);
            have_previous_instruction = false;
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
            uint32_t gap_address = 0U;
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
            current->data_region =
                is_annotated_gap_data_name(current->name, &gap_address) &&
                gap_address == current->start_address;
            current->skipped = current->data_region;
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
            bool executable_data =
                porpoise_asm_data_accepts_annotated_words(&data_parser);
            if ((in_data_section || executable_data) &&
                parse_instruction(line, &address, &word, mnemonic,
                                  sizeof(mnemonic), operands,
                                  sizeof(operands)) &&
                (executable_data || mnemonic[0] == '.')) {
                PorpoiseDataWord *data_word = file_add_data_word(file);
                if (data_word == NULL) { ok = false; break; }
                data_word->source_line = line_number;
                data_word->address = address;
                data_word->word = word;
                data_word->directive = porpoise_strdup(
                    executable_data ? ".4byte" : mnemonic);
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
                                               current->item_count - 1U,
                                               file->path)) {
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
    if (!porpoise_asm_data_finish_file(
            &data_parser, file, line_number, diagnostics)) {
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
                label_entry->file = file;
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
    if (left_ref->function->data_region != right_ref->function->data_region) {
        return left_ref->function->data_region ? 1 : -1;
    }
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
            !porpoise_relocation_operands_equal(
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
    const char *section,
    bool is_global,
    bool is_function_name,
    size_t source_line,
    const char *source_path,
    uint32_t address,
    size_t instruction_item_index) {
    size_t index;
    char *name_copy;
    char *c_name_copy;
    char *section_copy;
    PorpoiseAddressAlias *alias;

    for (index = 0U; index < function->alias_count; index++) {
        alias = &function->aliases[index];
        if (strcmp(alias->name, name) == 0 &&
            strcmp(alias->c_name, c_name) == 0 &&
            strcmp(alias->section, section) == 0 &&
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
    section_copy = porpoise_strdup(section);
    if (name_copy == NULL || c_name_copy == NULL || section_copy == NULL) {
        free(name_copy);
        free(c_name_copy);
        free(section_copy);
        return false;
    }
    if (function->alias_count == SIZE_MAX ||
        !porpoise_grow_array(
            (void **)&function->aliases, &function->alias_capacity,
            sizeof(*function->aliases), function->alias_count + 1U)) {
        free(name_copy);
        free(c_name_copy);
        free(section_copy);
        return false;
    }
    alias = &function->aliases[function->alias_count++];
    alias->name = name_copy;
    alias->c_name = c_name_copy;
    alias->section = section_copy;
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

    /* A real function declaration takes precedence over a redundant gap name. */
    canonical->data_region =
        canonical->data_region && duplicate->data_region;
    canonical->skipped = canonical->data_region;

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
                   duplicate->section,
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
                canonical, alias->name, alias->c_name, alias->section,
                alias->is_global,
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

static const PorpoiseSourceFile *program_file_for_path(
    const PorpoiseProgram *program,
    const char *path) {
    size_t file_index;
    if (path == NULL) return NULL;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        if (strcmp(program->files[file_index].path, path) == 0) {
            return &program->files[file_index];
        }
    }
    return NULL;
}

static bool make_local_c_name(
    const char *name,
    const PorpoiseSourceFile *file,
    const char *section,
    uint32_t address,
    char *destination,
    size_t capacity) {
    char base[PORPOISE_NAME_CAPACITY];
    char unit[PORPOISE_NAME_CAPACITY];
    char section_name[PORPOISE_NAME_CAPACITY];
    char suffix[96];
    int required;
    size_t base_length;
    size_t suffix_length;

    if (name == NULL || file == NULL || section == NULL ||
        destination == NULL || capacity == 0U) {
        return false;
    }
    porpoise_sanitize_identifier(name, base, sizeof(base));
    porpoise_sanitize_identifier(file->output_stem, unit, sizeof(unit));
    porpoise_sanitize_identifier(section, section_name, sizeof(section_name));
    required = snprintf(
        destination, capacity, "%s__%s_%s_%08lX", base, unit,
        section_name, (unsigned long)address);
    if (required >= 0 && (size_t)required < capacity) return true;

    required = snprintf(
        suffix, sizeof(suffix), "__tu%016llX_s%016llX_%08lX",
        (unsigned long long)hash_identifier(file->relative_path),
        (unsigned long long)hash_identifier(section),
        (unsigned long)address);
    if (required < 0 || (size_t)required >= sizeof(suffix)) return false;
    suffix_length = (size_t)required;
    if (suffix_length + 1U >= capacity) return false;
    base_length = strlen(base);
    if (base_length > capacity - suffix_length - 1U) {
        base_length = capacity - suffix_length - 1U;
    }
    memcpy(destination, base, base_length);
    memcpy(destination + base_length, suffix, suffix_length + 1U);
    return true;
}

static bool namespace_local_symbols(PorpoiseProgram *program) {
    size_t file_index;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        PorpoiseSourceFile *file = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            PorpoiseFunction *function = &file->functions[function_index];
            size_t alias_index;
            if (!function->is_global) {
                char c_name[PORPOISE_NAME_CAPACITY];
                char *replacement;
                if (!make_local_c_name(
                        function->name, file, function->section,
                        function->start_address, c_name, sizeof(c_name))) {
                    return false;
                }
                replacement = porpoise_strdup(c_name);
                if (replacement == NULL) return false;
                free(function->c_name);
                function->c_name = replacement;
            }
            for (alias_index = 0U;
                 alias_index < function->alias_count;
                 alias_index++) {
                PorpoiseAddressAlias *alias = &function->aliases[alias_index];
                const PorpoiseSourceFile *alias_file;
                char c_name[PORPOISE_NAME_CAPACITY];
                char *replacement;
                if (alias->is_global) continue;
                alias_file = program_file_for_path(program, alias->source_path);
                if (alias_file == NULL) alias_file = file;
                if (!make_local_c_name(
                        alias->name, alias_file, alias->section,
                        alias->address, c_name, sizeof(c_name))) {
                    return false;
                }
                replacement = porpoise_strdup(c_name);
                if (replacement == NULL) return false;
                free(alias->c_name);
                alias->c_name = replacement;
            }
        }
    }
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

static const char *name_validation_source_key(
    const PorpoiseNameValidationRef *reference,
    char *buffer,
    size_t capacity) {
    porpoise_sanitize_identifier(reference->name, buffer, capacity);
    return buffer;
}

static int compare_source_name_validation_refs(
    const void *left,
    const void *right) {
    const PorpoiseNameValidationRef *left_ref =
        (const PorpoiseNameValidationRef *)left;
    const PorpoiseNameValidationRef *right_ref =
        (const PorpoiseNameValidationRef *)right;
    char left_buffer[PORPOISE_NAME_CAPACITY];
    char right_buffer[PORPOISE_NAME_CAPACITY];
    int comparison = strcmp(
        name_validation_source_key(
            left_ref, left_buffer, sizeof(left_buffer)),
        name_validation_source_key(
            right_ref, right_buffer, sizeof(right_buffer)));
    if (comparison != 0) return comparison;
    if (left_ref->order < right_ref->order) return -1;
    if (left_ref->order > right_ref->order) return 1;
    return 0;
}

static bool source_name_validation_keys_equal(
    const PorpoiseNameValidationRef *left,
    const PorpoiseNameValidationRef *right) {
    char left_buffer[PORPOISE_NAME_CAPACITY];
    char right_buffer[PORPOISE_NAME_CAPACITY];
    return strcmp(
        name_validation_source_key(left, left_buffer, sizeof(left_buffer)),
        name_validation_source_key(right, right_buffer, sizeof(right_buffer))) == 0;
}

static bool name_validation_is_global(
    const PorpoiseNameValidationRef *reference) {
    if (reference->kind == PORPOISE_VALIDATION_FUNCTION) {
        return reference->function->is_global;
    }
    if (reference->kind == PORPOISE_VALIDATION_ALIAS) {
        return reference->alias->is_global;
    }
    return false;
}

static const char *name_validation_section(
    const PorpoiseNameValidationRef *reference) {
    if (reference->kind == PORPOISE_VALIDATION_ALIAS) {
        return reference->alias->section;
    }
    return reference->function->section;
}

static const PorpoiseSourceFile *name_validation_declaring_file(
    const PorpoiseProgram *program,
    const PorpoiseNameValidationRef *reference) {
    const PorpoiseSourceFile *file;
    if (reference->kind != PORPOISE_VALIDATION_ALIAS) {
        return reference->file;
    }
    file = program_file_for_path(program, reference->alias->source_path);
    return file != NULL ? file : reference->file;
}

static bool name_validation_same_local_scope(
    const PorpoiseProgram *program,
    const PorpoiseNameValidationRef *left,
    const PorpoiseNameValidationRef *right) {
    const char *left_section = name_validation_section(left);
    const char *right_section = name_validation_section(right);
    return name_validation_declaring_file(program, left) ==
               name_validation_declaring_file(program, right) &&
           left_section != NULL && right_section != NULL &&
           strcmp(left_section, right_section) == 0;
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

    /*
     * Local declarations intentionally receive distinct generated C names.
     * Validate their assembly spellings separately, within the translation
     * unit and section where a bare reference can actually see them.
     */
    if (name_count > 1U) {
        qsort(name_refs, name_count, sizeof(*name_refs),
              compare_source_name_validation_refs);
    }
    for (name_cursor = 0U; name_cursor < name_count;) {
        size_t group_end = name_cursor + 1U;
        size_t left_index;
        while (group_end < name_count &&
               source_name_validation_keys_equal(
                   &name_refs[name_cursor], &name_refs[group_end])) {
            group_end++;
        }
        for (left_index = name_cursor;
             left_index < group_end;
             left_index++) {
            const PorpoiseNameValidationRef *left = &name_refs[left_index];
            size_t right_index;
            if (left->kind == PORPOISE_VALIDATION_LABEL) continue;
            for (right_index = name_cursor;
                 right_index < group_end;
                 right_index++) {
                const PorpoiseNameValidationRef *right =
                    &name_refs[right_index];
                const PorpoiseNameValidationRef *alias_ref;
                const PorpoiseNameValidationRef *other_ref;

                if (right_index == left_index) continue;
                if (right->kind != PORPOISE_VALIDATION_LABEL &&
                    right_index < left_index) continue;
                if (left->kind == PORPOISE_VALIDATION_LABEL ||
                    right->kind == PORPOISE_VALIDATION_LABEL) {
                    alias_ref = left->kind == PORPOISE_VALIDATION_ALIAS
                        ? left
                        : right->kind == PORPOISE_VALIDATION_ALIAS
                            ? right
                            : NULL;
                    other_ref = left->kind == PORPOISE_VALIDATION_LABEL
                        ? left
                        : right;
                    if (alias_ref == NULL ||
                        name_validation_is_global(alias_ref) ||
                        alias_ref->function != other_ref->function ||
                        !name_validation_same_local_scope(
                            program, alias_ref, other_ref)) {
                        continue;
                    }
                    porpoise_diagnostics_add(
                        diagnostics, PORPOISE_SEVERITY_ERROR,
                        name_validation_source_path(alias_ref),
                        alias_ref->alias->source_line,
                        alias_ref->alias->address,
                        "symbol alias %s conflicts with label %s in function %s",
                        alias_ref->alias->name, other_ref->label->label,
                        other_ref->function->name);
                    ok = false;
                    continue;
                }
                if (name_validation_is_global(left) &&
                    name_validation_is_global(right)) {
                    continue;
                }
                if (!name_validation_is_global(left) &&
                    !name_validation_is_global(right) &&
                    !name_validation_same_local_scope(program, left, right)) {
                    continue;
                }
                if (left->kind == PORPOISE_VALIDATION_FUNCTION &&
                    right->kind == PORPOISE_VALIDATION_FUNCTION) {
                    /*
                     * DTK may preserve multiple local functions with the
                     * same source spelling in one TU/section.  Their C names
                     * already include TU, section, and address.  Bare source
                     * lookups remain ambiguous in the scoped resolver; only
                     * the generated identities are accepted automatically.
                     */
                    if (!name_validation_is_global(left) &&
                        !name_validation_is_global(right)) {
                        continue;
                    }
                    porpoise_diagnostics_add(
                        diagnostics, PORPOISE_SEVERITY_ERROR,
                        right->file->path, 0U,
                        right->function->start_address,
                        "duplicate or colliding function symbol %s in section %s",
                        right->function->name,
                        name_validation_section(right));
                    ok = false;
                    continue;
                }
                alias_ref = left->kind == PORPOISE_VALIDATION_ALIAS
                    ? left
                    : right;
                other_ref = alias_ref == left ? right : left;
                if (other_ref->kind == PORPOISE_VALIDATION_FUNCTION) {
                    porpoise_diagnostics_add(
                        diagnostics, PORPOISE_SEVERITY_ERROR,
                        name_validation_source_path(alias_ref),
                        alias_ref->alias->source_line,
                        alias_ref->alias->address,
                        "symbol alias %s conflicts with function symbol %s in section %s",
                        alias_ref->alias->name, other_ref->function->name,
                        name_validation_section(alias_ref));
                } else {
                    porpoise_diagnostics_add(
                        diagnostics, PORPOISE_SEVERITY_ERROR,
                        name_validation_source_path(alias_ref),
                        alias_ref->alias->source_line,
                        alias_ref->alias->address,
                        "duplicate or colliding symbol alias %s in section %s",
                        alias_ref->alias->name,
                        name_validation_section(alias_ref));
                }
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
        program->label_index_count != 0U ||
        program->data_symbol_index_count != 0U ||
        program->data_span_count != 0U) {
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
    if (ok && !namespace_local_symbols(program)) ok = false;
    if (ok && !materialize_gap_data_regions(program)) ok = false;
    if (ok && !ensure_unique_symbols(program, diagnostics)) ok = false;
    if (ok && !program_build_lookup_indices(program)) ok = false;
    if (ok && !porpoise_asm_data_finalize(program, diagnostics)) ok = false;
    if (!ok) {
        if (io_failure) return PORPOISE_EXIT_IO;
        return porpoise_diagnostics_have_errors(diagnostics) ? PORPOISE_EXIT_TRANSLATION : PORPOISE_EXIT_INTERNAL;
    }
    return PORPOISE_EXIT_OK;
}

static bool legacy_sanitized_name_matches(
    const char *source_name,
    const char *lookup_name) {
    char sanitized[PORPOISE_NAME_CAPACITY];
    porpoise_sanitize_identifier(source_name, sanitized, sizeof(sanitized));
    return strcmp(sanitized, lookup_name) == 0;
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
    for (index = 0U; index < program->file_count; index++) {
        const PorpoiseSourceFile *file = &program->files[index];
        size_t function_index;
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            size_t alias_index;
            if (!function->skipped &&
                legacy_sanitized_name_matches(function->name, name)) {
                return function;
            }
            if (function->skipped) continue;
            for (alias_index = 0U;
                 alias_index < function->alias_count;
                 alias_index++) {
                const PorpoiseAddressAlias *alias =
                    &function->aliases[alias_index];
                if (alias->is_function_name &&
                    legacy_sanitized_name_matches(alias->name, name)) {
                    return function;
                }
            }
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
    for (index = 0U; index < program->file_count; index++) {
        const PorpoiseSourceFile *file = &program->files[index];
        size_t function_index;
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            size_t alias_index;
            if (function->skipped) continue;
            for (alias_index = 0U;
                 alias_index < function->alias_count;
                 alias_index++) {
                const PorpoiseAddressAlias *alias =
                    &function->aliases[alias_index];
                if (legacy_sanitized_name_matches(alias->name, name)) {
                    if (function_out != NULL) *function_out = function;
                    return alias;
                }
            }
        }
    }
    return NULL;
}

const PorpoiseAddressAlias *porpoise_program_find_declared_alias(
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

        if (entry->alias == NULL ||
            strcmp(entry->alias->name, name) != 0) {
            continue;
        }
        if (function_out != NULL) *function_out = entry->function;
        return entry->alias;
    }
    return NULL;
}

bool porpoise_program_resolve_declared_function(
    const PorpoiseProgram *program,
    const char *name,
    const PorpoiseFunction **function_out,
    const PorpoiseAddressAlias **alias_out,
    uint32_t *address_out) {
    size_t index;

    if (function_out != NULL) *function_out = NULL;
    if (alias_out != NULL) *alias_out = NULL;
    if (address_out != NULL) *address_out = 0U;
    if (program == NULL || name == NULL) return false;

    index = program_symbol_lower_bound(program, name);
    while (index < program->symbol_index_count &&
           strcmp(program->symbol_index[index].name, name) == 0) {
        const PorpoiseProgramSymbolIndexEntry *entry =
            &program->symbol_index[index++];
        const PorpoiseAddressAlias *alias = entry->alias;

        if (alias == NULL) {
            if (strcmp(entry->function->name, name) != 0) continue;
            if (function_out != NULL) *function_out = entry->function;
            if (address_out != NULL) {
                *address_out = entry->function->start_address;
            }
            return true;
        }
        if (!alias->is_function_name || strcmp(alias->name, name) != 0) {
            continue;
        }
        if (function_out != NULL) *function_out = entry->function;
        if (alias_out != NULL) *alias_out = alias;
        if (address_out != NULL) *address_out = alias->address;
        return true;
    }
    return false;
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

static bool symbol_spelling_matches(
    const char *name,
    const char *source_name,
    const char *c_name) {
    return strcmp(name, source_name) == 0 || strcmp(name, c_name) == 0 ||
           legacy_sanitized_name_matches(source_name, name);
}

static bool scoped_symbol_accept_candidate(
    const PorpoiseFunction *function,
    const PorpoiseAddressAlias *alias,
    const PorpoiseFunction **matched_function,
    const PorpoiseAddressAlias **matched_alias,
    uint32_t *matched_address,
    size_t *match_count) {
    uint32_t address = alias != NULL
        ? alias->address
        : function->start_address;
    if (*match_count != 0U) {
        if (*matched_function == function && *matched_alias == alias &&
            *matched_address == address) {
            return true;
        }
        (*match_count)++;
        return false;
    }
    *matched_function = function;
    *matched_alias = alias;
    *matched_address = address;
    *match_count = 1U;
    return true;
}

bool porpoise_program_resolve_symbol_scoped(
    const PorpoiseProgram *program,
    const PorpoiseSourceFile *scope_file,
    const PorpoiseFunction *scope_function,
    const char *scope_section,
    const char *name,
    const PorpoiseFunction **function_out,
    const PorpoiseAddressAlias **alias_out,
    uint32_t *address_out) {
    const PorpoiseFunction *matched_function = NULL;
    const PorpoiseAddressAlias *matched_alias = NULL;
    uint32_t matched_address = 0U;
    size_t match_count = 0U;
    size_t file_index;

    if (function_out != NULL) *function_out = NULL;
    if (alias_out != NULL) *alias_out = NULL;
    if (address_out != NULL) *address_out = 0U;
    if (program == NULL || name == NULL) return false;
    if (scope_section == NULL && scope_function != NULL) {
        scope_section = scope_function->section;
    }

    /* The currently lowered function is the innermost symbol scope. */
    if (scope_function != NULL && !scope_function->skipped) {
        size_t alias_index;
        if (symbol_spelling_matches(
                name, scope_function->name, scope_function->c_name)) {
            if (function_out != NULL) *function_out = scope_function;
            if (address_out != NULL) {
                *address_out = scope_function->start_address;
            }
            return true;
        }
        for (alias_index = 0U;
             alias_index < scope_function->alias_count;
             alias_index++) {
            const PorpoiseAddressAlias *alias =
                &scope_function->aliases[alias_index];
            const PorpoiseSourceFile *alias_file =
                program_file_for_path(program, alias->source_path);
            bool same_scope =
                (alias_file == NULL ? scope_file : alias_file) == scope_file &&
                scope_section != NULL && alias->section != NULL &&
                strcmp(alias->section, scope_section) == 0;
            if ((!alias->is_global && !same_scope) ||
                !symbol_spelling_matches(
                    name, alias->name, alias->c_name)) {
                continue;
            }
            if (function_out != NULL) *function_out = scope_function;
            if (alias_out != NULL) *alias_out = alias;
            if (address_out != NULL) *address_out = alias->address;
            return true;
        }
    }

    /* Next search local declarations from this translation unit and section. */
    if (scope_file != NULL && scope_section != NULL) {
        for (file_index = 0U; file_index < program->file_count; file_index++) {
            const PorpoiseSourceFile *file = &program->files[file_index];
            size_t function_index;
            for (function_index = 0U;
                 function_index < file->function_count;
                 function_index++) {
                const PorpoiseFunction *function =
                    &file->functions[function_index];
                size_t alias_index;
                if (!function->skipped && !function->is_global &&
                    file == scope_file && function->section != NULL &&
                    strcmp(function->section, scope_section) == 0 &&
                    symbol_spelling_matches(
                        name, function->name, function->c_name) &&
                    !scoped_symbol_accept_candidate(
                        function, NULL, &matched_function, &matched_alias,
                        &matched_address, &match_count)) {
                    return false;
                }
                if (function->skipped) continue;
                for (alias_index = 0U;
                     alias_index < function->alias_count;
                     alias_index++) {
                    const PorpoiseAddressAlias *alias =
                        &function->aliases[alias_index];
                    const PorpoiseSourceFile *alias_file;
                    if (alias->is_global || alias->section == NULL ||
                        strcmp(alias->section, scope_section) != 0 ||
                        !symbol_spelling_matches(
                            name, alias->name, alias->c_name)) {
                        continue;
                    }
                    alias_file = program_file_for_path(
                        program, alias->source_path);
                    if ((alias_file != NULL ? alias_file : file) != scope_file) {
                        continue;
                    }
                    if (!scoped_symbol_accept_candidate(
                            function, alias, &matched_function,
                            &matched_alias, &matched_address, &match_count)) {
                        return false;
                    }
                }
            }
        }
        if (match_count == 1U) {
            if (function_out != NULL) *function_out = matched_function;
            if (alias_out != NULL) *alias_out = matched_alias;
            if (address_out != NULL) *address_out = matched_address;
            return true;
        }
    }

    matched_function = NULL;
    matched_alias = NULL;
    matched_address = 0U;
    match_count = 0U;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            size_t alias_index;
            if (function->skipped) continue;
            if (function->is_global &&
                symbol_spelling_matches(
                    name, function->name, function->c_name) &&
                !scoped_symbol_accept_candidate(
                    function, NULL, &matched_function, &matched_alias,
                    &matched_address, &match_count)) {
                return false;
            }
            for (alias_index = 0U;
                 alias_index < function->alias_count;
                 alias_index++) {
                const PorpoiseAddressAlias *alias =
                    &function->aliases[alias_index];
                if (!alias->is_global ||
                    !symbol_spelling_matches(
                        name, alias->name, alias->c_name)) {
                    continue;
                }
                if (!scoped_symbol_accept_candidate(
                        function, alias, &matched_function, &matched_alias,
                        &matched_address, &match_count)) {
                    return false;
                }
            }
        }
    }
    if (match_count != 1U) return false;
    if (function_out != NULL) *function_out = matched_function;
    if (alias_out != NULL) *alias_out = matched_alias;
    if (address_out != NULL) *address_out = matched_address;
    return true;
}

bool porpoise_program_resolve_symbol(
    const PorpoiseProgram *program,
    const char *name,
    const PorpoiseFunction **function_out,
    const PorpoiseAddressAlias **alias_out,
    uint32_t *address_out) {
    const PorpoiseFunction *matched_function = NULL;
    const PorpoiseAddressAlias *matched_alias = NULL;
    uint32_t matched_address = 0U;
    size_t match_count = 0U;
    size_t file_index;
    if (function_out != NULL) *function_out = NULL;
    if (alias_out != NULL) *alias_out = NULL;
    if (address_out != NULL) *address_out = 0U;
    if (program == NULL || name == NULL) return false;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            const PorpoiseFunction *function =
                &file->functions[function_index];
            size_t alias_index;
            if (function->skipped) continue;
            if (symbol_spelling_matches(
                    name, function->name, function->c_name) &&
                !scoped_symbol_accept_candidate(
                    function, NULL, &matched_function, &matched_alias,
                    &matched_address, &match_count)) {
                return false;
            }
            for (alias_index = 0U;
                 alias_index < function->alias_count;
                 alias_index++) {
                const PorpoiseAddressAlias *alias =
                    &function->aliases[alias_index];
                if (!symbol_spelling_matches(
                        name, alias->name, alias->c_name)) {
                    continue;
                }
                if (!scoped_symbol_accept_candidate(
                        function, alias, &matched_function,
                        &matched_alias, &matched_address, &match_count)) {
                    return false;
                }
            }
        }
    }
    if (match_count != 1U) return false;
    if (function_out != NULL) *function_out = matched_function;
    if (alias_out != NULL) *alias_out = matched_alias;
    if (address_out != NULL) *address_out = matched_address;
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
