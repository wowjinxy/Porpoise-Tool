#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "porpoise/build.h"

#include "porpoise/options.h"
#include "porpoise/sha256.h"
#include "porpoise/util.h"
#include "process_internal.h"
#include "jsmn.h"

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
#include <process.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#define BUILD_CHMOD_EXECUTABLE(path) ((void)(path))
#define BUILD_PROCESS_ID() ((unsigned long)_getpid())
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2U
#endif
#else
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>
#define BUILD_CHMOD_EXECUTABLE(path) ((void)chmod((path), 0755))
#define BUILD_PROCESS_ID() ((unsigned long)getpid())
#endif

#define BUILD_MARKER_FILE ".porpoise-build-cache-v1"
#define BUILD_MANIFEST_FILE "porpoise-build-manifest.json"
#define BUILD_NATIVE_FILE "porpoise-native.ini"
#define BUILD_DEPENDENCIES_DIRECTORY "dependencies"
#define BUILD_SDL2_OVERLAY_DIRECTORY "sdl2"
#define BUILD_SDL2_IMPORT_LIBRARY "libSDL2.dll.a"
#define BUILD_MAX_PROCESS_ARGUMENTS 32U
#define BUILD_MAX_RUNTIME_FILES 256U
#define BUILD_MAX_IDENTITY_DEPTH 64U

typedef struct BuildCompilerIdentity {
    char path[PORPOISE_PATH_CAPACITY];
    char family[PORPOISE_BUILD_VERSION_CAPACITY];
    char version[PORPOISE_BUILD_VERSION_CAPACITY];
    char target[PORPOISE_BUILD_VERSION_CAPACITY];
} BuildCompilerIdentity;

typedef struct BuildRuntimeQueue {
    char **names;
    size_t count;
    size_t capacity;
} BuildRuntimeQueue;

static const PorpoiseBuildEnvironmentEntry build_clean_toolchain_environment[] = {
    {"CFLAGS", ""},
    {"CXXFLAGS", ""},
    {"CPPFLAGS", ""},
    {"LDFLAGS", ""}
};

static int build_diagnostic(
    PorpoiseDiagnostics *diagnostics,
    PorpoiseSeverity severity,
    int result,
    const char *path,
    const char *format,
    ...);
static void build_progress(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildPhase phase,
    size_t completed,
    size_t total,
    const char *detail);
static const char *build_default(const char *value, const char *fallback);
static bool build_cancelled(const PorpoiseBuildRequest *request);
static bool build_is_file(const char *path);
static int build_validate_managed_cache_root(
    const PorpoiseBuildRequest *request,
    PorpoiseDiagnostics *diagnostics);
static int build_validate_managed_cache_path(
    const PorpoiseBuildRequest *request,
    const char *path,
    PorpoiseDiagnostics *diagnostics);
static int build_validate_managed_cache_layout(
    const PorpoiseBuildRequest *request,
    const PorpoiseBuildResult *result,
    PorpoiseDiagnostics *diagnostics);
static int build_run_process(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildPhase phase,
    const char *const *argv,
    const char *working_directory,
    const PorpoiseBuildEnvironmentEntry *environment,
    size_t environment_count,
    PorpoiseProcessCapture *capture,
    PorpoiseDiagnostics *diagnostics,
    const char *description);
static int build_configuration_digest(
    const PorpoiseBuildRequest *request,
    const PorpoiseBuildPreflight *preflight,
    char output[PORPOISE_BUILD_ID_CAPACITY + 1U],
    char generated_output_identity[PORPOISE_BUILD_ID_CAPACITY + 1U],
    char libporpoise_identity[PORPOISE_BUILD_ID_CAPACITY + 1U],
    char sdl2_dependency_identity[PORPOISE_BUILD_ID_CAPACITY + 1U],
    char title_host_identity[PORPOISE_BUILD_ID_CAPACITY + 1U],
    PorpoiseDiagnostics *diagnostics);
static int build_prepare_layout(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildResult *result,
    PorpoiseDiagnostics *diagnostics);
static int build_write_native_file(
    const PorpoiseBuildRequest *request,
    const PorpoiseBuildResult *result,
    char path[PORPOISE_PATH_CAPACITY],
    PorpoiseDiagnostics *diagnostics);
static int build_discover_executable(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildResult *result,
    char discovered[PORPOISE_PATH_CAPACITY],
    PorpoiseDiagnostics *diagnostics);
static int build_copy_executable(
    const PorpoiseBuildRequest *request,
    const char *source,
    PorpoiseBuildResult *result,
    PorpoiseDiagnostics *diagnostics);
static bool build_hash_file_hex(
    const char *path,
    char hex[PORPOISE_SHA256_HEX_SIZE]);
#ifdef _WIN32
static void build_runtime_queue_free(BuildRuntimeQueue *queue);
static bool build_find_objdump(
    const PorpoiseBuildRequest *request,
    const PorpoiseBuildPreflight *preflight,
    char output[PORPOISE_PATH_CAPACITY]);
static int build_collect_imports(
    const PorpoiseBuildRequest *request,
    const char *objdump,
    const char *binary,
    BuildRuntimeQueue *queue,
    PorpoiseDiagnostics *diagnostics);
static bool build_resolve_runtime_file(
    const PorpoiseBuildRequest *request,
    const PorpoiseBuildResult *result,
    const char *name,
    char output[PORPOISE_PATH_CAPACITY],
    PorpoiseDiagnostics *diagnostics);
#endif

static int build_diagnostic(
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
            0U, 0U, "%s", message)) return PORPOISE_EXIT_INTERNAL;
    return result;
}

static void build_progress(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildPhase phase,
    size_t completed,
    size_t total,
    const char *detail) {
    if (request != NULL && request->callbacks.progress != NULL) {
        request->callbacks.progress(
            request->callbacks.user_data, phase, completed, total,
            detail == NULL ? "" : detail);
    }
}

static bool build_cancelled(const PorpoiseBuildRequest *request) {
    return request != NULL && request->callbacks.cancelled != NULL &&
           request->callbacks.cancelled(request->callbacks.user_data);
}

static const char *build_default(const char *value, const char *fallback) {
    return value == NULL || value[0] == '\0' ? fallback : value;
}

void porpoise_build_request_init(PorpoiseBuildRequest *request) {
    if (request == NULL) return;
    memset(request, 0, sizeof(*request));
    request->meson_executable = "meson";
    request->c_compiler = "cc";
    request->cpp_compiler = "c++";
    request->build_type = "debugoptimized";
}

void porpoise_build_result_init(PorpoiseBuildResult *result) {
    if (result != NULL) memset(result, 0, sizeof(*result));
}

const char *porpoise_build_phase_name(PorpoiseBuildPhase phase) {
    switch (phase) {
    case PORPOISE_BUILD_PHASE_PREFLIGHT: return "preflight";
    case PORPOISE_BUILD_PHASE_BIND_DEPENDENCIES: return "bind dependencies";
    case PORPOISE_BUILD_PHASE_CONFIGURE: return "configure";
    case PORPOISE_BUILD_PHASE_COMPILE: return "compile";
    case PORPOISE_BUILD_PHASE_STAGE_RUNTIME: return "stage runtime";
    case PORPOISE_BUILD_PHASE_RUN: return "run";
    }
    return "unknown";
}

static bool build_valid_target_id(const char *value) {
    return porpoise_recovery_target_id_is_valid(value);
}

static bool build_valid_build_type(const char *value) {
    return strcmp(value, "plain") == 0 || strcmp(value, "debug") == 0 ||
           strcmp(value, "debugoptimized") == 0 ||
           strcmp(value, "release") == 0 || strcmp(value, "minsize") == 0;
}

static bool build_is_file(const char *path) {
    struct stat status;
    return path != NULL && stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

#ifdef _WIN32
static bool build_sdl2_source_paths(
    const PorpoiseBuildRequest *request,
    char headers[PORPOISE_PATH_CAPACITY],
    char import_library[PORPOISE_PATH_CAPACITY]) {
    char ucrt_root[PORPOISE_PATH_CAPACITY];
    char include_root[PORPOISE_PATH_CAPACITY];
    char library_root[PORPOISE_PATH_CAPACITY];
    return porpoise_path_join(
               ucrt_root, sizeof(ucrt_root),
               request->libporpoise_directory, "msys2/ucrt64") &&
           porpoise_path_join(
               include_root, sizeof(include_root), ucrt_root, "include") &&
           porpoise_path_join(
               headers, PORPOISE_PATH_CAPACITY, include_root, "SDL2") &&
           porpoise_path_join(
               library_root, sizeof(library_root), ucrt_root, "lib") &&
           porpoise_path_join(
               import_library, PORPOISE_PATH_CAPACITY, library_root,
               BUILD_SDL2_IMPORT_LIBRARY);
}

static bool build_sdl2_runtime_path(
    const PorpoiseBuildRequest *request,
    char runtime_library[PORPOISE_PATH_CAPACITY]) {
    char runtime_root[PORPOISE_PATH_CAPACITY];
    return porpoise_path_join(
               runtime_root, sizeof(runtime_root),
               request->libporpoise_directory, "msys2/ucrt64/bin") &&
           porpoise_path_join(
               runtime_library, PORPOISE_PATH_CAPACITY, runtime_root,
               "SDL2.dll");
}

static bool build_sdl2_overlay_paths(
    const PorpoiseBuildResult *result,
    char overlay[PORPOISE_PATH_CAPACITY],
    char include_root[PORPOISE_PATH_CAPACITY],
    char library_root[PORPOISE_PATH_CAPACITY]) {
    char dependencies[PORPOISE_PATH_CAPACITY];
    return porpoise_path_join(
               dependencies, sizeof(dependencies), result->cache_directory,
               BUILD_DEPENDENCIES_DIRECTORY) &&
           porpoise_path_join(
               overlay, PORPOISE_PATH_CAPACITY, dependencies,
               BUILD_SDL2_OVERLAY_DIRECTORY) &&
           porpoise_path_join(
               include_root, PORPOISE_PATH_CAPACITY, overlay, "include") &&
           porpoise_path_join(
               library_root, PORPOISE_PATH_CAPACITY, overlay, "lib");
}
#endif

static bool build_path_has_separator(const char *path) {
    return path != NULL &&
           (strchr(path, '/') != NULL || strchr(path, '\\') != NULL);
}

static bool build_resolve_command(
    const char *selection,
    char output[PORPOISE_PATH_CAPACITY]) {
#ifdef _WIN32
    char resolved[PORPOISE_PATH_CAPACITY];
    DWORD length;
    if (build_path_has_separator(selection)) {
        return GetFullPathNameA(
                   selection, PORPOISE_PATH_CAPACITY, output, NULL) != 0U &&
               build_is_file(output);
    }
    length = SearchPathA(NULL, selection, ".exe", sizeof(resolved), resolved, NULL);
    if (length == 0U || length >= sizeof(resolved))
        length = SearchPathA(NULL, selection, NULL, sizeof(resolved), resolved, NULL);
    return length != 0U && length < sizeof(resolved) &&
           porpoise_copy_string(output, PORPOISE_PATH_CAPACITY, resolved);
#else
    const char *path;
    const char *cursor;
    if (build_path_has_separator(selection))
        return realpath(selection, output) != NULL && access(output, X_OK) == 0;
    path = getenv("PATH");
    if (path == NULL) return false;
    cursor = path;
    for (;;) {
        const char *separator = strchr(cursor, ':');
        size_t length = separator == NULL ? strlen(cursor) :
            (size_t)(separator - cursor);
        char directory[PORPOISE_PATH_CAPACITY];
        char candidate[PORPOISE_PATH_CAPACITY];
        if (length == 0U) {
            if (!porpoise_copy_string(directory, sizeof(directory), "."))
                return false;
        } else if (length >= sizeof(directory)) {
            if (separator == NULL) break;
            cursor = separator + 1U;
            continue;
        } else {
            memcpy(directory, cursor, length);
            directory[length] = '\0';
        }
        if (porpoise_path_join(
                candidate, sizeof(candidate), directory, selection) &&
            access(candidate, X_OK) == 0 && realpath(candidate, output) != NULL)
            return true;
        if (separator == NULL) break;
        cursor = separator + 1U;
    }
    return false;
#endif
}

static int build_run_process(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildPhase phase,
    const char *const *argv,
    const char *working_directory,
    const PorpoiseBuildEnvironmentEntry *environment,
    size_t environment_count,
    PorpoiseProcessCapture *capture,
    PorpoiseDiagnostics *diagnostics,
    const char *description) {
    int result = porpoise_process_run(
        argv, working_directory, environment, environment_count, phase,
        &request->callbacks, capture, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (capture->exit_code != 0) {
        const char *detail = capture->standard_error != NULL &&
                             capture->standard_error[0] != '\0' ?
            capture->standard_error : capture->standard_output;
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_TRANSLATION,
            argv[0], "%s failed with exit code %d%s%s", description,
            capture->exit_code,
            detail != NULL && detail[0] != '\0' ? ": " : "",
            detail != NULL && detail[0] != '\0' ? detail : "");
    }
    return PORPOISE_EXIT_OK;
}

static void build_first_line(
    const char *text,
    char output[PORPOISE_BUILD_VERSION_CAPACITY]) {
    size_t length = 0U;
    while (text != NULL && text[length] != '\0' &&
           text[length] != '\r' && text[length] != '\n' &&
           length + 1U < PORPOISE_BUILD_VERSION_CAPACITY) {
        output[length] = text[length];
        length++;
    }
    output[length] = '\0';
    porpoise_trim(output);
}

static bool build_parse_semver(const char *text, unsigned int values[3]) {
    const char *cursor = text;
    bool found = false;
    while (cursor != NULL && *cursor != '\0') {
        unsigned int candidate[3] = {0U, 0U, 0U};
        const char *component = cursor;
        size_t index;
        bool valid = true;
        if (!isdigit((unsigned char)*cursor)) {
            cursor++;
            continue;
        }
        for (index = 0U; index < 3U; index++) {
            unsigned long value = 0UL;
            if (!isdigit((unsigned char)*component)) {
                valid = false;
                break;
            }
            while (isdigit((unsigned char)*component)) {
                value = value * 10UL +
                        (unsigned long)(*component - '0');
                if (value > 999999UL) {
                    valid = false;
                    break;
                }
                component++;
            }
            if (!valid) break;
            candidate[index] = (unsigned int)value;
            if (index == 0U && *component != '.') {
                valid = false;
                break;
            }
            if (*component != '.') break;
            component++;
        }
        if (valid) {
            memcpy(values, candidate, sizeof(candidate));
            found = true;
            cursor = component;
        } else {
            cursor++;
        }
    }
    return found;
}

static void build_numeric_version(
    const char *text,
    char output[PORPOISE_BUILD_VERSION_CAPACITY]) {
    unsigned int values[3];
    if (!build_parse_semver(text, values) ||
        !porpoise_format(
            output, PORPOISE_BUILD_VERSION_CAPACITY, "%u.%u.%u",
            values[0], values[1], values[2])) output[0] = '\0';
}

static int build_inspect_compiler(
    const PorpoiseBuildRequest *request,
    const char *selection,
    BuildCompilerIdentity *identity,
    PorpoiseDiagnostics *diagnostics) {
    const char *version_argv[3];
    const char *target_argv[3];
    PorpoiseProcessCapture capture;
    char first_line[PORPOISE_BUILD_VERSION_CAPACITY];
    int result;
    if (!build_resolve_command(selection, identity->path)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            selection, "compiler '%s' did not resolve to an executable",
            selection);
    }
    version_argv[0] = identity->path;
    version_argv[1] = "--version";
    version_argv[2] = NULL;
    porpoise_process_capture_init(&capture);
    result = build_run_process(
        request, PORPOISE_BUILD_PHASE_PREFLIGHT, version_argv, NULL,
        NULL, 0U, &capture, diagnostics, "compiler version query");
    if (result != PORPOISE_EXIT_OK) {
        porpoise_process_capture_free(&capture);
        return result;
    }
    build_first_line(capture.standard_output, first_line);
    if (strstr(first_line, "clang") != NULL ||
        strstr(first_line, "Clang") != NULL) {
        porpoise_copy_string(identity->family, sizeof(identity->family), "clang");
    } else if (strstr(first_line, "gcc") != NULL ||
               strstr(first_line, "GCC") != NULL ||
               strstr(first_line, "g++") != NULL ||
               strstr(first_line, "G++") != NULL ||
               strstr(first_line, "c++ (") != NULL ||
               strstr(first_line, "Free Software Foundation") != NULL) {
        porpoise_copy_string(identity->family, sizeof(identity->family), "gcc");
    } else {
        porpoise_process_capture_free(&capture);
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            identity->path,
            "unsupported compiler family in version string '%s'; select a matched GCC or Clang C/C++ pair",
            first_line);
    }
    build_numeric_version(first_line, identity->version);
    porpoise_process_capture_free(&capture);
    if (identity->version[0] == '\0') {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            identity->path, "cannot parse compiler version");
    }
    target_argv[0] = identity->path;
    target_argv[1] = "-dumpmachine";
    target_argv[2] = NULL;
    porpoise_process_capture_init(&capture);
    result = build_run_process(
        request, PORPOISE_BUILD_PHASE_PREFLIGHT, target_argv, NULL,
        NULL, 0U, &capture, diagnostics, "compiler target query");
    if (result == PORPOISE_EXIT_OK)
        build_first_line(capture.standard_output, identity->target);
    porpoise_process_capture_free(&capture);
    return result;
}

static bool build_same_parent(const char *left, const char *right) {
    char left_parent[PORPOISE_PATH_CAPACITY];
    char right_parent[PORPOISE_PATH_CAPACITY];
    if (!porpoise_path_parent(left_parent, sizeof(left_parent), left) ||
        !porpoise_path_parent(right_parent, sizeof(right_parent), right))
        return false;
#ifdef _WIN32
    return _stricmp(left_parent, right_parent) == 0;
#else
    return strcmp(left_parent, right_parent) == 0;
#endif
}

static int build_managed_cache_root_path(
    const PorpoiseBuildRequest *request,
    char root[PORPOISE_PATH_CAPACITY],
    PorpoiseDiagnostics *diagnostics) {
    char project_parent[PORPOISE_PATH_CAPACITY];
    char candidate[PORPOISE_PATH_CAPACITY];
    if (request == NULL || request->project_file == NULL ||
        !porpoise_path_parent(
            project_parent, sizeof(project_parent), request->project_file) ||
        !porpoise_path_join(
            candidate, sizeof(candidate), project_parent,
            ".porpoise-build") ||
        !porpoise_path_normalize_lexical(
            root, PORPOISE_PATH_CAPACITY, candidate)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            request == NULL || request->project_file == NULL ? "" :
                request->project_file,
            "cannot compute the project-adjacent build cache safely");
    }
    return PORPOISE_EXIT_OK;
}

static int build_validate_managed_cache_root(
    const PorpoiseBuildRequest *request,
    PorpoiseDiagnostics *diagnostics) {
    char root[PORPOISE_PATH_CAPACITY];
    int status = build_managed_cache_root_path(
        request, root, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
#ifdef _WIN32
    {
        DWORD attributes = GetFileAttributesA(root);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND ||
                error == ERROR_PATH_NOT_FOUND) return PORPOISE_EXIT_OK;
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                root,
                "cannot inspect the managed build cache root (Windows error %lu)",
                (unsigned long)error);
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR,
                PORPOISE_EXIT_USAGE, root,
                "managed build cache root must be an ordinary directory, not a symbolic link, junction, or reparse point; remove it and retry");
        }
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR,
                PORPOISE_EXIT_USAGE, root,
                "managed build cache root exists but is not a directory; remove it and retry");
        }
    }
#else
    {
        struct stat root_status;
        if (lstat(root, &root_status) != 0) {
            if (errno == ENOENT) return PORPOISE_EXIT_OK;
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                root, "cannot inspect the managed build cache root: %s",
                strerror(errno));
        }
        if (S_ISLNK(root_status.st_mode)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR,
                PORPOISE_EXIT_USAGE, root,
                "managed build cache root must be an ordinary directory, not a symbolic link; remove it and retry");
        }
        if (!S_ISDIR(root_status.st_mode)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR,
                PORPOISE_EXIT_USAGE, root,
                "managed build cache root exists but is not a directory; remove it and retry");
        }
    }
#endif
    return PORPOISE_EXIT_OK;
}

static bool build_cache_paths_equal(
    const char *left,
    const char *right) {
    size_t index = 0U;
    if (left == NULL || right == NULL) return false;
    while (left[index] != '\0' && right[index] != '\0') {
        unsigned char left_character = (unsigned char)left[index];
        unsigned char right_character = (unsigned char)right[index];
#ifdef _WIN32
        if ((left_character == '/' || left_character == '\\') &&
            (right_character == '/' || right_character == '\\')) {
            index++;
            continue;
        }
        if (tolower(left_character) != tolower(right_character))
            return false;
#else
        if (left_character != right_character) return false;
#endif
        index++;
    }
    return left[index] == '\0' && right[index] == '\0';
}

static int build_validate_managed_cache_component(
    const char *path,
    PorpoiseDiagnostics *diagnostics) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return PORPOISE_EXIT_OK;
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path,
            "cannot inspect a managed build cache directory (Windows error %lu)",
            (unsigned long)error);
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            PORPOISE_EXIT_USAGE, path,
            "managed build cache paths must be ordinary directories, not symbolic links, junctions, or reparse points; remove this path and retry");
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            PORPOISE_EXIT_USAGE, path,
            "managed build cache paths must be ordinary directories; remove this non-directory path and retry");
    }
#else
    struct stat path_status;
    if (lstat(path, &path_status) != 0) {
        if (errno == ENOENT) return PORPOISE_EXIT_OK;
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot inspect a managed build cache directory: %s",
            strerror(errno));
    }
    if (S_ISLNK(path_status.st_mode)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            PORPOISE_EXIT_USAGE, path,
            "managed build cache paths must be ordinary directories, not symbolic links; remove this path and retry");
    }
    if (!S_ISDIR(path_status.st_mode)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            PORPOISE_EXIT_USAGE, path,
            "managed build cache paths must be ordinary directories; remove this non-directory path and retry");
    }
#endif
    return PORPOISE_EXIT_OK;
}

static int build_validate_managed_cache_path(
    const PorpoiseBuildRequest *request,
    const char *path,
    PorpoiseDiagnostics *diagnostics) {
    char root[PORPOISE_PATH_CAPACITY];
    char normalized_root[PORPOISE_PATH_CAPACITY];
    char current[PORPOISE_PATH_CAPACITY];
    char parent[PORPOISE_PATH_CAPACITY];
    size_t depth;
    int status = build_managed_cache_root_path(
        request, root, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (path == NULL || path[0] == '\0' ||
        !porpoise_path_normalize_lexical(
            normalized_root, sizeof(normalized_root), root) ||
        !porpoise_path_normalize_lexical(
            current, sizeof(current), path)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            PORPOISE_EXIT_INTERNAL, path == NULL ? "" : path,
            "cannot normalize a managed build cache path safely");
    }
    for (depth = 0U; depth < BUILD_MAX_IDENTITY_DEPTH; depth++) {
        status = build_validate_managed_cache_component(
            current, diagnostics);
        if (status != PORPOISE_EXIT_OK) return status;
        if (build_cache_paths_equal(current, normalized_root))
            return PORPOISE_EXIT_OK;
        if (!porpoise_path_parent(
                parent, sizeof(parent), current) ||
            build_cache_paths_equal(parent, current) ||
            !porpoise_copy_string(current, sizeof(current), parent)) {
            break;
        }
    }
    return build_diagnostic(
        diagnostics, PORPOISE_SEVERITY_ERROR,
        PORPOISE_EXIT_USAGE, path,
        "managed build cache path is not contained by the project-adjacent cache root");
}

static int build_validate_managed_cache_layout(
    const PorpoiseBuildRequest *request,
    const PorpoiseBuildResult *result,
    PorpoiseDiagnostics *diagnostics) {
    if (result == NULL || result->cache_directory[0] == '\0') {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            PORPOISE_EXIT_INTERNAL, "",
            "managed build cache layout is unavailable");
    }
    return build_validate_managed_cache_path(
        request, result->cache_directory, diagnostics);
}

static int build_validate_cache_disjoint(
    const PorpoiseBuildRequest *request,
    PorpoiseDiagnostics *diagnostics) {
    char project_parent[PORPOISE_PATH_CAPACITY];
    char cache_root[PORPOISE_PATH_CAPACITY];
    const char *paths[3];
    const char *labels[3] = {
        "generated output", "libPorpoise", "title host"
    };
    size_t index;
    int status = build_validate_managed_cache_root(request, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (!porpoise_path_parent(
            project_parent, sizeof(project_parent), request->project_file) ||
        !porpoise_path_join(
            cache_root, sizeof(cache_root), project_parent,
            ".porpoise-build")) return PORPOISE_EXIT_INTERNAL;
    paths[0] = request->generated_directory;
    paths[1] = request->libporpoise_directory;
    paths[2] = request->title_host_directory;
    for (index = 0U; index < sizeof(paths) / sizeof(paths[0]); index++) {
        bool overlap = false;
        if (paths[index] == NULL || paths[index][0] == '\0') continue;
        if (!porpoise_path_trees_overlap(paths[index], cache_root, &overlap)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                paths[index],
                "cannot compare the %s tree with the managed build cache safely",
                labels[index]);
        }
        if (overlap) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR,
                PORPOISE_EXIT_USAGE, paths[index],
                "%s tree must not equal, contain, or be contained by the managed build cache '%s'",
                labels[index], cache_root);
        }
    }
    return PORPOISE_EXIT_OK;
}

static bool build_target_is_x64(const char *target) {
    return target != NULL &&
           (strstr(target, "x86_64") != NULL ||
            strstr(target, "amd64") != NULL ||
            strstr(target, "AMD64") != NULL);
}

static int build_validate_request(
    const PorpoiseBuildRequest *request,
    PorpoiseDiagnostics *diagnostics) {
    char meson_file[PORPOISE_PATH_CAPACITY];
    if (request == NULL || request->project_file == NULL ||
        request->project_file[0] == '\0' ||
        request->generated_directory == NULL ||
        request->generated_directory[0] == '\0' ||
        request->libporpoise_directory == NULL ||
        request->libporpoise_directory[0] == '\0' ||
        !build_valid_target_id(request->target_id)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE, "",
            "build requires a project file, target id, generated directory, and libPorpoise directory");
    }
    if ((request->recovery_target == NULL) != (request->plan == NULL)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE, "",
            "reviewed title-host staging requires both a recovery target and translation plan");
    }
    if (request->recovery_target == NULL &&
        (request->title_host_directory == NULL ||
         request->title_host_directory[0] == '\0')) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE, "",
            "build requires a reviewed title-host profile or an explicit prebuilt title-host directory");
    }
    if (request->generated_plan_digest == NULL ||
        request->generated_plan_digest[0] == '\0') {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE, "",
            "build requires the digest of the generated translation plan");
    }
    if (request->plan != NULL &&
        strcmp(request->generated_plan_digest,
               porpoise_plan_digest(request->plan)) != 0) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE, "",
            "generated plan digest does not match the reviewed translation plan");
    }
    if (request->recovery_target != NULL &&
        request->recovery_target->id != NULL &&
        strcmp(request->recovery_target->id, request->target_id) != 0) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE, "",
            "reviewed title-host target '%s' does not match build target '%s'",
            request->recovery_target->id, request->target_id);
    }
    if (request->recovery_target != NULL &&
        request->title_host_directory != NULL &&
        request->title_host_directory[0] != '\0') {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE, "",
            "select either reviewed title-host generation or a prebuilt title host, not both");
    }
    {
        int status = build_validate_cache_disjoint(request, diagnostics);
        if (status != PORPOISE_EXIT_OK) return status;
    }
    if (!build_is_file(request->project_file)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            request->project_file, "project file does not exist");
    }
    if (!porpoise_path_join(
            meson_file, sizeof(meson_file), request->generated_directory,
            "meson.build") || !build_is_file(meson_file)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            request->generated_directory,
            "generated directory has no meson.build; generate the target first");
    }
    if (!porpoise_path_join(
            meson_file, sizeof(meson_file), request->libporpoise_directory,
            "meson.build") || !build_is_file(meson_file)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            request->libporpoise_directory,
            "libPorpoise directory has no meson.build");
    }
    if (request->title_host_directory != NULL &&
        request->title_host_directory[0] != '\0' &&
        (!porpoise_path_join(
             meson_file, sizeof(meson_file), request->title_host_directory,
             "meson.build") || !build_is_file(meson_file))) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            request->title_host_directory,
            "title-host directory has no meson.build");
    }
    if (!build_valid_build_type(
            build_default(request->build_type, "debugoptimized"))) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            request->build_type,
            "build type must be plain, debug, debugoptimized, release, or minsize");
    }
    if (request->runtime_search_directory_count != 0U &&
        request->runtime_search_directories == NULL) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE, "",
            "runtime search directory count has no directory array");
    }
    if (request->run_argument_count != 0U && request->run_arguments == NULL) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE, "",
            "run argument count has no argument array");
    }
    if (request->environment_count != 0U && request->environment == NULL) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE, "",
            "environment entry count has no entry array");
    }
    return PORPOISE_EXIT_OK;
}

static int build_write_probe_file(
    const char *path,
    const char *text,
    PorpoiseDiagnostics *diagnostics) {
    FILE *file = fopen(path, "wb");
    bool ok;
    if (file == NULL) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot create toolchain probe: %s", strerror(errno));
    }
    ok = porpoise_write_all(file, text);
    if (fclose(file) != 0) ok = false;
    file = NULL;
    if (!ok) {
        if (file != NULL) (void)fclose(file);
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot write toolchain probe");
    }
    return PORPOISE_EXIT_OK;
}

static int build_mixed_link_probe(
    const PorpoiseBuildRequest *request,
    const BuildCompilerIdentity *c_identity,
    const BuildCompilerIdentity *cpp_identity,
    PorpoiseDiagnostics *diagnostics) {
    char project_parent[PORPOISE_PATH_CAPACITY];
    char build_root[PORPOISE_PATH_CAPACITY];
    char probe[PORPOISE_PATH_CAPACITY];
    char c_source[PORPOISE_PATH_CAPACITY];
    char cpp_source[PORPOISE_PATH_CAPACITY];
    char object[PORPOISE_PATH_CAPACITY];
    char executable[PORPOISE_PATH_CAPACITY];
    const char *compile_argv[7];
    const char *link_argv[7];
    PorpoiseProcessCapture capture;
    int result;
    if (!porpoise_path_parent(
            project_parent, sizeof(project_parent), request->project_file) ||
        !porpoise_path_join(
            build_root, sizeof(build_root), project_parent,
            ".porpoise-build") ||
        !porpoise_format(
            probe, sizeof(probe), "%s/.preflight-%lu", build_root,
            BUILD_PROCESS_ID())) {
        return PORPOISE_EXIT_IO;
    }
    result = build_validate_managed_cache_path(
        request, probe, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (!porpoise_make_directories(build_root, diagnostics) ||
        !porpoise_make_directories(probe, diagnostics) ||
        !porpoise_path_join(c_source, sizeof(c_source), probe, "probe.c") ||
        !porpoise_path_join(cpp_source, sizeof(cpp_source), probe, "probe.cpp") ||
        !porpoise_path_join(object, sizeof(object), probe, "probe.o") ||
        !porpoise_path_join(
            executable, sizeof(executable), probe,
#ifdef _WIN32
            "probe.exe"
#else
            "probe"
#endif
            )) {
        return PORPOISE_EXIT_IO;
    }
    result = build_validate_managed_cache_path(
        request, probe, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    result = build_write_probe_file(
        c_source, "int porpoise_c_probe(void) { return 0; }\n", diagnostics);
    if (result == PORPOISE_EXIT_OK) {
        result = build_write_probe_file(
            cpp_source,
            "extern \"C\" int porpoise_c_probe(void);\n"
            "int main(void) { return porpoise_c_probe(); }\n",
            diagnostics);
    }
    compile_argv[0] = c_identity->path;
    compile_argv[1] = "-c";
    compile_argv[2] = c_source;
    compile_argv[3] = "-o";
    compile_argv[4] = object;
    compile_argv[5] = NULL;
    if (result == PORPOISE_EXIT_OK) {
        porpoise_process_capture_init(&capture);
        result = build_run_process(
            request, PORPOISE_BUILD_PHASE_PREFLIGHT, compile_argv, NULL,
            NULL, 0U, &capture, diagnostics, "mixed C/C++ probe compilation");
        porpoise_process_capture_free(&capture);
    }
    link_argv[0] = cpp_identity->path;
    link_argv[1] = cpp_source;
    link_argv[2] = object;
    link_argv[3] = "-o";
    link_argv[4] = executable;
    link_argv[5] = NULL;
    if (result == PORPOISE_EXIT_OK) {
        porpoise_process_capture_init(&capture);
        result = build_run_process(
            request, PORPOISE_BUILD_PHASE_PREFLIGHT, link_argv, NULL,
            NULL, 0U, &capture, diagnostics, "mixed C/C++ probe link");
        porpoise_process_capture_free(&capture);
    }
    {
        int root_status = build_validate_managed_cache_path(
            request, probe, diagnostics);
        if (root_status != PORPOISE_EXIT_OK) {
            if (result == PORPOISE_EXIT_OK) result = root_status;
        } else if (!porpoise_remove_tree(probe, diagnostics) &&
                   result == PORPOISE_EXIT_OK) {
            result = PORPOISE_EXIT_IO;
        }
    }
    return result;
}

int porpoise_build_preflight(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildPreflight *result,
    PorpoiseDiagnostics *diagnostics) {
    BuildCompilerIdentity c_identity;
    BuildCompilerIdentity cpp_identity;
    PorpoiseProcessCapture capture;
    const char *meson_argv[3];
    unsigned int meson_version[3];
    char meson_selection[PORPOISE_PATH_CAPACITY];
    int status;
    if (result == NULL) return PORPOISE_EXIT_INTERNAL;
    memset(result, 0, sizeof(*result));
    memset(&c_identity, 0, sizeof(c_identity));
    memset(&cpp_identity, 0, sizeof(cpp_identity));
    status = build_validate_request(request, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    build_progress(
        request, PORPOISE_BUILD_PHASE_PREFLIGHT, 0U, 4U,
        "Validating Meson");
    if (!build_resolve_command(
            build_default(request->meson_executable, "meson"),
            meson_selection)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            build_default(request->meson_executable, "meson"),
            "Meson did not resolve to an executable");
    }
    meson_argv[0] = meson_selection;
    meson_argv[1] = "--version";
    meson_argv[2] = NULL;
    porpoise_process_capture_init(&capture);
    status = build_run_process(
        request, PORPOISE_BUILD_PHASE_PREFLIGHT, meson_argv, NULL,
        NULL, 0U, &capture, diagnostics, "Meson version query");
    if (status != PORPOISE_EXIT_OK) {
        porpoise_process_capture_free(&capture);
        return status;
    }
    build_first_line(capture.standard_output, result->meson_version);
    porpoise_process_capture_free(&capture);
    if (!build_parse_semver(result->meson_version, meson_version) ||
        meson_version[0] < 1U ||
        (meson_version[0] == 1U && meson_version[1] < 2U)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            meson_selection, "Meson 1.2 or newer is required; found '%s'",
            result->meson_version);
    }
    porpoise_copy_string(result->meson_path, sizeof(result->meson_path),
                         meson_selection);
    build_progress(
        request, PORPOISE_BUILD_PHASE_PREFLIGHT, 1U, 4U,
        "Inspecting the C compiler");
    status = build_inspect_compiler(
        request, build_default(request->c_compiler, "cc"),
        &c_identity, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    build_progress(
        request, PORPOISE_BUILD_PHASE_PREFLIGHT, 2U, 4U,
        "Inspecting the C++ compiler");
    status = build_inspect_compiler(
        request, build_default(request->cpp_compiler, "c++"),
        &cpp_identity, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (strcmp(c_identity.family, cpp_identity.family) != 0 ||
        strcmp(c_identity.version, cpp_identity.version) != 0 ||
        strcmp(c_identity.target, cpp_identity.target) != 0 ||
        !build_same_parent(c_identity.path, cpp_identity.path)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE, "",
            "C and C++ compilers must come from one matched toolchain; got %s %s (%s) at %s and %s %s (%s) at %s",
            c_identity.family, c_identity.version, c_identity.target,
            c_identity.path, cpp_identity.family, cpp_identity.version,
            cpp_identity.target, cpp_identity.path);
    }
    if (!build_target_is_x64(c_identity.target)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            c_identity.path,
            "Porpoise runtime builds require an x86_64 compiler target; got '%s'",
            c_identity.target);
    }
    build_progress(
        request, PORPOISE_BUILD_PHASE_PREFLIGHT, 3U, 4U,
        "Linking a mixed C/C++ probe");
    status = build_mixed_link_probe(
        request, &c_identity, &cpp_identity, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    porpoise_copy_string(
        result->compiler_family, sizeof(result->compiler_family),
        c_identity.family);
    porpoise_copy_string(
        result->compiler_version, sizeof(result->compiler_version),
        c_identity.version);
    porpoise_copy_string(
        result->compiler_target, sizeof(result->compiler_target),
        c_identity.target);
    porpoise_copy_string(
        result->c_compiler_path, sizeof(result->c_compiler_path),
        c_identity.path);
    porpoise_copy_string(
        result->cpp_compiler_path, sizeof(result->cpp_compiler_path),
        cpp_identity.path);
    build_progress(
        request, PORPOISE_BUILD_PHASE_PREFLIGHT, 4U, 4U,
        "Toolchain is compatible");
    return PORPOISE_EXIT_OK;
}

static bool build_sha_update_file(
    PorpoiseSha256Context *context,
    const char *path) {
    FILE *file = fopen(path, "rb");
    unsigned char buffer[16384];
    size_t count;
    if (file == NULL) return false;
    while ((count = fread(buffer, 1U, sizeof(buffer), file)) != 0U)
        porpoise_sha256_update(context, buffer, count);
    if (ferror(file)) {
        fclose(file);
        return false;
    }
    return fclose(file) == 0;
}

static void build_sha_update_text(
    PorpoiseSha256Context *context,
    const char *label,
    const char *value) {
    const char separator = '\0';
    value = value == NULL ? "" : value;
    porpoise_sha256_update(context, label, strlen(label));
    porpoise_sha256_update(context, &separator, 1U);
    porpoise_sha256_update(context, value, strlen(value));
    porpoise_sha256_update(context, &separator, 1U);
}

static void build_sha_update_optional_file(
    PorpoiseSha256Context *context,
    const char *root,
    const char *relative) {
    char path[PORPOISE_PATH_CAPACITY];
    build_sha_update_text(context, "file", relative);
    if (root != NULL &&
        porpoise_path_join(path, sizeof(path), root, relative) &&
        build_is_file(path)) {
        (void)build_sha_update_file(context, path);
    }
}

static void build_hash_git_identity(
    PorpoiseSha256Context *context,
    const char *root) {
    char git[PORPOISE_PATH_CAPACITY];
    char head_path[PORPOISE_PATH_CAPACITY];
    char index_path[PORPOISE_PATH_CAPACITY];
    FILE *head;
    char line[PORPOISE_PATH_CAPACITY];
    if (!porpoise_path_join(git, sizeof(git), root, ".git") ||
        !porpoise_path_is_directory(git)) return;
    if (porpoise_path_join(head_path, sizeof(head_path), git, "HEAD") &&
        build_is_file(head_path)) {
        (void)build_sha_update_file(context, head_path);
        head = fopen(head_path, "rb");
        if (head != NULL && fgets(line, sizeof(line), head) != NULL) {
            const char *prefix = "ref: ";
            porpoise_trim(line);
            if (strncmp(line, prefix, strlen(prefix)) == 0) {
                char ref_path[PORPOISE_PATH_CAPACITY];
                if (porpoise_path_join(
                        ref_path, sizeof(ref_path), git,
                        line + strlen(prefix)) && build_is_file(ref_path))
                    (void)build_sha_update_file(context, ref_path);
            }
        }
        if (head != NULL) fclose(head);
    }
    if (porpoise_path_join(index_path, sizeof(index_path), git, "index") &&
        build_is_file(index_path))
        (void)build_sha_update_file(context, index_path);
}

static void build_sha_finish_identity(
    PorpoiseSha256Context *context,
    char output[PORPOISE_BUILD_ID_CAPACITY + 1U]) {
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    porpoise_sha256_final(context, digest);
    porpoise_sha256_hex(digest, output);
}

static void build_hash_reviewed_title_host(
    PorpoiseSha256Context *context,
    const PorpoiseRecoveryTarget *target) {
    const PorpoiseRecoveryTitleHostProfile *profile = &target->title_host;
    size_t index;
    build_sha_update_text(
        context, "reviewed_host", target->has_title_host ? "present" : "missing");
    if (!target->has_title_host) return;
    porpoise_sha256_update(context, profile->gpr, sizeof(profile->gpr));
    porpoise_sha256_update(
        context, &profile->entry_address, sizeof(profile->entry_address));
    porpoise_sha256_update(
        context, &profile->arena_lo, sizeof(profile->arena_lo));
    porpoise_sha256_update(
        context, &profile->arena_hi, sizeof(profile->arena_hi));
    porpoise_sha256_update(
        context, &profile->initialize_dvd, sizeof(profile->initialize_dvd));
    for (index = 0U; index < profile->startup_function_count; index++) {
        build_sha_update_text(
            context, "startup_module",
            profile->startup_functions[index].module);
        build_sha_update_text(
            context, "startup_fingerprint",
            profile->startup_functions[index].normalized_fingerprint);
        porpoise_sha256_update(
            context, &profile->startup_functions[index].address,
            sizeof(profile->startup_functions[index].address));
        porpoise_sha256_update(
            context, &profile->startup_functions[index].size,
            sizeof(profile->startup_functions[index].size));
        porpoise_sha256_update(
            context, &profile->startup_functions[index].flags,
            sizeof(profile->startup_functions[index].flags));
    }
    porpoise_sha256_update(
        context, profile->initial_words,
        profile->initial_word_count * sizeof(profile->initial_words[0]));
    build_sha_update_text(context, "host_input", profile->input_sha256);
    build_sha_update_text(
        context, "host_symbols", profile->symbol_sources_sha256);
    build_sha_update_text(
        context, "host_catalogs", profile->sdk_catalogs_sha256);
}

static int build_identity_name_compare(const void *left, const void *right) {
    const char *const *a = (const char *const *)left;
    const char *const *b = (const char *const *)right;
    return strcmp(*a, *b);
}

static void build_identity_names_free(char **names, size_t count) {
    size_t index;
    for (index = 0U; index < count; index++) free(names[index]);
    free(names);
}

static bool build_identity_skip_entry(
    const char *name,
    bool is_directory,
    bool is_root_entry,
    bool skip_bundled_toolchain) {
    if (!is_root_entry) return false;
    if (is_directory &&
        (strcmp(name, ".git") == 0 ||
         strcmp(name, ".porpoise-build") == 0)) return true;
    if (!skip_bundled_toolchain) return false;
    if (!is_directory) return strcmp(name, "libPorpoise.zip") == 0;
    return strcmp(name, "tests") == 0 ||
           strcmp(name, "benchmarks") == 0 ||
           strcmp(name, "docs") == 0 ||
           strcmp(name, "build") == 0 ||
           strncmp(name, "build-", 6U) == 0 ||
           strcmp(name, "msys2") == 0;
}

static int build_identity_hash_file(
    const PorpoiseBuildRequest *request,
    PorpoiseSha256Context *context,
    const char *path,
    PorpoiseDiagnostics *diagnostics) {
    FILE *file = fopen(path, "rb");
    unsigned char buffer[16384];
    size_t count;
    if (file == NULL) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot open build identity input: %s", strerror(errno));
    }
    while ((count = fread(buffer, 1U, sizeof(buffer), file)) != 0U) {
        if (build_cancelled(request)) {
            fclose(file);
            return PORPOISE_EXIT_CANCELLED;
        }
        porpoise_sha256_update(context, buffer, count);
    }
    if (ferror(file) || fclose(file) != 0) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot hash build identity input");
    }
    return PORPOISE_EXIT_OK;
}

static int build_hash_directory_identity(
    const PorpoiseBuildRequest *request,
    PorpoiseSha256Context *context,
    const char *root,
    const char *relative,
    unsigned int depth,
    bool skip_bundled_toolchain,
    PorpoiseDiagnostics *diagnostics) {
    char directory_path[PORPOISE_PATH_CAPACITY];
    DIR *directory;
    const struct dirent *entry;
    char **names = NULL;
    size_t name_count = 0U;
    size_t name_capacity = 0U;
    size_t index;
    int result = PORPOISE_EXIT_OK;
    if (depth > BUILD_MAX_IDENTITY_DEPTH) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            root, "build identity tree exceeds the supported directory depth");
    }
    if (relative[0] == '\0') {
        if (!porpoise_copy_string(
                directory_path, sizeof(directory_path), root))
            return PORPOISE_EXIT_INTERNAL;
    } else if (!porpoise_path_join(
                   directory_path, sizeof(directory_path), root, relative)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    directory = opendir(directory_path);
    if (directory == NULL) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            directory_path, "cannot inspect build identity: %s",
            strerror(errno));
    }
    while ((entry = readdir(directory)) != NULL) {
        char *copy;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;
        if (!porpoise_grow_array(
                (void **)&names, &name_capacity, sizeof(*names),
                name_count + 1U) ||
            (copy = porpoise_strdup(entry->d_name)) == NULL) {
            result = PORPOISE_EXIT_INTERNAL;
            break;
        }
        names[name_count++] = copy;
    }
    if (closedir(directory) != 0 && result == PORPOISE_EXIT_OK) {
        result = build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            directory_path, "cannot finish inspecting build identity");
    }
    if (result != PORPOISE_EXIT_OK) {
        build_identity_names_free(names, name_count);
        return result;
    }
    if (name_count > 1U) {
        qsort(names, name_count, sizeof(*names), build_identity_name_compare);
    }
    for (index = 0U; index < name_count; index++) {
        char relative_path[PORPOISE_PATH_CAPACITY];
        char full_path[PORPOISE_PATH_CAPACITY];
        struct stat status;
        uint64_t file_size;
        if (build_cancelled(request)) {
            result = PORPOISE_EXIT_CANCELLED;
            break;
        }
        if (!porpoise_format(
                relative_path, sizeof(relative_path),
                relative[0] == '\0' ? "%s" : "%s/%s",
                relative[0] == '\0' ? names[index] : relative,
                names[index]) ||
            !porpoise_path_join(
                full_path, sizeof(full_path), root, relative_path) ||
            stat(full_path, &status) != 0) {
            result = build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                directory_path, "cannot inspect build identity entry '%s'",
                names[index]);
            break;
        }
        if (build_identity_skip_entry(
                names[index], S_ISDIR(status.st_mode), relative[0] == '\0',
                skip_bundled_toolchain)) continue;
        if (S_ISDIR(status.st_mode)) {
            build_sha_update_text(context, "directory", relative_path);
            result = build_hash_directory_identity(
                request, context, root, relative_path, depth + 1U,
                skip_bundled_toolchain, diagnostics);
        } else if (S_ISREG(status.st_mode)) {
            build_sha_update_text(context, "file", relative_path);
            file_size = status.st_size < 0 ? 0U : (uint64_t)status.st_size;
            porpoise_sha256_update(context, &file_size, sizeof(file_size));
            result = build_identity_hash_file(
                request, context, full_path, diagnostics);
        } else {
            build_sha_update_text(context, "other", relative_path);
        }
        if (result != PORPOISE_EXIT_OK) break;
    }
    build_identity_names_free(names, name_count);
    return result;
}

static int build_libporpoise_identity(
    const PorpoiseBuildRequest *request,
    char output[PORPOISE_BUILD_ID_CAPACITY + 1U],
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSha256Context context;
    int result;
    porpoise_sha256_init(&context);
    build_sha_update_text(&context, "schema", "porpoise-libporpoise-identity-v1");
    build_hash_git_identity(&context, request->libporpoise_directory);
    result = build_hash_directory_identity(
        request, &context, request->libporpoise_directory, "", 0U, true,
        diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    build_sha_finish_identity(&context, output);
    return PORPOISE_EXIT_OK;
}

static int build_sdl2_dependency_identity(
    const PorpoiseBuildRequest *request,
    char output[PORPOISE_BUILD_ID_CAPACITY + 1U],
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSha256Context context;
    int result = PORPOISE_EXIT_OK;
    porpoise_sha256_init(&context);
    build_sha_update_text(
        &context, "schema", "porpoise-sdl2-dependency-identity-v1");
#ifdef _WIN32
    {
        char headers[PORPOISE_PATH_CAPACITY];
        char import_library[PORPOISE_PATH_CAPACITY];
        char runtime_library[PORPOISE_PATH_CAPACITY];
        if (!build_sdl2_source_paths(request, headers, import_library) ||
            !build_sdl2_runtime_path(request, runtime_library))
            return PORPOISE_EXIT_INTERNAL;
        if (!porpoise_path_is_directory(headers)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                headers,
                "libPorpoise's Windows dependency bundle is missing the SDL2 header directory");
        }
        if (!build_is_file(import_library)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                import_library,
                "libPorpoise's Windows dependency bundle is missing the SDL2 import library '%s'",
                BUILD_SDL2_IMPORT_LIBRARY);
        }
        if (!build_is_file(runtime_library)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                runtime_library,
                "libPorpoise's Windows dependency bundle is missing the SDL2 runtime library 'SDL2.dll'");
        }
        build_sha_update_text(&context, "headers", "SDL2");
        result = build_hash_directory_identity(
            request, &context, headers, "", 0U, false, diagnostics);
        if (result == PORPOISE_EXIT_OK) {
            build_sha_update_text(
                &context, "import_library", BUILD_SDL2_IMPORT_LIBRARY);
            result = build_identity_hash_file(
                request, &context, import_library, diagnostics);
        }
        if (result == PORPOISE_EXIT_OK) {
            build_sha_update_text(
                &context, "runtime_library", "SDL2.dll");
            result = build_identity_hash_file(
                request, &context, runtime_library, diagnostics);
        }
    }
#else
    (void)request;
    (void)diagnostics;
    build_sha_update_text(&context, "platform", "system-sdl2");
#endif
    if (result != PORPOISE_EXIT_OK) return result;
    build_sha_finish_identity(&context, output);
    return PORPOISE_EXIT_OK;
}

static int build_title_host_identity(
    const PorpoiseBuildRequest *request,
    char output[PORPOISE_BUILD_ID_CAPACITY + 1U],
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSha256Context context;
    int result = PORPOISE_EXIT_OK;
    porpoise_sha256_init(&context);
    build_sha_update_text(&context, "schema", "porpoise-title-host-identity-v1");
    if (request->recovery_target != NULL) {
        build_sha_update_text(&context, "kind", "reviewed-profile");
        build_hash_reviewed_title_host(&context, request->recovery_target);
    } else {
        build_sha_update_text(&context, "kind", "prebuilt-directory");
        build_hash_git_identity(&context, request->title_host_directory);
        result = build_hash_directory_identity(
            request, &context, request->title_host_directory, "", 0U, false,
            diagnostics);
    }
    if (result != PORPOISE_EXIT_OK) return result;
    build_sha_finish_identity(&context, output);
    return PORPOISE_EXIT_OK;
}

static int build_generated_output_identity(
    const PorpoiseBuildRequest *request,
    char output[PORPOISE_BUILD_ID_CAPACITY + 1U],
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSha256Context context;
    int result;

    porpoise_sha256_init(&context);
    build_sha_update_text(
        &context, "schema", "porpoise-generated-output-identity-v1");
    result = build_hash_directory_identity(
        request, &context, request->generated_directory, "", 0U, false,
        diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    build_sha_finish_identity(&context, output);
    return PORPOISE_EXIT_OK;
}

static int build_configuration_digest(
    const PorpoiseBuildRequest *request,
    const PorpoiseBuildPreflight *preflight,
    char output[PORPOISE_BUILD_ID_CAPACITY + 1U],
    char generated_output_identity[PORPOISE_BUILD_ID_CAPACITY + 1U],
    char libporpoise_identity[PORPOISE_BUILD_ID_CAPACITY + 1U],
    char sdl2_dependency_identity[PORPOISE_BUILD_ID_CAPACITY + 1U],
    char title_host_identity[PORPOISE_BUILD_ID_CAPACITY + 1U],
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseSha256Context context;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    int result = build_generated_output_identity(
        request, generated_output_identity, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    result = build_libporpoise_identity(
        request, libporpoise_identity, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    result = build_sdl2_dependency_identity(
        request, sdl2_dependency_identity, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    result = build_title_host_identity(
        request, title_host_identity, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    porpoise_sha256_init(&context);
    build_sha_update_text(&context, "schema", "porpoise-build-v4");
    build_sha_update_text(&context, "tool_version", PORPOISE_TOOL_VERSION);
    build_sha_update_text(&context, "target", request->target_id);
    build_sha_update_text(
        &context, "buildtype",
        build_default(request->build_type, "debugoptimized"));
    build_sha_update_text(&context, "generated", request->generated_directory);
    build_sha_update_text(&context, "libporpoise", request->libporpoise_directory);
    build_sha_update_text(&context, "title_host", request->title_host_directory);
    build_sha_update_text(
        &context, "generated_output_identity", generated_output_identity);
    build_sha_update_text(
        &context, "libporpoise_identity", libporpoise_identity);
    build_sha_update_text(
        &context, "sdl2_dependency_identity", sdl2_dependency_identity);
    build_sha_update_text(&context, "title_host_identity", title_host_identity);
    build_sha_update_text(
        &context, "plan", request->generated_plan_digest);
    build_sha_update_text(&context, "meson", preflight->meson_version);
    build_sha_update_text(&context, "cc", preflight->c_compiler_path);
    build_sha_update_text(&context, "cxx", preflight->cpp_compiler_path);
    build_sha_update_text(&context, "compiler", preflight->compiler_version);
    build_sha_update_text(&context, "triple", preflight->compiler_target);
    build_sha_update_optional_file(
        &context, request->generated_directory, "meson.build");
    build_sha_update_optional_file(
        &context, request->generated_directory, "porpoise-report.json");
    build_sha_update_optional_file(
        &context, request->libporpoise_directory, "meson.build");
    build_sha_update_optional_file(
        &context, request->libporpoise_directory, "meson.options");
    build_hash_git_identity(&context, request->libporpoise_directory);
    if (request->recovery_target != NULL) {
        build_hash_reviewed_title_host(&context, request->recovery_target);
    } else if (request->title_host_directory != NULL &&
        request->title_host_directory[0] != '\0') {
        build_sha_update_optional_file(
            &context, request->title_host_directory, "meson.build");
        build_sha_update_optional_file(
            &context, request->title_host_directory, "host.c");
        build_hash_git_identity(&context, request->title_host_directory);
    }
    porpoise_sha256_final(&context, digest);
    porpoise_sha256_hex(digest, output);
    if (output[0] == '\0') {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_INTERNAL,
            "", "cannot compute build configuration digest");
    }
    return PORPOISE_EXIT_OK;
}

static int build_revalidate_input_identities(
    const PorpoiseBuildRequest *request,
    const PorpoiseBuildResult *result,
    const char *boundary,
    PorpoiseDiagnostics *diagnostics) {
    char configuration[PORPOISE_BUILD_ID_CAPACITY + 1U];
    char generated[PORPOISE_BUILD_ID_CAPACITY + 1U];
    char libporpoise[PORPOISE_BUILD_ID_CAPACITY + 1U];
    char sdl2[PORPOISE_BUILD_ID_CAPACITY + 1U];
    char title_host[PORPOISE_BUILD_ID_CAPACITY + 1U];
    const char *changed = NULL;
    int status = build_configuration_digest(
        request, &result->preflight, configuration, generated,
        libporpoise, sdl2, title_host, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (strcmp(generated, result->generated_output_identity) != 0)
        changed = "generated output";
    else if (strcmp(libporpoise, result->libporpoise_identity) != 0)
        changed = "libPorpoise";
    else if (strcmp(sdl2, result->sdl2_dependency_identity) != 0)
        changed = "SDL2 dependency";
    else if (strcmp(title_host, result->title_host_identity) != 0)
        changed = "title-host";
    else if (strcmp(configuration, result->configuration_digest) != 0)
        changed = "build configuration";
    if (changed == NULL) return PORPOISE_EXIT_OK;
    return build_diagnostic(
        diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
        request->project_file,
        "%s identity changed while %s; no cached success or new build manifest was accepted. Keep the inputs stable and retry the build",
        changed, boundary == NULL ? "the build was running" : boundary);
}

static bool build_should_skip_copy_entry(const char *name) {
    return strcmp(name, ".git") == 0 || strcmp(name, "tests") == 0 ||
           strcmp(name, "benchmarks") == 0 || strcmp(name, "docs") == 0 ||
           strcmp(name, "libPorpoise.zip") == 0 ||
           strcmp(name, "build") == 0 || strncmp(name, "build-", 6U) == 0;
}

static bool build_tree_size(
    const char *path,
    uint64_t *size,
    PorpoiseDiagnostics *diagnostics) {
    DIR *directory = opendir(path);
    const struct dirent *entry;
    if (directory == NULL) {
        build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot inspect copy size: %s", strerror(errno));
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        char child[PORPOISE_PATH_CAPACITY];
        struct stat status;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            build_should_skip_copy_entry(entry->d_name)) continue;
        if (!porpoise_path_join(
                child, sizeof(child), path, entry->d_name) ||
            stat(child, &status) != 0) {
            closedir(directory);
            build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                path, "cannot inspect copy size");
            return false;
        }
        if (S_ISDIR(status.st_mode)) {
            if (!build_tree_size(child, size, diagnostics)) {
                closedir(directory);
                return false;
            }
        } else if (S_ISREG(status.st_mode)) {
            uint64_t file_size = status.st_size < 0 ? 0U :
                (uint64_t)status.st_size;
            if (file_size > UINT64_MAX - *size) {
                closedir(directory);
                build_diagnostic(
                    diagnostics, PORPOISE_SEVERITY_ERROR,
                    PORPOISE_EXIT_USAGE, child,
                    "copy size exceeds the supported range");
                return false;
            }
            *size += file_size;
        }
    }
    return closedir(directory) == 0;
}

static bool build_check_copy_capacity_bytes(
    uint64_t source_size,
    const char *destination,
    PorpoiseDiagnostics *diagnostics) {
    char destination_parent[PORPOISE_PATH_CAPACITY];
    uint64_t reserve = UINT64_C(64) * UINT64_C(1024) * UINT64_C(1024);
    uint64_t required;
    uint64_t available;
    if (!porpoise_path_parent(
            destination_parent, sizeof(destination_parent), destination))
        return false;
    if (source_size / 10U > reserve) reserve = source_size / 10U;
    if (source_size > UINT64_MAX - reserve) return false;
    required = source_size + reserve;
#ifdef _WIN32
    {
        ULARGE_INTEGER free_bytes;
        if (!GetDiskFreeSpaceExA(
                destination_parent, &free_bytes, NULL, NULL)) {
            build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                destination_parent,
                "cannot verify free space for confirmed copy fallback (Windows error %lu)",
                (unsigned long)GetLastError());
            return false;
        }
        available = (uint64_t)free_bytes.QuadPart;
    }
#else
    {
        struct statvfs filesystem;
        uint64_t fragment_size;
        if (statvfs(destination_parent, &filesystem) != 0) {
            build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                destination_parent,
                "cannot verify free space for confirmed copy fallback: %s",
                strerror(errno));
            return false;
        }
        fragment_size = filesystem.f_frsize != 0U ?
            (uint64_t)filesystem.f_frsize : (uint64_t)filesystem.f_bsize;
        if (fragment_size == 0U) {
            build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                destination_parent,
                "filesystem reported no allocation unit for copy fallback");
            return false;
        }
        if ((uint64_t)filesystem.f_bavail > UINT64_MAX / fragment_size) {
            available = UINT64_MAX;
        } else {
            available = (uint64_t)filesystem.f_bavail *
                        fragment_size;
        }
    }
#endif
    if (available < required) {
        build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            destination_parent,
            "copy fallback needs %llu bytes including reserve, but only %llu bytes are available",
            (unsigned long long)required, (unsigned long long)available);
        return false;
    }
    return true;
}

static bool build_check_copy_capacity(
    const char *source,
    const char *destination,
    PorpoiseDiagnostics *diagnostics) {
    uint64_t source_size = 0U;
    return build_tree_size(source, &source_size, diagnostics) &&
           build_check_copy_capacity_bytes(
               source_size, destination, diagnostics);
}

#ifdef _WIN32
static bool build_check_copy_file_capacity(
    const char *source,
    const char *destination,
    PorpoiseDiagnostics *diagnostics) {
    struct stat status;
    if (stat(source, &status) != 0 || !S_ISREG(status.st_mode)) {
        build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            source, "cannot inspect copy size: %s", strerror(errno));
        return false;
    }
    return build_check_copy_capacity_bytes(
        status.st_size < 0 ? 0U : (uint64_t)status.st_size,
        destination, diagnostics);
}
#endif

static int build_copy_tree(
    const PorpoiseBuildRequest *request,
    const char *source,
    const char *destination,
    PorpoiseDiagnostics *diagnostics) {
    DIR *directory;
    const struct dirent *entry;
    bool overlap = false;
    int cache_status = build_validate_managed_cache_path(
        request, destination, diagnostics);
    if (cache_status != PORPOISE_EXIT_OK) return cache_status;
    if (!porpoise_path_trees_overlap(source, destination, &overlap) || overlap) {
        build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            destination,
            "copy fallback source and destination must be disjoint");
        return PORPOISE_EXIT_USAGE;
    }
    if (!porpoise_make_directories(destination, diagnostics))
        return PORPOISE_EXIT_IO;
    directory = opendir(source);
    if (directory == NULL) {
        build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            source, "cannot open dependency for copy: %s", strerror(errno));
        return PORPOISE_EXIT_IO;
    }
    while ((entry = readdir(directory)) != NULL) {
        char child_source[PORPOISE_PATH_CAPACITY];
        char child_destination[PORPOISE_PATH_CAPACITY];
        struct stat status;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            build_should_skip_copy_entry(entry->d_name)) continue;
        if (build_cancelled(request)) {
            closedir(directory);
            return PORPOISE_EXIT_CANCELLED;
        }
        if (!porpoise_path_join(
                child_source, sizeof(child_source), source, entry->d_name) ||
            !porpoise_path_join(
                child_destination, sizeof(child_destination), destination,
                entry->d_name) || stat(child_source, &status) != 0) {
            closedir(directory);
            return PORPOISE_EXIT_IO;
        }
        if (S_ISDIR(status.st_mode)) {
            int result = build_copy_tree(
                request, child_source, child_destination, diagnostics);
            if (result != PORPOISE_EXIT_OK) {
                closedir(directory);
                return result;
            }
        } else if (S_ISREG(status.st_mode) &&
                   !porpoise_copy_file(
                       child_source, child_destination, diagnostics)) {
            closedir(directory);
            return PORPOISE_EXIT_IO;
        }
    }
    return closedir(directory) == 0 ? PORPOISE_EXIT_OK : PORPOISE_EXIT_IO;
}

#ifdef _WIN32
typedef struct BuildMountPointReparseBuffer {
    DWORD reparse_tag;
    WORD reparse_data_length;
    WORD reserved;
    WORD substitute_name_offset;
    WORD substitute_name_length;
    WORD print_name_offset;
    WORD print_name_length;
    WCHAR path_buffer[(PORPOISE_PATH_CAPACITY * 2U) + 16U];
} BuildMountPointReparseBuffer;

static bool build_create_directory_junction(
    const char *source,
    const char *destination) {
    char absolute_source[PORPOISE_PATH_CAPACITY];
    WCHAR print_name[PORPOISE_PATH_CAPACITY];
    WCHAR substitute_name[PORPOISE_PATH_CAPACITY + 16U];
    BuildMountPointReparseBuffer reparse;
    DWORD attributes;
    DWORD absolute_length;
    DWORD bytes_returned = 0U;
    size_t print_length;
    size_t substitute_length;
    size_t path_bytes;
    size_t input_bytes;
    int converted;
    HANDLE handle;
    bool ok = false;
    attributes = GetFileAttributesA(source);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
        GetFileAttributesA(destination) != INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    absolute_length = GetFullPathNameA(
        source, (DWORD)sizeof(absolute_source), absolute_source, NULL);
    if (absolute_length == 0U || absolute_length >= sizeof(absolute_source))
        return false;
    converted = MultiByteToWideChar(
        CP_ACP, 0U, absolute_source, -1, print_name,
        (int)(sizeof(print_name) / sizeof(print_name[0])));
    if (converted <= 1) return false;
    print_length = (size_t)converted - 1U;
    if (print_length >= 2U && print_name[0] == L'\\' &&
        print_name[1] == L'\\') {
        static const WCHAR prefix[] = L"\\??\\UNC\\";
        size_t prefix_length =
            sizeof(prefix) / sizeof(prefix[0]) - 1U;
        if (prefix_length + print_length - 2U + 1U >
            sizeof(substitute_name) / sizeof(substitute_name[0])) {
            return false;
        }
        memcpy(
            substitute_name, prefix, prefix_length * sizeof(WCHAR));
        memcpy(
            substitute_name + prefix_length, print_name + 2U,
            (print_length - 2U + 1U) * sizeof(WCHAR));
        substitute_length = prefix_length + print_length - 2U;
    } else {
        static const WCHAR prefix[] = L"\\??\\";
        size_t prefix_length =
            sizeof(prefix) / sizeof(prefix[0]) - 1U;
        if (prefix_length + print_length + 1U >
            sizeof(substitute_name) / sizeof(substitute_name[0])) {
            return false;
        }
        memcpy(
            substitute_name, prefix, prefix_length * sizeof(WCHAR));
        memcpy(
            substitute_name + prefix_length, print_name,
            (print_length + 1U) * sizeof(WCHAR));
        substitute_length = prefix_length + print_length;
    }
    path_bytes =
        (substitute_length + 1U + print_length + 1U) * sizeof(WCHAR);
    if (path_bytes > sizeof(reparse.path_buffer) ||
        path_bytes + 8U > UINT16_MAX) {
        return false;
    }
    memset(&reparse, 0, sizeof(reparse));
    reparse.reparse_tag = IO_REPARSE_TAG_MOUNT_POINT;
    reparse.reparse_data_length = (WORD)(8U + path_bytes);
    reparse.substitute_name_length =
        (WORD)(substitute_length * sizeof(WCHAR));
    reparse.print_name_offset =
        (WORD)((substitute_length + 1U) * sizeof(WCHAR));
    reparse.print_name_length = (WORD)(print_length * sizeof(WCHAR));
    memcpy(
        reparse.path_buffer, substitute_name,
        (substitute_length + 1U) * sizeof(WCHAR));
    memcpy(
        (unsigned char *)reparse.path_buffer + reparse.print_name_offset,
        print_name, (print_length + 1U) * sizeof(WCHAR));
    input_bytes = 8U + (size_t)reparse.reparse_data_length;
    if (!CreateDirectoryA(destination, NULL)) return false;
    handle = CreateFileA(
        destination, GENERIC_WRITE, 0U, NULL, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle != INVALID_HANDLE_VALUE) {
        ok = DeviceIoControl(
                 handle, FSCTL_SET_REPARSE_POINT, &reparse,
                 (DWORD)input_bytes, NULL, 0U, &bytes_returned, NULL) != 0;
        (void)CloseHandle(handle);
    }
    if (!ok) (void)RemoveDirectoryA(destination);
    return ok;
}
#endif

static bool build_link_directory(const char *source, const char *destination) {
#ifdef _WIN32
    if (CreateSymbolicLinkA(
            destination, source,
            SYMBOLIC_LINK_FLAG_DIRECTORY |
            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0) {
        return true;
    }
    return build_create_directory_junction(source, destination);
#else
    return symlink(source, destination) == 0;
#endif
}

static bool build_link_file(const char *source, const char *destination) {
#ifdef _WIN32
    return CreateHardLinkA(destination, source, NULL) != 0;
#else
    return link(source, destination) == 0;
#endif
}

#ifdef _WIN32
static bool build_directory_has_exact_entries(
    const char *path,
    const char *const *expected,
    size_t expected_count) {
    DIR *directory = opendir(path);
    const struct dirent *entry;
    bool *seen;
    size_t count = 0U;
    bool exact = true;
    if (directory == NULL) return false;
    seen = (bool *)calloc(expected_count == 0U ? 1U : expected_count,
                          sizeof(*seen));
    if (seen == NULL) {
        closedir(directory);
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        size_t index;
        bool matched = false;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;
        count++;
        for (index = 0U; index < expected_count; index++) {
            if (strcmp(entry->d_name, expected[index]) == 0 && !seen[index]) {
                seen[index] = true;
                matched = true;
                break;
            }
        }
        if (!matched) exact = false;
    }
    if (closedir(directory) != 0 || count != expected_count) exact = false;
    free(seen);
    return exact;
}

static bool build_sdl2_overlay_path_complete(const char *overlay) {
    static const char *const overlay_entries[] = {"include", "lib"};
    static const char *const include_entries[] = {"SDL2"};
    static const char *const library_entries[] = {
        BUILD_SDL2_IMPORT_LIBRARY
    };
    char include_root[PORPOISE_PATH_CAPACITY];
    char library_root[PORPOISE_PATH_CAPACITY];
    char headers[PORPOISE_PATH_CAPACITY];
    char import_library[PORPOISE_PATH_CAPACITY];
    return porpoise_path_join(
               include_root, sizeof(include_root), overlay, "include") &&
           porpoise_path_join(
               library_root, sizeof(library_root), overlay, "lib") &&
           porpoise_path_join(
               headers, sizeof(headers), include_root, "SDL2") &&
           porpoise_path_join(
               import_library, sizeof(import_library), library_root,
               BUILD_SDL2_IMPORT_LIBRARY) &&
           porpoise_path_is_directory(headers) &&
           build_is_file(import_library) &&
           build_directory_has_exact_entries(
               overlay, overlay_entries,
               sizeof(overlay_entries) / sizeof(overlay_entries[0])) &&
           build_directory_has_exact_entries(
               include_root, include_entries,
               sizeof(include_entries) / sizeof(include_entries[0])) &&
           build_directory_has_exact_entries(
               library_root, library_entries,
               sizeof(library_entries) / sizeof(library_entries[0]));
}

static bool build_sdl2_overlay_complete(
    const PorpoiseBuildResult *result) {
    char overlay[PORPOISE_PATH_CAPACITY];
    char include_root[PORPOISE_PATH_CAPACITY];
    char library_root[PORPOISE_PATH_CAPACITY];
    return build_sdl2_overlay_paths(
               result, overlay, include_root, library_root) &&
           build_sdl2_overlay_path_complete(overlay);
}

static int build_stage_sdl2_overlay(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildResult *result,
    PorpoiseDiagnostics *diagnostics) {
    char source_headers[PORPOISE_PATH_CAPACITY];
    char source_import_library[PORPOISE_PATH_CAPACITY];
    char overlay[PORPOISE_PATH_CAPACITY];
    char include_root[PORPOISE_PATH_CAPACITY];
    char library_root[PORPOISE_PATH_CAPACITY];
    char stage[PORPOISE_PATH_CAPACITY];
    char stage_include_root[PORPOISE_PATH_CAPACITY];
    char stage_library_root[PORPOISE_PATH_CAPACITY];
    char staged_headers[PORPOISE_PATH_CAPACITY];
    char staged_import_library[PORPOISE_PATH_CAPACITY];
    int status = PORPOISE_EXIT_OK;
    if (!build_sdl2_source_paths(
            request, source_headers, source_import_library) ||
        !build_sdl2_overlay_paths(
            result, overlay, include_root, library_root) ||
        !porpoise_format(
            stage, sizeof(stage), "%s.stage.%lu", overlay,
            BUILD_PROCESS_ID()) ||
        !porpoise_path_join(
            stage_include_root, sizeof(stage_include_root), stage,
            "include") ||
        !porpoise_path_join(
            stage_library_root, sizeof(stage_library_root), stage, "lib") ||
        !porpoise_path_join(
            staged_headers, sizeof(staged_headers), stage_include_root,
            "SDL2") ||
        !porpoise_path_join(
            staged_import_library, sizeof(staged_import_library),
            stage_library_root, BUILD_SDL2_IMPORT_LIBRARY))
        return PORPOISE_EXIT_INTERNAL;
    status = build_validate_managed_cache_layout(
        request, result, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    status = build_validate_managed_cache_path(
        request, overlay, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    status = build_validate_managed_cache_path(
        request, stage, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (build_sdl2_overlay_complete(result)) return PORPOISE_EXIT_OK;
    if (porpoise_path_exists(overlay) &&
        !porpoise_remove_tree(overlay, diagnostics)) return PORPOISE_EXIT_IO;
    if (porpoise_path_exists(stage) &&
        !porpoise_remove_tree(stage, diagnostics)) return PORPOISE_EXIT_IO;
    if (!porpoise_make_directories(stage_include_root, diagnostics) ||
        !porpoise_make_directories(stage_library_root, diagnostics))
        return PORPOISE_EXIT_IO;
    if (!build_link_directory(source_headers, staged_headers)) {
        if (!request->allow_copy_fallback) {
            status = build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                staged_headers,
                "cannot link the isolated SDL2 headers into build staging; enable the confirmed copy fallback or enable filesystem symbolic links");
        } else if (!build_check_copy_capacity(
                       source_headers, staged_headers, diagnostics)) {
            status = PORPOISE_EXIT_IO;
        } else {
            status = build_copy_tree(
                request, source_headers, staged_headers, diagnostics);
        }
    }
    if (status == PORPOISE_EXIT_OK &&
        !build_link_file(source_import_library, staged_import_library)) {
        if (!request->allow_copy_fallback) {
            status = build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                staged_import_library,
                "cannot hard-link the SDL2 import library into build staging; enable the confirmed copy fallback for cross-volume inputs");
        } else if (!build_check_copy_file_capacity(
                       source_import_library, staged_import_library,
                       diagnostics)) {
            status = PORPOISE_EXIT_IO;
        } else if (!porpoise_copy_file(
                       source_import_library, staged_import_library,
                       diagnostics)) {
            status = PORPOISE_EXIT_IO;
        }
    }
    if (status == PORPOISE_EXIT_OK && build_cancelled(request))
        status = PORPOISE_EXIT_CANCELLED;
    if (status == PORPOISE_EXIT_OK &&
        !build_sdl2_overlay_path_complete(stage)) {
        status = build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            stage,
            "isolated SDL2 staging contains files outside the header and import-library overlay");
    }
    if (status == PORPOISE_EXIT_OK &&
        !porpoise_move_path(stage, overlay, diagnostics))
        status = PORPOISE_EXIT_IO;
    if (status == PORPOISE_EXIT_OK && !build_sdl2_overlay_complete(result)) {
        status = build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            overlay, "cannot validate the published SDL2 dependency overlay");
    }
    if (status != PORPOISE_EXIT_OK)
        (void)porpoise_remove_tree(stage, diagnostics);
    return status;
}
#endif

static int build_mirror_generated_tree(
    const PorpoiseBuildRequest *request,
    const char *source,
    const char *destination,
    bool top_level,
    PorpoiseDiagnostics *diagnostics) {
    DIR *directory;
    const struct dirent *entry;
    int cache_status = build_validate_managed_cache_path(
        request, destination, diagnostics);
    if (cache_status != PORPOISE_EXIT_OK) return cache_status;
    if (!porpoise_make_directories(destination, diagnostics))
        return PORPOISE_EXIT_IO;
    directory = opendir(source);
    if (directory == NULL) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            source, "cannot open generated source for staging: %s",
            strerror(errno));
    }
    while ((entry = readdir(directory)) != NULL) {
        char child_source[PORPOISE_PATH_CAPACITY];
        char child_destination[PORPOISE_PATH_CAPACITY];
        struct stat status;
        int result;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;
        if (top_level && strcmp(entry->d_name, "subprojects") == 0) {
            closedir(directory);
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                source,
                "generated output contains an unmanaged subprojects directory; regenerate into a clean output before building");
        }
        if (!porpoise_path_join(
                child_source, sizeof(child_source), source, entry->d_name) ||
            !porpoise_path_join(
                child_destination, sizeof(child_destination), destination,
                entry->d_name) || stat(child_source, &status) != 0) {
            closedir(directory);
            return PORPOISE_EXIT_IO;
        }
        if (S_ISDIR(status.st_mode)) {
            result = build_mirror_generated_tree(
                request, child_source, child_destination, false,
                diagnostics);
            if (result != PORPOISE_EXIT_OK) {
                closedir(directory);
                return result;
            }
        } else if (S_ISREG(status.st_mode) &&
                   !build_link_file(child_source, child_destination)) {
            if (!request->allow_copy_fallback) {
                closedir(directory);
                return build_diagnostic(
                    diagnostics, PORPOISE_SEVERITY_ERROR,
                    PORPOISE_EXIT_IO, child_destination,
                    "cannot hard-link generated source into build staging; enable the confirmed copy fallback for cross-volume inputs");
            }
            if (!porpoise_copy_file(
                    child_source, child_destination, diagnostics)) {
                closedir(directory);
                return PORPOISE_EXIT_IO;
            }
        }
    }
    if (closedir(directory) != 0) return PORPOISE_EXIT_IO;
    return PORPOISE_EXIT_OK;
}

static int build_bind_directory(
    const PorpoiseBuildRequest *request,
    const char *source,
    const char *destination,
    PorpoiseDiagnostics *diagnostics) {
    char meson_file[PORPOISE_PATH_CAPACITY];
    char stage[PORPOISE_PATH_CAPACITY];
    char staged_meson[PORPOISE_PATH_CAPACITY];
    int result;
    if (!porpoise_path_join(
            meson_file, sizeof(meson_file), destination, "meson.build") ||
        !porpoise_format(
            stage, sizeof(stage), "%s.stage.%lu", destination,
            BUILD_PROCESS_ID()) ||
        !porpoise_path_join(
            staged_meson, sizeof(staged_meson), stage, "meson.build"))
        return PORPOISE_EXIT_INTERNAL;
    result = build_validate_managed_cache_path(
        request, stage, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (build_is_file(meson_file)) return PORPOISE_EXIT_OK;
    if (porpoise_path_exists(destination) &&
        !porpoise_remove_tree(destination, diagnostics)) return PORPOISE_EXIT_IO;
    if (porpoise_path_exists(stage) &&
        !porpoise_remove_tree(stage, diagnostics)) return PORPOISE_EXIT_IO;
    if (!build_link_directory(source, stage)) {
        if (!request->allow_copy_fallback) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                destination,
                "cannot create dependency link; enable the confirmed copy fallback or enable filesystem symbolic links");
        }
        if (!build_check_copy_capacity(source, stage, diagnostics))
            return PORPOISE_EXIT_IO;
        result = build_copy_tree(request, source, stage, diagnostics);
        if (result != PORPOISE_EXIT_OK) {
            (void)porpoise_remove_tree(stage, diagnostics);
            return result;
        }
    }
    if (!build_is_file(staged_meson)) {
        (void)porpoise_remove_tree(stage, diagnostics);
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            source, "staged dependency has no meson.build");
    }
    if (build_cancelled(request)) {
        (void)porpoise_remove_tree(stage, diagnostics);
        return PORPOISE_EXIT_CANCELLED;
    }
    if (!porpoise_move_path(stage, destination, diagnostics)) {
        (void)porpoise_remove_tree(stage, diagnostics);
        return PORPOISE_EXIT_IO;
    }
    return PORPOISE_EXIT_OK;
}

static int build_write_text_file(
    const char *path,
    const char *text,
    PorpoiseDiagnostics *diagnostics) {
    FILE *file = fopen(path, "wb");
    bool ok;
    if (file == NULL) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot create build file: %s", strerror(errno));
    }
    ok = porpoise_write_all(file, text);
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot write build file");
    }
    return PORPOISE_EXIT_OK;
}

static int build_prepare_layout(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildResult *result,
    PorpoiseDiagnostics *diagnostics) {
    char root[PORPOISE_PATH_CAPACITY];
    char target_root[PORPOISE_PATH_CAPACITY];
    char target_cache_key[PORPOISE_RECOVERY_TARGET_CACHE_KEY_SIZE];
    char cache_key[25];
    char marker[PORPOISE_PATH_CAPACITY];
    char staged_meson[PORPOISE_PATH_CAPACITY];
    char source_stage[PORPOISE_PATH_CAPACITY];
    char source_stage_meson[PORPOISE_PATH_CAPACITY];
    char subprojects[PORPOISE_PATH_CAPACITY];
    char libporpoise_link[PORPOISE_PATH_CAPACITY];
    char title_host_link[PORPOISE_PATH_CAPACITY];
    int status;
    memcpy(cache_key, result->configuration_digest, 24U);
    cache_key[24] = '\0';
    if (!porpoise_recovery_target_cache_key(
            request->target_id, target_cache_key) ||
        build_managed_cache_root_path(
            request, root, diagnostics) != PORPOISE_EXIT_OK ||
        !porpoise_path_join(
            target_root, sizeof(target_root), root, target_cache_key) ||
        !porpoise_path_join(
            result->cache_directory, sizeof(result->cache_directory),
            target_root, cache_key) ||
        !porpoise_path_join(
            result->source_directory, sizeof(result->source_directory),
            result->cache_directory, "source") ||
        !porpoise_path_join(
            result->build_directory, sizeof(result->build_directory),
            result->cache_directory, "build") ||
        !porpoise_path_join(
            result->manifest_path, sizeof(result->manifest_path),
            result->cache_directory, BUILD_MANIFEST_FILE) ||
        !porpoise_path_join(
            marker, sizeof(marker), result->cache_directory,
            BUILD_MARKER_FILE)) return PORPOISE_EXIT_INTERNAL;
    result->cache_reused = false;
    status = build_validate_managed_cache_layout(
        request, result, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    status = build_validate_managed_cache_path(
        request, result->build_directory, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (!porpoise_make_directories(result->cache_directory, diagnostics) ||
        !porpoise_make_directories(result->build_directory, diagnostics))
        return PORPOISE_EXIT_IO;
    status = build_validate_managed_cache_layout(
        request, result, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (!build_is_file(marker)) {
        status = build_write_text_file(
            marker, "schema_version=1\n", diagnostics);
        if (status != PORPOISE_EXIT_OK) return status;
    }
#ifdef _WIN32
    status = build_stage_sdl2_overlay(request, result, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
#endif
    if (!porpoise_path_join(
            staged_meson, sizeof(staged_meson), result->source_directory,
            "meson.build") ||
        !porpoise_format(
            source_stage, sizeof(source_stage), "%s.stage.%lu",
            result->source_directory, BUILD_PROCESS_ID()) ||
        !porpoise_path_join(
            source_stage_meson, sizeof(source_stage_meson), source_stage,
            "meson.build")) return PORPOISE_EXIT_INTERNAL;
    status = build_validate_managed_cache_path(
        request, result->source_directory, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    status = build_validate_managed_cache_path(
        request, source_stage, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (!build_is_file(staged_meson)) {
        if (porpoise_path_exists(result->source_directory) &&
            !porpoise_remove_tree(result->source_directory, diagnostics))
            return PORPOISE_EXIT_IO;
        if (porpoise_path_exists(source_stage) &&
            !porpoise_remove_tree(source_stage, diagnostics))
            return PORPOISE_EXIT_IO;
        if (request->allow_copy_fallback &&
            !build_check_copy_capacity(
                request->generated_directory, source_stage,
                diagnostics)) return PORPOISE_EXIT_IO;
        status = build_mirror_generated_tree(
            request, request->generated_directory,
            source_stage, true, diagnostics);
        if (status != PORPOISE_EXIT_OK || !build_is_file(source_stage_meson) ||
            build_cancelled(request)) {
            (void)porpoise_remove_tree(source_stage, diagnostics);
            return status == PORPOISE_EXIT_OK ?
                (build_cancelled(request) ? PORPOISE_EXIT_CANCELLED :
                 build_diagnostic(
                     diagnostics, PORPOISE_SEVERITY_ERROR,
                     PORPOISE_EXIT_IO, request->generated_directory,
                     "staged generated source has no meson.build")) : status;
        }
        if (!porpoise_move_path(
                source_stage, result->source_directory, diagnostics)) {
            (void)porpoise_remove_tree(source_stage, diagnostics);
            return PORPOISE_EXIT_IO;
        }
    }
    if (!porpoise_path_join(
            subprojects, sizeof(subprojects), result->source_directory,
            "subprojects"))
        return PORPOISE_EXIT_INTERNAL;
    status = build_validate_managed_cache_path(
        request, subprojects, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (!porpoise_make_directories(subprojects, diagnostics))
        return PORPOISE_EXIT_IO;
    if (!porpoise_path_join(
            libporpoise_link, sizeof(libporpoise_link), subprojects,
            "libPorpoise") ||
        !porpoise_path_join(
            title_host_link, sizeof(title_host_link), subprojects,
            "porpoise-title-host")) return PORPOISE_EXIT_INTERNAL;
    status = build_bind_directory(
        request, request->libporpoise_directory, libporpoise_link,
        diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (request->recovery_target != NULL) {
        char title_host_meson[PORPOISE_PATH_CAPACITY];
        char title_host_stage[PORPOISE_PATH_CAPACITY];
        char title_host_stage_meson[PORPOISE_PATH_CAPACITY];
        status = porpoise_recovery_title_host_validate(
            request->recovery_target, request->plan, diagnostics);
        if (status != PORPOISE_EXIT_OK) return status;
        if (!porpoise_path_join(
                title_host_meson, sizeof(title_host_meson), title_host_link,
                "meson.build") ||
            !porpoise_format(
                title_host_stage, sizeof(title_host_stage), "%s.stage.%lu",
                title_host_link, BUILD_PROCESS_ID()) ||
            !porpoise_path_join(
                title_host_stage_meson, sizeof(title_host_stage_meson),
                title_host_stage, "meson.build"))
            return PORPOISE_EXIT_INTERNAL;
        if (!build_is_file(title_host_meson)) {
            if (porpoise_path_exists(title_host_link) &&
                !porpoise_remove_tree(title_host_link, diagnostics))
                return PORPOISE_EXIT_IO;
            if (porpoise_path_exists(title_host_stage) &&
                !porpoise_remove_tree(title_host_stage, diagnostics))
                return PORPOISE_EXIT_IO;
            status = porpoise_recovery_title_host_generate(
                request->recovery_target, request->plan, title_host_stage,
                diagnostics);
            if (status != PORPOISE_EXIT_OK ||
                !build_is_file(title_host_stage_meson) ||
                build_cancelled(request)) {
                (void)porpoise_remove_tree(title_host_stage, diagnostics);
                return status == PORPOISE_EXIT_OK ?
                    (build_cancelled(request) ? PORPOISE_EXIT_CANCELLED :
                     PORPOISE_EXIT_IO) : status;
            }
            if (!porpoise_move_path(
                    title_host_stage, title_host_link, diagnostics)) {
                (void)porpoise_remove_tree(title_host_stage, diagnostics);
                return PORPOISE_EXIT_IO;
            }
        }
    } else if (request->title_host_directory != NULL &&
               request->title_host_directory[0] != '\0') {
        status = build_bind_directory(
            request, request->title_host_directory, title_host_link,
            diagnostics);
        if (status != PORPOISE_EXIT_OK) return status;
    }
    return PORPOISE_EXIT_OK;
}

static void build_meson_quote(FILE *file, const char *value) {
    const unsigned char *cursor = (const unsigned char *)value;
    fputc('\'', file);
    while (*cursor != '\0') {
        if (*cursor == '\\') fputc('/', file);
        else if (*cursor == '\'') fputs("\\'", file);
        else fputc((int)*cursor, file);
        cursor++;
    }
    fputc('\'', file);
}

static int build_write_native_file(
    const PorpoiseBuildRequest *request,
    const PorpoiseBuildResult *result,
    char path[PORPOISE_PATH_CAPACITY],
    PorpoiseDiagnostics *diagnostics) {
    FILE *file;
    int status = build_validate_managed_cache_layout(
        request, result, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (!porpoise_path_join(
            path, PORPOISE_PATH_CAPACITY, result->cache_directory,
            BUILD_NATIVE_FILE)) return PORPOISE_EXIT_INTERNAL;
    file = fopen(path, "wb");
    if (file == NULL) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot create Meson native file: %s", strerror(errno));
    }
    fputs("[binaries]\nc = [", file);
    build_meson_quote(file, result->preflight.c_compiler_path);
    fputs("]\ncpp = [", file);
    build_meson_quote(file, result->preflight.cpp_compiler_path);
    fputs("]\n", file);
#ifdef _WIN32
    {
        char overlay[PORPOISE_PATH_CAPACITY];
        char include_root[PORPOISE_PATH_CAPACITY];
        char library_root[PORPOISE_PATH_CAPACITY];
        char include_arg[PORPOISE_PATH_CAPACITY + 3U];
        char library_arg[PORPOISE_PATH_CAPACITY + 3U];
        if (!build_sdl2_overlay_paths(
                result, overlay, include_root, library_root) ||
            !porpoise_format(
                include_arg, sizeof(include_arg), "-I%s", include_root) ||
            !porpoise_format(
                library_arg, sizeof(library_arg), "-L%s", library_root)) {
            fclose(file);
            return PORPOISE_EXIT_INTERNAL;
        }
        fputs("\n[built-in options]\nc_args = [", file);
        build_meson_quote(file, include_arg);
        fputs(", '-Wno-gnu-pointer-arith', '-Wno-macro-redefined']\n", file);
        fputs("cpp_args = [", file);
        build_meson_quote(file, include_arg);
        fputs(", '-Wno-gnu-pointer-arith', '-Wno-macro-redefined', '-Wno-register']\n", file);
        fputs("c_link_args = [", file);
        build_meson_quote(file, library_arg);
        fputs("]\ncpp_link_args = [", file);
        build_meson_quote(file, library_arg);
        fputs("]\n", file);
    }
#endif
    if (fclose(file) != 0) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            path, "cannot finish Meson native file");
    }
    return PORPOISE_EXIT_OK;
}

static bool build_json_token_equals(
    const char *json,
    const jsmntok_t *token,
    const char *value) {
    size_t length;
    if (token->type != JSMN_STRING || token->start < 0 || token->end < token->start)
        return false;
    length = (size_t)(token->end - token->start);
    return strlen(value) == length &&
           memcmp(json + token->start, value, length) == 0;
}

static bool build_json_copy_string(
    const char *json,
    const jsmntok_t *token,
    char *output,
    size_t capacity) {
    const char *cursor;
    const char *end;
    size_t length = 0U;
    if (token->type != JSMN_STRING || capacity == 0U) return false;
    cursor = json + token->start;
    end = json + token->end;
    while (cursor < end) {
        unsigned char value = (unsigned char)*cursor++;
        if (value == '\\') {
            if (cursor >= end) return false;
            switch (*cursor++) {
            case '"': value = '"'; break;
            case '\\': value = '\\'; break;
            case '/': value = '/'; break;
            case 'b': value = '\b'; break;
            case 'f': value = '\f'; break;
            case 'n': value = '\n'; break;
            case 'r': value = '\r'; break;
            case 't': value = '\t'; break;
            default: return false;
            }
        }
        if (length + 1U >= capacity) return false;
        output[length++] = (char)value;
    }
    output[length] = '\0';
    return true;
}

static int build_parse_json_tokens(
    const char *json,
    jsmntok_t **tokens_out,
    size_t *count_out) {
    size_t capacity = 256U;
    for (;;) {
        jsmn_parser parser;
        jsmntok_t *tokens = (jsmntok_t *)malloc(capacity * sizeof(*tokens));
        int count;
        if (tokens == NULL) return PORPOISE_EXIT_INTERNAL;
        jsmn_init(&parser);
        count = jsmn_parse(&parser, json, strlen(json), tokens, capacity);
        if (count == JSMN_ERROR_NOMEM) {
            free(tokens);
            if (capacity > SIZE_MAX / 2U) return PORPOISE_EXIT_INTERNAL;
            capacity *= 2U;
            continue;
        }
        if (count < 0) {
            free(tokens);
            return PORPOISE_EXIT_TRANSLATION;
        }
        *tokens_out = tokens;
        *count_out = (size_t)count;
        return PORPOISE_EXIT_OK;
    }
}

static bool build_load_success_manifest(PorpoiseBuildResult *result) {
    FILE *file;
    long length;
    char *json;
    jsmntok_t *tokens = NULL;
    size_t token_count = 0U;
    size_t index;
    char digest[PORPOISE_BUILD_ID_CAPACITY + 1U] = "";
    char executable[PORPOISE_PATH_CAPACITY] = "";
    char executable_sha256[PORPOISE_SHA256_HEX_SIZE] = "";
    char actual_executable_sha256[PORPOISE_SHA256_HEX_SIZE] = "";
    bool have_digest = false;
    bool have_executable = false;
    bool have_executable_sha256 = false;
    if (!build_is_file(result->manifest_path)) return false;
    file = fopen(result->manifest_path, "rb");
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 || length > 1024L * 1024L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return false;
    }
    json = (char *)malloc((size_t)length + 1U);
    if (json == NULL) {
        fclose(file);
        return false;
    }
    if (fread(json, 1U, (size_t)length, file) != (size_t)length ||
        fclose(file) != 0) {
        free(json);
        return false;
    }
    json[length] = '\0';
    if (build_parse_json_tokens(json, &tokens, &token_count) !=
        PORPOISE_EXIT_OK) {
        free(json);
        return false;
    }
    for (index = 0U; index + 1U < token_count; index++) {
        if (build_json_token_equals(
                json, &tokens[index], "configuration_digest")) {
            have_digest = build_json_copy_string(
                json, &tokens[index + 1U], digest, sizeof(digest));
        } else if (build_json_token_equals(
                       json, &tokens[index], "executable")) {
            have_executable = build_json_copy_string(
                json, &tokens[index + 1U], executable,
                sizeof(executable));
        } else if (build_json_token_equals(
                       json, &tokens[index], "executable_sha256")) {
            have_executable_sha256 = build_json_copy_string(
                json, &tokens[index + 1U], executable_sha256,
                sizeof(executable_sha256));
        }
    }
    free(tokens);
    free(json);
    if (!have_digest || !have_executable || !have_executable_sha256 ||
        strcmp(digest, result->configuration_digest) != 0 ||
        (executable[0] != '\0' &&
         (!build_is_file(executable) ||
          !build_hash_file_hex(executable, actual_executable_sha256) ||
          strcmp(executable_sha256, actual_executable_sha256) != 0)) ||
        (executable[0] == '\0' && executable_sha256[0] != '\0')) return false;
    if (!porpoise_copy_string(
            result->executable_path, sizeof(result->executable_path),
            executable)) return false;
    result->configured = true;
    result->compiled = true;
    result->executable_available = executable[0] != '\0';
    result->cache_reused = true;
    return true;
}

static int build_discover_executable(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildResult *result,
    char discovered[PORPOISE_PATH_CAPACITY],
    PorpoiseDiagnostics *diagnostics) {
    const char *argv[5];
    PorpoiseProcessCapture capture;
    jsmntok_t *tokens = NULL;
    size_t token_count = 0U;
    size_t index;
    int status;
    discovered[0] = '\0';
    argv[0] = result->preflight.meson_path;
    argv[1] = "introspect";
    argv[2] = "--targets";
    argv[3] = result->build_directory;
    argv[4] = NULL;
    porpoise_process_capture_init(&capture);
    status = build_run_process(
        request, PORPOISE_BUILD_PHASE_COMPILE, argv, NULL,
        NULL, 0U, &capture, diagnostics, "Meson target introspection");
    if (status != PORPOISE_EXIT_OK) {
        porpoise_process_capture_free(&capture);
        return status;
    }
    status = build_parse_json_tokens(
        capture.standard_output, &tokens, &token_count);
    if (status != PORPOISE_EXIT_OK) {
        porpoise_process_capture_free(&capture);
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_TRANSLATION,
            result->build_directory,
            "Meson target introspection returned malformed JSON");
    }
    for (index = 0U; index + 1U < token_count; index++) {
        size_t object_index;
        size_t cursor;
        bool executable = false;
        const jsmntok_t *filename = NULL;
        if (!build_json_token_equals(
                capture.standard_output, &tokens[index], "name") ||
            !build_json_token_equals(
                capture.standard_output, &tokens[index + 1U],
                "porpoise_title")) continue;
        object_index = index;
        while (object_index > 0U) {
            object_index--;
            if (tokens[object_index].type == JSMN_OBJECT &&
                tokens[object_index].start <= tokens[index].start &&
                tokens[object_index].end >= tokens[index + 1U].end) break;
        }
        if (tokens[object_index].type != JSMN_OBJECT) continue;
        for (cursor = object_index + 1U; cursor + 1U < token_count &&
             tokens[cursor].start < tokens[object_index].end; cursor++) {
            if (build_json_token_equals(
                    capture.standard_output, &tokens[cursor], "type") &&
                build_json_token_equals(
                    capture.standard_output, &tokens[cursor + 1U],
                    "executable")) executable = true;
            if (build_json_token_equals(
                    capture.standard_output, &tokens[cursor], "filename")) {
                size_t value_index = cursor + 1U;
                if (tokens[value_index].type == JSMN_ARRAY) value_index++;
                if (value_index < token_count &&
                    tokens[value_index].type == JSMN_STRING)
                    filename = &tokens[value_index];
            }
        }
        if (executable && filename != NULL &&
            build_json_copy_string(
                capture.standard_output, filename, discovered,
                PORPOISE_PATH_CAPACITY)) break;
    }
    free(tokens);
    porpoise_process_capture_free(&capture);
    return PORPOISE_EXIT_OK;
}

static bool build_hash_file_hex(
    const char *path,
    char hex[PORPOISE_SHA256_HEX_SIZE]) {
    PorpoiseSha256Context context;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    porpoise_sha256_init(&context);
    if (!build_sha_update_file(&context, path)) return false;
    porpoise_sha256_final(&context, digest);
    porpoise_sha256_hex(digest, hex);
    return true;
}

static int build_copy_executable(
    const PorpoiseBuildRequest *request,
    const char *source,
    PorpoiseBuildResult *result,
    PorpoiseDiagnostics *diagnostics) {
    char artifacts[PORPOISE_PATH_CAPACITY];
    char basename[PORPOISE_NAME_CAPACITY];
    char artifact_name[PORPOISE_NAME_CAPACITY];
    char temporary[PORPOISE_PATH_CAPACITY];
    char backup[PORPOISE_PATH_CAPACITY];
    char source_hash[PORPOISE_SHA256_HEX_SIZE];
    char destination_hash[PORPOISE_SHA256_HEX_SIZE];
    const char *extension;
    size_t stem_length;
    int status = build_validate_managed_cache_layout(
        request, result, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    status = build_revalidate_input_identities(
        request, result, "the executable was being staged", diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (!build_is_file(source)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            source, "Meson reported an executable that does not exist");
    }
    if (!porpoise_path_join(
            artifacts, sizeof(artifacts), result->cache_directory,
            "artifacts") ||
        !porpoise_path_basename(basename, sizeof(basename), source))
        return PORPOISE_EXIT_IO;
    status = build_validate_managed_cache_path(
        request, artifacts, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (!porpoise_make_directories(artifacts, diagnostics))
        return PORPOISE_EXIT_IO;
    if (!build_hash_file_hex(source, source_hash)) return PORPOISE_EXIT_IO;
    extension = "";
    stem_length = strlen(basename);
#ifdef _WIN32
    if (stem_length >= 4U &&
        _stricmp(basename + stem_length - 4U, ".exe") == 0) {
        stem_length -= 4U;
        extension = ".exe";
    }
#endif
    if (!porpoise_format(
            artifact_name, sizeof(artifact_name), "%.*s-%.12s%s",
            (int)stem_length, basename, source_hash, extension) ||
        !porpoise_path_join(
            result->executable_path, sizeof(result->executable_path),
            artifacts, artifact_name) ||
        !porpoise_format(
            temporary, sizeof(temporary), "%s.stage.%lu",
            result->executable_path, BUILD_PROCESS_ID()) ||
        !porpoise_format(
            backup, sizeof(backup), "%s.backup.%lu",
            result->executable_path, BUILD_PROCESS_ID()))
        return PORPOISE_EXIT_IO;
    if (build_is_file(result->executable_path)) {
        if (build_hash_file_hex(result->executable_path, destination_hash) &&
            strcmp(source_hash, destination_hash) == 0) {
            BUILD_CHMOD_EXECUTABLE(result->executable_path);
            return PORPOISE_EXIT_OK;
        }
    }
    if ((porpoise_path_exists(temporary) &&
         !porpoise_remove_tree(temporary, diagnostics)) ||
        (porpoise_path_exists(backup) &&
         !porpoise_remove_tree(backup, diagnostics))) return PORPOISE_EXIT_IO;
    if (!porpoise_copy_file(source, temporary, diagnostics))
        return PORPOISE_EXIT_IO;
    BUILD_CHMOD_EXECUTABLE(temporary);
    if (porpoise_path_exists(result->executable_path) &&
        !porpoise_move_path(
            result->executable_path, backup, diagnostics)) {
        (void)porpoise_remove_tree(temporary, diagnostics);
        return PORPOISE_EXIT_IO;
    }
    if (!porpoise_move_path(
            temporary, result->executable_path, diagnostics)) {
        if (porpoise_path_exists(backup)) {
            (void)porpoise_move_path(
                backup, result->executable_path, diagnostics);
        }
        return PORPOISE_EXIT_IO;
    }
    if (porpoise_path_exists(backup) &&
        !porpoise_remove_tree(backup, diagnostics)) return PORPOISE_EXIT_IO;
    BUILD_CHMOD_EXECUTABLE(result->executable_path);
    return PORPOISE_EXIT_OK;
}

#ifdef _WIN32
static bool build_runtime_queue_contains(
    const BuildRuntimeQueue *queue,
    const char *name) {
    size_t index;
    for (index = 0U; index < queue->count; index++) {
#ifdef _WIN32
        if (_stricmp(queue->names[index], name) == 0) return true;
#else
        if (strcmp(queue->names[index], name) == 0) return true;
#endif
    }
    return false;
}

static bool build_runtime_queue_add(
    BuildRuntimeQueue *queue,
    const char *name) {
    char *copy;
    if (build_runtime_queue_contains(queue, name)) return true;
    if (queue->count >= BUILD_MAX_RUNTIME_FILES ||
        !porpoise_grow_array(
            (void **)&queue->names, &queue->capacity,
            sizeof(*queue->names), queue->count + 1U)) return false;
    copy = porpoise_strdup(name);
    if (copy == NULL) return false;
    queue->names[queue->count++] = copy;
    return true;
}

static void build_runtime_queue_free(BuildRuntimeQueue *queue) {
    size_t index;
    for (index = 0U; index < queue->count; index++) free(queue->names[index]);
    free(queue->names);
    memset(queue, 0, sizeof(*queue));
}

static bool build_windows_system_dll(const char *name) {
    static const char *const system_names[] = {
        "kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll",
        "shell32.dll", "ole32.dll", "oleaut32.dll", "ws2_32.dll",
        "ucrtbase.dll", "ntdll.dll", "msvcrt.dll", "combase.dll",
        "imm32.dll", "setupapi.dll", "version.dll", "shlwapi.dll",
        "rpcrt4.dll", "bcrypt.dll", "winmm.dll", "secur32.dll",
        "crypt32.dll", "cfgmgr32.dll"
    };
    size_t index;
    if (_strnicmp(name, "api-ms-win-", 11U) == 0 ||
        _strnicmp(name, "ext-ms-win-", 11U) == 0) return true;
    for (index = 0U; index < sizeof(system_names) / sizeof(system_names[0]);
         index++) {
        if (_stricmp(name, system_names[index]) == 0) return true;
    }
    return false;
}

static int build_validate_x64_pe(
    const char *path,
    const char *kind,
    PorpoiseDiagnostics *diagnostics) {
    unsigned char dos[64];
    unsigned char coff[6];
    uint32_t pe_offset;
    uint16_t machine;
    FILE *file = fopen(path, "rb");
    bool valid_header = file != NULL &&
        fread(dos, 1U, sizeof(dos), file) == sizeof(dos) &&
        dos[0] == 'M' && dos[1] == 'Z';
    if (valid_header) {
        pe_offset = (uint32_t)dos[0x3CU] |
                    ((uint32_t)dos[0x3DU] << 8U) |
                    ((uint32_t)dos[0x3EU] << 16U) |
                    ((uint32_t)dos[0x3FU] << 24U);
        valid_header = pe_offset <= (uint32_t)LONG_MAX &&
            fseek(file, (long)pe_offset, SEEK_SET) == 0 &&
            fread(coff, 1U, sizeof(coff), file) == sizeof(coff) &&
            coff[0] == 'P' && coff[1] == 'E' &&
            coff[2] == 0U && coff[3] == 0U;
    }
    if (file != NULL) fclose(file);
    if (!valid_header) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            path, "%s is not a valid Windows PE image", kind);
    }
    machine = (uint16_t)((uint16_t)coff[4] | ((uint16_t)coff[5] << 8U));
    if (machine != UINT16_C(0x8664)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            path,
            "%s has PE machine 0x%04X; the selected x64 build requires AMD64 (0x8664)",
            kind, (unsigned int)machine);
    }
    return PORPOISE_EXIT_OK;
}

static bool build_find_objdump(
    const PorpoiseBuildRequest *request,
    const PorpoiseBuildPreflight *preflight,
    char output[PORPOISE_PATH_CAPACITY]) {
    char compiler_dir[PORPOISE_PATH_CAPACITY];
    char candidate[PORPOISE_PATH_CAPACITY];
    char compiler_name[PORPOISE_NAME_CAPACITY];
    char *gcc;
    if (request->objdump_executable != NULL &&
        request->objdump_executable[0] != '\0')
        return build_resolve_command(request->objdump_executable, output);
    if (porpoise_path_parent(
            compiler_dir, sizeof(compiler_dir),
            preflight->c_compiler_path)) {
        if (porpoise_path_join(
                candidate, sizeof(candidate), compiler_dir,
                "llvm-objdump.exe") && build_is_file(candidate))
            return porpoise_copy_string(
                output, PORPOISE_PATH_CAPACITY, candidate);
        if (porpoise_path_join(
                candidate, sizeof(candidate), compiler_dir,
                "objdump.exe") && build_is_file(candidate))
            return porpoise_copy_string(
                output, PORPOISE_PATH_CAPACITY, candidate);
        if (porpoise_path_basename(
                compiler_name, sizeof(compiler_name),
                preflight->c_compiler_path)) {
            gcc = strstr(compiler_name, "gcc");
            if (gcc != NULL) {
                memmove(gcc + 7U, gcc + 3U, strlen(gcc + 3U) + 1U);
                memcpy(gcc, "objdump", 7U);
                if (porpoise_path_join(
                        candidate, sizeof(candidate), compiler_dir,
                        compiler_name) && build_is_file(candidate))
                    return porpoise_copy_string(
                        output, PORPOISE_PATH_CAPACITY, candidate);
            }
        }
    }
    return build_resolve_command("llvm-objdump", output) ||
           build_resolve_command("objdump", output);
}

static int build_collect_imports(
    const PorpoiseBuildRequest *request,
    const char *objdump,
    const char *binary,
    BuildRuntimeQueue *queue,
    PorpoiseDiagnostics *diagnostics) {
    const char *argv[4];
    PorpoiseProcessCapture capture;
    const char *cursor;
    int status;
    argv[0] = objdump;
    argv[1] = "-p";
    argv[2] = binary;
    argv[3] = NULL;
    porpoise_process_capture_init(&capture);
    status = build_run_process(
        request, PORPOISE_BUILD_PHASE_STAGE_RUNTIME, argv, NULL,
        NULL, 0U, &capture, diagnostics, "PE import inspection");
    if (status != PORPOISE_EXIT_OK) {
        porpoise_process_capture_free(&capture);
        return status;
    }
    cursor = capture.standard_output;
    while (cursor != NULL && *cursor != '\0') {
        const char *line_end = strpbrk(cursor, "\r\n");
        const char *marker = strstr(cursor, "DLL Name:");
        if (marker != NULL && (line_end == NULL || marker < line_end)) {
            char name[PORPOISE_NAME_CAPACITY];
            size_t length;
            marker += strlen("DLL Name:");
            while (*marker == ' ' || *marker == '\t') marker++;
            length = line_end == NULL ? strlen(marker) :
                (size_t)(line_end - marker);
            while (length > 0U && isspace((unsigned char)marker[length - 1U]))
                length--;
            if (length != 0U && length < sizeof(name)) {
                memcpy(name, marker, length);
                name[length] = '\0';
                if (!build_windows_system_dll(name) &&
                    !build_runtime_queue_add(queue, name)) {
                    porpoise_process_capture_free(&capture);
                    return PORPOISE_EXIT_INTERNAL;
                }
            }
        }
        if (line_end == NULL) break;
        cursor = line_end + 1U;
        if (line_end[0] == '\r' && *cursor == '\n') cursor++;
    }
    porpoise_process_capture_free(&capture);
    return PORPOISE_EXIT_OK;
}

static bool build_resolve_runtime_file(
    const PorpoiseBuildRequest *request,
    const PorpoiseBuildResult *result,
    const char *name,
    char output[PORPOISE_PATH_CAPACITY],
    PorpoiseDiagnostics *diagnostics) {
    char compiler_dir[PORPOISE_PATH_CAPACITY];
    char libporpoise_bin[PORPOISE_PATH_CAPACITY];
    const char *directories[BUILD_MAX_RUNTIME_FILES];
    size_t count = 0U;
    size_t index;
    char selected_hash[PORPOISE_SHA256_HEX_SIZE] = "";
    bool found = false;
    bool is_sdl2 = _stricmp(name, "SDL2.dll") == 0;
    if (!is_sdl2 && porpoise_path_parent(
            compiler_dir, sizeof(compiler_dir),
            result->preflight.cpp_compiler_path))
        directories[count++] = compiler_dir;
    if (is_sdl2 && porpoise_path_join(
            libporpoise_bin, sizeof(libporpoise_bin),
            request->libporpoise_directory, "msys2/ucrt64/bin"))
        directories[count++] = libporpoise_bin;
    for (index = 0U;
         index < request->runtime_search_directory_count &&
         count < BUILD_MAX_RUNTIME_FILES; index++)
        directories[count++] = request->runtime_search_directories[index];
    for (index = 0U; index < count; index++) {
        char candidate[PORPOISE_PATH_CAPACITY];
        char hash[PORPOISE_SHA256_HEX_SIZE];
        if (directories[index] == NULL || directories[index][0] == '\0' ||
            !porpoise_path_join(
                candidate, sizeof(candidate), directories[index], name) ||
            !build_is_file(candidate)) continue;
        if (!build_hash_file_hex(candidate, hash)) continue;
        if (!found) {
            if (!porpoise_copy_string(output, PORPOISE_PATH_CAPACITY, candidate) ||
                !porpoise_copy_string(
                    selected_hash, sizeof(selected_hash), hash)) return false;
            found = true;
        } else if (strcmp(selected_hash, hash) != 0) {
            build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                candidate,
                "runtime DLL '%s' has conflicting authoritative copies; keep one matching toolchain/runtime set",
                name);
            return false;
        }
    }
    return found;
}
#endif

static int build_stage_runtime(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildResult *result,
    PorpoiseDiagnostics *diagnostics) {
    int cache_status = build_validate_managed_cache_layout(
        request, result, diagnostics);
    if (cache_status != PORPOISE_EXIT_OK) return cache_status;
    build_progress(
        request, PORPOISE_BUILD_PHASE_STAGE_RUNTIME, 0U, 1U,
        "Inspecting runtime dependencies");
#ifdef _WIN32
    {
        BuildRuntimeQueue queue;
        char objdump[PORPOISE_PATH_CAPACITY];
        char artifact_directory[PORPOISE_PATH_CAPACITY];
        size_t index;
        int status;
        memset(&queue, 0, sizeof(queue));
        if (!build_find_objdump(request, &result->preflight, objdump)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                request->objdump_executable,
                "cannot find llvm-objdump or objdump for recursive PE import inspection");
        }
        if (!porpoise_path_parent(
                artifact_directory, sizeof(artifact_directory),
                result->executable_path)) return PORPOISE_EXIT_INTERNAL;
        status = build_validate_managed_cache_path(
            request, artifact_directory, diagnostics);
        if (status != PORPOISE_EXIT_OK) return status;
        status = build_validate_x64_pe(
            result->executable_path, "generated executable", diagnostics);
        if (status == PORPOISE_EXIT_OK) {
            status = build_collect_imports(
                request, objdump, result->executable_path, &queue,
                diagnostics);
        }
        for (index = 0U; status == PORPOISE_EXIT_OK && index < queue.count;
             index++) {
            const char *runtime_name =
                _stricmp(queue.names[index], "SDL2.dll") == 0 ?
                    "SDL2.dll" : queue.names[index];
            char source[PORPOISE_PATH_CAPACITY];
            char destination[PORPOISE_PATH_CAPACITY];
            char source_hash[PORPOISE_SHA256_HEX_SIZE];
            char destination_hash[PORPOISE_SHA256_HEX_SIZE];
            char temporary[PORPOISE_PATH_CAPACITY];
            if (!build_resolve_runtime_file(
                    request, result, runtime_name, source,
                    diagnostics)) {
                status = porpoise_diagnostics_have_errors(diagnostics) ?
                    PORPOISE_EXIT_USAGE : build_diagnostic(
                        diagnostics, PORPOISE_SEVERITY_ERROR,
                        PORPOISE_EXIT_USAGE, runtime_name,
                        _stricmp(runtime_name, "SDL2.dll") == 0 ?
                            "required SDL2 runtime DLL '%s' was not found in libPorpoise's dependency bundle or the authoritative runtime roots" :
                            "required compiler runtime DLL '%s' was not found in the selected compiler or the authoritative runtime roots",
                        runtime_name);
                break;
            }
            if (!porpoise_path_join(
                    destination, sizeof(destination), artifact_directory,
                    runtime_name) ||
                !build_hash_file_hex(source, source_hash)) {
                status = PORPOISE_EXIT_IO;
                break;
            }
            status = build_validate_x64_pe(
                source, "runtime DLL", diagnostics);
            if (status != PORPOISE_EXIT_OK) break;
            if (build_is_file(destination)) {
                if (!build_hash_file_hex(destination, destination_hash) ||
                    strcmp(source_hash, destination_hash) != 0) {
                    status = build_diagnostic(
                        diagnostics, PORPOISE_SEVERITY_ERROR,
                        PORPOISE_EXIT_USAGE, destination,
                        "staged runtime DLL '%s' conflicts with the selected authoritative copy",
                        runtime_name);
                    break;
                }
            } else {
                if (!porpoise_format(
                        temporary, sizeof(temporary), "%s.stage.%lu",
                        destination, BUILD_PROCESS_ID()) ||
                    !porpoise_copy_file(source, temporary, diagnostics) ||
                    !porpoise_move_path(temporary, destination, diagnostics)) {
                    (void)porpoise_remove_tree(temporary, diagnostics);
                    status = PORPOISE_EXIT_IO;
                    break;
                }
                result->runtime_file_count++;
            }
            status = build_collect_imports(
                request, objdump, destination, &queue, diagnostics);
            build_progress(
                request, PORPOISE_BUILD_PHASE_STAGE_RUNTIME, index + 1U,
                queue.count, runtime_name);
        }
        build_runtime_queue_free(&queue);
        if (status != PORPOISE_EXIT_OK) return status;
    }
#else
    (void)diagnostics;
#endif
    build_progress(
        request, PORPOISE_BUILD_PHASE_STAGE_RUNTIME, 1U, 1U,
        "Runtime dependencies are staged");
    return PORPOISE_EXIT_OK;
}

static int build_write_manifest(
    const PorpoiseBuildRequest *request,
    const PorpoiseBuildResult *result,
    PorpoiseDiagnostics *diagnostics) {
    char temporary[PORPOISE_PATH_CAPACITY];
    char backup[PORPOISE_PATH_CAPACITY];
    char executable_sha256[PORPOISE_SHA256_HEX_SIZE] = "";
    FILE *file;
    int status = build_validate_managed_cache_layout(
        request, result, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    status = build_revalidate_input_identities(
        request, result, "the build manifest was being published",
        diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (!porpoise_format(
            temporary, sizeof(temporary), "%s.stage.%lu",
            result->manifest_path, BUILD_PROCESS_ID()) ||
        !porpoise_format(
            backup, sizeof(backup), "%s.backup.%lu",
            result->manifest_path, BUILD_PROCESS_ID()))
        return PORPOISE_EXIT_INTERNAL;
    if (result->executable_path[0] != '\0' &&
        !build_hash_file_hex(result->executable_path, executable_sha256)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            result->executable_path,
            "cannot hash the staged executable before publishing its build manifest");
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            temporary, "cannot create build manifest: %s", strerror(errno));
    }
    fprintf(file, "{\n  \"schema_version\": %u,\n",
            PORPOISE_BUILD_MANIFEST_SCHEMA_VERSION);
    fputs("  \"configuration_digest\": ", file);
    porpoise_json_write_string(file, result->configuration_digest);
    fputs(",\n  \"porpoise_tool_version\": ", file);
    porpoise_json_write_string(file, PORPOISE_TOOL_VERSION);
    fputs(",\n  \"target\": ", file);
    porpoise_json_write_string(file, request->target_id);
    fputs(",\n  \"build_type\": ", file);
    porpoise_json_write_string(
        file, build_default(request->build_type, "debugoptimized"));
    fputs(",\n  \"generated_directory\": ", file);
    porpoise_json_write_string(file, request->generated_directory);
    fputs(",\n  \"generated_plan_digest\": ", file);
    porpoise_json_write_string(file, request->generated_plan_digest);
    fputs(",\n  \"libporpoise_directory\": ", file);
    porpoise_json_write_string(file, request->libporpoise_directory);
    fputs(",\n  \"libporpoise_identity\": ", file);
    porpoise_json_write_string(file, result->libporpoise_identity);
    fputs(",\n  \"generated_output_identity\": ", file);
    porpoise_json_write_string(file, result->generated_output_identity);
    fputs(",\n  \"sdl2_dependency_identity\": ", file);
    porpoise_json_write_string(file, result->sdl2_dependency_identity);
    fputs(",\n  \"title_host_directory\": ", file);
    porpoise_json_write_string(file, request->title_host_directory);
    fputs(",\n  \"title_host_source\": ", file);
    porpoise_json_write_string(
        file, request->recovery_target != NULL ?
            "reviewed_profile" : "prebuilt_directory");
    fputs(",\n  \"title_host_identity\": ", file);
    porpoise_json_write_string(file, result->title_host_identity);
    fputs(",\n  \"meson\": {\"path\": ", file);
    porpoise_json_write_string(file, result->preflight.meson_path);
    fputs(", \"version\": ", file);
    porpoise_json_write_string(file, result->preflight.meson_version);
    fputs("},\n  \"toolchain\": {\"family\": ", file);
    porpoise_json_write_string(file, result->preflight.compiler_family);
    fputs(", \"version\": ", file);
    porpoise_json_write_string(file, result->preflight.compiler_version);
    fputs(", \"target\": ", file);
    porpoise_json_write_string(file, result->preflight.compiler_target);
    fputs(", \"c\": ", file);
    porpoise_json_write_string(file, result->preflight.c_compiler_path);
    fputs(", \"cpp\": ", file);
    porpoise_json_write_string(file, result->preflight.cpp_compiler_path);
    fputs("},\n  \"executable\": ", file);
    porpoise_json_write_string(file, result->executable_path);
    fputs(",\n  \"executable_sha256\": ", file);
    porpoise_json_write_string(file, executable_sha256);
    fprintf(file, ",\n  \"runtime_file_count\": %zu\n}\n",
            result->runtime_file_count);
    if (fclose(file) != 0) {
        (void)porpoise_remove_tree(temporary, diagnostics);
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            temporary, "cannot finish build manifest");
    }
    status = build_revalidate_input_identities(
        request, result, "the build manifest was being published",
        diagnostics);
    if (status != PORPOISE_EXIT_OK) {
        if (build_validate_managed_cache_layout(request, result, NULL) ==
            PORPOISE_EXIT_OK)
            (void)porpoise_remove_tree(temporary, diagnostics);
        return status;
    }
    status = build_validate_managed_cache_layout(
        request, result, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (porpoise_path_exists(backup) &&
        !porpoise_remove_tree(backup, diagnostics)) {
        (void)porpoise_remove_tree(temporary, diagnostics);
        return PORPOISE_EXIT_IO;
    }
    if (porpoise_path_exists(result->manifest_path) &&
        !porpoise_move_path(result->manifest_path, backup, diagnostics)) {
        (void)porpoise_remove_tree(temporary, diagnostics);
        return PORPOISE_EXIT_IO;
    }
    if (!porpoise_move_path(temporary, result->manifest_path, diagnostics)) {
        if (porpoise_path_exists(backup)) {
            (void)porpoise_move_path(
                backup, result->manifest_path, diagnostics);
        }
        return PORPOISE_EXIT_IO;
    }
    if (porpoise_path_exists(backup) &&
        !porpoise_remove_tree(backup, diagnostics)) return PORPOISE_EXIT_IO;
    return PORPOISE_EXIT_OK;
}

int porpoise_project_build(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildResult *result,
    PorpoiseDiagnostics *diagnostics) {
    char native_file[PORPOISE_PATH_CAPACITY];
    char configured_marker[PORPOISE_PATH_CAPACITY];
    char target_option[64];
    const char *setup_argv[BUILD_MAX_PROCESS_ARGUMENTS];
    const char *compile_argv[6];
    PorpoiseProcessCapture capture;
    char discovered[PORPOISE_PATH_CAPACITY];
    size_t argument_count = 0U;
    int status;
    if (result == NULL) return PORPOISE_EXIT_INTERNAL;
    porpoise_build_result_init(result);
    status = porpoise_build_preflight(request, &result->preflight, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (build_cancelled(request)) return PORPOISE_EXIT_CANCELLED;
    if (request->recovery_target != NULL) {
        status = porpoise_recovery_title_host_validate(
            request->recovery_target, request->plan, diagnostics);
        if (status != PORPOISE_EXIT_OK) return status;
    }
    status = build_configuration_digest(
        request, &result->preflight, result->configuration_digest,
        result->generated_output_identity,
        result->libporpoise_identity, result->sdl2_dependency_identity,
        result->title_host_identity, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    build_progress(
        request, PORPOISE_BUILD_PHASE_BIND_DEPENDENCIES, 0U, 3U,
        "Preparing managed build staging");
    status = build_prepare_layout(request, result, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
#ifdef _WIN32
    {
        char staged_sdl2_identity[PORPOISE_BUILD_ID_CAPACITY + 1U];
        status = build_sdl2_dependency_identity(
            request, staged_sdl2_identity, diagnostics);
        if (status != PORPOISE_EXIT_OK) return status;
        if (strcmp(
                staged_sdl2_identity,
                result->sdl2_dependency_identity) != 0) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR,
                PORPOISE_EXIT_USAGE, request->libporpoise_directory,
                "SDL2 dependency inputs changed while build staging was prepared; retry the build so the cache identity remains immutable");
        }
    }
#endif
    build_progress(
        request, PORPOISE_BUILD_PHASE_BIND_DEPENDENCIES, 2U, 3U,
        "Writing the compiler configuration");
    status = build_write_native_file(
        request, result, native_file, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    build_progress(
        request, PORPOISE_BUILD_PHASE_BIND_DEPENDENCIES, 3U, 3U,
        "Dependencies are staged");
    status = build_validate_managed_cache_layout(
        request, result, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    status = build_revalidate_input_identities(
        request, result, "a cached success was being selected", diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (!request->force_reconfigure && build_load_success_manifest(result)) {
        if (result->executable_available) {
            status = build_stage_runtime(request, result, diagnostics);
            if (status != PORPOISE_EXIT_OK) return status;
        }
        status = build_revalidate_input_identities(
            request, result, "a cached success was being accepted",
            diagnostics);
        if (status != PORPOISE_EXIT_OK) {
            result->cache_reused = false;
            return status;
        }
        build_progress(
            request, PORPOISE_BUILD_PHASE_COMPILE, 1U, 1U,
            result->executable_available ?
                "Reused the verified runnable build cache" :
                "Reused the verified library-only build cache");
        return PORPOISE_EXIT_OK;
    }
    if (build_cancelled(request)) return PORPOISE_EXIT_CANCELLED;

    if (!porpoise_path_join(
            configured_marker, sizeof(configured_marker),
            result->build_directory, "meson-private/coredata.dat") ||
        !porpoise_format(
            target_option, sizeof(target_option),
#ifdef _WIN32
            "-DlibPorpoise:build_target=win64"
#else
            "-DlibPorpoise:build_target=linux"
#endif
            )) return PORPOISE_EXIT_INTERNAL;
    setup_argv[argument_count++] = result->preflight.meson_path;
    setup_argv[argument_count++] = "setup";
    setup_argv[argument_count++] = result->build_directory;
    setup_argv[argument_count++] = result->source_directory;
    setup_argv[argument_count++] = "--wrap-mode=nodownload";
    setup_argv[argument_count++] = "--buildtype";
    setup_argv[argument_count++] = build_default(
        request->build_type, "debugoptimized");
    setup_argv[argument_count++] = "--native-file";
    setup_argv[argument_count++] = native_file;
    setup_argv[argument_count++] = target_option;
    setup_argv[argument_count++] = "-DlibPorpoise:tests=disabled";
    setup_argv[argument_count++] = "-DlibPorpoise:benchmarks=disabled";
    if (build_is_file(configured_marker) || request->force_reconfigure)
        setup_argv[argument_count++] = "--reconfigure";
    setup_argv[argument_count] = NULL;
    build_progress(
        request, PORPOISE_BUILD_PHASE_CONFIGURE, 0U, 1U,
        "Configuring the generated target");
    status = build_validate_managed_cache_layout(
        request, result, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    porpoise_process_capture_init(&capture);
    status = build_run_process(
        request, PORPOISE_BUILD_PHASE_CONFIGURE, setup_argv, NULL,
        build_clean_toolchain_environment,
        sizeof(build_clean_toolchain_environment) /
            sizeof(build_clean_toolchain_environment[0]),
        &capture, diagnostics, "Meson configuration");
    porpoise_process_capture_free(&capture);
    if (status != PORPOISE_EXIT_OK) return status;
    result->configured = true;
    build_progress(
        request, PORPOISE_BUILD_PHASE_CONFIGURE, 1U, 1U,
        "Configuration completed");
    if (build_cancelled(request)) return PORPOISE_EXIT_CANCELLED;
    compile_argv[0] = result->preflight.meson_path;
    compile_argv[1] = "compile";
    compile_argv[2] = "-C";
    compile_argv[3] = result->build_directory;
    compile_argv[4] = NULL;
    build_progress(
        request, PORPOISE_BUILD_PHASE_COMPILE, 0U, 1U,
        "Compiling the generated target");
    status = build_validate_managed_cache_layout(
        request, result, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    porpoise_process_capture_init(&capture);
    status = build_run_process(
        request, PORPOISE_BUILD_PHASE_COMPILE, compile_argv, NULL,
        build_clean_toolchain_environment,
        sizeof(build_clean_toolchain_environment) /
            sizeof(build_clean_toolchain_environment[0]),
        &capture, diagnostics, "Meson compilation");
    porpoise_process_capture_free(&capture);
    if (status != PORPOISE_EXIT_OK) return status;
    result->compiled = true;
    status = build_validate_managed_cache_layout(
        request, result, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    status = build_revalidate_input_identities(
        request, result, "the generated target was compiling", diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    status = build_discover_executable(
        request, result, discovered, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    if (discovered[0] != '\0') {
        status = build_copy_executable(
            request, discovered, result, diagnostics);
        if (status != PORPOISE_EXIT_OK) return status;
        result->executable_available = true;
        status = build_stage_runtime(request, result, diagnostics);
        if (status != PORPOISE_EXIT_OK) return status;
    }
    status = build_write_manifest(request, result, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    build_progress(
        request, PORPOISE_BUILD_PHASE_COMPILE, 1U, 1U,
        result->executable_available ?
            "Build completed and is runnable" :
            "Library-only build completed");
    return PORPOISE_EXIT_OK;
}

static int build_reserve_status_file(
    PorpoiseBuildResult *build,
    PorpoiseDiagnostics *diagnostics) {
    static const char pending[] = "PORPOISE_STATUS_PENDING\n";
    char directory[PORPOISE_PATH_CAPACITY];
    unsigned int attempt;
    if (!porpoise_path_join(
            directory, sizeof(directory), build->cache_directory,
            "run-status") ||
        !porpoise_make_directories(directory, diagnostics))
        return PORPOISE_EXIT_IO;
    for (attempt = 0U; attempt < 1024U; attempt++) {
        if (!porpoise_format(
                build->status_file_path,
                sizeof(build->status_file_path), "%s/run-%lu-%u.status",
                directory, BUILD_PROCESS_ID(), attempt))
            return PORPOISE_EXIT_INTERNAL;
#ifdef _WIN32
        {
            HANDLE file = CreateFileA(
                build->status_file_path, GENERIC_WRITE, 0U, NULL,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            DWORD written = 0U;
            bool ok;
            if (file == INVALID_HANDLE_VALUE) {
                DWORD error = GetLastError();
                if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
                    continue;
                return build_diagnostic(
                    diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                    build->status_file_path,
                    "cannot reserve guest status file (Windows error %lu)",
                    (unsigned long)error);
            }
            ok = WriteFile(
                     file, pending, (DWORD)(sizeof(pending) - 1U),
                     &written, NULL) != 0 &&
                 written == (DWORD)(sizeof(pending) - 1U) &&
                 FlushFileBuffers(file) != 0;
            if (!CloseHandle(file)) ok = false;
            if (!ok) {
                (void)DeleteFileA(build->status_file_path);
                return build_diagnostic(
                    diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                    build->status_file_path,
                    "cannot initialize guest status file");
            }
            return PORPOISE_EXIT_OK;
        }
#else
        {
            int file = open(
                build->status_file_path,
                O_WRONLY | O_CREAT | O_EXCL, 0600);
            size_t offset = 0U;
            bool ok = true;
            if (file < 0) {
                if (errno == EEXIST) continue;
                return build_diagnostic(
                    diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                    build->status_file_path,
                    "cannot reserve guest status file: %s", strerror(errno));
            }
            while (offset < sizeof(pending) - 1U) {
                ssize_t count = write(
                    file, pending + offset,
                    sizeof(pending) - 1U - offset);
                if (count > 0) offset += (size_t)count;
                else if (count < 0 && errno == EINTR) continue;
                else {
                    ok = false;
                    break;
                }
            }
            if (ok && fsync(file) != 0) ok = false;
            if (close(file) != 0) ok = false;
            if (!ok) {
                (void)unlink(build->status_file_path);
                return build_diagnostic(
                    diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                    build->status_file_path,
                    "cannot initialize guest status file");
            }
            return PORPOISE_EXIT_OK;
        }
#endif
    }
    return build_diagnostic(
        diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
        directory,
        "cannot allocate a unique guest status file; clean stale run-status files");
}

static void build_cleanup_status_file(
    PorpoiseBuildResult *build,
    PorpoiseDiagnostics *diagnostics,
    int *status) {
    bool removed;
    unsigned long error_code = 0UL;
    if (build == NULL || status == NULL ||
        build->status_file_path[0] == '\0') {
        return;
    }
#ifdef _WIN32
    removed = DeleteFileA(build->status_file_path) != 0;
    if (!removed) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            removed = true;
        else error_code = (unsigned long)error;
    }
#else
    removed = unlink(build->status_file_path) == 0;
    if (!removed) {
        if (errno == ENOENT) removed = true;
        else error_code = (unsigned long)errno;
    }
#endif
    if (removed) return;
    if (*status == PORPOISE_EXIT_OK) {
        *status = build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
            build->status_file_path,
            "cannot remove the completed guest status file (error %lu)",
            error_code);
    } else {
        (void)build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_WARNING, *status,
            build->status_file_path,
            "cannot remove the completed guest status file (error %lu)",
            error_code);
    }
}

static bool build_parse_guest_pc(
    const char *text,
    size_t length,
    uint32_t *value) {
    uint32_t parsed = 0U;
    size_t index;
    if (length != 8U) return false;
    for (index = 0U; index < length; index++) {
        unsigned char character = (unsigned char)text[index];
        unsigned int digit;
        if (character >= '0' && character <= '9') digit = character - '0';
        else if (character >= 'a' && character <= 'f')
            digit = character - 'a' + 10U;
        else if (character >= 'A' && character <= 'F')
            digit = character - 'A' + 10U;
        else return false;
        parsed = (parsed << 4U) | digit;
    }
    *value = parsed;
    return true;
}

static int build_read_guest_status(
    PorpoiseBuildResult *build,
    PorpoiseDiagnostics *diagnostics) {
    char contents[PORPOISE_MESSAGE_CAPACITY + 128U];
    FILE *file;
    long length;
    char *status;
    char *pc;
    char *message;
    char *separator;
    size_t status_length;
    size_t pc_length;
    bool fault;
    static const char prefix[] = PORPOISE_BUILD_STATUS_MAGIC "\t";
    if (!build_is_file(build->status_file_path)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            PORPOISE_EXIT_TRANSLATION, build->status_file_path,
            "native title exited without publishing guest completion status");
    }
    file = fopen(build->status_file_path, "rb");
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 ||
        (size_t)length >= sizeof(contents) ||
        fseek(file, 0L, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            PORPOISE_EXIT_TRANSLATION, build->status_file_path,
            "guest completion status is missing, unreadable, or too large");
    }
    if (fread(contents, 1U, (size_t)length, file) != (size_t)length ||
        fclose(file) != 0) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            PORPOISE_EXIT_TRANSLATION, build->status_file_path,
            "cannot read guest completion status");
    }
    if (memchr(contents, '\0', (size_t)length) != NULL) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            PORPOISE_EXIT_TRANSLATION, build->status_file_path,
            "native title published a malformed guest status line");
    }
    contents[length] = '\0';
    if (length > 0 && contents[length - 1L] == '\n')
        contents[--length] = '\0';
    if (length > 0 && contents[length - 1L] == '\r')
        contents[--length] = '\0';
    if (strncmp(contents, prefix, sizeof(prefix) - 1U) != 0 ||
        strchr(contents, '\n') != NULL || strchr(contents, '\r') != NULL) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            PORPOISE_EXIT_TRANSLATION, build->status_file_path,
            "native title did not publish a valid Porpoise guest status line");
    }
    status = contents + sizeof(prefix) - 1U;
    separator = strchr(status, '\t');
    if (separator == NULL) goto malformed;
    status_length = (size_t)(separator - status);
    pc = separator + 1U;
    separator = strchr(pc, '\t');
    if (separator == NULL) goto malformed;
    pc_length = (size_t)(separator - pc);
    message = separator + 1U;
    if (message[0] == '\0' || strchr(message, '\t') != NULL ||
        !build_parse_guest_pc(pc, pc_length, &build->guest_pc) ||
        !porpoise_copy_string(
            build->guest_status_message,
            sizeof(build->guest_status_message), message)) goto malformed;
    fault = status_length == strlen("FAULT") &&
            memcmp(status, "FAULT", status_length) == 0;
    if (!fault &&
        !(status_length == strlen("OK") &&
          memcmp(status, "OK", status_length) == 0)) goto malformed;
    build->guest_status_reported = true;
    build->guest_faulted = fault;
    if (fault) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            PORPOISE_EXIT_TRANSLATION, build->status_file_path,
            "guest fault at 0x%08X: %s", build->guest_pc,
            build->guest_status_message);
    }
    return PORPOISE_EXIT_OK;

malformed:
    return build_diagnostic(
        diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_TRANSLATION,
        build->status_file_path,
        "native title did not publish a valid Porpoise guest status line");
}

static bool build_environment_name_equals(
    const char *left,
    const char *right) {
    if (left == NULL || right == NULL) return false;
#ifdef _WIN32
    return _stricmp(left, right) == 0;
#else
    return strcmp(left, right) == 0;
#endif
}

static bool build_reserved_environment_name(const char *name) {
    return build_environment_name_equals(
               name, PORPOISE_BUILD_STATUS_FILE_ENV) ||
           build_environment_name_equals(name, "PORPOISE_DVD_ROOT") ||
           build_environment_name_equals(name, "PORPOISE_TRACE") ||
           build_environment_name_equals(name, "PORPOISE_FRAME_LIMIT") ||
           build_environment_name_equals(
               name, PORPOISE_REJECT_APPROXIMATIONS_ENV);
}

int porpoise_project_run(
    const PorpoiseBuildRequest *request,
    PorpoiseBuildResult *build,
    PorpoiseDiagnostics *diagnostics) {
    const char **argv;
    PorpoiseBuildEnvironmentEntry *environment;
    size_t argument_count;
    size_t environment_count;
    size_t index;
    char frame_limit[32];
    char trace_candidate[PORPOISE_PATH_CAPACITY];
    char trace_file[PORPOISE_PATH_CAPACITY];
    char trace_parent[PORPOISE_PATH_CAPACITY];
    char working_directory[PORPOISE_PATH_CAPACITY];
    PorpoiseProcessCapture capture;
    int status;
    bool paths_overlap;
    if (request == NULL || build == NULL || !build->compiled ||
        !build->executable_available || !build_is_file(build->executable_path)) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE, "",
            "Run requires a successful executable build");
    }
    if (request->recovery_target != NULL &&
        request->recovery_target->has_title_host &&
        request->recovery_target->title_host.initialize_dvd &&
        (request->dvd_root == NULL || request->dvd_root[0] == '\0')) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
            request->project_file,
            "this reviewed title-host profile initializes DVD; configure a nonempty machine-local DVD root before Run");
    }
    if ((request->run_argument_count != 0U &&
         request->run_arguments == NULL) ||
        (request->environment_count != 0U && request->environment == NULL) ||
        request->run_argument_count > SIZE_MAX - 2U ||
        request->environment_count > SIZE_MAX - 5U) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE, "",
            "Run argument or environment arrays are invalid");
    }
    for (index = 0U; index < request->environment_count; index++) {
        const char *name = request->environment[index].name;
        if (name == NULL || name[0] == '\0' || strchr(name, '=') != NULL ||
            request->environment[index].value == NULL ||
            build_reserved_environment_name(name)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR,
                PORPOISE_EXIT_USAGE, "",
                "Run environment entry %zu is invalid or overrides a Porpoise-owned runtime setting",
                index);
        }
    }
    if (request->frame_limit > (size_t)UINT32_MAX) {
        return build_diagnostic(
            diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE, "",
            "frame limit must be from 1 to %lu when specified",
            (unsigned long)UINT32_MAX);
    }
    if (request->run_working_directory != NULL &&
        request->run_working_directory[0] != '\0') {
        if (!porpoise_path_is_directory(request->run_working_directory)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                request->run_working_directory,
                "run working directory does not exist");
        }
        if (!porpoise_path_normalize_lexical(
                working_directory, sizeof(working_directory),
                request->run_working_directory)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                request->run_working_directory,
                "cannot normalize the run working directory");
        }
    } else if (!porpoise_path_parent(
                   working_directory, sizeof(working_directory),
                   build->executable_path)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    trace_file[0] = '\0';
    if (request->trace_file != NULL && request->trace_file[0] != '\0') {
        if (porpoise_path_is_absolute(request->trace_file)) {
            if (!porpoise_copy_string(
                    trace_candidate, sizeof(trace_candidate),
                    request->trace_file)) {
                return build_diagnostic(
                    diagnostics, PORPOISE_SEVERITY_ERROR,
                    PORPOISE_EXIT_USAGE, request->trace_file,
                    "trace output path is too long");
            }
        } else if (!porpoise_path_join(
                       trace_candidate, sizeof(trace_candidate),
                       working_directory, request->trace_file)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR,
                PORPOISE_EXIT_USAGE, request->trace_file,
                "trace output path is too long");
        }
        if (!porpoise_path_normalize_lexical(
                trace_file, sizeof(trace_file), trace_candidate)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                request->trace_file,
                "cannot normalize the trace output path");
        }
        if (porpoise_path_is_directory(trace_file)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                trace_file,
                "trace output path names an existing directory");
        }
        if (!porpoise_path_trees_overlap(
                trace_file, build->executable_path, &paths_overlap)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                trace_file,
                "cannot compare trace and executable paths safely");
        }
        if (paths_overlap) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                trace_file,
                "trace output path must not overwrite the generated executable");
        }
        if (request->project_file != NULL &&
            request->project_file[0] != '\0') {
            if (!porpoise_path_trees_overlap(
                    trace_file, request->project_file, &paths_overlap)) {
                return build_diagnostic(
                    diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_IO,
                    trace_file,
                    "cannot compare trace and project paths safely");
            }
            if (paths_overlap) {
                return build_diagnostic(
                    diagnostics, PORPOISE_SEVERITY_ERROR,
                    PORPOISE_EXIT_USAGE, trace_file,
                    "trace output path must not overwrite the project file");
            }
        }
        if (!porpoise_path_parent(
                trace_parent, sizeof(trace_parent), trace_file)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                trace_file,
                "trace output path has no writable parent directory");
        }
        if (!porpoise_make_directories(trace_parent, diagnostics)) {
            return PORPOISE_EXIT_IO;
        }
        if (!porpoise_path_is_directory(trace_parent)) {
            return build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR, PORPOISE_EXIT_USAGE,
                trace_parent,
                "trace output parent path is not a directory");
        }
    }
    build->guest_status_reported = false;
    build->guest_faulted = false;
    build->guest_pc = 0U;
    build->guest_status_message[0] = '\0';
    build->status_file_path[0] = '\0';
    status = build_reserve_status_file(build, diagnostics);
    if (status != PORPOISE_EXIT_OK) return status;
    argument_count = request->run_argument_count + 2U;
    argv = (const char **)calloc(argument_count, sizeof(*argv));
    environment_count = request->environment_count +
        (request->dvd_root != NULL && request->dvd_root[0] != '\0' ? 1U : 0U) +
        (request->trace_file != NULL && request->trace_file[0] != '\0' ? 1U : 0U) +
        (request->frame_limit != 0U ? 1U : 0U) + 1U;
    if (request->reject_approximations) environment_count++;
    environment = (PorpoiseBuildEnvironmentEntry *)calloc(
        environment_count, sizeof(*environment));
    if (argv == NULL || environment == NULL) {
        free(argv);
        free(environment);
        status = PORPOISE_EXIT_INTERNAL;
        build_cleanup_status_file(build, diagnostics, &status);
        return status;
    }
    argv[0] = build->executable_path;
    for (index = 0U; index < request->run_argument_count; index++)
        argv[index + 1U] = request->run_arguments[index];
    argv[request->run_argument_count + 1U] = NULL;
    for (index = 0U; index < request->environment_count; index++)
        environment[index] = request->environment[index];
    environment_count = request->environment_count;
    if (request->dvd_root != NULL && request->dvd_root[0] != '\0') {
        environment[environment_count].name = "PORPOISE_DVD_ROOT";
        environment[environment_count++].value = request->dvd_root;
    }
    if (request->trace_file != NULL && request->trace_file[0] != '\0') {
        environment[environment_count].name = "PORPOISE_TRACE";
        environment[environment_count++].value = trace_file;
    }
    if (request->frame_limit != 0U) {
        if (!porpoise_format(
                frame_limit, sizeof(frame_limit), "%zu",
                request->frame_limit)) {
            free(argv);
            free(environment);
            status = PORPOISE_EXIT_INTERNAL;
            build_cleanup_status_file(build, diagnostics, &status);
            return status;
        }
        environment[environment_count].name = "PORPOISE_FRAME_LIMIT";
        environment[environment_count++].value = frame_limit;
    }
    if (request->reject_approximations) {
        environment[environment_count].name =
            PORPOISE_REJECT_APPROXIMATIONS_ENV;
        environment[environment_count++].value = "1";
    }
    environment[environment_count].name = PORPOISE_BUILD_STATUS_FILE_ENV;
    environment[environment_count++].value = build->status_file_path;
    build_progress(
        request, PORPOISE_BUILD_PHASE_RUN, 0U, 1U,
        "Running the generated target");
    porpoise_process_capture_init(&capture);
    status = porpoise_process_run(
        argv, working_directory, environment, environment_count,
        PORPOISE_BUILD_PHASE_RUN, &request->callbacks, &capture,
        diagnostics);
    free(argv);
    free(environment);
    if (status == PORPOISE_EXIT_OK) {
        build->process_exit_code = capture.exit_code;
        if (capture.exit_code != 0) {
            const char *detail = capture.standard_error != NULL &&
                                 capture.standard_error[0] != '\0' ?
                capture.standard_error : capture.standard_output;
            status = build_diagnostic(
                diagnostics, PORPOISE_SEVERITY_ERROR,
                PORPOISE_EXIT_TRANSLATION, build->executable_path,
                "generated target exited with code %d%s%s",
                capture.exit_code,
                detail != NULL && detail[0] != '\0' ? ": " : "",
                detail != NULL && detail[0] != '\0' ? detail : "");
        } else {
            status = build_read_guest_status(build, diagnostics);
        }
    }
    porpoise_process_capture_free(&capture);
    build_cleanup_status_file(build, diagnostics, &status);
    if (status == PORPOISE_EXIT_OK) {
        build_progress(
            request, PORPOISE_BUILD_PHASE_RUN, 1U, 1U,
            "Run completed");
    }
    return status;
}
