#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "porpoise/util.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PORPOISE_RUNTIME_DIR
#define PORPOISE_RUNTIME_DIR "runtime"
#endif
#ifndef PORPOISE_INSTALLED_RUNTIME_DIR
#define PORPOISE_INSTALLED_RUNTIME_DIR "runtime"
#endif

#ifdef _WIN32
#include <direct.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define PORPOISE_MKDIR(path) _mkdir(path)
#define PORPOISE_RMDIR(path) _rmdir(path)
#else
#include <unistd.h>
#define PORPOISE_MKDIR(path) mkdir((path), 0755)
#define PORPOISE_RMDIR(path) rmdir(path)
#endif

static bool porpoise_is_separator(char value) {
    return value == '/' || value == '\\';
}

const char *porpoise_default_runtime_directory(void) {
    const char *candidates[] = {
        PORPOISE_RUNTIME_DIR,
        PORPOISE_INSTALLED_RUNTIME_DIR
    };
    char probe[PORPOISE_PATH_CAPACITY];
    size_t index;
    for (index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); index++) {
        if (porpoise_path_join(probe, sizeof(probe), candidates[index],
                               "include/porpoise_lifted.h") &&
            porpoise_path_exists(probe)) {
            return candidates[index];
        }
    }
    return PORPOISE_RUNTIME_DIR;
}

char *porpoise_strdup(const char *value) {
    size_t length;
    char *copy;
    if (value == NULL) {
        return NULL;
    }
    length = strlen(value) + 1U;
    copy = (char *)malloc(length);
    if (copy != NULL) {
        memcpy(copy, value, length);
    }
    return copy;
}

bool porpoise_grow_array(void **items, size_t *capacity, size_t item_size, size_t minimum) {
    size_t next;
    void *replacement;
    if (items == NULL || capacity == NULL || item_size == 0U) {
        return false;
    }
    if (*capacity >= minimum) {
        return true;
    }
    next = *capacity == 0U ? 8U : *capacity;
    while (next < minimum) {
        if (next > SIZE_MAX / 2U) {
            return false;
        }
        next *= 2U;
    }
    if (next > SIZE_MAX / item_size) {
        return false;
    }
    replacement = realloc(*items, next * item_size);
    if (replacement == NULL) {
        return false;
    }
    *items = replacement;
    *capacity = next;
    return true;
}

void porpoise_trim(char *text) {
    char *start;
    size_t length;
    if (text == NULL) {
        return;
    }
    start = text;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1U);
    }
    length = strlen(text);
    while (length > 0U && isspace((unsigned char)text[length - 1U])) {
        text[--length] = '\0';
    }
}

bool porpoise_copy_string(char *destination, size_t capacity, const char *source) {
    size_t length;
    if (destination == NULL || capacity == 0U || source == NULL) {
        return false;
    }
    length = strlen(source);
    if (length >= capacity) {
        destination[0] = '\0';
        return false;
    }
    memcpy(destination, source, length + 1U);
    return true;
}

bool porpoise_format(char *destination, size_t capacity, const char *format, ...) {
    int written;
    va_list arguments;
    if (destination == NULL || capacity == 0U || format == NULL) {
        return false;
    }
    va_start(arguments, format);
    written = vsnprintf(destination, capacity, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= capacity) {
        destination[0] = '\0';
        return false;
    }
    return true;
}

bool porpoise_path_join(char *destination, size_t capacity, const char *left, const char *right) {
    size_t left_length;
    while (right != NULL && porpoise_is_separator(*right)) {
        right++;
    }
    if (left == NULL || right == NULL) {
        return false;
    }
    left_length = strlen(left);
    if (left_length == 0U) {
        return porpoise_copy_string(destination, capacity, right);
    }
    if (porpoise_is_separator(left[left_length - 1U])) {
        return porpoise_format(destination, capacity, "%s%s", left, right);
    }
    return porpoise_format(destination, capacity, "%s/%s", left, right);
}

bool porpoise_path_parent(char *destination, size_t capacity, const char *path) {
    char *slash;
    if (!porpoise_copy_string(destination, capacity, path)) {
        return false;
    }
    slash = strrchr(destination, '/');
    {
        char *backslash = strrchr(destination, '\\');
        if (backslash != NULL && (slash == NULL || backslash > slash)) {
            slash = backslash;
        }
    }
    if (slash == NULL) {
        return porpoise_copy_string(destination, capacity, ".");
    }
    if (slash == destination) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    return true;
}

bool porpoise_path_basename(char *destination, size_t capacity, const char *path) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *base = path;
    if (slash != NULL) {
        base = slash + 1;
    }
    if (backslash != NULL && backslash + 1 > base) {
        base = backslash + 1;
    }
    return porpoise_copy_string(destination, capacity, base);
}

bool porpoise_path_without_extension(char *destination, size_t capacity, const char *path) {
    char *dot;
    if (!porpoise_path_basename(destination, capacity, path)) {
        return false;
    }
    dot = strrchr(destination, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    return true;
}

bool porpoise_path_is_absolute(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }
#ifdef _WIN32
    return (isalpha((unsigned char)path[0]) && path[1] == ':') ||
           (porpoise_is_separator(path[0]) && porpoise_is_separator(path[1]));
#else
    return path[0] == '/';
#endif
}

bool porpoise_path_exists(const char *path) {
    struct stat status;
    return path != NULL && stat(path, &status) == 0;
}

bool porpoise_path_is_directory(const char *path) {
    struct stat status;
    return path != NULL && stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

#ifndef _WIN32
static bool porpoise_normalize_missing_posix_path(
    char *destination,
    size_t capacity,
    const char *path) {
    char absolute[PORPOISE_PATH_CAPACITY];
    const char *cursor;
    size_t output_length = 1U;
    if (path[0] == '/') {
        if (!porpoise_copy_string(absolute, sizeof(absolute), path)) return false;
    } else {
        char working_directory[PORPOISE_PATH_CAPACITY];
        if (getcwd(working_directory, sizeof(working_directory)) == NULL ||
            !porpoise_path_join(absolute, sizeof(absolute), working_directory, path)) return false;
    }
    if (capacity < 2U) return false;
    destination[0] = '/';
    destination[1] = '\0';
    cursor = absolute;
    while (*cursor != '\0') {
        const char *start;
        size_t segment_length;
        while (*cursor == '/') cursor++;
        if (*cursor == '\0') break;
        start = cursor;
        while (*cursor != '\0' && *cursor != '/') cursor++;
        segment_length = (size_t)(cursor - start);
        if (segment_length == 1U && start[0] == '.') continue;
        if (segment_length == 2U && start[0] == '.' && start[1] == '.') {
            if (output_length > 1U) {
                if (destination[output_length - 1U] == '/') output_length--;
                while (output_length > 1U && destination[output_length - 1U] != '/') output_length--;
                if (output_length > 1U && destination[output_length - 1U] == '/') output_length--;
                destination[output_length] = '\0';
            }
            continue;
        }
        if (output_length > 1U) {
            if (output_length + 1U >= capacity) return false;
            destination[output_length++] = '/';
        }
        if (segment_length >= capacity - output_length) return false;
        memcpy(destination + output_length, start, segment_length);
        output_length += segment_length;
        destination[output_length] = '\0';
    }
    return true;
}
#endif

#ifdef _WIN32
static bool porpoise_windows_final_path(
    char *destination,
    size_t capacity,
    const char *path) {
    DWORD attributes = GetFileAttributesA(path);
    HANDLE handle;
    DWORD length;
    if (attributes == INVALID_FILE_ATTRIBUTES) return false;
    handle = CreateFileA(
        path,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ? FILE_FLAG_BACKUP_SEMANTICS : 0U,
        NULL);
    if (handle == INVALID_HANDLE_VALUE) return false;
    length = GetFinalPathNameByHandleA(handle, destination, (DWORD)capacity, FILE_NAME_NORMALIZED);
    CloseHandle(handle);
    if (length == 0U || (size_t)length >= capacity) return false;
    if (length >= 8U && strncmp(destination, "\\\\?\\UNC\\", 8U) == 0) {
        size_t remainder = (size_t)length - 8U;
        if (remainder + 3U > capacity) return false;
        memmove(destination + 2U, destination + 8U, remainder + 1U);
        destination[0] = '\\';
        destination[1] = '\\';
    } else if (length >= 4U && strncmp(destination, "\\\\?\\", 4U) == 0) {
        memmove(destination, destination + 4U, (size_t)length - 3U);
    }
    return true;
}
#endif

static bool porpoise_resolution_parent(
    char *destination,
    size_t capacity,
    const char *path) {
    if (!porpoise_path_parent(destination, capacity, path)) return false;
#ifdef _WIN32
    if (strlen(destination) == 2U && destination[1] == ':' &&
        isalpha((unsigned char)destination[0])) {
        if (capacity < 4U) return false;
        destination[2] = '\\';
        destination[3] = '\0';
    }
#endif
    return true;
}

static bool porpoise_resolve_existing_path(
    char *destination,
    size_t capacity,
    const char *path) {
#ifdef _WIN32
    return porpoise_windows_final_path(destination, capacity, path);
#else
    (void)capacity;
    return realpath(path, destination) != NULL;
#endif
}

static bool porpoise_absolute_normalized_path(
    char *destination,
    size_t capacity,
    const char *path) {
    char lexical[PORPOISE_PATH_CAPACITY];
    char ancestor[PORPOISE_PATH_CAPACITY];
    char parent[PORPOISE_PATH_CAPACITY];
    char resolved[PORPOISE_PATH_CAPACITY];
    char combined[PORPOISE_PATH_CAPACITY];
    const char *suffix;
    size_t ancestor_length;
    if (destination == NULL || capacity == 0U || path == NULL || path[0] == '\0') return false;
    if (!porpoise_path_normalize_lexical(
            lexical, sizeof(lexical), path) ||
        !porpoise_copy_string(ancestor, sizeof(ancestor), lexical)) {
        return false;
    }
    while (!porpoise_path_exists(ancestor)) {
        if (!porpoise_resolution_parent(
                parent, sizeof(parent), ancestor) ||
            strcmp(parent, ancestor) == 0 ||
            !porpoise_copy_string(ancestor, sizeof(ancestor), parent)) {
            return false;
        }
    }
    if (!porpoise_resolve_existing_path(
            resolved, sizeof(resolved), ancestor)) {
        return false;
    }
    ancestor_length = strlen(ancestor);
    suffix = lexical + ancestor_length;
    while (porpoise_is_separator(*suffix)) suffix++;
    if (*suffix == '\0') {
        return porpoise_path_normalize_lexical(
            destination, capacity, resolved);
    }
    if (!porpoise_path_join(
            combined, sizeof(combined), resolved, suffix)) {
        return false;
    }
    return porpoise_path_normalize_lexical(
        destination, capacity, combined);
}

bool porpoise_path_normalize_lexical(char *destination, size_t capacity, const char *path) {
    bool normalized;
    size_t length;
    if (destination == NULL || capacity == 0U || path == NULL || path[0] == '\0') return false;
#ifdef _WIN32
    normalized = _fullpath(destination, path, capacity) != NULL;
#else
    normalized = porpoise_normalize_missing_posix_path(destination, capacity, path);
#endif
    if (!normalized) return false;
    length = strlen(destination);
    while (length > 1U && porpoise_is_separator(destination[length - 1U])) {
#ifdef _WIN32
        if (length == 3U && destination[1] == ':') break;
#endif
        destination[--length] = '\0';
    }
    return true;
}

static bool porpoise_path_character_equal(char left, char right) {
#ifdef _WIN32
    if (porpoise_is_separator(left) && porpoise_is_separator(right)) return true;
    return tolower((unsigned char)left) == tolower((unsigned char)right);
#else
    return left == right;
#endif
}

static size_t porpoise_trimmed_path_length(const char *path) {
    size_t length = strlen(path);
    while (length > 1U && porpoise_is_separator(path[length - 1U])) {
#ifdef _WIN32
        if (length == 3U && path[1] == ':') break;
#endif
        length--;
    }
    return length;
}

static bool porpoise_path_contains(const char *parent, const char *child) {
    size_t parent_length = porpoise_trimmed_path_length(parent);
    size_t child_length = porpoise_trimmed_path_length(child);
    size_t index;
    if (parent_length > child_length) return false;
    for (index = 0U; index < parent_length; index++) {
        if (!porpoise_path_character_equal(parent[index], child[index])) return false;
    }
    if (parent_length == child_length) return true;
    return porpoise_is_separator(parent[parent_length - 1U]) ||
           porpoise_is_separator(child[parent_length]);
}

static bool porpoise_existing_paths_same_object(
    const char *left,
    const char *right) {
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION left_information;
    BY_HANDLE_FILE_INFORMATION right_information;
    DWORD left_attributes = GetFileAttributesA(left);
    DWORD right_attributes = GetFileAttributesA(right);
    HANDLE left_handle;
    HANDLE right_handle;
    bool same = false;
    if (left_attributes == INVALID_FILE_ATTRIBUTES ||
        right_attributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    left_handle = CreateFileA(
        left, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING,
        (left_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U
            ? FILE_FLAG_BACKUP_SEMANTICS : 0U,
        NULL);
    if (left_handle == INVALID_HANDLE_VALUE) return false;
    right_handle = CreateFileA(
        right, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING,
        (right_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U
            ? FILE_FLAG_BACKUP_SEMANTICS : 0U,
        NULL);
    if (right_handle != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(left_handle, &left_information) &&
        GetFileInformationByHandle(right_handle, &right_information)) {
        same = left_information.dwVolumeSerialNumber ==
                   right_information.dwVolumeSerialNumber &&
               left_information.nFileIndexHigh ==
                   right_information.nFileIndexHigh &&
               left_information.nFileIndexLow ==
                   right_information.nFileIndexLow;
    }
    if (right_handle != INVALID_HANDLE_VALUE) CloseHandle(right_handle);
    CloseHandle(left_handle);
    return same;
#else
    struct stat left_status;
    struct stat right_status;
    return stat(left, &left_status) == 0 &&
           stat(right, &right_status) == 0 &&
           left_status.st_dev == right_status.st_dev &&
           left_status.st_ino == right_status.st_ino;
#endif
}

bool porpoise_path_contains_path(const char *parent, const char *child, bool *contains) {
    char lexical_parent[PORPOISE_PATH_CAPACITY];
    char lexical_child[PORPOISE_PATH_CAPACITY];
    char normalized_parent[PORPOISE_PATH_CAPACITY];
    char normalized_child[PORPOISE_PATH_CAPACITY];
    if (contains == NULL) return false;
    *contains = false;
    if (!porpoise_path_normalize_lexical(
            lexical_parent, sizeof(lexical_parent), parent) ||
        !porpoise_path_normalize_lexical(
            lexical_child, sizeof(lexical_child), child) ||
        !porpoise_absolute_normalized_path(normalized_parent, sizeof(normalized_parent), parent) ||
        !porpoise_absolute_normalized_path(normalized_child, sizeof(normalized_child), child)) return false;
    *contains = porpoise_path_contains(lexical_parent, lexical_child) ||
                porpoise_path_contains(normalized_parent, normalized_child) ||
                porpoise_existing_paths_same_object(parent, child);
    return true;
}

bool porpoise_path_trees_overlap(const char *left, const char *right, bool *overlap) {
    char lexical_left[PORPOISE_PATH_CAPACITY];
    char lexical_right[PORPOISE_PATH_CAPACITY];
    char normalized_left[PORPOISE_PATH_CAPACITY];
    char normalized_right[PORPOISE_PATH_CAPACITY];
    if (overlap == NULL) return false;
    *overlap = false;
    if (!porpoise_path_normalize_lexical(
            lexical_left, sizeof(lexical_left), left) ||
        !porpoise_path_normalize_lexical(
            lexical_right, sizeof(lexical_right), right) ||
        !porpoise_absolute_normalized_path(normalized_left, sizeof(normalized_left), left) ||
        !porpoise_absolute_normalized_path(normalized_right, sizeof(normalized_right), right)) return false;
    *overlap = porpoise_path_contains(lexical_left, lexical_right) ||
               porpoise_path_contains(lexical_right, lexical_left) ||
               porpoise_path_contains(normalized_left, normalized_right) ||
               porpoise_path_contains(normalized_right, normalized_left) ||
               porpoise_existing_paths_same_object(left, right);
    return true;
}

bool porpoise_directory_is_empty(const char *path) {
    DIR *directory;
    const struct dirent *entry;
    directory = opendir(path);
    if (directory == NULL) {
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            closedir(directory);
            return false;
        }
    }
    closedir(directory);
    return true;
}

bool porpoise_make_directories(const char *path, PorpoiseDiagnostics *diagnostics) {
    char buffer[PORPOISE_PATH_CAPACITY];
    size_t index;
    size_t length;
    if (!porpoise_copy_string(buffer, sizeof(buffer), path)) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, 0U, 0U,
                                 "path exceeds the supported length");
        return false;
    }
    length = strlen(buffer);
    for (index = 1U; index <= length; index++) {
        if (index == length || porpoise_is_separator(buffer[index])) {
            char saved = buffer[index];
            buffer[index] = '\0';
            if (buffer[0] != '\0' && !(index == 2U && buffer[1] == ':') &&
                !porpoise_path_exists(buffer) && PORPOISE_MKDIR(buffer) != 0 && errno != EEXIST) {
                porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, buffer, 0U, 0U,
                                         "cannot create directory: %s", strerror(errno));
                return false;
            }
            buffer[index] = saved;
        }
    }
    return true;
}

bool porpoise_copy_file(const char *source, const char *destination, PorpoiseDiagnostics *diagnostics) {
    FILE *input = fopen(source, "rb");
    FILE *output;
    unsigned char buffer[16384];
    size_t count;
    if (input == NULL) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, source, 0U, 0U,
                                 "cannot open source file: %s", strerror(errno));
        return false;
    }
    output = fopen(destination, "wb");
    if (output == NULL) {
        fclose(input);
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, destination, 0U, 0U,
                                 "cannot create destination file: %s", strerror(errno));
        return false;
    }
    while ((count = fread(buffer, 1U, sizeof(buffer), input)) != 0U) {
        if (fwrite(buffer, 1U, count, output) != count) {
            fclose(input);
            fclose(output);
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, destination, 0U, 0U,
                                     "failed while writing destination file");
            return false;
        }
    }
    {
        bool failed = ferror(input) != 0;
        if (fclose(input) != 0) failed = true;
        if (fclose(output) != 0) failed = true;
        if (!failed) return true;
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, destination, 0U, 0U,
                                 "failed while copying file");
        return false;
    }
}

bool porpoise_remove_tree(const char *path, PorpoiseDiagnostics *diagnostics) {
    DIR *directory;
    const struct dirent *entry;
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) return true;
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, 0U, 0U,
                                 "cannot inspect path before removal");
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        int result = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ? PORPOISE_RMDIR(path) : remove(path);
        if (result != 0) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, 0U, 0U,
                                     "cannot remove filesystem link: %s", strerror(errno));
            return false;
        }
        return true;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
#else
    struct stat status;
    if (lstat(path, &status) != 0) {
        if (errno == ENOENT) return true;
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, 0U, 0U,
                                 "cannot inspect path before removal: %s", strerror(errno));
        return false;
    }
    if (!S_ISDIR(status.st_mode)) {
#endif
        if (remove(path) != 0) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, 0U, 0U,
                                     "cannot remove file: %s", strerror(errno));
            return false;
        }
        return true;
    }
    directory = opendir(path);
    if (directory == NULL) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, 0U, 0U,
                                 "cannot open directory for removal: %s", strerror(errno));
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        char child[PORPOISE_PATH_CAPACITY];
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!porpoise_path_join(child, sizeof(child), path, entry->d_name) ||
            !porpoise_remove_tree(child, diagnostics)) {
            closedir(directory);
            return false;
        }
    }
    closedir(directory);
    if (PORPOISE_RMDIR(path) != 0) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, path, 0U, 0U,
                                 "cannot remove directory: %s", strerror(errno));
        return false;
    }
    return true;
}

bool porpoise_move_path(const char *source, const char *destination, PorpoiseDiagnostics *diagnostics) {
    unsigned int attempt;
    for (attempt = 0U; attempt < 21U; attempt++) {
        if (rename(source, destination) == 0) return true;
#ifdef _WIN32
        if ((errno == EACCES || errno == EBUSY) && attempt < 20U) {
            Sleep(25U);
            continue;
        }
#endif
        break;
    }
    {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, source, 0U, 0U,
                                 "cannot move to %s: %s", destination, strerror(errno));
        return false;
    }
}

bool porpoise_write_all(FILE *file, const char *text) {
    return file != NULL && text != NULL && fputs(text, file) >= 0;
}

void porpoise_sanitize_identifier(const char *input, char *output, size_t capacity) {
    size_t source = 0U;
    size_t destination = 0U;
    if (capacity == 0U) {
        return;
    }
    if (input == NULL || input[0] == '\0') {
        porpoise_copy_string(output, capacity, "unnamed");
        return;
    }
    if (isdigit((unsigned char)input[0]) && destination + 1U < capacity) {
        output[destination++] = '_';
    }
    while (input[source] != '\0' && destination + 1U < capacity) {
        unsigned char value = (unsigned char)input[source++];
        output[destination++] = (isalnum(value) || value == '_') ? (char)value : '_';
    }
    output[destination] = '\0';
}

void porpoise_json_write_string(FILE *file, const char *value) {
    const unsigned char *cursor = (const unsigned char *)(value == NULL ? "" : value);
    fputc('"', file);
    while (*cursor != '\0') {
        switch (*cursor) {
        case '"': fputs("\\\"", file); break;
        case '\\': fputs("\\\\", file); break;
        case '\b': fputs("\\b", file); break;
        case '\f': fputs("\\f", file); break;
        case '\n': fputs("\\n", file); break;
        case '\r': fputs("\\r", file); break;
        case '\t': fputs("\\t", file); break;
        default:
            if (*cursor < 0x20U) {
                fprintf(file, "\\u%04x", (unsigned int)*cursor);
            } else {
                fputc((int)*cursor, file);
            }
            break;
        }
        cursor++;
    }
    fputc('"', file);
}

void porpoise_diagnostics_init(PorpoiseDiagnostics *diagnostics) {
    memset(diagnostics, 0, sizeof(*diagnostics));
}

void porpoise_diagnostics_free(PorpoiseDiagnostics *diagnostics) {
    size_t index;
    if (diagnostics == NULL) {
        return;
    }
    for (index = 0U; index < diagnostics->count; index++) {
        free(diagnostics->items[index].file);
        free(diagnostics->items[index].message);
    }
    free(diagnostics->items);
    memset(diagnostics, 0, sizeof(*diagnostics));
}

bool porpoise_diagnostics_add(
    PorpoiseDiagnostics *diagnostics,
    PorpoiseSeverity severity,
    const char *file,
    size_t line,
    uint32_t address,
    const char *format,
    ...) {
    char message[PORPOISE_MESSAGE_CAPACITY];
    int written;
    va_list arguments;
    PorpoiseDiagnostic *item;
    char *file_copy;
    char *message_copy;
    if (diagnostics == NULL || format == NULL) {
        return false;
    }
    va_start(arguments, format);
    written = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (written < 0) {
        return false;
    }
    if (!porpoise_grow_array((void **)&diagnostics->items, &diagnostics->capacity,
                             sizeof(*diagnostics->items), diagnostics->count + 1U)) {
        return false;
    }
    file_copy = porpoise_strdup(file == NULL ? "" : file);
    message_copy = porpoise_strdup(message);
    if (file_copy == NULL || message_copy == NULL) {
        free(file_copy);
        free(message_copy);
        return false;
    }
    item = &diagnostics->items[diagnostics->count];
    item->severity = severity;
    item->file = file_copy;
    item->line = line;
    item->address = address;
    item->message = message_copy;
    diagnostics->count++;
    return true;
}

bool porpoise_diagnostics_have_errors(const PorpoiseDiagnostics *diagnostics) {
    size_t index;
    if (diagnostics == NULL) {
        return false;
    }
    for (index = 0U; index < diagnostics->count; index++) {
        if (diagnostics->items[index].severity == PORPOISE_SEVERITY_ERROR) {
            return true;
        }
    }
    return false;
}
