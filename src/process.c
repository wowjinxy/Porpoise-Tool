#ifndef _WIN32
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "process_internal.h"

#include "porpoise/util.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
#endif

typedef struct ProcessBuffer {
    char *text;
    size_t length;
    size_t capacity;
    size_t start;
    bool truncated;
} ProcessBuffer;

static int process_diagnostic(
    PorpoiseDiagnostics *diagnostics,
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
            diagnostics, PORPOISE_SEVERITY_ERROR,
            path == NULL ? "" : path, 0U, 0U, "%s", message)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    return result;
}

static bool process_buffer_reserve(ProcessBuffer *buffer, size_t minimum) {
    const size_t limit = (size_t)PORPOISE_PROCESS_CAPTURE_LIMIT_BYTES;
    size_t capacity;
    char *replacement;
    if (minimum > limit) return false;
    if (minimum <= buffer->capacity) return true;
    capacity = buffer->capacity == 0U ? 4096U : buffer->capacity;
    while (capacity < minimum) {
        if (capacity > limit / 2U) {
            capacity = limit;
            break;
        }
        capacity *= 2U;
    }
    replacement = (char *)realloc(buffer->text, capacity + 1U);
    if (replacement == NULL) return false;
    buffer->text = replacement;
    buffer->capacity = capacity;
    return true;
}

static bool process_buffer_append(
    ProcessBuffer *buffer,
    const char *text,
    size_t length) {
    const size_t limit = (size_t)PORPOISE_PROCESS_CAPTURE_LIMIT_BYTES;
    size_t write_at;
    size_t first;
    if (length == 0U) return true;
    if (length >= limit) {
        buffer->truncated = buffer->truncated || buffer->length != 0U ||
                            length > limit;
        if (!process_buffer_reserve(buffer, limit)) return false;
        if (limit != 0U) memcpy(buffer->text, text + length - limit, limit);
        buffer->length = limit;
        buffer->start = 0U;
        return true;
    }
    if (length > limit - buffer->length) {
        size_t discarded = length - (limit - buffer->length);
        if (!process_buffer_reserve(buffer, limit)) return false;
        buffer->start = (buffer->start + discarded) % limit;
        buffer->length -= discarded;
        buffer->truncated = true;
    } else if (!process_buffer_reserve(
                   buffer, buffer->length + length)) {
        return false;
    }
    write_at = (buffer->start + buffer->length) % buffer->capacity;
    first = buffer->capacity - write_at;
    if (first > length) first = length;
    memcpy(buffer->text + write_at, text, first);
    if (first != length)
        memcpy(buffer->text, text + first, length - first);
    buffer->length += length;
    return true;
}

static bool process_buffer_finish(ProcessBuffer *buffer) {
    char *linear;
    size_t first;
    if (buffer->text == NULL) {
        buffer->text = (char *)malloc(1U);
        if (buffer->text == NULL) return false;
        buffer->text[0] = '\0';
        return true;
    }
    if (buffer->start == 0U) {
        buffer->text[buffer->length] = '\0';
        return true;
    }
    linear = (char *)malloc(buffer->length + 1U);
    if (linear == NULL) return false;
    first = buffer->capacity - buffer->start;
    if (first > buffer->length) first = buffer->length;
    memcpy(linear, buffer->text + buffer->start, first);
    if (first != buffer->length)
        memcpy(linear + first, buffer->text, buffer->length - first);
    linear[buffer->length] = '\0';
    free(buffer->text);
    buffer->text = linear;
    buffer->capacity = buffer->length;
    buffer->start = 0U;
    return true;
}

static bool process_deliver(
    ProcessBuffer *buffer,
    const char *text,
    size_t length,
    bool standard_error,
    PorpoiseBuildPhase phase,
    const PorpoiseBuildCallbacks *callbacks) {
    if (!process_buffer_append(buffer, text, length)) return false;
    if (callbacks != NULL && callbacks->log != NULL && length != 0U) {
        callbacks->log(
            callbacks->user_data, phase, standard_error, text, length);
    }
    return true;
}

static bool process_cancelled(const PorpoiseBuildCallbacks *callbacks) {
    return callbacks != NULL && callbacks->cancelled != NULL &&
           callbacks->cancelled(callbacks->user_data);
}

void porpoise_process_capture_init(PorpoiseProcessCapture *capture) {
    if (capture != NULL) memset(capture, 0, sizeof(*capture));
}

void porpoise_process_capture_free(PorpoiseProcessCapture *capture) {
    if (capture == NULL) return;
    free(capture->standard_output);
    free(capture->standard_error);
    memset(capture, 0, sizeof(*capture));
}

#ifdef _WIN32
typedef struct ProcessCommandLine {
    char *text;
    size_t length;
    size_t capacity;
} ProcessCommandLine;

static bool process_command_append(
    ProcessCommandLine *line,
    const char *text,
    size_t length) {
    size_t required;
    char *replacement;
    size_t capacity;
    if (length > SIZE_MAX - line->length - 1U) return false;
    required = line->length + length + 1U;
    if (required > line->capacity) {
        capacity = line->capacity == 0U ? 256U : line->capacity;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2U) return false;
            capacity *= 2U;
        }
        replacement = (char *)realloc(line->text, capacity);
        if (replacement == NULL) return false;
        line->text = replacement;
        line->capacity = capacity;
    }
    memcpy(line->text + line->length, text, length);
    line->length += length;
    line->text[line->length] = '\0';
    return true;
}

static bool process_command_character(
    ProcessCommandLine *line,
    char character) {
    return process_command_append(line, &character, 1U);
}

static bool process_command_quote(
    ProcessCommandLine *line,
    const char *argument) {
    const char *cursor = argument;
    if (!process_command_character(line, '"')) return false;
    while (*cursor != '\0') {
        size_t slash_count = 0U;
        size_t index;
        while (*cursor == '\\') {
            slash_count++;
            cursor++;
        }
        if (*cursor == '\0') {
            for (index = 0U; index < slash_count * 2U; index++) {
                if (!process_command_character(line, '\\')) return false;
            }
            break;
        }
        if (*cursor == '"') {
            for (index = 0U; index < slash_count * 2U + 1U; index++) {
                if (!process_command_character(line, '\\')) return false;
            }
            if (!process_command_character(line, *cursor++)) return false;
        } else {
            for (index = 0U; index < slash_count; index++) {
                if (!process_command_character(line, '\\')) return false;
            }
            if (!process_command_character(line, *cursor++)) return false;
        }
    }
    return process_command_character(line, '"');
}

static char *process_build_command_line(const char *const *argv) {
    ProcessCommandLine line;
    size_t index;
    memset(&line, 0, sizeof(line));
    for (index = 0U; argv[index] != NULL; index++) {
        if (index != 0U && !process_command_character(&line, ' ')) {
            free(line.text);
            return NULL;
        }
        if (!process_command_quote(&line, argv[index])) {
            free(line.text);
            return NULL;
        }
    }
    return line.text;
}

static size_t process_environment_name_length(const char *entry) {
    const char *equals = strchr(entry, '=');
    return equals == NULL ? strlen(entry) : (size_t)(equals - entry);
}

static bool process_environment_overridden(
    const char *entry,
    const PorpoiseBuildEnvironmentEntry *overrides,
    size_t override_count) {
    size_t entry_length = process_environment_name_length(entry);
    size_t index;
    for (index = 0U; index < override_count; index++) {
        const char *name = overrides[index].name;
        if (name != NULL && strlen(name) == entry_length &&
            _strnicmp(entry, name, entry_length) == 0) return true;
    }
    return false;
}

static char *process_build_environment(
    const PorpoiseBuildEnvironmentEntry *overrides,
    size_t override_count) {
    LPCH inherited;
    const char *cursor;
    size_t size = 1U;
    size_t index;
    char *block;
    char *output;
    if (override_count == 0U) return NULL;
    inherited = GetEnvironmentStringsA();
    if (inherited == NULL) return NULL;
    cursor = inherited;
    while (*cursor != '\0') {
        size_t length = strlen(cursor) + 1U;
        if (!process_environment_overridden(cursor, overrides, override_count))
            size += length;
        cursor += length;
    }
    for (index = 0U; index < override_count; index++) {
        const char *name = overrides[index].name;
        const char *value = overrides[index].value;
        if (name == NULL || name[0] == '\0' || strchr(name, '=') != NULL ||
            value == NULL || strlen(name) > SIZE_MAX - strlen(value) - 2U) {
            FreeEnvironmentStringsA(inherited);
            return NULL;
        }
        size += strlen(name) + strlen(value) + 2U;
    }
    block = (char *)malloc(size);
    if (block == NULL) {
        FreeEnvironmentStringsA(inherited);
        return NULL;
    }
    output = block;
    cursor = inherited;
    while (*cursor != '\0') {
        size_t length = strlen(cursor) + 1U;
        if (!process_environment_overridden(cursor, overrides, override_count)) {
            memcpy(output, cursor, length);
            output += length;
        }
        cursor += length;
    }
    FreeEnvironmentStringsA(inherited);
    for (index = 0U; index < override_count; index++) {
        size_t name_length = strlen(overrides[index].name);
        size_t value_length = strlen(overrides[index].value);
        memcpy(output, overrides[index].name, name_length);
        output += name_length;
        *output++ = '=';
        memcpy(output, overrides[index].value, value_length);
        output += value_length;
        *output++ = '\0';
    }
    *output = '\0';
    return block;
}

static bool process_read_windows_pipe(
    HANDLE pipe,
    ProcessBuffer *buffer,
    bool standard_error,
    PorpoiseBuildPhase phase,
    const PorpoiseBuildCallbacks *callbacks,
    bool *open) {
    char chunk[4096];
    for (;;) {
        DWORD available = 0U;
        DWORD read_count = 0U;
        if (!PeekNamedPipe(pipe, NULL, 0U, NULL, &available, NULL)) {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE) {
                *open = false;
                return true;
            }
            return false;
        }
        if (available == 0U) return true;
        if (!ReadFile(
                pipe, chunk,
                available < sizeof(chunk) ? available : (DWORD)sizeof(chunk),
                &read_count, NULL)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                *open = false;
                return true;
            }
            return false;
        }
        if (read_count > (DWORD)sizeof(chunk)) return false;
        if (!process_deliver(
                buffer, chunk, (size_t)read_count, standard_error,
                phase, callbacks)) return false;
    }
}
#else
static size_t process_posix_environment_name_length(const char *entry) {
    const char *equals = strchr(entry, '=');
    return equals == NULL ? strlen(entry) : (size_t)(equals - entry);
}

static bool process_posix_environment_name_equals(
    const char *left,
    size_t left_length,
    const char *right) {
    return right != NULL && strlen(right) == left_length &&
           memcmp(left, right, left_length) == 0;
}

static bool process_posix_environment_overridden(
    const char *entry,
    const PorpoiseBuildEnvironmentEntry *overrides,
    size_t override_count) {
    size_t entry_length = process_posix_environment_name_length(entry);
    size_t index;
    for (index = 0U; index < override_count; index++) {
        if (process_posix_environment_name_equals(
                entry, entry_length, overrides[index].name)) return true;
    }
    return false;
}

static bool process_posix_override_is_last(
    const PorpoiseBuildEnvironmentEntry *overrides,
    size_t override_count,
    size_t candidate) {
    size_t name_length = strlen(overrides[candidate].name);
    size_t index;
    for (index = candidate + 1U; index < override_count; index++) {
        if (process_posix_environment_name_equals(
                overrides[candidate].name, name_length,
                overrides[index].name)) return false;
    }
    return true;
}

static void process_posix_environment_free(char **environment) {
    size_t index;
    if (environment == NULL) return;
    for (index = 0U; environment[index] != NULL; index++)
        free(environment[index]);
    free(environment);
}

static char **process_build_posix_environment(
    const PorpoiseBuildEnvironmentEntry *overrides,
    size_t override_count,
    int *error_out) {
    size_t inherited_count = 0U;
    size_t retained_count = 0U;
    size_t override_unique_count = 0U;
    size_t index;
    size_t output_index = 0U;
    char **output;
    if (error_out != NULL) *error_out = 0;
    for (index = 0U; index < override_count; index++) {
        const char *name = overrides[index].name;
        const char *value = overrides[index].value;
        if (name == NULL || name[0] == '\0' || strchr(name, '=') != NULL ||
            value == NULL || strlen(name) > SIZE_MAX - strlen(value) - 2U) {
            if (error_out != NULL) *error_out = EINVAL;
            return NULL;
        }
    }
    if (environ != NULL) {
        while (environ[inherited_count] != NULL) inherited_count++;
    }
    for (index = 0U; index < inherited_count; index++) {
        if (!process_posix_environment_overridden(
                environ[index], overrides, override_count)) retained_count++;
    }
    for (index = 0U; index < override_count; index++) {
        if (process_posix_override_is_last(
                overrides, override_count, index)) override_unique_count++;
    }
    if (override_unique_count == SIZE_MAX ||
        retained_count > SIZE_MAX - override_unique_count - 1U ||
        retained_count + override_unique_count + 1U >
            SIZE_MAX / sizeof(*output)) {
        if (error_out != NULL) *error_out = ENOMEM;
        return NULL;
    }
    output = (char **)calloc(
        retained_count + override_unique_count + 1U, sizeof(*output));
    if (output == NULL) {
        if (error_out != NULL) *error_out = ENOMEM;
        return NULL;
    }
    for (index = 0U; index < inherited_count; index++) {
        if (process_posix_environment_overridden(
                environ[index], overrides, override_count)) continue;
        output[output_index] = strdup(environ[index]);
        if (output[output_index] == NULL) goto allocation_failed;
        output_index++;
    }
    for (index = 0U; index < override_count; index++) {
        size_t name_length;
        size_t value_length;
        char *entry;
        if (!process_posix_override_is_last(
                overrides, override_count, index)) continue;
        name_length = strlen(overrides[index].name);
        value_length = strlen(overrides[index].value);
        entry = (char *)malloc(name_length + value_length + 2U);
        if (entry == NULL) goto allocation_failed;
        memcpy(entry, overrides[index].name, name_length);
        entry[name_length] = '=';
        memcpy(
            entry + name_length + 1U, overrides[index].value,
            value_length + 1U);
        output[output_index++] = entry;
    }
    return output;

allocation_failed:
    if (error_out != NULL) *error_out = ENOMEM;
    process_posix_environment_free(output);
    return NULL;
}

static int process_posix_pipe(int descriptors[2]) {
    size_t index;
    descriptors[0] = -1;
    descriptors[1] = -1;
#if defined(__linux__) && defined(O_CLOEXEC)
    if (pipe2(descriptors, O_CLOEXEC) != 0) {
        descriptors[0] = -1;
        descriptors[1] = -1;
        return -1;
    }
#else
    if (pipe(descriptors) != 0) {
        descriptors[0] = -1;
        descriptors[1] = -1;
        return -1;
    }
    for (index = 0U; index < 2U; index++) {
        int flags = fcntl(descriptors[index], F_GETFD);
        if (flags < 0 ||
            fcntl(descriptors[index], F_SETFD, flags | FD_CLOEXEC) < 0) {
            int saved_error = errno;
            close(descriptors[0]);
            close(descriptors[1]);
            descriptors[0] = -1;
            descriptors[1] = -1;
            errno = saved_error;
            return -1;
        }
    }
#endif
    /* Keep the file-actions close list independent from stdin/stdout/stderr. */
    for (index = 0U; index < 2U; index++) {
        if (descriptors[index] <= STDERR_FILENO) {
            int replacement = fcntl(
                descriptors[index], F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
            if (replacement < 0) {
                int saved_error = errno;
                close(descriptors[0]);
                close(descriptors[1]);
                descriptors[0] = -1;
                descriptors[1] = -1;
                errno = saved_error;
                return -1;
            }
            close(descriptors[index]);
            descriptors[index] = replacement;
        }
    }
    return 0;
}
#endif

int porpoise_process_run(
    const char *const *argv,
    const char *working_directory,
    const PorpoiseBuildEnvironmentEntry *environment,
    size_t environment_count,
    PorpoiseBuildPhase phase,
    const PorpoiseBuildCallbacks *callbacks,
    PorpoiseProcessCapture *capture,
    PorpoiseDiagnostics *diagnostics) {
    ProcessBuffer output;
    ProcessBuffer error;
    bool cancelled = false;
    int result = PORPOISE_EXIT_OK;
    if (argv == NULL || argv[0] == NULL || argv[0][0] == '\0' ||
        capture == NULL ||
        (environment_count != 0U && environment == NULL)) {
        return process_diagnostic(
            diagnostics, PORPOISE_EXIT_INTERNAL, "",
            "process runner arguments are invalid");
    }
    porpoise_process_capture_init(capture);
    memset(&output, 0, sizeof(output));
    memset(&error, 0, sizeof(error));

#ifdef _WIN32
    {
        SECURITY_ATTRIBUTES security;
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits;
        HANDLE output_read = NULL;
        HANDLE output_write = NULL;
        HANDLE error_read = NULL;
        HANDLE error_write = NULL;
        HANDLE job = NULL;
        char *command_line = NULL;
        char *environment_block = NULL;
        DWORD child_exit = 0U;
        bool output_open = true;
        bool error_open = true;
        BOOL created;

        memset(&security, 0, sizeof(security));
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;
        if (!CreatePipe(&output_read, &output_write, &security, 0U) ||
            !CreatePipe(&error_read, &error_write, &security, 0U) ||
            !SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0U) ||
            !SetHandleInformation(error_read, HANDLE_FLAG_INHERIT, 0U)) {
            result = process_diagnostic(
                diagnostics, PORPOISE_EXIT_IO, argv[0],
                "cannot create process capture pipes (Windows error %lu)",
                (unsigned long)GetLastError());
            goto windows_cleanup;
        }
        command_line = process_build_command_line(argv);
        if (environment_count != 0U)
            environment_block = process_build_environment(
                environment, environment_count);
        if (command_line == NULL ||
            (environment_count != 0U && environment_block == NULL)) {
            result = process_diagnostic(
                diagnostics, PORPOISE_EXIT_INTERNAL, argv[0],
                "cannot prepare shell-free process invocation");
            goto windows_cleanup;
        }
        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = output_write;
        startup.hStdError = error_write;
        created = CreateProcessA(
            NULL, command_line, NULL, NULL, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP,
            environment_block, working_directory, &startup, &process);
        if (!created) {
            result = process_diagnostic(
                diagnostics, PORPOISE_EXIT_IO, argv[0],
                "cannot launch process (Windows error %lu)",
                (unsigned long)GetLastError());
            goto windows_cleanup;
        }
        CloseHandle(output_write);
        output_write = NULL;
        CloseHandle(error_write);
        error_write = NULL;
        job = CreateJobObjectA(NULL, NULL);
        memset(&job_limits, 0, sizeof(job_limits));
        job_limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (job == NULL ||
            !SetInformationJobObject(
                job, JobObjectExtendedLimitInformation,
                &job_limits, sizeof(job_limits)) ||
            !AssignProcessToJobObject(job, process.hProcess)) {
            (void)TerminateProcess(process.hProcess, 1U);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            result = process_diagnostic(
                diagnostics, PORPOISE_EXIT_IO, argv[0],
                "cannot contain process in a cancellable job (Windows error %lu)",
                (unsigned long)GetLastError());
            goto windows_cleanup;
        }
        (void)ResumeThread(process.hThread);
        CloseHandle(process.hThread);
        for (;;) {
            DWORD wait_result;
            if (!process_read_windows_pipe(
                    output_read, &output, false, phase, callbacks,
                    &output_open) ||
                !process_read_windows_pipe(
                    error_read, &error, true, phase, callbacks,
                    &error_open)) {
                result = PORPOISE_EXIT_IO;
                (void)TerminateJobObject(job, 1U);
                break;
            }
            wait_result = WaitForSingleObject(process.hProcess, 25U);
            if (wait_result == WAIT_OBJECT_0) break;
            if (wait_result == WAIT_FAILED) {
                result = PORPOISE_EXIT_IO;
                (void)TerminateJobObject(job, 1U);
                break;
            }
            if (process_cancelled(callbacks)) {
                cancelled = true;
                (void)TerminateJobObject(job, 1U);
                (void)WaitForSingleObject(process.hProcess, INFINITE);
                break;
            }
        }
        (void)process_read_windows_pipe(
            output_read, &output, false, phase, callbacks, &output_open);
        (void)process_read_windows_pipe(
            error_read, &error, true, phase, callbacks, &error_open);
        if (!GetExitCodeProcess(process.hProcess, &child_exit))
            result = PORPOISE_EXIT_IO;
        capture->exit_code = child_exit > (DWORD)INT_MAX ? -1 : (int)child_exit;
        CloseHandle(process.hProcess);

windows_cleanup:
        if (job != NULL) CloseHandle(job);
        if (output_read != NULL) CloseHandle(output_read);
        if (output_write != NULL) CloseHandle(output_write);
        if (error_read != NULL) CloseHandle(error_read);
        if (error_write != NULL) CloseHandle(error_write);
        free(command_line);
        free(environment_block);
    }
#else
    {
        int output_pipe[2] = {-1, -1};
        int error_pipe[2] = {-1, -1};
        pid_t child = -1;
        int wait_status = 0;
        int environment_error = 0;
        int spawn_error = 0;
        int output_flags;
        int error_flags;
        bool child_done = false;
        bool have_wait_status = false;
        bool output_open = true;
        bool error_open = true;
        bool termination_started = false;
        bool force_killed = false;
        struct timespec termination_deadline;
        char **child_environment = NULL;
        posix_spawn_file_actions_t file_actions;
        posix_spawnattr_t spawn_attributes;
        bool file_actions_initialized = false;
        bool spawn_attributes_initialized = false;
        short spawn_flags = POSIX_SPAWN_SETPGROUP;
        if (process_posix_pipe(output_pipe) != 0 ||
            process_posix_pipe(error_pipe) != 0) {
            int saved_error = errno;
            result = process_diagnostic(
                diagnostics, PORPOISE_EXIT_IO, argv[0],
                "cannot create process capture pipes: %s",
                strerror(saved_error));
            goto posix_done;
        }
        child_environment = process_build_posix_environment(
            environment, environment_count, &environment_error);
        if (child_environment == NULL) {
            result = process_diagnostic(
                diagnostics, PORPOISE_EXIT_INTERNAL, argv[0],
                "cannot prepare process environment: %s",
                strerror(environment_error == 0 ? ENOMEM : environment_error));
            goto posix_done;
        }
        spawn_error = posix_spawn_file_actions_init(&file_actions);
        if (spawn_error == 0) file_actions_initialized = true;
        if (spawn_error == 0 && working_directory != NULL &&
            working_directory[0] != '\0') {
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
            spawn_error = posix_spawn_file_actions_addchdir_np(
                &file_actions, working_directory);
#else
            spawn_error = ENOTSUP;
#endif
        }
        if (spawn_error == 0)
            spawn_error = posix_spawn_file_actions_adddup2(
                &file_actions, output_pipe[1], STDOUT_FILENO);
        if (spawn_error == 0)
            spawn_error = posix_spawn_file_actions_adddup2(
                &file_actions, error_pipe[1], STDERR_FILENO);
        if (spawn_error == 0)
            spawn_error = posix_spawn_file_actions_addclose(
                &file_actions, output_pipe[0]);
        if (spawn_error == 0)
            spawn_error = posix_spawn_file_actions_addclose(
                &file_actions, output_pipe[1]);
        if (spawn_error == 0)
            spawn_error = posix_spawn_file_actions_addclose(
                &file_actions, error_pipe[0]);
        if (spawn_error == 0)
            spawn_error = posix_spawn_file_actions_addclose(
                &file_actions, error_pipe[1]);
        if (spawn_error == 0) {
            spawn_error = posix_spawnattr_init(&spawn_attributes);
            if (spawn_error == 0) spawn_attributes_initialized = true;
        }
        if (spawn_error == 0)
            spawn_error = posix_spawnattr_setpgroup(&spawn_attributes, 0);
        if (spawn_error == 0)
            spawn_error = posix_spawnattr_setflags(
                &spawn_attributes, spawn_flags);
        if (spawn_error == 0) {
            spawn_error = posix_spawnp(
                &child, argv[0], &file_actions, &spawn_attributes,
                (char *const *)argv, child_environment);
        }
        if (spawn_attributes_initialized) {
            (void)posix_spawnattr_destroy(&spawn_attributes);
            spawn_attributes_initialized = false;
        }
        if (file_actions_initialized) {
            (void)posix_spawn_file_actions_destroy(&file_actions);
            file_actions_initialized = false;
        }
        process_posix_environment_free(child_environment);
        child_environment = NULL;
        if (spawn_error != 0) {
            result = process_diagnostic(
                diagnostics, PORPOISE_EXIT_IO, argv[0],
                "cannot launch process: %s", strerror(spawn_error));
            goto posix_done;
        }
        close(output_pipe[1]);
        output_pipe[1] = -1;
        close(error_pipe[1]);
        error_pipe[1] = -1;
        output_flags = fcntl(output_pipe[0], F_GETFL);
        error_flags = fcntl(error_pipe[0], F_GETFL);
        if (output_flags < 0 || error_flags < 0 ||
            fcntl(output_pipe[0], F_SETFL, output_flags | O_NONBLOCK) < 0 ||
            fcntl(error_pipe[0], F_SETFL, error_flags | O_NONBLOCK) < 0) {
            result = process_diagnostic(
                diagnostics, PORPOISE_EXIT_IO, argv[0],
                "cannot configure process capture pipes: %s",
                strerror(errno));
            (void)kill(-child, SIGKILL);
            force_killed = true;
        }
        while (!child_done || output_open || error_open) {
            struct pollfd descriptors[2];
            int polled;
            size_t index;
            if (!termination_started && process_cancelled(callbacks)) {
                cancelled = true;
                termination_started = true;
                (void)kill(-child, SIGTERM);
                if (clock_gettime(CLOCK_MONOTONIC, &termination_deadline) != 0) {
                    termination_deadline.tv_sec = 0;
                    termination_deadline.tv_nsec = 0;
                } else {
                    termination_deadline.tv_nsec += 250000000L;
                    if (termination_deadline.tv_nsec >= 1000000000L) {
                        termination_deadline.tv_sec++;
                        termination_deadline.tv_nsec -= 1000000000L;
                    }
                }
            }
            if (termination_started && !force_killed) {
                struct timespec now;
                if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
                    now.tv_sec > termination_deadline.tv_sec ||
                    (now.tv_sec == termination_deadline.tv_sec &&
                     now.tv_nsec >= termination_deadline.tv_nsec)) {
                    (void)kill(-child, SIGKILL);
                    force_killed = true;
                }
            }
            descriptors[0].fd = output_open ? output_pipe[0] : -1;
            descriptors[0].events = POLLIN | POLLHUP;
            descriptors[0].revents = 0;
            descriptors[1].fd = error_open ? error_pipe[0] : -1;
            descriptors[1].events = POLLIN | POLLHUP;
            descriptors[1].revents = 0;
            polled = poll(descriptors, 2, 25);
            if (polled < 0 && errno != EINTR) {
                result = PORPOISE_EXIT_IO;
                (void)kill(-child, SIGKILL);
                force_killed = true;
            }
            for (index = 0U; index < 2U; index++) {
                ProcessBuffer *buffer = index == 0U ? &output : &error;
                bool *open = index == 0U ? &output_open : &error_open;
                int descriptor = index == 0U ? output_pipe[0] : error_pipe[0];
                if (!*open ||
                    (descriptors[index].revents & (POLLIN | POLLHUP)) == 0)
                    continue;
                {
                    size_t drained_chunks = 0U;
                    for (;;) {
                        char chunk[4096];
                        ssize_t count = read(
                            descriptor, chunk, sizeof(chunk));
                        if (count > 0) {
                            if (!process_deliver(
                                    buffer, chunk, (size_t)count, index != 0U,
                                    phase, callbacks)) {
                                result = PORPOISE_EXIT_INTERNAL;
                                (void)kill(-child, SIGKILL);
                                force_killed = true;
                                break;
                            }
                            drained_chunks++;
                            if (drained_chunks == 16U) break;
                        } else if (count == 0) {
                            *open = false;
                            close(descriptor);
                            break;
                        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        } else {
                            result = PORPOISE_EXIT_IO;
                            *open = false;
                            close(descriptor);
                            (void)kill(-child, SIGKILL);
                            force_killed = true;
                            break;
                        }
                    }
                }
            }
            if (!child_done) {
                pid_t waited = waitpid(child, &wait_status, WNOHANG);
                if (waited == child) {
                    child_done = true;
                    have_wait_status = true;
                } else if (waited < 0 && errno != EINTR) {
                    result = PORPOISE_EXIT_IO;
                    child_done = true;
                }
            }
            if (force_killed && child_done) {
                if (output_open) {
                    close(output_pipe[0]);
                    output_open = false;
                }
                if (error_open) {
                    close(error_pipe[0]);
                    error_open = false;
                }
            }
        }
        if (!child_done) {
            pid_t waited;
            do {
                waited = waitpid(child, &wait_status, 0);
            } while (waited < 0 && errno == EINTR);
            if (waited == child) have_wait_status = true;
        }
        if (have_wait_status && WIFEXITED(wait_status))
            capture->exit_code = WEXITSTATUS(wait_status);
        else capture->exit_code = -1;

posix_done:
        if (spawn_attributes_initialized)
            (void)posix_spawnattr_destroy(&spawn_attributes);
        if (file_actions_initialized)
            (void)posix_spawn_file_actions_destroy(&file_actions);
        process_posix_environment_free(child_environment);
        if (output_open && output_pipe[0] >= 0) close(output_pipe[0]);
        if (output_pipe[1] >= 0) close(output_pipe[1]);
        if (error_open && error_pipe[0] >= 0) close(error_pipe[0]);
        if (error_pipe[1] >= 0) close(error_pipe[1]);
    }
#endif

    if (!process_buffer_finish(&output)) {
        free(output.text);
        output.text = NULL;
        result = PORPOISE_EXIT_INTERNAL;
    }
    if (!process_buffer_finish(&error)) {
        free(error.text);
        error.text = NULL;
        result = PORPOISE_EXIT_INTERNAL;
    }
    capture->standard_output = output.text;
    capture->standard_error = error.text;
    capture->standard_output_truncated = output.truncated;
    capture->standard_error_truncated = error.truncated;
    if (cancelled) return PORPOISE_EXIT_CANCELLED;
    if (result != PORPOISE_EXIT_OK) {
        return process_diagnostic(
            diagnostics, result, argv[0],
            "process execution or output capture failed");
    }
    return PORPOISE_EXIT_OK;
}
