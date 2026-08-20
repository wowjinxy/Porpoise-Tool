#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "porpoise/dtk_import.h"

#include "porpoise/program.h"
#include "porpoise/util.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define DTK_MKDIR(path) _mkdir(path)
#define DTK_RMDIR(path) _rmdir(path)
#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define DTK_MKDIR(path) mkdir((path), 0755)
#define DTK_RMDIR(path) rmdir(path)
#endif

#define DTK_CAPTURE_INITIAL_CAPACITY 4096U
#define DTK_TREE_MAX_DEPTH 64U
#define DTK_METADATA_LINE_CAPACITY 512U

typedef struct DtkTreeEntry {
    char *relative_path;
    char *case_key;
    char *full_path;
    bool directory;
} DtkTreeEntry;

typedef struct DtkTree {
    DtkTreeEntry *entries;
    size_t count;
    size_t capacity;
} DtkTree;

typedef struct DtkValidation {
    PorpoiseDtkImportMetadata metadata;
    DtkTree tree;
} DtkValidation;

static int dtk_diagnostic(
    PorpoiseDiagnostics *diagnostics,
    PorpoiseSeverity severity,
    int result,
    const char *path,
    const char *format,
    ...) {
    char message[PORPOISE_MESSAGE_CAPACITY];
    va_list arguments;
    int written;

    va_start(arguments, format);
    written = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (written < 0) return PORPOISE_EXIT_INTERNAL;
    if (diagnostics != NULL &&
        !porpoise_diagnostics_add(
            diagnostics, severity, path == NULL ? "" : path,
            0U, 0U, "%s", message)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    return result;
}

void porpoise_dtk_process_result_init(PorpoiseDtkProcessResult *result) {
    if (result != NULL) memset(result, 0, sizeof(*result));
}

void porpoise_dtk_process_result_free(PorpoiseDtkProcessResult *result) {
    if (result == NULL) return;
    free(result->standard_output);
    free(result->standard_error);
    memset(result, 0, sizeof(*result));
}

void porpoise_dtk_import_options_init(PorpoiseDtkImportOptions *options) {
    if (options == NULL) return;
    memset(options, 0, sizeof(*options));
    options->source_kind = PORPOISE_DTK_SOURCE_MANAGED_ELF;
    options->settings_identity = "";
    options->minimum_dtk_major = 1U;
    options->minimum_dtk_minor = 8U;
    options->minimum_dtk_patch = 0U;
    options->allow_cache_reuse = true;
    options->prepared_require_link_order = true;
}

void porpoise_dtk_import_result_init(PorpoiseDtkImportResult *result) {
    if (result != NULL) memset(result, 0, sizeof(*result));
}

static bool dtk_capture_stream(FILE *stream, char **text_out) {
    char *text = NULL;
    size_t length = 0U;
    size_t capacity = 0U;

    if (fflush(stream) != 0 || fseek(stream, 0L, SEEK_SET) != 0) return false;
    for (;;) {
        size_t available;
        size_t count;
        if (capacity - length < 2048U) {
            size_t next = capacity == 0U ?
                DTK_CAPTURE_INITIAL_CAPACITY : capacity * 2U;
            char *replacement;
            if (next <= capacity) {
                free(text);
                return false;
            }
            replacement = (char *)realloc(text, next);
            if (replacement == NULL) {
                free(text);
                return false;
            }
            text = replacement;
            capacity = next;
        }
        available = capacity - length - 1U;
        count = fread(text + length, 1U, available, stream);
        length += count;
        if (count < available) {
            if (ferror(stream)) {
                free(text);
                return false;
            }
            break;
        }
    }
    text[length] = '\0';
    *text_out = text;
    return true;
}

#ifdef _WIN32
typedef struct DtkCommandLine {
    char *text;
    size_t length;
    size_t capacity;
} DtkCommandLine;

static bool dtk_command_append(
    DtkCommandLine *line,
    const char *text,
    size_t length) {
    if (!porpoise_grow_array(
            (void **)&line->text, &line->capacity, sizeof(char),
            line->length + length + 1U)) {
        return false;
    }
    memcpy(line->text + line->length, text, length);
    line->length += length;
    line->text[line->length] = '\0';
    return true;
}

static bool dtk_command_append_character(
    DtkCommandLine *line,
    char character) {
    return dtk_command_append(line, &character, 1U);
}

static bool dtk_command_append_quoted(
    DtkCommandLine *line,
    const char *argument) {
    const char *cursor = argument;

    if (!dtk_command_append_character(line, '"')) return false;
    while (*cursor != '\0') {
        size_t slash_count = 0U;
        size_t index;
        while (cursor[slash_count] == '\\') slash_count++;
        cursor += slash_count;
        if (*cursor == '"' || *cursor == '\0') {
            for (index = 0U; index < slash_count * 2U; index++) {
                if (!dtk_command_append_character(line, '\\')) return false;
            }
            if (*cursor == '"') {
                if (!dtk_command_append(line, "\\\"", 2U)) return false;
                cursor++;
            }
        } else {
            for (index = 0U; index < slash_count; index++) {
                if (!dtk_command_append_character(line, '\\')) return false;
            }
            if (!dtk_command_append_character(line, *cursor++)) return false;
        }
    }
    return dtk_command_append_character(line, '"');
}

static bool dtk_build_command_line(
    const char *const *argv,
    char **command_line_out) {
    DtkCommandLine line;
    size_t index;

    memset(&line, 0, sizeof(line));
    for (index = 0U; argv[index] != NULL; index++) {
        if (index != 0U && !dtk_command_append_character(&line, ' ')) {
            free(line.text);
            return false;
        }
        if (!dtk_command_append_quoted(&line, argv[index])) {
            free(line.text);
            return false;
        }
    }
    if (line.text == NULL) return false;
    *command_line_out = line.text;
    return true;
}
#endif

int porpoise_dtk_run_process_default(
    void *user_data,
    const char *const *argv,
    const char *working_directory,
    const PorpoiseOperationCallbacks *operation,
    PorpoiseDtkProcessResult *result,
    PorpoiseDiagnostics *diagnostics) {
    FILE *standard_output;
    FILE *standard_error;
    int status = PORPOISE_EXIT_OK;
    bool cancelled = false;

    (void)user_data;
    if (argv == NULL || argv[0] == NULL || argv[0][0] == '\0' ||
        result == NULL) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_INTERNAL,
            "", "DTK process runner arguments are invalid");
    }
    porpoise_dtk_process_result_init(result);
    standard_output = tmpfile();
    standard_error = tmpfile();
    if (standard_output == NULL || standard_error == NULL) {
        if (standard_output != NULL) fclose(standard_output);
        if (standard_error != NULL) fclose(standard_error);
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            argv[0], "cannot create temporary DTK process capture files");
    }

#ifdef _WIN32
    {
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        HANDLE output_handle = (HANDLE)_get_osfhandle(_fileno(standard_output));
        HANDLE error_handle = (HANDLE)_get_osfhandle(_fileno(standard_error));
        char *command_line = NULL;
        BOOL created;
        DWORD child_exit = 0U;

        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = output_handle;
        startup.hStdError = error_handle;
        if (output_handle == INVALID_HANDLE_VALUE ||
            error_handle == INVALID_HANDLE_VALUE ||
            !SetHandleInformation(
                output_handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) ||
            !SetHandleInformation(
                error_handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) ||
            !dtk_build_command_line(argv, &command_line)) {
            free(command_line);
            fclose(standard_output);
            fclose(standard_error);
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_INTERNAL,
                argv[0], "cannot prepare shell-free DTK process invocation");
        }
        created = CreateProcessA(
            argv[0], command_line, NULL, NULL, TRUE, CREATE_NO_WINDOW,
            NULL, working_directory, &startup, &process);
        (void)SetHandleInformation(output_handle, HANDLE_FLAG_INHERIT, 0U);
        (void)SetHandleInformation(error_handle, HANDLE_FLAG_INHERIT, 0U);
        free(command_line);
        if (!created) {
            fclose(standard_output);
            fclose(standard_error);
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                argv[0], "cannot launch DTK process (Windows error %lu)",
                (unsigned long)GetLastError());
        }
        CloseHandle(process.hThread);
        for (;;) {
            DWORD wait_result = WaitForSingleObject(process.hProcess, 25U);
            if (wait_result == WAIT_OBJECT_0) break;
            if (wait_result == WAIT_FAILED) {
                status = PORPOISE_EXIT_IO;
                (void)TerminateProcess(process.hProcess, 1U);
                (void)WaitForSingleObject(process.hProcess, INFINITE);
                break;
            }
            if (porpoise_operation_cancelled(operation)) {
                cancelled = true;
                (void)TerminateProcess(process.hProcess, 1U);
                (void)WaitForSingleObject(process.hProcess, INFINITE);
                break;
            }
        }
        if (!GetExitCodeProcess(process.hProcess, &child_exit))
            status = PORPOISE_EXIT_IO;
        CloseHandle(process.hProcess);
        result->exit_code = child_exit > (DWORD)INT_MAX ? -1 : (int)child_exit;
    }
#else
    {
        pid_t child;
        int wait_status = 0;
        size_t argument_count = 0U;
        char **exec_arguments;

        while (argv[argument_count] != NULL) argument_count++;
        exec_arguments = (char **)calloc(
            argument_count + 1U, sizeof(*exec_arguments));
        if (exec_arguments == NULL) {
            fclose(standard_output);
            fclose(standard_error);
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_INTERNAL,
                argv[0], "out of memory while preparing DTK process");
        }
        {
            size_t index;
            for (index = 0U; index < argument_count; index++)
                exec_arguments[index] = (char *)argv[index];
        }
        child = fork();
        if (child == 0) {
            if (working_directory != NULL && working_directory[0] != '\0' &&
                chdir(working_directory) != 0) {
                _exit(126);
            }
            if (dup2(fileno(standard_output), STDOUT_FILENO) < 0 ||
                dup2(fileno(standard_error), STDERR_FILENO) < 0) {
                _exit(126);
            }
            execv(exec_arguments[0], exec_arguments);
            _exit(127);
        }
        free(exec_arguments);
        if (child < 0) {
            fclose(standard_output);
            fclose(standard_error);
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                argv[0], "cannot launch DTK process: %s", strerror(errno));
        }
        for (;;) {
            pid_t waited = waitpid(child, &wait_status, WNOHANG);
            if (waited == child) break;
            if (waited < 0) {
                status = PORPOISE_EXIT_IO;
                (void)kill(child, SIGTERM);
                (void)waitpid(child, &wait_status, 0);
                break;
            }
            if (porpoise_operation_cancelled(operation)) {
                cancelled = true;
                (void)kill(child, SIGTERM);
                (void)waitpid(child, &wait_status, 0);
                break;
            }
            {
                struct timespec delay;
                delay.tv_sec = 0;
                delay.tv_nsec = 20000000L;
                (void)nanosleep(&delay, NULL);
            }
        }
        if (WIFEXITED(wait_status)) result->exit_code = WEXITSTATUS(wait_status);
        else result->exit_code = -1;
    }
#endif

    if (!dtk_capture_stream(standard_output, &result->standard_output) ||
        !dtk_capture_stream(standard_error, &result->standard_error)) {
        status = PORPOISE_EXIT_INTERNAL;
    }
    {
        int output_close = fclose(standard_output);
        int error_close = fclose(standard_error);
        if (output_close != 0 || error_close != 0) {
            if (status == PORPOISE_EXIT_OK) status = PORPOISE_EXIT_IO;
        }
    }
    if (cancelled) return PORPOISE_EXIT_CANCELLED;
    if (status != PORPOISE_EXIT_OK) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, status, argv[0],
            "DTK process execution or output capture failed");
    }
    return PORPOISE_EXIT_OK;
}

static void dtk_tree_init(DtkTree *tree) {
    memset(tree, 0, sizeof(*tree));
}

static void dtk_tree_free(DtkTree *tree) {
    size_t index;
    if (tree == NULL) return;
    for (index = 0U; index < tree->count; index++) {
        free(tree->entries[index].relative_path);
        free(tree->entries[index].case_key);
        free(tree->entries[index].full_path);
    }
    free(tree->entries);
    memset(tree, 0, sizeof(*tree));
}

static bool dtk_ascii_equal_ignore_case(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        unsigned char a = (unsigned char)*left++;
        unsigned char b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return *left == *right;
}

static bool dtk_windows_reserved_component(
    const char *component,
    size_t length) {
    char base[16];
    size_t base_length = 0U;
    size_t index;

    while (base_length < length && component[base_length] != '.') {
        if (base_length + 1U >= sizeof(base)) return false;
        base[base_length] = component[base_length];
        base_length++;
    }
    base[base_length] = '\0';
    for (index = 0U; index < base_length; index++) {
        if (base[index] >= 'A' && base[index] <= 'Z')
            base[index] = (char)(base[index] - 'A' + 'a');
    }
    if (strcmp(base, "con") == 0 || strcmp(base, "prn") == 0 ||
        strcmp(base, "aux") == 0 || strcmp(base, "nul") == 0) {
        return true;
    }
    return base_length == 4U &&
           ((memcmp(base, "com", 3U) == 0) ||
            (memcmp(base, "lpt", 3U) == 0)) &&
           base[3] >= '1' && base[3] <= '9';
}

static bool dtk_safe_component(const char *component, size_t length) {
    size_t index;
    if (length == 0U ||
        (length == 1U && component[0] == '.') ||
        (length == 2U && component[0] == '.' && component[1] == '.') ||
        component[length - 1U] == '.' || component[length - 1U] == ' ') {
        return false;
    }
    for (index = 0U; index < length; index++) {
        unsigned char value = (unsigned char)component[index];
        if (value < 0x20U || value >= 0x7fU || value == '/' ||
            value == '\\' || value == ':' || value == '"' ||
            value == '<' || value == '>' || value == '|' ||
            value == '*' || value == '?') {
            return false;
        }
    }
    return !dtk_windows_reserved_component(component, length);
}

static bool dtk_normalize_safe_relative(
    const char *input,
    char *output,
    size_t capacity) {
    const char *cursor;
    size_t output_length = 0U;

    if (input == NULL || input[0] == '\0' || capacity == 0U ||
        input[0] == '/' || input[0] == '\\' ||
        porpoise_path_is_absolute(input)) {
        return false;
    }
    cursor = input;
    while (*cursor != '\0') {
        const char *component = cursor;
        size_t length = 0U;
        bool had_separator;
        while (cursor[length] != '\0' &&
               cursor[length] != '/' && cursor[length] != '\\') {
            length++;
        }
        if (!dtk_safe_component(component, length)) return false;
        if (output_length != 0U) {
            if (output_length + 1U >= capacity) return false;
            output[output_length++] = '/';
        }
        if (length >= capacity - output_length) return false;
        memcpy(output + output_length, component, length);
        output_length += length;
        cursor += length;
        had_separator = *cursor == '/' || *cursor == '\\';
        if (had_separator) cursor++;
        if (had_separator && *cursor == '\0') return false;
    }
    output[output_length] = '\0';
    return output_length != 0U;
}

static char *dtk_case_key(const char *relative_path) {
    size_t length = strlen(relative_path);
    char *key = (char *)malloc(length + 1U);
    size_t index;
    if (key == NULL) return NULL;
    for (index = 0U; index < length; index++) {
        unsigned char value = (unsigned char)relative_path[index];
        if (value >= 'A' && value <= 'Z') value = (unsigned char)(value - 'A' + 'a');
        key[index] = value == '\\' ? '/' : (char)value;
    }
    key[length] = '\0';
    return key;
}

static int dtk_tree_add(
    DtkTree *tree,
    const char *relative_path,
    const char *full_path,
    bool directory,
    PorpoiseDiagnostics *diagnostics) {
    DtkTreeEntry entry;
    size_t index;

    memset(&entry, 0, sizeof(entry));
    entry.relative_path = porpoise_strdup(relative_path);
    entry.case_key = dtk_case_key(relative_path);
    entry.full_path = porpoise_strdup(full_path);
    entry.directory = directory;
    if (entry.relative_path == NULL || entry.case_key == NULL ||
        entry.full_path == NULL) {
        free(entry.relative_path);
        free(entry.case_key);
        free(entry.full_path);
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_INTERNAL,
            full_path, "out of memory while indexing DTK output");
    }
    for (index = 0U; index < tree->count; index++) {
        if (strcmp(tree->entries[index].case_key, entry.case_key) == 0) {
            int result = dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                full_path,
                "DTK paths '%s' and '%s' collide under Windows case folding",
                tree->entries[index].relative_path, relative_path);
            free(entry.relative_path);
            free(entry.case_key);
            free(entry.full_path);
            return result;
        }
    }
    if (!porpoise_grow_array(
            (void **)&tree->entries, &tree->capacity,
            sizeof(*tree->entries), tree->count + 1U)) {
        free(entry.relative_path);
        free(entry.case_key);
        free(entry.full_path);
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_INTERNAL,
            full_path, "out of memory while indexing DTK output");
    }
    tree->entries[tree->count++] = entry;
    return PORPOISE_EXIT_OK;
}

static int dtk_inspect_path(
    const char *path,
    bool *directory_out,
    PorpoiseDiagnostics *diagnostics) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot inspect DTK output path");
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            path, "DTK output must not contain filesystem links or reparse points");
    }
    *directory_out = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
    return PORPOISE_EXIT_OK;
#else
    struct stat status;
    if (lstat(path, &status) != 0) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot inspect DTK output path: %s", strerror(errno));
    }
    if (S_ISLNK(status.st_mode)) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            path, "DTK output must not contain filesystem links");
    }
    if (!S_ISDIR(status.st_mode) && !S_ISREG(status.st_mode)) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            path, "DTK output contains a non-regular filesystem object");
    }
    *directory_out = S_ISDIR(status.st_mode);
    return PORPOISE_EXIT_OK;
#endif
}

static int dtk_collect_directory(
    const char *root,
    const char *relative_directory,
    unsigned int depth,
    DtkTree *tree,
    const PorpoiseOperationCallbacks *operation,
    PorpoiseDiagnostics *diagnostics) {
    char directory_path[PORPOISE_PATH_CAPACITY];
    DIR *directory;
    const struct dirent *item;

    if (depth > DTK_TREE_MAX_DEPTH) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            root, "DTK output directory is nested too deeply");
    }
    if (porpoise_operation_cancelled(operation)) return PORPOISE_EXIT_CANCELLED;
    if (relative_directory[0] == '\0') {
        if (!porpoise_copy_string(directory_path, sizeof(directory_path), root))
            return PORPOISE_EXIT_INTERNAL;
    } else if (!porpoise_path_join(
                   directory_path, sizeof(directory_path),
                   root, relative_directory)) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            root, "DTK output path exceeds the supported length");
    }
    directory = opendir(directory_path);
    if (directory == NULL) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            directory_path, "cannot open DTK output directory: %s",
            strerror(errno));
    }
    while ((item = readdir(directory)) != NULL) {
        char relative[PORPOISE_PATH_CAPACITY];
        char full_path[PORPOISE_PATH_CAPACITY];
        bool is_directory = false;
        int result;
        size_t name_length;

        if (porpoise_operation_cancelled(operation)) {
            closedir(directory);
            return PORPOISE_EXIT_CANCELLED;
        }

        if (strcmp(item->d_name, ".") == 0 ||
            strcmp(item->d_name, "..") == 0) {
            continue;
        }
        name_length = strlen(item->d_name);
        if (!dtk_safe_component(item->d_name, name_length)) {
            closedir(directory);
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                directory_path, "DTK output contains unsafe path component '%s'",
                item->d_name);
        }
        if (relative_directory[0] == '\0') {
            if (!porpoise_copy_string(relative, sizeof(relative), item->d_name)) {
                closedir(directory);
                return PORPOISE_EXIT_INTERNAL;
            }
        } else if (!porpoise_format(
                       relative, sizeof(relative), "%s/%s",
                       relative_directory, item->d_name)) {
            closedir(directory);
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                directory_path, "DTK output path exceeds the supported length");
        }
        if (!porpoise_path_join(
                full_path, sizeof(full_path), root, relative)) {
            closedir(directory);
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                directory_path, "DTK output path exceeds the supported length");
        }
        result = dtk_inspect_path(full_path, &is_directory, diagnostics);
        if (result != PORPOISE_EXIT_OK) {
            closedir(directory);
            return result;
        }
        result = dtk_tree_add(
            tree, relative, full_path, is_directory, diagnostics);
        if (result != PORPOISE_EXIT_OK) {
            closedir(directory);
            return result;
        }
        if (is_directory) {
            result = dtk_collect_directory(
                root, relative, depth + 1U, tree, operation, diagnostics);
            if (result != PORPOISE_EXIT_OK) {
                closedir(directory);
                return result;
            }
        }
    }
    if (closedir(directory) != 0) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            directory_path, "cannot close DTK output directory");
    }
    return PORPOISE_EXIT_OK;
}

static int dtk_tree_compare(const void *left, const void *right) {
    const DtkTreeEntry *a = (const DtkTreeEntry *)left;
    const DtkTreeEntry *b = (const DtkTreeEntry *)right;
    return strcmp(a->relative_path, b->relative_path);
}

static void dtk_hash_u64(PorpoiseSha256Context *hash, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0U; index < sizeof(bytes); index++)
        bytes[sizeof(bytes) - 1U - index] = (uint8_t)(value >> (index * 8U));
    porpoise_sha256_update(hash, bytes, sizeof(bytes));
}

static void dtk_hash_u32(PorpoiseSha256Context *hash, uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
    porpoise_sha256_update(hash, bytes, sizeof(bytes));
}

static int dtk_hash_file_into(
    PorpoiseSha256Context *hash,
    const char *path,
    const PorpoiseOperationCallbacks *operation,
    PorpoiseDiagnostics *diagnostics) {
    FILE *file = fopen(path, "rb");
    uint8_t buffer[16384];
    size_t count;
    uint64_t total = 0U;

    if (file == NULL) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot open file for hashing: %s", strerror(errno));
    }
    while ((count = fread(buffer, 1U, sizeof(buffer), file)) != 0U) {
        if (porpoise_operation_cancelled(operation)) {
            fclose(file);
            return PORPOISE_EXIT_CANCELLED;
        }
        if (UINT64_MAX - total < count) {
            fclose(file);
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_INTERNAL,
                path, "file is too large to hash");
        }
        total += (uint64_t)count;
        porpoise_sha256_update(hash, buffer, count);
    }
    {
        bool failed = ferror(file) != 0;
        if (fclose(file) != 0) failed = true;
        if (failed) {
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                path, "failed while hashing file");
        }
    }
    dtk_hash_u64(hash, total);
    return PORPOISE_EXIT_OK;
}

static int dtk_hash_file(
    const char *path,
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE],
    const PorpoiseOperationCallbacks *operation,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSha256Context hash;
    FILE *file;
    uint8_t buffer[16384];
    size_t count;

    file = fopen(path, "rb");
    if (file == NULL) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot open file for hashing: %s", strerror(errno));
    }
    porpoise_sha256_init(&hash);
    while ((count = fread(buffer, 1U, sizeof(buffer), file)) != 0U) {
        if (porpoise_operation_cancelled(operation)) {
            fclose(file);
            return PORPOISE_EXIT_CANCELLED;
        }
        porpoise_sha256_update(&hash, buffer, count);
    }
    {
        bool failed = ferror(file) != 0;
        if (fclose(file) != 0) failed = true;
        if (failed) {
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                path, "failed while hashing file");
        }
    }
    porpoise_sha256_final(&hash, digest);
    return PORPOISE_EXIT_OK;
}

static int dtk_hash_tree(
    const DtkTree *tree,
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE],
    const PorpoiseOperationCallbacks *operation,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSha256Context hash;
    size_t index;
    static const uint8_t magic[] = { 'D', 'T', 'K', 'T', 'R', 'E', 'E', 1U };

    porpoise_sha256_init(&hash);
    porpoise_sha256_update(&hash, magic, sizeof(magic));
    for (index = 0U; index < tree->count; index++) {
        const DtkTreeEntry *entry = &tree->entries[index];
        uint8_t kind;
        size_t path_length;
        int result;
        if (!entry->directory &&
            strcmp(entry->relative_path, PORPOISE_DTK_IMPORT_METADATA_FILE) == 0) {
            continue;
        }
        kind = entry->directory ? 1U : 2U;
        path_length = strlen(entry->relative_path);
        porpoise_sha256_update(&hash, &kind, sizeof(kind));
        dtk_hash_u64(&hash, (uint64_t)path_length);
        porpoise_sha256_update(&hash, entry->relative_path, path_length);
        if (!entry->directory) {
            result = dtk_hash_file_into(
                &hash, entry->full_path, operation, diagnostics);
            if (result != PORPOISE_EXIT_OK) return result;
        }
    }
    porpoise_sha256_final(&hash, digest);
    return PORPOISE_EXIT_OK;
}

static bool dtk_has_asm_extension(const char *path) {
    size_t length = strlen(path);
    return length >= 2U && path[length - 2U] == '.' && path[length - 1U] == 's';
}

static const DtkTreeEntry *dtk_tree_find(
    const DtkTree *tree,
    const char *relative_path) {
    size_t index;
    for (index = 0U; index < tree->count; index++) {
        if (strcmp(tree->entries[index].relative_path, relative_path) == 0)
            return &tree->entries[index];
    }
    return NULL;
}

static int dtk_validate_link_order(
    const char *root,
    const DtkTree *tree,
    size_t asm_file_count,
    PorpoiseDiagnostics *diagnostics) {
    const DtkTreeEntry *order = dtk_tree_find(tree, "link_order.txt");
    FILE *file;
    char line[PORPOISE_PATH_CAPACITY + 8U];
    bool *listed;
    size_t line_number = 0U;
    size_t listed_count = 0U;

    if (order == NULL || order->directory) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            root, "DTK output is missing root link_order.txt");
    }
    listed = tree->count == 0U ? NULL :
        (bool *)calloc(tree->count, sizeof(*listed));
    if (tree->count != 0U && listed == NULL) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_INTERNAL,
            order->full_path, "out of memory while validating link_order.txt");
    }
    file = fopen(order->full_path, "rb");
    if (file == NULL) {
        free(listed);
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            order->full_path, "cannot open link_order.txt");
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char normalized[PORPOISE_PATH_CAPACITY];
        size_t index;
        const DtkTreeEntry *entry;
        line_number++;
        if (strchr(line, '\n') == NULL && !feof(file)) {
            fclose(file);
            free(listed);
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                order->full_path,
                "link_order.txt line %lu exceeds the supported length",
                (unsigned long)line_number);
        }
        porpoise_trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        if (!dtk_normalize_safe_relative(line, normalized, sizeof(normalized))) {
            fclose(file);
            free(listed);
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                order->full_path,
                "link_order.txt line %lu contains an unsafe path",
                (unsigned long)line_number);
        }
        entry = dtk_tree_find(tree, normalized);
        if (entry == NULL || entry->directory ||
            !dtk_has_asm_extension(entry->relative_path)) {
            fclose(file);
            free(listed);
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                order->full_path,
                "link_order.txt line %lu does not name a generated assembly file",
                (unsigned long)line_number);
        }
        index = (size_t)(entry - tree->entries);
        if (listed[index]) {
            fclose(file);
            free(listed);
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                order->full_path,
                "link_order.txt lists '%s' more than once",
                entry->relative_path);
        }
        listed[index] = true;
        listed_count++;
    }
    {
        bool failed = ferror(file) != 0;
        if (fclose(file) != 0) failed = true;
        if (failed) {
            free(listed);
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                order->full_path, "failed while reading link_order.txt");
        }
    }
    if (listed_count != asm_file_count) {
        free(listed);
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            order->full_path,
            "link_order.txt lists %lu assembly files, but the tree contains %lu",
            (unsigned long)listed_count, (unsigned long)asm_file_count);
    }
    {
        size_t index;
        for (index = 0U; index < tree->count; index++) {
            if (!tree->entries[index].directory &&
                dtk_has_asm_extension(tree->entries[index].relative_path) &&
                !listed[index]) {
                free(listed);
                return dtk_diagnostic(
                    diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                    order->full_path,
                    "link_order.txt omits assembly file '%s'",
                    tree->entries[index].relative_path);
            }
        }
    }
    free(listed);
    return PORPOISE_EXIT_OK;
}

static int dtk_validate_assembly(
    const char *path,
    size_t expected_asm_count,
    PorpoiseDtkImportMetadata *metadata,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseProgram program;
    size_t file_index;
    int result;

    porpoise_program_init(&program);
    result = porpoise_program_load(&program, path, diagnostics);
    if (result != PORPOISE_EXIT_OK) {
        porpoise_program_free(&program);
        return result;
    }
    if (program.file_count != expected_asm_count) {
        porpoise_program_free(&program);
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            path, "assembly discovery did not match the validated DTK file set");
    }
    for (file_index = 0U; file_index < program.file_count; file_index++) {
        const PorpoiseSourceFile *file = &program.files[file_index];
        size_t function_index;
        metadata->function_count += file->function_count;
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            size_t item_index;
            for (item_index = 0U; item_index < function->item_count; item_index++) {
                if (function->items[item_index].kind == PORPOISE_ASM_INSTRUCTION)
                    metadata->annotation_count++;
            }
        }
    }
    porpoise_program_free(&program);
    if (metadata->function_count == 0U || metadata->annotation_count == 0U) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            path, "DTK assembly must contain annotated functions and instructions");
    }
    return PORPOISE_EXIT_OK;
}

static int dtk_validate_tree(
    const char *path,
    bool require_link_order,
    bool allow_metadata,
    const PorpoiseOperationCallbacks *operation,
    DtkValidation *validation,
    PorpoiseDiagnostics *diagnostics) {
    bool root_is_directory = false;
    int result;
    size_t index;
    uint8_t content_digest[PORPOISE_SHA256_DIGEST_SIZE];

    memset(validation, 0, sizeof(*validation));
    dtk_tree_init(&validation->tree);
    result = dtk_inspect_path(path, &root_is_directory, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (!root_is_directory) {
        char base[PORPOISE_PATH_CAPACITY];
        if (!dtk_has_asm_extension(path) || require_link_order) {
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                path, "prepared DTK input must be an assembly tree with link_order.txt");
        }
        if (!porpoise_path_basename(base, sizeof(base), path) ||
            !dtk_safe_component(base, strlen(base))) {
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                path, "prepared assembly filename is not portable and safe");
        }
        result = dtk_tree_add(
            &validation->tree, base, path, false, diagnostics);
        if (result != PORPOISE_EXIT_OK) return result;
    } else {
        result = dtk_collect_directory(
            path, "", 0U, &validation->tree, operation, diagnostics);
        if (result != PORPOISE_EXIT_OK) return result;
    }
    if (validation->tree.count > 1U) {
        qsort(validation->tree.entries, validation->tree.count,
              sizeof(*validation->tree.entries), dtk_tree_compare);
    }
    for (index = 0U; index < validation->tree.count; index++) {
        const DtkTreeEntry *entry = &validation->tree.entries[index];
        if (!entry->directory &&
            strcmp(entry->relative_path, PORPOISE_DTK_IMPORT_METADATA_FILE) == 0 &&
            !allow_metadata) {
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                entry->full_path,
                "DTK attempted to provide Porpoise-owned cache metadata");
        }
        if (!entry->directory && dtk_has_asm_extension(entry->relative_path))
            validation->metadata.asm_file_count++;
    }
    if (validation->metadata.asm_file_count == 0U) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            path, "DTK output contains no assembly files");
    }
    if (require_link_order) {
        result = dtk_validate_link_order(
            path, &validation->tree,
            validation->metadata.asm_file_count, diagnostics);
        if (result != PORPOISE_EXIT_OK) return result;
    }
    if (porpoise_operation_cancelled(operation)) return PORPOISE_EXIT_CANCELLED;
    result = dtk_validate_assembly(
        path, validation->metadata.asm_file_count,
        &validation->metadata, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    result = dtk_hash_tree(
        &validation->tree, content_digest, operation, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    porpoise_sha256_hex(
        content_digest, validation->metadata.content_sha256);
    return porpoise_operation_cancelled(operation) ?
        PORPOISE_EXIT_CANCELLED : PORPOISE_EXIT_OK;
}

static bool dtk_output_is_plain(const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    if (text == NULL) return true;
    while (*cursor != '\0') {
        if (*cursor == 0x1bU) return false;
        cursor++;
    }
    return true;
}

static bool dtk_parse_decimal_component(
    const char **cursor_in_out,
    unsigned int *value_out) {
    const char *cursor = *cursor_in_out;
    unsigned int value = 0U;
    if (*cursor < '0' || *cursor > '9') return false;
    do {
        unsigned int digit = (unsigned int)(*cursor - '0');
        if (value > (UINT_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
        cursor++;
    } while (*cursor >= '0' && *cursor <= '9');
    *cursor_in_out = cursor;
    *value_out = value;
    return true;
}

static bool dtk_version_at_least(
    unsigned int major,
    unsigned int minor,
    unsigned int patch,
    const PorpoiseDtkImportOptions *options) {
    if (major != options->minimum_dtk_major)
        return major > options->minimum_dtk_major;
    if (minor != options->minimum_dtk_minor)
        return minor > options->minimum_dtk_minor;
    return patch >= options->minimum_dtk_patch;
}

static int dtk_parse_version(
    const char *output,
    const PorpoiseDtkImportOptions *options,
    char version_out[PORPOISE_DTK_VERSION_CAPACITY],
    PorpoiseDiagnostics *diagnostics) {
    const char *line_end;
    const char *cursor;
    size_t line_length;
    char product[32];
    size_t product_length = 0U;
    unsigned int major;
    unsigned int minor;
    unsigned int patch;

    if (output == NULL || output[0] == '\0' ||
        !dtk_output_is_plain(output)) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->dtk_path,
            "DTK version output is empty or contains terminal escapes");
    }
    line_end = strpbrk(output, "\r\n");
    line_length = line_end == NULL ? strlen(output) : (size_t)(line_end - output);
    while (line_length > 0U &&
           (output[line_length - 1U] == ' ' || output[line_length - 1U] == '\t')) {
        line_length--;
    }
    if (line_length == 0U || line_length >= PORPOISE_DTK_VERSION_CAPACITY) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->dtk_path, "DTK version output is malformed or too long");
    }
    cursor = output;
    while ((size_t)(cursor - output) < line_length &&
           *cursor != ' ' && *cursor != '\t') {
        if (product_length + 1U >= sizeof(product)) {
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                options->dtk_path, "DTK version product name is malformed");
        }
        product[product_length++] = *cursor++;
    }
    product[product_length] = '\0';
    if (!(dtk_ascii_equal_ignore_case(product, "dtk") ||
          dtk_ascii_equal_ignore_case(product, "dtk.exe"))) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->dtk_path, "tool version output does not identify DTK");
    }
    while ((size_t)(cursor - output) < line_length &&
           (*cursor == ' ' || *cursor == '\t')) cursor++;
    if (!dtk_parse_decimal_component(&cursor, &major) || *cursor++ != '.' ||
        !dtk_parse_decimal_component(&cursor, &minor) || *cursor++ != '.' ||
        !dtk_parse_decimal_component(&cursor, &patch)) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->dtk_path, "DTK version is not semantic major.minor.patch");
    }
    if ((size_t)(cursor - output) < line_length &&
        *cursor != ' ' && *cursor != '\t' && *cursor != '-' && *cursor != '+') {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->dtk_path, "DTK version suffix is malformed");
    }
    if (!dtk_version_at_least(major, minor, patch, options)) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->dtk_path,
            "DTK %u.%u.%u is older than required %u.%u.%u",
            major, minor, patch,
            options->minimum_dtk_major,
            options->minimum_dtk_minor,
            options->minimum_dtk_patch);
    }
    memcpy(version_out, output, line_length);
    version_out[line_length] = '\0';
    return PORPOISE_EXIT_OK;
}

static int dtk_run_checked(
    const PorpoiseDtkImportOptions *options,
    const char *const *argv,
    PorpoiseDtkProcessResult *process,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseDtkRunCallback runner = options->runner == NULL ?
        porpoise_dtk_run_process_default : options->runner;
    int result;

    porpoise_dtk_process_result_init(process);
    result = runner(
        options->runner_user_data, argv, NULL, options->operation,
        process, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (process->exit_code != 0) {
        const char *detail = process->standard_error != NULL &&
                             process->standard_error[0] != '\0' ?
            process->standard_error : process->standard_output;
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_TRANSLATION,
            options->dtk_path,
            "DTK command failed with exit code %d%s%.400s",
            process->exit_code,
            detail != NULL && detail[0] != '\0' ? ": " : "",
            detail == NULL ? "" : detail);
    }
    return PORPOISE_EXIT_OK;
}

static void dtk_hash_bytes_hex(
    const void *data,
    size_t size,
    char output[PORPOISE_SHA256_HEX_SIZE]) {
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    porpoise_sha256(data, size, digest);
    porpoise_sha256_hex(digest, output);
}

static void dtk_hash_settings(
    const PorpoiseDtkImportOptions *options,
    char output[PORPOISE_SHA256_HEX_SIZE]) {
    PorpoiseSha256Context hash;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    uint8_t source_kind = (uint8_t)options->source_kind;
    uint8_t require_link_order = options->prepared_require_link_order ? 1U : 0U;
    const char *identity = options->settings_identity == NULL ?
        "" : options->settings_identity;
    size_t identity_length = strlen(identity);
    static const uint8_t magic[] = { 'P', 'D', 'T', 'K', 'S', 'E', 'T', 1U };

    porpoise_sha256_init(&hash);
    porpoise_sha256_update(&hash, magic, sizeof(magic));
    porpoise_sha256_update(&hash, &source_kind, sizeof(source_kind));
    porpoise_sha256_update(
        &hash, &require_link_order, sizeof(require_link_order));
    dtk_hash_u32(&hash, (uint32_t)options->minimum_dtk_major);
    dtk_hash_u32(&hash, (uint32_t)options->minimum_dtk_minor);
    dtk_hash_u32(&hash, (uint32_t)options->minimum_dtk_patch);
    dtk_hash_u64(&hash, (uint64_t)identity_length);
    porpoise_sha256_update(&hash, identity, identity_length);
    porpoise_sha256_final(&hash, digest);
    porpoise_sha256_hex(digest, output);
}

static bool dtk_hex_decode(
    const char hex[PORPOISE_SHA256_HEX_SIZE],
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE]) {
    size_t index;
    if (hex == NULL || strlen(hex) != PORPOISE_SHA256_DIGEST_SIZE * 2U)
        return false;
    for (index = 0U; index < PORPOISE_SHA256_DIGEST_SIZE; index++) {
        char high_character = hex[index * 2U];
        char low_character = hex[index * 2U + 1U];
        int high = high_character >= '0' && high_character <= '9' ?
            high_character - '0' :
            high_character >= 'a' && high_character <= 'f' ?
                high_character - 'a' + 10 : -1;
        int low = low_character >= '0' && low_character <= '9' ?
            low_character - '0' :
            low_character >= 'a' && low_character <= 'f' ?
                low_character - 'a' + 10 : -1;
        if (high < 0 || low < 0) return false;
        digest[index] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static bool dtk_compute_dependency_digest(
    PorpoiseDtkSourceKind source_kind,
    const char *input_hex,
    const char *tool_hex,
    const char *settings_hex,
    const char *version,
    char output[PORPOISE_SHA256_HEX_SIZE]) {
    PorpoiseSha256Context hash;
    uint8_t input_digest[PORPOISE_SHA256_DIGEST_SIZE];
    uint8_t tool_digest[PORPOISE_SHA256_DIGEST_SIZE];
    uint8_t settings_digest[PORPOISE_SHA256_DIGEST_SIZE];
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    uint8_t kind = (uint8_t)source_kind;
    size_t version_length = version == NULL ? 0U : strlen(version);
    static const uint8_t magic[] = { 'P', 'D', 'T', 'K', 'D', 'E', 'P', 1U };

    if (!dtk_hex_decode(input_hex, input_digest) ||
        !dtk_hex_decode(tool_hex, tool_digest) ||
        !dtk_hex_decode(settings_hex, settings_digest)) {
        return false;
    }
    porpoise_sha256_init(&hash);
    porpoise_sha256_update(&hash, magic, sizeof(magic));
    porpoise_sha256_update(&hash, &kind, sizeof(kind));
    porpoise_sha256_update(&hash, input_digest, sizeof(input_digest));
    porpoise_sha256_update(&hash, tool_digest, sizeof(tool_digest));
    porpoise_sha256_update(&hash, settings_digest, sizeof(settings_digest));
    dtk_hash_u64(&hash, (uint64_t)version_length);
    if (version_length != 0U)
        porpoise_sha256_update(&hash, version, version_length);
    porpoise_sha256_final(&hash, digest);
    porpoise_sha256_hex(digest, output);
    return true;
}

static bool dtk_hex_is_canonical(const char *text) {
    size_t index;
    if (text == NULL || strlen(text) != PORPOISE_SHA256_DIGEST_SIZE * 2U)
        return false;
    for (index = 0U; index < PORPOISE_SHA256_DIGEST_SIZE * 2U; index++) {
        if (!((text[index] >= '0' && text[index] <= '9') ||
              (text[index] >= 'a' && text[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool dtk_parse_size(const char *text, size_t *value_out) {
    size_t value = 0U;
    const char *cursor = text;
    if (text == NULL || *text == '\0') return false;
    while (*cursor != '\0') {
        unsigned int digit;
        if (*cursor < '0' || *cursor > '9') return false;
        digit = (unsigned int)(*cursor - '0');
        if (value > (SIZE_MAX - digit) / 10U) return false;
        value = value * 10U + (size_t)digit;
        cursor++;
    }
    *value_out = value;
    return true;
}

static bool dtk_metadata_line(
    FILE *file,
    char line[DTK_METADATA_LINE_CAPACITY]) {
    size_t length;
    if (fgets(line, DTK_METADATA_LINE_CAPACITY, file) == NULL) return false;
    length = strlen(line);
    if (length == 0U) return false;
    if (line[length - 1U] != '\n' && !feof(file)) return false;
    while (length > 0U &&
           (line[length - 1U] == '\n' || line[length - 1U] == '\r')) {
        line[--length] = '\0';
    }
    return true;
}

static bool dtk_metadata_value(
    const char *line,
    const char *key,
    const char **value_out) {
    size_t key_length = strlen(key);
    if (strncmp(line, key, key_length) != 0 || line[key_length] != '=')
        return false;
    *value_out = line + key_length + 1U;
    return true;
}

static bool dtk_read_cache_metadata(
    const char *cache_path,
    PorpoiseDtkImportMetadata *metadata) {
    char path[PORPOISE_PATH_CAPACITY];
    char line[DTK_METADATA_LINE_CAPACITY];
    const char *value;
    FILE *file;
    size_t parsed_size;

    memset(metadata, 0, sizeof(*metadata));
    if (!porpoise_path_join(
            path, sizeof(path), cache_path,
            PORPOISE_DTK_IMPORT_METADATA_FILE)) {
        return false;
    }
    file = fopen(path, "rb");
    if (file == NULL) return false;
#define DTK_READ_LINE()                                                         \
    do {                                                                        \
        if (!dtk_metadata_line(file, line)) goto invalid;                       \
    } while (0)
#define DTK_READ_HEX(key, field)                                                \
    do {                                                                        \
        DTK_READ_LINE();                                                        \
        if (!dtk_metadata_value(line, key, &value) ||                           \
            !dtk_hex_is_canonical(value) ||                                     \
            !porpoise_copy_string(field, sizeof(field), value)) goto invalid;   \
    } while (0)
#define DTK_READ_SIZE(key, field)                                               \
    do {                                                                        \
        DTK_READ_LINE();                                                        \
        if (!dtk_metadata_value(line, key, &value) ||                           \
            !dtk_parse_size(value, &parsed_size)) goto invalid;                 \
        field = parsed_size;                                                    \
    } while (0)
    DTK_READ_LINE();
    if (strcmp(line, "porpoise_dtk_cache_v1") != 0) goto invalid;
    DTK_READ_LINE();
    if (!dtk_metadata_value(line, "schema_version", &value) ||
        strcmp(value, "1") != 0) goto invalid;
    metadata->schema_version = PORPOISE_DTK_IMPORT_METADATA_SCHEMA_VERSION;
    DTK_READ_LINE();
    if (!dtk_metadata_value(line, "source_kind", &value) ||
        strcmp(value, "managed_elf") != 0) goto invalid;
    metadata->source_kind = PORPOISE_DTK_SOURCE_MANAGED_ELF;
    DTK_READ_LINE();
    if (!dtk_metadata_value(line, "dtk_version", &value) ||
        value[0] == '\0' ||
        !porpoise_copy_string(
            metadata->dtk_version, sizeof(metadata->dtk_version), value)) {
        goto invalid;
    }
    DTK_READ_HEX("input_sha256", metadata->input_sha256);
    DTK_READ_HEX("tool_sha256", metadata->tool_sha256);
    DTK_READ_HEX("settings_sha256", metadata->settings_sha256);
    DTK_READ_HEX("dependency_sha256", metadata->dependency_sha256);
    DTK_READ_HEX("content_sha256", metadata->content_sha256);
    DTK_READ_SIZE("asm_file_count", metadata->asm_file_count);
    DTK_READ_SIZE("function_count", metadata->function_count);
    DTK_READ_SIZE("annotation_count", metadata->annotation_count);
    {
        bool extra = fgets(line, sizeof(line), file) != NULL;
        bool failed = ferror(file) != 0;
        if (fclose(file) != 0) failed = true;
        if (extra || failed) return false;
    }
#undef DTK_READ_LINE
#undef DTK_READ_HEX
#undef DTK_READ_SIZE
    return true;

invalid:
#undef DTK_READ_LINE
#undef DTK_READ_HEX
#undef DTK_READ_SIZE
    fclose(file);
    return false;
}

static int dtk_write_cache_metadata(
    const char *stage_path,
    const PorpoiseDtkImportMetadata *metadata,
    PorpoiseDiagnostics *diagnostics) {
    char path[PORPOISE_PATH_CAPACITY];
    FILE *file;
    bool ok;

    if (!porpoise_path_join(
            path, sizeof(path), stage_path,
            PORPOISE_DTK_IMPORT_METADATA_FILE)) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_INTERNAL,
            stage_path, "DTK cache metadata path exceeds the supported length");
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot create DTK cache metadata: %s", strerror(errno));
    }
    ok = fprintf(
        file,
        "porpoise_dtk_cache_v1\n"
        "schema_version=%u\n"
        "source_kind=managed_elf\n"
        "dtk_version=%s\n"
        "input_sha256=%s\n"
        "tool_sha256=%s\n"
        "settings_sha256=%s\n"
        "dependency_sha256=%s\n"
        "content_sha256=%s\n"
        "asm_file_count=%llu\n"
        "function_count=%llu\n"
        "annotation_count=%llu\n",
        PORPOISE_DTK_IMPORT_METADATA_SCHEMA_VERSION,
        metadata->dtk_version,
        metadata->input_sha256,
        metadata->tool_sha256,
        metadata->settings_sha256,
        metadata->dependency_sha256,
        metadata->content_sha256,
        (unsigned long long)metadata->asm_file_count,
        (unsigned long long)metadata->function_count,
        (unsigned long long)metadata->annotation_count) >= 0;
    if (!ok || fflush(file) != 0) {
        (void)fclose(file);
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "failed while writing DTK cache metadata");
    }
    if (fclose(file) != 0) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "failed while writing DTK cache metadata");
    }
    return PORPOISE_EXIT_OK;
}

static bool dtk_dependency_metadata_equal(
    const PorpoiseDtkImportMetadata *left,
    const PorpoiseDtkImportMetadata *right) {
    return left->schema_version == right->schema_version &&
           left->source_kind == right->source_kind &&
           strcmp(left->dtk_version, right->dtk_version) == 0 &&
           strcmp(left->input_sha256, right->input_sha256) == 0 &&
           strcmp(left->tool_sha256, right->tool_sha256) == 0 &&
           strcmp(left->settings_sha256, right->settings_sha256) == 0 &&
           strcmp(left->dependency_sha256, right->dependency_sha256) == 0;
}

static int dtk_try_cache(
    const PorpoiseDtkImportOptions *options,
    const PorpoiseDtkImportMetadata *dependency,
    PorpoiseDtkImportResult *result,
    bool *hit_out) {
    PorpoiseDtkImportMetadata stored;
    DtkValidation validation;
    PorpoiseDiagnostics ignored;
    int validation_result;

    *hit_out = false;
    if (!options->allow_cache_reuse ||
        !porpoise_path_exists(options->cache_path) ||
        !dtk_read_cache_metadata(options->cache_path, &stored) ||
        !dtk_dependency_metadata_equal(&stored, dependency)) {
        return PORPOISE_EXIT_OK;
    }
    porpoise_diagnostics_init(&ignored);
    validation_result = dtk_validate_tree(
        options->cache_path, true, true, options->operation,
        &validation, &ignored);
    porpoise_diagnostics_free(&ignored);
    if (validation_result == PORPOISE_EXIT_CANCELLED) {
        dtk_tree_free(&validation.tree);
        return PORPOISE_EXIT_CANCELLED;
    }
    if (validation_result == PORPOISE_EXIT_INTERNAL) {
        dtk_tree_free(&validation.tree);
        return PORPOISE_EXIT_INTERNAL;
    }
    if (validation_result != PORPOISE_EXIT_OK) {
        dtk_tree_free(&validation.tree);
        return PORPOISE_EXIT_OK;
    }
    if (strcmp(stored.content_sha256,
               validation.metadata.content_sha256) != 0 ||
        stored.asm_file_count != validation.metadata.asm_file_count ||
        stored.function_count != validation.metadata.function_count ||
        stored.annotation_count != validation.metadata.annotation_count) {
        dtk_tree_free(&validation.tree);
        return PORPOISE_EXIT_OK;
    }
    dtk_tree_free(&validation.tree);
    porpoise_dtk_import_result_init(result);
    if (!porpoise_copy_string(
            result->validated_path, sizeof(result->validated_path),
            options->cache_path)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    result->cache_hit = true;
    result->metadata = stored;
    *hit_out = true;
    return PORPOISE_EXIT_OK;
}

static unsigned long dtk_process_identifier(void) {
#ifdef _WIN32
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static int dtk_create_stage(
    const char *cache_path,
    char stage_path[PORPOISE_PATH_CAPACITY],
    PorpoiseDiagnostics *diagnostics) {
    unsigned int attempt;
    unsigned long process_id = dtk_process_identifier();
    for (attempt = 0U; attempt < 10000U; attempt++) {
        if (!porpoise_format(
                stage_path, PORPOISE_PATH_CAPACITY,
                "%s.stage.%lu.%u", cache_path, process_id, attempt)) {
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                cache_path, "DTK staging path exceeds the supported length");
        }
        if (DTK_MKDIR(stage_path) == 0) return PORPOISE_EXIT_OK;
        if (errno != EEXIST) {
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                stage_path, "cannot create fresh DTK staging directory: %s",
                strerror(errno));
        }
    }
    return dtk_diagnostic(
        diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
        cache_path, "cannot reserve a unique DTK staging directory");
}

static void dtk_cleanup_stage(
    const char *stage_path,
    PorpoiseDiagnostics *diagnostics) {
    if (stage_path == NULL || stage_path[0] == '\0' ||
        !porpoise_path_exists(stage_path)) {
        return;
    }
    if (!porpoise_remove_tree(stage_path, diagnostics)) {
        (void)dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_WARNING, PORPOISE_EXIT_OK,
            stage_path, "failed to completely remove DTK staging directory");
    }
}

static bool dtk_unique_backup_path(
    const char *cache_path,
    char backup_path[PORPOISE_PATH_CAPACITY]) {
    unsigned int attempt;
    unsigned long process_id = dtk_process_identifier();
    for (attempt = 0U; attempt < 10000U; attempt++) {
        if (!porpoise_format(
                backup_path, PORPOISE_PATH_CAPACITY,
                "%s.backup.%lu.%u", cache_path, process_id, attempt)) {
            return false;
        }
        if (!porpoise_path_exists(backup_path)) return true;
    }
    return false;
}

static int dtk_publish_cache(
    const char *stage_path,
    const char *cache_path,
    PorpoiseDiagnostics *diagnostics) {
    char backup_path[PORPOISE_PATH_CAPACITY];
    bool had_previous = porpoise_path_exists(cache_path);

    backup_path[0] = '\0';
    if (had_previous) {
        if (!dtk_unique_backup_path(cache_path, backup_path)) {
            return dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                cache_path, "cannot reserve DTK cache rollback path");
        }
        if (!porpoise_move_path(cache_path, backup_path, diagnostics))
            return PORPOISE_EXIT_IO;
    }
    if (!porpoise_move_path(stage_path, cache_path, diagnostics)) {
        if (had_previous && !porpoise_move_path(
                backup_path, cache_path, diagnostics)) {
            (void)dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                backup_path,
                "DTK publication failed and the previous cache could not be restored");
        }
        return PORPOISE_EXIT_IO;
    }
    if (had_previous) {
        PorpoiseDiagnostics ignored;
        bool removed;
        porpoise_diagnostics_init(&ignored);
        removed = porpoise_remove_tree(backup_path, &ignored);
        porpoise_diagnostics_free(&ignored);
        if (!removed) {
            (void)dtk_diagnostic(
                diagnostics, PORPOISE_SEVERITY_WARNING, PORPOISE_EXIT_OK,
                backup_path,
                "new DTK cache was published, but its rollback backup remains");
        }
    }
    return PORPOISE_EXIT_OK;
}

static int dtk_validate_info_output(
    const PorpoiseDtkImportOptions *options,
    const PorpoiseDtkProcessResult *process,
    PorpoiseDiagnostics *diagnostics) {
    const char *output = process->standard_output != NULL &&
                         process->standard_output[0] != '\0' ?
        process->standard_output : process->standard_error;
    if (!dtk_output_is_plain(process->standard_output) ||
        !dtk_output_is_plain(process->standard_error) ||
        output == NULL || strstr(output, "ELF type:") == NULL ||
        strstr(output, "Section count:") == NULL) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->input_path,
            "DTK elf info did not produce the expected uncolored ELF summary");
    }
    return PORPOISE_EXIT_OK;
}

static int dtk_validate_managed_arguments(
    const PorpoiseDtkImportOptions *options,
    PorpoiseDiagnostics *diagnostics) {
    size_t cache_path_length;
    bool input_directory = false;
    bool tool_directory = false;
    bool overlap = false;
    int result;

    if (options->cache_path == NULL || options->cache_path[0] == '\0' ||
        options->dtk_path == NULL || options->dtk_path[0] == '\0') {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->input_path,
            "managed DTK ELF import requires cache_path and dtk_path");
    }
    cache_path_length = strlen(options->cache_path);
    if (options->cache_path[cache_path_length - 1U] == '/' ||
        options->cache_path[cache_path_length - 1U] == '\\') {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->cache_path,
            "managed DTK cache path must name a directory, not end in a separator");
    }
    result = dtk_inspect_path(
        options->input_path, &input_directory, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (input_directory) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->input_path, "managed DTK input must be an ELF file");
    }
    result = dtk_inspect_path(options->dtk_path, &tool_directory, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (tool_directory) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->dtk_path, "DTK tool path must be a regular file");
    }
    if (!porpoise_path_trees_overlap(
            options->input_path, options->cache_path, &overlap)) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->cache_path,
            "cannot resolve ELF and cache paths for overlap validation");
    }
    if (overlap) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->cache_path,
            "DTK cache must not contain or replace its ELF input");
    }
    if (!porpoise_path_trees_overlap(
            options->dtk_path, options->cache_path, &overlap)) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->cache_path,
            "cannot resolve DTK tool and cache paths for overlap validation");
    }
    if (overlap) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->cache_path,
            "DTK cache must not contain or replace the DTK executable");
    }
    return PORPOISE_EXIT_OK;
}

static int dtk_verify_file_digest(
    const char *path,
    const char *expected_hex,
    const PorpoiseOperationCallbacks *operation,
    PorpoiseDiagnostics *diagnostics) {
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    char actual_hex[PORPOISE_SHA256_HEX_SIZE];
    int result = dtk_hash_file(path, digest, operation, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    porpoise_sha256_hex(digest, actual_hex);
    if (strcmp(actual_hex, expected_hex) != 0) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            path, "dependency changed during managed DTK import");
    }
    return PORPOISE_EXIT_OK;
}

static int dtk_verify_managed_dependencies(
    const PorpoiseDtkImportOptions *options,
    const PorpoiseDtkImportMetadata *metadata,
    PorpoiseDiagnostics *diagnostics) {
    int result = dtk_verify_file_digest(
        options->input_path, metadata->input_sha256,
        options->operation, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    return dtk_verify_file_digest(
        options->dtk_path, metadata->tool_sha256,
        options->operation, diagnostics);
}

static int dtk_validate_prepared_options(
    const PorpoiseDtkImportOptions *options,
    PorpoiseDtkImportResult *result,
    PorpoiseDiagnostics *diagnostics) {
    DtkValidation validation;
    PorpoiseDtkImportMetadata metadata;
    int validation_result;

    porpoise_operation_progress(
        options->operation, PORPOISE_PHASE_VALIDATE, 0U, 2U,
        "validate prepared DTK assembly");
    validation_result = dtk_validate_tree(
        options->input_path, options->prepared_require_link_order,
        true, options->operation, &validation, diagnostics);
    if (validation_result != PORPOISE_EXIT_OK) {
        dtk_tree_free(&validation.tree);
        return validation_result;
    }
    metadata = validation.metadata;
    dtk_tree_free(&validation.tree);
    metadata.schema_version = PORPOISE_DTK_IMPORT_METADATA_SCHEMA_VERSION;
    metadata.source_kind = PORPOISE_DTK_SOURCE_PREPARED_ASM;
    if (!porpoise_copy_string(
            metadata.input_sha256, sizeof(metadata.input_sha256),
            metadata.content_sha256)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    dtk_hash_bytes_hex("", 0U, metadata.tool_sha256);
    dtk_hash_settings(options, metadata.settings_sha256);
    if (!dtk_compute_dependency_digest(
            metadata.source_kind, metadata.input_sha256,
            metadata.tool_sha256, metadata.settings_sha256, "",
            metadata.dependency_sha256)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    porpoise_dtk_import_result_init(result);
    if (!porpoise_copy_string(
            result->validated_path, sizeof(result->validated_path),
            options->input_path)) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->input_path,
            "prepared DTK assembly path exceeds the supported length");
    }
    result->metadata = metadata;
    porpoise_operation_progress(
        options->operation, PORPOISE_PHASE_VALIDATE, 2U, 2U,
        "prepared DTK assembly validated");
    return PORPOISE_EXIT_OK;
}

static int dtk_import_managed(
    const PorpoiseDtkImportOptions *options,
    PorpoiseDtkImportResult *result,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseDtkImportMetadata metadata;
    PorpoiseDtkProcessResult process;
    PorpoiseDtkImportResult cached;
    DtkValidation validation;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    const char *version_argv[3];
    const char *info_argv[7];
    const char *disasm_argv[8];
    char stage_path[PORPOISE_PATH_CAPACITY];
    char cache_parent[PORPOISE_PATH_CAPACITY];
    bool cache_hit;
    int import_result;

    memset(&metadata, 0, sizeof(metadata));
    stage_path[0] = '\0';
    metadata.schema_version = PORPOISE_DTK_IMPORT_METADATA_SCHEMA_VERSION;
    metadata.source_kind = PORPOISE_DTK_SOURCE_MANAGED_ELF;
    porpoise_operation_progress(
        options->operation, PORPOISE_PHASE_IMPORT, 0U, 8U,
        "validate managed DTK import paths");
    import_result = dtk_validate_managed_arguments(options, diagnostics);
    if (import_result != PORPOISE_EXIT_OK) return import_result;
    if (porpoise_operation_cancelled(options->operation))
        return PORPOISE_EXIT_CANCELLED;

    porpoise_operation_progress(
        options->operation, PORPOISE_PHASE_IMPORT, 1U, 8U,
        "hash ELF, DTK, and import settings");
    import_result = dtk_hash_file(
        options->input_path, digest, options->operation, diagnostics);
    if (import_result != PORPOISE_EXIT_OK) return import_result;
    porpoise_sha256_hex(digest, metadata.input_sha256);
    import_result = dtk_hash_file(
        options->dtk_path, digest, options->operation, diagnostics);
    if (import_result != PORPOISE_EXIT_OK) return import_result;
    porpoise_sha256_hex(digest, metadata.tool_sha256);
    dtk_hash_settings(options, metadata.settings_sha256);
    if (porpoise_operation_cancelled(options->operation))
        return PORPOISE_EXIT_CANCELLED;

    porpoise_operation_progress(
        options->operation, PORPOISE_PHASE_IMPORT, 2U, 8U,
        "validate DTK version");
    version_argv[0] = options->dtk_path;
    version_argv[1] = "--version";
    version_argv[2] = NULL;
    import_result = dtk_run_checked(
        options, version_argv, &process, diagnostics);
    if (import_result == PORPOISE_EXIT_OK) {
        const char *version_output = process.standard_output != NULL &&
                                     process.standard_output[0] != '\0' ?
            process.standard_output : process.standard_error;
        import_result = dtk_parse_version(
            version_output, options, metadata.dtk_version, diagnostics);
    }
    porpoise_dtk_process_result_free(&process);
    if (import_result != PORPOISE_EXIT_OK) return import_result;

    porpoise_operation_progress(
        options->operation, PORPOISE_PHASE_IMPORT, 3U, 8U,
        "inspect ELF with DTK");
    info_argv[0] = options->dtk_path;
    info_argv[1] = "--no-color";
    info_argv[2] = "elf";
    info_argv[3] = "info";
    info_argv[4] = options->input_path;
    info_argv[5] = NULL;
    info_argv[6] = NULL;
    import_result = dtk_run_checked(
        options, info_argv, &process, diagnostics);
    if (import_result == PORPOISE_EXIT_OK)
        import_result = dtk_validate_info_output(options, &process, diagnostics);
    porpoise_dtk_process_result_free(&process);
    if (import_result != PORPOISE_EXIT_OK) return import_result;
    if (!dtk_compute_dependency_digest(
            metadata.source_kind, metadata.input_sha256,
            metadata.tool_sha256, metadata.settings_sha256,
            metadata.dtk_version, metadata.dependency_sha256)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    import_result = dtk_verify_managed_dependencies(
        options, &metadata, diagnostics);
    if (import_result != PORPOISE_EXIT_OK) return import_result;

    porpoise_dtk_import_result_init(&cached);
    import_result = dtk_try_cache(
        options, &metadata, &cached, &cache_hit);
    if (import_result != PORPOISE_EXIT_OK) return import_result;
    if (cache_hit) {
        *result = cached;
        porpoise_operation_progress(
            options->operation, PORPOISE_PHASE_IMPORT, 8U, 8U,
            "validated DTK cache hit");
        return PORPOISE_EXIT_OK;
    }
    if (porpoise_operation_cancelled(options->operation))
        return PORPOISE_EXIT_CANCELLED;

    if (!porpoise_path_parent(
            cache_parent, sizeof(cache_parent), options->cache_path)) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->cache_path, "DTK cache path has no valid parent");
    }
    if (!porpoise_make_directories(cache_parent, diagnostics))
        return PORPOISE_EXIT_IO;
    porpoise_operation_progress(
        options->operation, PORPOISE_PHASE_IMPORT, 4U, 8U,
        "create fresh DTK staging directory");
    import_result = dtk_create_stage(
        options->cache_path, stage_path, diagnostics);
    if (import_result != PORPOISE_EXIT_OK) return import_result;

    disasm_argv[0] = options->dtk_path;
    disasm_argv[1] = "--no-color";
    disasm_argv[2] = "elf";
    disasm_argv[3] = "disasm";
    disasm_argv[4] = options->input_path;
    disasm_argv[5] = stage_path;
    disasm_argv[6] = NULL;
    disasm_argv[7] = NULL;
    porpoise_operation_progress(
        options->operation, PORPOISE_PHASE_IMPORT, 5U, 8U,
        "disassemble ELF into fresh stage");
    import_result = dtk_run_checked(
        options, disasm_argv, &process, diagnostics);
    porpoise_dtk_process_result_free(&process);
    if (import_result != PORPOISE_EXIT_OK) {
        dtk_cleanup_stage(stage_path, diagnostics);
        return import_result;
    }
    if (porpoise_operation_cancelled(options->operation)) {
        dtk_cleanup_stage(stage_path, diagnostics);
        return PORPOISE_EXIT_CANCELLED;
    }

    porpoise_operation_progress(
        options->operation, PORPOISE_PHASE_VALIDATE, 6U, 8U,
        "validate staged DTK assembly");
    import_result = dtk_validate_tree(
        stage_path, true, false, options->operation,
        &validation, diagnostics);
    if (import_result != PORPOISE_EXIT_OK) {
        dtk_tree_free(&validation.tree);
        dtk_cleanup_stage(stage_path, diagnostics);
        return import_result;
    }
    metadata.asm_file_count = validation.metadata.asm_file_count;
    metadata.function_count = validation.metadata.function_count;
    metadata.annotation_count = validation.metadata.annotation_count;
    if (!porpoise_copy_string(
            metadata.content_sha256, sizeof(metadata.content_sha256),
            validation.metadata.content_sha256)) {
        dtk_tree_free(&validation.tree);
        dtk_cleanup_stage(stage_path, diagnostics);
        return PORPOISE_EXIT_INTERNAL;
    }
    dtk_tree_free(&validation.tree);
    import_result = dtk_verify_managed_dependencies(
        options, &metadata, diagnostics);
    if (import_result != PORPOISE_EXIT_OK) {
        dtk_cleanup_stage(stage_path, diagnostics);
        return import_result;
    }
    import_result = dtk_write_cache_metadata(
        stage_path, &metadata, diagnostics);
    if (import_result != PORPOISE_EXIT_OK) {
        dtk_cleanup_stage(stage_path, diagnostics);
        return import_result;
    }
    if (porpoise_operation_cancelled(options->operation)) {
        dtk_cleanup_stage(stage_path, diagnostics);
        return PORPOISE_EXIT_CANCELLED;
    }

    porpoise_operation_progress(
        options->operation, PORPOISE_PHASE_PUBLISH, 0U, 1U,
        "atomically publish DTK cache");
    import_result = dtk_publish_cache(
        stage_path, options->cache_path, diagnostics);
    if (import_result != PORPOISE_EXIT_OK) {
        dtk_cleanup_stage(stage_path, diagnostics);
        return import_result;
    }
    porpoise_dtk_import_result_init(result);
    if (!porpoise_copy_string(
            result->validated_path, sizeof(result->validated_path),
            options->cache_path)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    result->metadata = metadata;
    porpoise_operation_progress(
        options->operation, PORPOISE_PHASE_PUBLISH, 1U, 1U,
        "DTK cache published");
    porpoise_operation_progress(
        options->operation, PORPOISE_PHASE_IMPORT, 8U, 8U,
        "managed DTK ELF import complete");
    return PORPOISE_EXIT_OK;
}

int porpoise_dtk_import_run(
    const PorpoiseDtkImportOptions *options,
    PorpoiseDtkImportResult *result,
    PorpoiseDiagnostics *diagnostics) {
    if (result != NULL) porpoise_dtk_import_result_init(result);
    if (options == NULL || result == NULL || diagnostics == NULL ||
        options->input_path == NULL || options->input_path[0] == '\0') {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_INTERNAL,
            options == NULL ? "" : options->input_path,
            "DTK import arguments are invalid");
    }
    if (porpoise_operation_cancelled(options->operation))
        return PORPOISE_EXIT_CANCELLED;
    if (options->source_kind == PORPOISE_DTK_SOURCE_PREPARED_ASM)
        return dtk_validate_prepared_options(options, result, diagnostics);
    if (options->source_kind != PORPOISE_DTK_SOURCE_MANAGED_ELF) {
        return dtk_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            options->input_path, "unknown DTK import source kind");
    }
    return dtk_import_managed(options, result, diagnostics);
}

int porpoise_dtk_validate_prepared(
    const char *path,
    bool require_link_order,
    const char *settings_identity,
    const PorpoiseOperationCallbacks *operation,
    PorpoiseDtkImportResult *result,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseDtkImportOptions options;
    porpoise_dtk_import_options_init(&options);
    options.source_kind = PORPOISE_DTK_SOURCE_PREPARED_ASM;
    options.input_path = path;
    options.settings_identity = settings_identity;
    options.prepared_require_link_order = require_link_order;
    options.operation = operation;
    return porpoise_dtk_import_run(&options, result, diagnostics);
}
