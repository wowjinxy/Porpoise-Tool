#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "porpoise/build.h"
#include "porpoise/util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#define TEST_MKDIR(path) _mkdir(path)
#define TEST_CHDIR(path) _chdir(path)
#define TEST_GETCWD(buffer, capacity) _getcwd((buffer), (int)(capacity))
#define TEST_PROCESS_ID() ((unsigned long)_getpid())
#define TEST_SET_ENV(name, value) \
    (SetEnvironmentVariableA((name), (value)) != 0)
#else
#include <sys/stat.h>
#include <unistd.h>
#define TEST_MKDIR(path) mkdir((path), 0755)
#define TEST_CHDIR(path) chdir(path)
#define TEST_GETCWD(buffer, capacity) getcwd((buffer), (capacity))
#define TEST_PROCESS_ID() ((unsigned long)getpid())
#define TEST_SET_ENV(name, value) (setenv((name), (value), 1) == 0)
#endif

#ifdef _WIN32
typedef struct TestMountPointReparseBuffer {
    DWORD reparse_tag;
    WORD reparse_data_length;
    WORD reserved;
    WORD substitute_name_offset;
    WORD substitute_name_length;
    WORD print_name_offset;
    WORD print_name_length;
    WCHAR path_buffer[(PORPOISE_PATH_CAPACITY * 2U) + 16U];
} TestMountPointReparseBuffer;

static bool test_windows_extended_path(
    const char *path,
    WCHAR output[PORPOISE_PATH_CAPACITY + 16U]) {
    WCHAR converted[PORPOISE_PATH_CAPACITY];
    WCHAR absolute[PORPOISE_PATH_CAPACITY];
    DWORD length;
    size_t index;
    int converted_size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, converted,
        (int)(sizeof(converted) / sizeof(converted[0])));
    if (converted_size <= 0) return false;
    length = GetFullPathNameW(
        converted, (DWORD)(sizeof(absolute) / sizeof(absolute[0])),
        absolute, NULL);
    if (length == 0U || length >= sizeof(absolute) / sizeof(absolute[0]))
        return false;
    for (index = 0U; index < (size_t)length; index++) {
        if (absolute[index] == L'/') absolute[index] = L'\\';
    }
    if (length >= 2U && absolute[0] == L'\\' && absolute[1] == L'\\') {
        static const WCHAR prefix[] = L"\\\\?\\UNC\\";
        size_t prefix_size = sizeof(prefix) / sizeof(prefix[0]) - 1U;
        if (prefix_size + (size_t)length - 2U + 1U >
            PORPOISE_PATH_CAPACITY + 16U) return false;
        memcpy(output, prefix, prefix_size * sizeof(*output));
        memcpy(
            output + prefix_size, absolute + 2U,
            ((size_t)length - 2U + 1U) * sizeof(*output));
    } else {
        static const WCHAR prefix[] = L"\\\\?\\";
        size_t prefix_size = sizeof(prefix) / sizeof(prefix[0]) - 1U;
        if (prefix_size + (size_t)length + 1U >
            PORPOISE_PATH_CAPACITY + 16U) return false;
        memcpy(output, prefix, prefix_size * sizeof(*output));
        memcpy(
            output + prefix_size, absolute,
            ((size_t)length + 1U) * sizeof(*output));
    }
    return true;
}

static bool test_create_extended_directory(const char *path) {
    WCHAR extended[PORPOISE_PATH_CAPACITY + 16U];
    return test_windows_extended_path(path, extended) &&
           CreateDirectoryW(extended, NULL) != 0;
}

static bool test_create_extended_file(const char *path) {
    WCHAR extended[PORPOISE_PATH_CAPACITY + 16U];
    HANDLE file;
    if (!test_windows_extended_path(path, extended)) return false;
    file = CreateFileW(
        extended, GENERIC_WRITE, 0U, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    return CloseHandle(file) != 0;
}

static bool test_create_directory_link(
    const char *source,
    const char *destination) {
    char absolute_source[PORPOISE_PATH_CAPACITY];
    WCHAR print_name[PORPOISE_PATH_CAPACITY];
    WCHAR substitute_name[PORPOISE_PATH_CAPACITY + 16U];
    TestMountPointReparseBuffer reparse;
    DWORD absolute_length;
    DWORD bytes_returned = 0U;
    size_t print_length;
    size_t substitute_length;
    size_t path_bytes;
    size_t input_bytes;
    int converted;
    HANDLE handle;
    bool ok = false;
    static const WCHAR prefix[] = L"\\??\\";
    size_t prefix_length = sizeof(prefix) / sizeof(prefix[0]) - 1U;
    absolute_length = GetFullPathNameA(
        source, (DWORD)sizeof(absolute_source), absolute_source, NULL);
    if (absolute_length == 0U || absolute_length >= sizeof(absolute_source))
        return false;
    converted = MultiByteToWideChar(
        CP_ACP, 0U, absolute_source, -1, print_name,
        (int)(sizeof(print_name) / sizeof(print_name[0])));
    if (converted <= 1) return false;
    print_length = (size_t)converted - 1U;
    if (prefix_length + print_length + 1U >
        sizeof(substitute_name) / sizeof(substitute_name[0])) return false;
    memcpy(substitute_name, prefix, prefix_length * sizeof(WCHAR));
    memcpy(
        substitute_name + prefix_length, print_name,
        (print_length + 1U) * sizeof(WCHAR));
    substitute_length = prefix_length + print_length;
    path_bytes =
        (substitute_length + 1U + print_length + 1U) * sizeof(WCHAR);
    if (path_bytes > sizeof(reparse.path_buffer) ||
        path_bytes + 8U > UINT16_MAX) return false;
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

static bool test_remove_directory_link(const char *path) {
    return RemoveDirectoryA(path) != 0;
}
#else
static bool test_create_directory_link(
    const char *source,
    const char *destination) {
    return symlink(source, destination) == 0;
}

static bool test_remove_directory_link(const char *path) {
    return unlink(path) == 0;
}
#endif

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

#ifdef _WIN32
static void test_long_path_remove_tree(const char *temporary) {
    static const char component[] =
        "segment-abcdefghijklmnopqrstuvwxyz-0123456789";
    char root[PORPOISE_PATH_CAPACITY];
    char current[PORPOISE_PATH_CAPACITY];
    char next[PORPOISE_PATH_CAPACITY];
    char sentinel[PORPOISE_PATH_CAPACITY];
    PorpoiseDiagnostics diagnostics;
    bool ok;
    CHECK(porpoise_path_join(
        root, sizeof(root), temporary, "long-path-removal"));
    if (!test_create_extended_directory(root)) {
        CHECK(false);
        return;
    }
    CHECK(porpoise_copy_string(current, sizeof(current), root));
    while (strlen(current) <= 280U) {
        if (!porpoise_path_join(
                next, sizeof(next), current, component) ||
            !test_create_extended_directory(next) ||
            !porpoise_copy_string(current, sizeof(current), next)) {
            CHECK(false);
            return;
        }
    }
    CHECK(porpoise_path_join(
        sentinel, sizeof(sentinel), current, "sentinel.txt"));
    CHECK(strlen(sentinel) > 260U);
    CHECK(test_create_extended_file(sentinel));
    porpoise_diagnostics_init(&diagnostics);
    ok = porpoise_remove_tree(root, &diagnostics);
    if (!ok && diagnostics.count != 0U) {
        fprintf(
            stderr, "long-path cleanup diagnostic: %s: %s\n",
            diagnostics.items[diagnostics.count - 1U].file,
            diagnostics.items[diagnostics.count - 1U].message);
    }
    CHECK(ok);
    CHECK(GetFileAttributesA(root) == INVALID_FILE_ATTRIBUTES);
    porpoise_diagnostics_free(&diagnostics);
}
#endif

typedef struct TestCallbacks {
    bool saw_compile_log;
    bool cancel;
    bool mutation_armed;
    bool mutation_done;
    bool mutation_succeeded;
    PorpoiseBuildPhase mutation_phase;
    size_t mutation_completed;
    const char *mutation_path;
    const char *mutation_text;
} TestCallbacks;

static void test_progress(
    void *user_data,
    PorpoiseBuildPhase phase,
    size_t completed,
    size_t total,
    const char *detail) {
    TestCallbacks *callbacks = (TestCallbacks *)user_data;
    FILE *file;
    (void)total;
    (void)detail;
    if (!callbacks->mutation_armed || callbacks->mutation_done ||
        phase != callbacks->mutation_phase ||
        completed != callbacks->mutation_completed) return;
    callbacks->mutation_done = true;
    file = fopen(callbacks->mutation_path, "ab");
    if (file == NULL) return;
    callbacks->mutation_succeeded =
        fputs(callbacks->mutation_text, file) >= 0;
    if (fclose(file) != 0) callbacks->mutation_succeeded = false;
}

static void test_log(
    void *user_data,
    PorpoiseBuildPhase phase,
    bool standard_error,
    const char *text,
    size_t length) {
    TestCallbacks *callbacks = (TestCallbacks *)user_data;
    (void)standard_error;
    if (phase == PORPOISE_BUILD_PHASE_COMPILE && length != 0U)
        callbacks->saw_compile_log = true;
    if (phase == PORPOISE_BUILD_PHASE_RUN && length >= 15U) {
        const char marker[] = "ready-to-cancel";
        size_t index;
        for (index = 0U; index + sizeof(marker) - 1U <= length; index++) {
            if (memcmp(text + index, marker, sizeof(marker) - 1U) == 0) {
                callbacks->cancel = true;
                break;
            }
        }
    }
}

static bool test_cancelled(void *user_data) {
    return ((TestCallbacks *)user_data)->cancel;
}

static bool write_project_file(const char *path) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    if (fputs("{\"schema_version\":2,\"targets\":[]}", file) < 0) {
        fclose(file);
        return false;
    }
    return fclose(file) == 0;
}

static bool write_text_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    if (fputs(text, file) < 0) {
        fclose(file);
        return false;
    }
    return fclose(file) == 0;
}

static bool file_contains(const char *path, const char *needle) {
    FILE *file = fopen(path, "rb");
    char *contents;
    long length;
    bool found;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 || fseek(file, 0L, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return false;
    }
    contents = (char *)malloc((size_t)length + 1U);
    if (contents == NULL) {
        fclose(file);
        return false;
    }
    if (fread(contents, 1U, (size_t)length, file) != (size_t)length ||
        fclose(file) != 0) {
        free(contents);
        return false;
    }
    contents[length] = '\0';
    found = strstr(contents, needle) != NULL;
    free(contents);
    return found;
}

#ifdef _WIN32
static bool directory_has_exact_entries(
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

static bool copy_fixture_file(
    const char *source_root,
    const char *destination_root,
    const char *relative,
    PorpoiseDiagnostics *diagnostics) {
    char source[PORPOISE_PATH_CAPACITY];
    char destination[PORPOISE_PATH_CAPACITY];
    char parent[PORPOISE_PATH_CAPACITY];
    return porpoise_path_join(
               source, sizeof(source), source_root, relative) &&
           porpoise_path_join(
               destination, sizeof(destination), destination_root, relative) &&
           porpoise_path_parent(parent, sizeof(parent), destination) &&
           porpoise_make_directories(parent, diagnostics) &&
           porpoise_copy_file(source, destination, diagnostics);
}
#endif

static bool sibling_cpp(
    const char *c_compiler,
    char output[PORPOISE_PATH_CAPACITY]) {
    char directory[PORPOISE_PATH_CAPACITY];
    const char *names[] = {
#ifdef _WIN32
        "c++.exe", "clang++.exe", "g++.exe"
#else
        "c++", "clang++", "g++"
#endif
    };
    size_t index;
    if (!porpoise_path_parent(directory, sizeof(directory), c_compiler))
        return false;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); index++) {
        if (porpoise_path_join(
                output, PORPOISE_PATH_CAPACITY, directory, names[index]) &&
            porpoise_path_exists(output)) return true;
    }
    return false;
}

static bool resolve_executable(
    const char *selection,
    char output[PORPOISE_PATH_CAPACITY]) {
#ifdef _WIN32
    DWORD length = SearchPathA(
        NULL, selection, ".exe", PORPOISE_PATH_CAPACITY, output, NULL);
    return length != 0U && length < PORPOISE_PATH_CAPACITY;
#else
    const char *path = getenv("PATH");
    const char *cursor;
    if (strchr(selection, '/') != NULL)
        return realpath(selection, output) != NULL;
    if (path == NULL) return false;
    cursor = path;
    for (;;) {
        const char *separator = strchr(cursor, ':');
        size_t length = separator == NULL ? strlen(cursor) :
            (size_t)(separator - cursor);
        char directory[PORPOISE_PATH_CAPACITY];
        char candidate[PORPOISE_PATH_CAPACITY];
        if (length < sizeof(directory)) {
            memcpy(directory, cursor, length);
            directory[length] = '\0';
            if (porpoise_path_join(
                    candidate, sizeof(candidate),
                    length == 0U ? "." : directory, selection) &&
                access(candidate, X_OK) == 0 &&
                realpath(candidate, output) != NULL) return true;
        }
        if (separator == NULL) break;
        cursor = separator + 1U;
    }
    return false;
#endif
}

static void print_diagnostics(const PorpoiseDiagnostics *diagnostics) {
    size_t index;
    for (index = 0U; index < diagnostics->count; index++) {
        fprintf(stderr, "diagnostic: %s: %s\n",
                diagnostics->items[index].file,
                diagnostics->items[index].message);
    }
}

static bool diagnostics_contain(
    const PorpoiseDiagnostics *diagnostics,
    const char *needle) {
    size_t index;
    for (index = 0U; index < diagnostics->count; index++) {
        if (strstr(diagnostics->items[index].message, needle) != NULL)
            return true;
    }
    return false;
}

int main(int argc, char **argv) {
    char fixture[PORPOISE_PATH_CAPACITY];
    char fixture_generated[PORPOISE_PATH_CAPACITY];
    char generated[PORPOISE_PATH_CAPACITY];
    char fixture_libporpoise[PORPOISE_PATH_CAPACITY];
    char libporpoise[PORPOISE_PATH_CAPACITY];
    char title_host[PORPOISE_PATH_CAPACITY];
    char original_working_directory[PORPOISE_PATH_CAPACITY];
    char temporary[PORPOISE_PATH_CAPACITY];
    char project[PORPOISE_PATH_CAPACITY];
    char c_compiler[PORPOISE_PATH_CAPACITY];
    char cpp_compiler[PORPOISE_PATH_CAPACITY];
    char source_path[PORPOISE_PATH_CAPACITY];
    char destination_path[PORPOISE_PATH_CAPACITY];
    char absolute_trace[PORPOISE_PATH_CAPACITY];
    char relative_trace[PORPOISE_PATH_CAPACITY];
    char invalid_trace_parent[PORPOISE_PATH_CAPACITY];
    char invalid_trace[PORPOISE_PATH_CAPACITY];
    char cache_root[PORPOISE_PATH_CAPACITY];
    char cache_outside[PORPOISE_PATH_CAPACITY];
    char cache_outside_sentinel[PORPOISE_PATH_CAPACITY];
    char cache_outside_target[PORPOISE_PATH_CAPACITY];
    char cache_outside_digest[PORPOISE_PATH_CAPACITY];
    char cache_outside_build[PORPOISE_PATH_CAPACITY];
    char cache_target[PORPOISE_PATH_CAPACITY];
    char cache_digest[PORPOISE_PATH_CAPACITY];
    char digest_key[25];
    char overlapping_child[PORPOISE_PATH_CAPACITY];
#ifdef _WIN32
    char native_file[PORPOISE_PATH_CAPACITY];
    char overlay[PORPOISE_PATH_CAPACITY];
    char overlay_include[PORPOISE_PATH_CAPACITY];
    char overlay_headers[PORPOISE_PATH_CAPACITY];
    char overlay_library[PORPOISE_PATH_CAPACITY];
    char overlay_import[PORPOISE_PATH_CAPACITY];
    char sdl2_source_header[PORPOISE_PATH_CAPACITY];
    char sdl2_source_import[PORPOISE_PATH_CAPACITY];
    char sdl2_source_runtime[PORPOISE_PATH_CAPACITY];
#endif
    const char *run_ok[] = {"run-ok"};
    const char *run_bad[] = {"bad"};
    const char *run_fault[] = {"fault"};
    const char *run_missing[] = {"missing-status"};
    const char *run_cancel[] = {"cancel"};
    PorpoiseBuildRequest request;
    PorpoiseBuildResult first;
    PorpoiseBuildResult second;
#ifdef _WIN32
    PorpoiseBuildResult unrelated_ucrt_changed;
    PorpoiseBuildResult sdl2_runtime_changed;
    PorpoiseBuildResult sdl2_headers_changed;
    PorpoiseBuildResult sdl2_changed;
#endif
    PorpoiseBuildResult dependency_changed;
    PorpoiseBuildResult generated_changed;
    PorpoiseBuildResult cache_identity_changed;
    PorpoiseBuildResult compile_identity_changed;
    PorpoiseBuildResult repaired_executable;
    PorpoiseBuildResult failed_rebuild;
    PorpoiseBuildResult changed;
    PorpoiseRecoveryTarget dvd_target;
    PorpoiseDiagnostics diagnostics;
    TestCallbacks callbacks;
    bool changed_working_directory = false;
    int result;

    if (argc != 5) {
        fprintf(stderr, "usage: test_build SOURCE_ROOT BUILD_ROOT MESON CC\n");
        return 2;
    }
    CHECK(strcmp(
        porpoise_build_phase_name(PORPOISE_BUILD_PHASE_STAGE_RUNTIME),
        "stage runtime") == 0);
    CHECK(porpoise_path_join(
        fixture, sizeof(fixture), argv[1], "tests/fixtures/build_core"));
    CHECK(porpoise_path_join(
        fixture_generated, sizeof(fixture_generated), fixture, "generated"));
    CHECK(porpoise_path_join(
        fixture_libporpoise, sizeof(fixture_libporpoise), fixture,
        "libPorpoise"));
    CHECK(porpoise_path_join(
        title_host, sizeof(title_host), fixture, "title-host"));
    CHECK(porpoise_format(
        temporary, sizeof(temporary), "%s/build-core-test-%lu",
        argv[2], TEST_PROCESS_ID()));
    if (porpoise_path_exists(temporary)) {
        PorpoiseDiagnostics cleanup;
        porpoise_diagnostics_init(&cleanup);
        CHECK(porpoise_remove_tree(temporary, &cleanup));
        porpoise_diagnostics_free(&cleanup);
    }
    CHECK(TEST_MKDIR(temporary) == 0);
#ifdef _WIN32
    test_long_path_remove_tree(temporary);
#endif
    CHECK(porpoise_path_join(
        generated, sizeof(generated), temporary, "generated"));
    CHECK(TEST_MKDIR(generated) == 0);
    CHECK(porpoise_path_join(
        libporpoise, sizeof(libporpoise), temporary, "libPorpoise"));
    CHECK(TEST_MKDIR(libporpoise) == 0);
    porpoise_diagnostics_init(&diagnostics);
    {
        static const char *const generated_files[] = {
            "meson.build", "main.c", "helper.cpp", "build-break.h"
        };
        size_t generated_index;
        for (generated_index = 0U;
             generated_index <
                 sizeof(generated_files) / sizeof(generated_files[0]);
             generated_index++) {
            CHECK(porpoise_path_join(
                source_path, sizeof(source_path), fixture_generated,
                generated_files[generated_index]));
            CHECK(porpoise_path_join(
                destination_path, sizeof(destination_path), generated,
                generated_files[generated_index]));
            CHECK(porpoise_copy_file(
                source_path, destination_path, &diagnostics));
        }
    }
    CHECK(porpoise_path_join(
        source_path, sizeof(source_path), fixture_libporpoise,
        "meson.build"));
    CHECK(porpoise_path_join(
        destination_path, sizeof(destination_path), libporpoise,
        "meson.build"));
    CHECK(porpoise_copy_file(source_path, destination_path, &diagnostics));
    CHECK(porpoise_path_join(
        source_path, sizeof(source_path), fixture_libporpoise,
        "meson.options"));
    CHECK(porpoise_path_join(
        destination_path, sizeof(destination_path), libporpoise,
        "meson.options"));
    CHECK(porpoise_copy_file(source_path, destination_path, &diagnostics));
#ifdef _WIN32
    {
        static const char *const dependency_files[] = {
            "msys2/ucrt64/include/SDL2/SDL.h",
            "msys2/ucrt64/include/SDL2/SDL_config.h",
            "msys2/ucrt64/include/foreign_crt.h",
            "msys2/ucrt64/lib/libSDL2.dll.a",
            "msys2/ucrt64/lib/libSDL2.a",
            "msys2/ucrt64/lib/libmingw32.a",
            "msys2/ucrt64/bin/SDL2.dll"
        };
        size_t dependency_index;
        for (dependency_index = 0U;
             dependency_index <
                 sizeof(dependency_files) / sizeof(dependency_files[0]);
             dependency_index++) {
            CHECK(copy_fixture_file(
                fixture_libporpoise, libporpoise,
                dependency_files[dependency_index], &diagnostics));
        }
    }
#endif
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    porpoise_diagnostics_free(&diagnostics);
    CHECK(porpoise_path_join(
        project, sizeof(project), temporary, "fixture.porpoise.json"));
    CHECK(write_project_file(project));

    CHECK(resolve_executable(argv[4], c_compiler));
    CHECK(sibling_cpp(c_compiler, cpp_compiler));
    memset(&callbacks, 0, sizeof(callbacks));
    porpoise_build_request_init(&request);
    request.project_file = project;
    request.target_id = "fixture";
    request.generated_directory = generated;
    request.libporpoise_directory = libporpoise;
    request.title_host_directory = title_host;
    request.meson_executable = argv[3];
    request.c_compiler = c_compiler;
    request.cpp_compiler = cpp_compiler;
    request.generated_plan_digest = "plan-one";
    request.allow_copy_fallback = true;
    request.callbacks.progress = test_progress;
    request.callbacks.log = test_log;
    request.callbacks.cancelled = test_cancelled;
    request.callbacks.user_data = &callbacks;
    request.libporpoise_directory = "";
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_project_build(&request, &first, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics.count != 0U);
    porpoise_diagnostics_free(&diagnostics);
    request.libporpoise_directory = libporpoise;
    CHECK(porpoise_path_join(
        cache_root, sizeof(cache_root), temporary, ".porpoise-build"));
    CHECK(porpoise_path_join(
        overlapping_child, sizeof(overlapping_child), cache_root,
        "unsafe-input"));
    request.generated_directory = overlapping_child;
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_project_build(&request, &first, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "generated output tree"));
    porpoise_diagnostics_free(&diagnostics);
    request.generated_directory = generated;

    request.libporpoise_directory = temporary;
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_project_build(&request, &first, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "libPorpoise tree"));
    porpoise_diagnostics_free(&diagnostics);
    request.libporpoise_directory = libporpoise;

    request.title_host_directory = overlapping_child;
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_project_build(&request, &first, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "title host tree"));
    porpoise_diagnostics_free(&diagnostics);
    request.title_host_directory = title_host;

    CHECK(porpoise_path_join(
        cache_outside, sizeof(cache_outside), temporary,
        "cache-link-outside"));
    CHECK(TEST_MKDIR(cache_outside) == 0);
    CHECK(porpoise_path_join(
        cache_outside_sentinel, sizeof(cache_outside_sentinel),
        cache_outside, "sentinel.txt"));
    CHECK(write_text_file(cache_outside_sentinel, "outside sentinel\n"));
    CHECK(porpoise_path_join(
        cache_outside_target, sizeof(cache_outside_target),
        cache_outside, "fixture"));
    CHECK(test_create_directory_link(cache_outside, cache_root));
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_project_build(&request, &first, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "ordinary directory"));
    CHECK(file_contains(cache_outside_sentinel, "outside sentinel"));
    CHECK(!porpoise_path_exists(cache_outside_target));
    porpoise_diagnostics_free(&diagnostics);
    CHECK(test_remove_directory_link(cache_root));
    CHECK(file_contains(cache_outside_sentinel, "outside sentinel"));

    CHECK(TEST_MKDIR(cache_root) == 0);
    CHECK(porpoise_path_join(
        cache_target, sizeof(cache_target), cache_root, "fixture"));
    CHECK(test_create_directory_link(cache_outside, cache_target));
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_project_build(&request, &first, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "ordinary directories"));
    CHECK(strlen(first.configuration_digest) == PORPOISE_BUILD_ID_CAPACITY);
    memcpy(digest_key, first.configuration_digest, 24U);
    digest_key[24] = '\0';
    CHECK(porpoise_path_join(
        cache_outside_digest, sizeof(cache_outside_digest),
        cache_outside, digest_key));
    CHECK(file_contains(cache_outside_sentinel, "outside sentinel"));
    CHECK(!porpoise_path_exists(cache_outside_digest));
    porpoise_diagnostics_free(&diagnostics);
    CHECK(test_remove_directory_link(cache_target));
    CHECK(file_contains(cache_outside_sentinel, "outside sentinel"));

    CHECK(TEST_MKDIR(cache_target) == 0);
    CHECK(porpoise_path_join(
        cache_digest, sizeof(cache_digest), cache_target, digest_key));
    CHECK(test_create_directory_link(cache_outside, cache_digest));
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_project_build(&request, &first, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "ordinary directories"));
    CHECK(porpoise_path_join(
        cache_outside_build, sizeof(cache_outside_build),
        cache_outside, "build"));
    CHECK(file_contains(cache_outside_sentinel, "outside sentinel"));
    CHECK(!porpoise_path_exists(cache_outside_build));
    porpoise_diagnostics_free(&diagnostics);
    CHECK(test_remove_directory_link(cache_digest));
    CHECK(file_contains(cache_outside_sentinel, "outside sentinel"));

    CHECK(TEST_SET_ENV(
        "CFLAGS", "-fporpoise-cflags-must-not-reach-meson"));
    CHECK(TEST_SET_ENV(
        "CXXFLAGS", "-fporpoise-cxxflags-must-not-reach-meson"));
    CHECK(TEST_SET_ENV(
        "CPPFLAGS", "-fporpoise-cppflags-must-not-reach-meson"));
    CHECK(TEST_SET_ENV(
        "LDFLAGS", "-fporpoise-ldflags-must-not-reach-meson"));
    original_working_directory[0] = '\0';
    CHECK(TEST_GETCWD(
        original_working_directory,
        sizeof(original_working_directory)) != NULL);
    if (original_working_directory[0] != '\0') {
        changed_working_directory = TEST_CHDIR(temporary) == 0;
        CHECK(changed_working_directory);
    }
    request.project_file = changed_working_directory ?
        "fixture.porpoise.json" : project;
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: first build from relative project path\n", stderr);
    fflush(stderr);
    result = porpoise_project_build(&request, &first, &diagnostics);
    if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(first.configured);
    CHECK(first.compiled);
    CHECK(first.executable_available);
    CHECK(!first.cache_reused);
    CHECK(porpoise_path_is_absolute(first.cache_directory));
    CHECK(porpoise_path_is_absolute(first.source_directory));
    CHECK(porpoise_path_is_absolute(first.build_directory));
    CHECK(porpoise_path_exists(first.executable_path));
    CHECK(porpoise_path_exists(first.manifest_path));
    CHECK(file_contains(first.manifest_path, "\"schema_version\": 3"));
    CHECK(file_contains(first.manifest_path, "\"executable_sha256\""));
    CHECK(file_contains(first.manifest_path, "\"porpoise_tool_version\""));
    CHECK(file_contains(first.manifest_path, "\"generated_plan_digest\""));
    CHECK(file_contains(first.manifest_path, "\"generated_output_identity\""));
    CHECK(file_contains(first.manifest_path, "\"libporpoise_identity\""));
    CHECK(file_contains(first.manifest_path, "\"sdl2_dependency_identity\""));
    CHECK(file_contains(first.manifest_path, "\"title_host_identity\""));
    CHECK(first.libporpoise_identity[0] != '\0');
    CHECK(first.generated_output_identity[0] != '\0');
    CHECK(first.sdl2_dependency_identity[0] != '\0');
    CHECK(first.title_host_identity[0] != '\0');
    CHECK(callbacks.saw_compile_log);
#ifdef _WIN32
    {
        static const char *const overlay_entries[] = {"include", "lib"};
        static const char *const include_entries[] = {"SDL2"};
        static const char *const header_entries[] = {"SDL.h", "SDL_config.h"};
        static const char *const library_entries[] = {"libSDL2.dll.a"};
        char excluded[PORPOISE_PATH_CAPACITY];
        CHECK(strstr(first.preflight.c_compiler_path, libporpoise) == NULL);
        CHECK(porpoise_path_join(
            native_file, sizeof(native_file), first.cache_directory,
            "porpoise-native.ini"));
        CHECK(porpoise_path_exists(native_file));
        CHECK(!file_contains(native_file, "msys2/ucrt64/include"));
        CHECK(!file_contains(native_file, "msys2/ucrt64/lib"));
        CHECK(file_contains(native_file, "dependencies/sdl2/include"));
        CHECK(file_contains(native_file, "dependencies/sdl2/lib"));
        CHECK(porpoise_path_join(
            overlay, sizeof(overlay), first.cache_directory,
            "dependencies/sdl2"));
        CHECK(porpoise_path_join(
            overlay_include, sizeof(overlay_include), overlay, "include"));
        CHECK(porpoise_path_join(
            overlay_headers, sizeof(overlay_headers), overlay_include,
            "SDL2"));
        CHECK(porpoise_path_join(
            overlay_library, sizeof(overlay_library), overlay, "lib"));
        CHECK(porpoise_path_join(
            overlay_import, sizeof(overlay_import), overlay_library,
            "libSDL2.dll.a"));
        CHECK(directory_has_exact_entries(
            overlay, overlay_entries,
            sizeof(overlay_entries) / sizeof(overlay_entries[0])));
        CHECK(directory_has_exact_entries(
            overlay_include, include_entries,
            sizeof(include_entries) / sizeof(include_entries[0])));
        CHECK(directory_has_exact_entries(
            overlay_headers, header_entries,
            sizeof(header_entries) / sizeof(header_entries[0])));
        CHECK(directory_has_exact_entries(
            overlay_library, library_entries,
            sizeof(library_entries) / sizeof(library_entries[0])));
        CHECK(porpoise_path_join(
            excluded, sizeof(excluded), overlay_headers, "SDL.h"));
        CHECK(file_contains(excluded, "PORPOISE_BUILD_CORE_SDL_HEADER"));
        CHECK(file_contains(
            overlay_import,
            "PORPOISE BUILD CORE SDL2 IMPORT LIBRARY FIXTURE"));
        CHECK(porpoise_path_join(
            excluded, sizeof(excluded), overlay_include, "foreign_crt.h"));
        CHECK(!porpoise_path_exists(excluded));
        CHECK(porpoise_path_join(
            excluded, sizeof(excluded), overlay_library, "libSDL2.a"));
        CHECK(!porpoise_path_exists(excluded));
        CHECK(porpoise_path_join(
            excluded, sizeof(excluded), overlay_library, "libmingw32.a"));
        CHECK(!porpoise_path_exists(excluded));
        CHECK(porpoise_path_join(
            excluded, sizeof(excluded), overlay, "bin"));
        CHECK(!porpoise_path_exists(excluded));
        CHECK(porpoise_path_join(
            excluded, sizeof(excluded), libporpoise,
            "msys2/ucrt64/bin/SDL2.dll"));
        CHECK(file_contains(
            excluded, "PORPOISE BUILD CORE SDL2 RUNTIME FIXTURE"));
    }
#endif
    porpoise_diagnostics_free(&diagnostics);

    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: cache reuse\n", stderr);
    fflush(stderr);
    result = porpoise_project_build(&request, &second, &diagnostics);
    if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(second.cache_reused);
    CHECK(strcmp(first.configuration_digest, second.configuration_digest) == 0);
    CHECK(strcmp(first.cache_directory, second.cache_directory) == 0);
    porpoise_diagnostics_free(&diagnostics);

    CHECK(porpoise_path_join(
        destination_path, sizeof(destination_path), generated, "main.c"));
    callbacks.mutation_armed = true;
    callbacks.mutation_done = false;
    callbacks.mutation_succeeded = false;
    callbacks.mutation_phase = PORPOISE_BUILD_PHASE_BIND_DEPENDENCIES;
    callbacks.mutation_completed = 0U;
    callbacks.mutation_path = destination_path;
    callbacks.mutation_text =
        "\n/* cache-selection identity race */\n";
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: cache selection rejects changed inputs\n", stderr);
    fflush(stderr);
    result = porpoise_project_build(
        &request, &cache_identity_changed, &diagnostics);
    CHECK(callbacks.mutation_done);
    CHECK(callbacks.mutation_succeeded);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(!cache_identity_changed.cache_reused);
    CHECK(diagnostics_contain(&diagnostics, "identity changed"));
    CHECK(diagnostics_contain(&diagnostics, "cached success"));
    CHECK(porpoise_path_exists(first.executable_path));
    CHECK(porpoise_path_exists(first.manifest_path));
    porpoise_diagnostics_free(&diagnostics);
    callbacks.mutation_armed = false;
    CHECK(porpoise_path_join(
        source_path, sizeof(source_path), fixture_generated, "main.c"));
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_copy_file(
        source_path, destination_path, &diagnostics));
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    porpoise_diagnostics_free(&diagnostics);

    request.generated_plan_digest = "plan-identity-race";
    callbacks.mutation_armed = true;
    callbacks.mutation_done = false;
    callbacks.mutation_succeeded = false;
    callbacks.mutation_phase = PORPOISE_BUILD_PHASE_COMPILE;
    callbacks.mutation_completed = 0U;
    callbacks.mutation_path = destination_path;
    callbacks.mutation_text =
        "\n/* post-configuration identity race */\n";
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: compile rejects changed inputs\n", stderr);
    fflush(stderr);
    result = porpoise_project_build(
        &request, &compile_identity_changed, &diagnostics);
    if (result != PORPOISE_EXIT_USAGE) print_diagnostics(&diagnostics);
    CHECK(callbacks.mutation_done);
    CHECK(callbacks.mutation_succeeded);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics, "identity changed"));
    CHECK(!porpoise_path_exists(compile_identity_changed.manifest_path));
    CHECK(porpoise_path_exists(first.executable_path));
    CHECK(porpoise_path_exists(first.manifest_path));
    porpoise_diagnostics_free(&diagnostics);
    callbacks.mutation_armed = false;
    request.generated_plan_digest = "plan-one";
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_copy_file(
        source_path, destination_path, &diagnostics));
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    porpoise_diagnostics_free(&diagnostics);

    {
        static const char tamper_marker[] =
            "PORPOISE_TAMPERED_EXECUTABLE_ARTIFACT";
        FILE *tampered = fopen(second.executable_path, "ab");
        CHECK(tampered != NULL);
        if (tampered != NULL) {
            CHECK(fputs(tamper_marker, tampered) >= 0);
            CHECK(fclose(tampered) == 0);
        }
        porpoise_diagnostics_init(&diagnostics);
        fputs("build-core: cached executable integrity repair\n", stderr);
        fflush(stderr);
        result = porpoise_project_build(
            &request, &repaired_executable, &diagnostics);
        if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
        CHECK(result == PORPOISE_EXIT_OK);
        CHECK(!repaired_executable.cache_reused);
        CHECK(!file_contains(
            repaired_executable.executable_path, tamper_marker));
        porpoise_diagnostics_free(&diagnostics);
    }

    CHECK(porpoise_path_join(
        destination_path, sizeof(destination_path), generated,
        "build-break.h"));
    CHECK(write_text_file(
        destination_path,
        "#error intentional failed-rebuild fixture\n"));
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: failed rebuild preserves last success\n", stderr);
    fflush(stderr);
    result = porpoise_project_build(
        &request, &failed_rebuild, &diagnostics);
    CHECK(result != PORPOISE_EXIT_OK);
    CHECK(porpoise_path_exists(repaired_executable.executable_path));
    porpoise_diagnostics_free(&diagnostics);
    CHECK(write_text_file(
        destination_path,
        "#ifndef PORPOISE_BUILD_CORE_BREAK_H\n"
        "#define PORPOISE_BUILD_CORE_BREAK_H\n\n"
        "#define PORPOISE_BUILD_CORE_FIXTURE_READY 1\n\n"
        "#endif\n"));
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_project_build(&request, &second, &diagnostics);
    if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(second.cache_reused);
    porpoise_diagnostics_free(&diagnostics);
    request.run_arguments = run_ok;
    request.run_argument_count = 1U;
    request.dvd_root = "fixture-dvd";
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_project_run(&request, &second, &diagnostics);
    if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(second.process_exit_code == 0);
    porpoise_diagnostics_free(&diagnostics);
    request.run_arguments = NULL;
    request.run_argument_count = 0U;
    request.dvd_root = NULL;

#ifdef _WIN32
    CHECK(porpoise_path_join(
        destination_path, sizeof(destination_path), libporpoise,
        "msys2/ucrt64/include/foreign_crt.h"));
    {
        FILE *changed_file = fopen(destination_path, "ab");
        CHECK(changed_file != NULL);
        if (changed_file != NULL) {
            CHECK(fputs("unrelated UCRT change\n", changed_file) >= 0);
            CHECK(fclose(changed_file) == 0);
        }
    }
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: unrelated UCRT change preserves cache\n", stderr);
    fflush(stderr);
    result = porpoise_project_build(
        &request, &unrelated_ucrt_changed, &diagnostics);
    if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(unrelated_ucrt_changed.cache_reused);
    CHECK(strcmp(
        first.sdl2_dependency_identity,
        unrelated_ucrt_changed.sdl2_dependency_identity) == 0);
    CHECK(strcmp(
        first.configuration_digest,
        unrelated_ucrt_changed.configuration_digest) == 0);
    CHECK(strcmp(
        first.cache_directory,
        unrelated_ucrt_changed.cache_directory) == 0);
    porpoise_diagnostics_free(&diagnostics);

    CHECK(porpoise_path_join(
        sdl2_source_runtime, sizeof(sdl2_source_runtime), libporpoise,
        "msys2/ucrt64/bin/SDL2.dll"));
    {
        FILE *changed_file = fopen(sdl2_source_runtime, "ab");
        CHECK(changed_file != NULL);
        if (changed_file != NULL) {
            CHECK(fputs("SDL2 runtime identity change\n", changed_file) >= 0);
            CHECK(fclose(changed_file) == 0);
        }
    }
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: SDL2 runtime identity invalidation\n", stderr);
    fflush(stderr);
    result = porpoise_project_build(
        &request, &sdl2_runtime_changed, &diagnostics);
    if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(!sdl2_runtime_changed.cache_reused);
    CHECK(strcmp(
        first.libporpoise_identity,
        sdl2_runtime_changed.libporpoise_identity) == 0);
    CHECK(strcmp(
        first.sdl2_dependency_identity,
        sdl2_runtime_changed.sdl2_dependency_identity) != 0);
    CHECK(strcmp(
        first.configuration_digest,
        sdl2_runtime_changed.configuration_digest) != 0);
    CHECK(strcmp(
        first.cache_directory,
        sdl2_runtime_changed.cache_directory) != 0);
    porpoise_diagnostics_free(&diagnostics);

    CHECK(porpoise_path_join(
        sdl2_source_header, sizeof(sdl2_source_header), libporpoise,
        "msys2/ucrt64/include/SDL2/SDL_config.h"));
    {
        FILE *changed_file = fopen(sdl2_source_header, "ab");
        CHECK(changed_file != NULL);
        if (changed_file != NULL) {
            CHECK(fputs("SDL2 header identity change\n", changed_file) >= 0);
            CHECK(fclose(changed_file) == 0);
        }
    }
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: SDL2 header identity invalidation\n", stderr);
    fflush(stderr);
    result = porpoise_project_build(
        &request, &sdl2_headers_changed, &diagnostics);
    if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(!sdl2_headers_changed.cache_reused);
    CHECK(strcmp(
        sdl2_runtime_changed.libporpoise_identity,
        sdl2_headers_changed.libporpoise_identity) == 0);
    CHECK(strcmp(
        sdl2_runtime_changed.sdl2_dependency_identity,
        sdl2_headers_changed.sdl2_dependency_identity) != 0);
    CHECK(strcmp(
        sdl2_runtime_changed.configuration_digest,
        sdl2_headers_changed.configuration_digest) != 0);
    CHECK(strcmp(
        sdl2_runtime_changed.cache_directory,
        sdl2_headers_changed.cache_directory) != 0);
    porpoise_diagnostics_free(&diagnostics);

    CHECK(porpoise_path_join(
        sdl2_source_import, sizeof(sdl2_source_import), libporpoise,
        "msys2/ucrt64/lib/libSDL2.dll.a"));
    {
        FILE *changed_file = fopen(sdl2_source_import, "ab");
        CHECK(changed_file != NULL);
        if (changed_file != NULL) {
            CHECK(fputs("SDL2 dependency identity change\n", changed_file) >= 0);
            CHECK(fclose(changed_file) == 0);
        }
    }
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: SDL2 dependency identity invalidation\n", stderr);
    fflush(stderr);
    result = porpoise_project_build(&request, &sdl2_changed, &diagnostics);
    if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(!sdl2_changed.cache_reused);
    CHECK(strcmp(
        first.libporpoise_identity,
        sdl2_changed.libporpoise_identity) == 0);
    CHECK(strcmp(
        first.sdl2_dependency_identity,
        sdl2_changed.sdl2_dependency_identity) != 0);
    CHECK(strcmp(
        sdl2_headers_changed.sdl2_dependency_identity,
        sdl2_changed.sdl2_dependency_identity) != 0);
    CHECK(strcmp(
        first.configuration_digest,
        sdl2_changed.configuration_digest) != 0);
    CHECK(strcmp(
        first.cache_directory,
        sdl2_changed.cache_directory) != 0);
    CHECK(porpoise_path_join(
        overlay_import, sizeof(overlay_import),
        sdl2_changed.cache_directory,
        "dependencies/sdl2/lib/libSDL2.dll.a"));
    CHECK(file_contains(
        overlay_import, "SDL2 dependency identity change"));
    CHECK(porpoise_path_join(
        native_file, sizeof(native_file), sdl2_changed.cache_directory,
        "porpoise-native.ini"));
    CHECK(!file_contains(native_file, "msys2/ucrt64/include"));
    CHECK(!file_contains(native_file, "msys2/ucrt64/lib"));
    porpoise_diagnostics_free(&diagnostics);
#endif

    CHECK(porpoise_path_join(
        destination_path, sizeof(destination_path), libporpoise,
        "meson.build"));
    {
        FILE *changed_file = fopen(destination_path, "ab");
        CHECK(changed_file != NULL);
        if (changed_file != NULL) {
            CHECK(fputs("\n# identity change\n", changed_file) >= 0);
            CHECK(fclose(changed_file) == 0);
        }
    }
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: dependency identity invalidation\n", stderr);
    fflush(stderr);
    result = porpoise_project_build(
        &request, &dependency_changed, &diagnostics);
    if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(strcmp(
        first.libporpoise_identity,
        dependency_changed.libporpoise_identity) != 0);
#ifdef _WIN32
    CHECK(strcmp(
        sdl2_changed.sdl2_dependency_identity,
        dependency_changed.sdl2_dependency_identity) == 0);
#endif
    CHECK(strcmp(
        first.configuration_digest,
        dependency_changed.configuration_digest) != 0);
    CHECK(strcmp(
        first.cache_directory,
        dependency_changed.cache_directory) != 0);
    porpoise_diagnostics_free(&diagnostics);

    CHECK(porpoise_path_join(
        destination_path, sizeof(destination_path), generated, "main.c"));
    {
        FILE *changed_file = fopen(destination_path, "ab");
        CHECK(changed_file != NULL);
        if (changed_file != NULL) {
            CHECK(fputs("\n/* generated output identity change */\n",
                        changed_file) >= 0);
            CHECK(fclose(changed_file) == 0);
        }
    }
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: generated output identity invalidation\n", stderr);
    fflush(stderr);
    result = porpoise_project_build(
        &request, &generated_changed, &diagnostics);
    if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(!generated_changed.cache_reused);
    CHECK(strcmp(
        dependency_changed.generated_output_identity,
        generated_changed.generated_output_identity) != 0);
    CHECK(strcmp(
        dependency_changed.libporpoise_identity,
        generated_changed.libporpoise_identity) == 0);
    CHECK(strcmp(
        dependency_changed.title_host_identity,
        generated_changed.title_host_identity) == 0);
    CHECK(strcmp(
        dependency_changed.configuration_digest,
        generated_changed.configuration_digest) != 0);
    CHECK(strcmp(
        dependency_changed.cache_directory,
        generated_changed.cache_directory) != 0);
    CHECK(porpoise_path_join(
        source_path, sizeof(source_path), generated_changed.source_directory,
        "main.c"));
    CHECK(file_contains(
        source_path, "generated output identity change"));
    CHECK(file_contains(
        generated_changed.manifest_path,
        generated_changed.generated_output_identity));
    porpoise_diagnostics_free(&diagnostics);

    request.generated_plan_digest = "plan-two";
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: cache invalidation\n", stderr);
    fflush(stderr);
    result = porpoise_project_build(&request, &changed, &diagnostics);
    if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(strcmp(first.configuration_digest, changed.configuration_digest) != 0);
    CHECK(strcmp(first.cache_directory, changed.cache_directory) != 0);
    porpoise_diagnostics_free(&diagnostics);

    memset(&dvd_target, 0, sizeof(dvd_target));
    dvd_target.has_title_host = true;
    dvd_target.title_host.initialize_dvd = true;
    request.recovery_target = &dvd_target;
    request.run_arguments = run_ok;
    request.run_argument_count = 1U;
    request.dvd_root = NULL;
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_project_run(&request, &changed, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics.count != 0U);
    if (diagnostics.count != 0U)
        CHECK(strstr(
            diagnostics.items[diagnostics.count - 1U].message,
            "DVD root") != NULL);
    porpoise_diagnostics_free(&diagnostics);
    request.recovery_target = NULL;

    request.run_arguments = run_ok;
    request.run_argument_count = 1U;
    request.dvd_root = "fixture-dvd";
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: successful run\n", stderr);
    fflush(stderr);
    result = porpoise_project_run(&request, &changed, &diagnostics);
    if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(changed.process_exit_code == 0);
    CHECK(changed.guest_status_reported);
    CHECK(!changed.guest_faulted);
    CHECK(changed.guest_pc == UINT32_C(0x800055E0));
    CHECK(strcmp(changed.guest_status_message, "fixture completed") == 0);
    CHECK(changed.status_file_path[0] != '\0');
    CHECK(!porpoise_path_exists(changed.status_file_path));
    porpoise_diagnostics_free(&diagnostics);

    request.run_arguments = run_bad;
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: nonzero native exit cleanup\n", stderr);
    fflush(stderr);
    result = porpoise_project_run(&request, &changed, &diagnostics);
    CHECK(result == PORPOISE_EXIT_TRANSLATION);
    CHECK(changed.process_exit_code == 9);
    CHECK(changed.status_file_path[0] != '\0');
    CHECK(!porpoise_path_exists(changed.status_file_path));
    porpoise_diagnostics_free(&diagnostics);
    request.run_arguments = run_ok;

    request.trace_file = temporary;
    request.frame_limit = 7U;
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: trace path rejects directory\n", stderr);
    fflush(stderr);
    result = porpoise_project_run(&request, &changed, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics.count != 0U);
    if (diagnostics.count != 0U)
        CHECK(strstr(
            diagnostics.items[diagnostics.count - 1U].message,
            "existing directory") != NULL);
    porpoise_diagnostics_free(&diagnostics);

    CHECK(porpoise_path_join(
        invalid_trace_parent, sizeof(invalid_trace_parent), temporary,
        "trace-parent-is-a-file"));
    {
        FILE *parent_file = fopen(invalid_trace_parent, "wb");
        CHECK(parent_file != NULL);
        if (parent_file != NULL) {
            CHECK(fputs("not a directory\n", parent_file) >= 0);
            CHECK(fclose(parent_file) == 0);
        }
    }
    CHECK(porpoise_path_join(
        invalid_trace, sizeof(invalid_trace), invalid_trace_parent,
        "boot.jsonl"));
    request.trace_file = invalid_trace;
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: trace parent rejects file\n", stderr);
    fflush(stderr);
    result = porpoise_project_run(&request, &changed, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics.count != 0U);
    if (diagnostics.count != 0U)
        CHECK(strstr(
            diagnostics.items[diagnostics.count - 1U].message,
            "parent path is not a directory") != NULL);
    porpoise_diagnostics_free(&diagnostics);

    CHECK(porpoise_path_join(
        absolute_trace, sizeof(absolute_trace), temporary,
        "traces/absolute/boot.jsonl"));
    request.trace_file = absolute_trace;
    request.reject_approximations = true;
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: absolute trace output\n", stderr);
    fflush(stderr);
    result = porpoise_project_run(&request, &changed, &diagnostics);
    if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(porpoise_path_exists(absolute_trace));
    CHECK(file_contains(absolute_trace, "frame_limit=7"));
    CHECK(file_contains(absolute_trace, "reject_approximations=1"));
    porpoise_diagnostics_free(&diagnostics);
    request.reject_approximations = false;

    CHECK(porpoise_path_join(
        relative_trace, sizeof(relative_trace), temporary,
        "traces/relative/boot.jsonl"));
    request.trace_file = "traces/relative/boot.jsonl";
    request.run_working_directory = temporary;
    request.frame_limit = 9U;
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: relative trace output\n", stderr);
    fflush(stderr);
    result = porpoise_project_run(&request, &changed, &diagnostics);
    if (result != PORPOISE_EXIT_OK) print_diagnostics(&diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(porpoise_path_exists(relative_trace));
    CHECK(file_contains(relative_trace, "frame_limit=9"));
    porpoise_diagnostics_free(&diagnostics);

    request.trace_file = NULL;
    request.run_working_directory = NULL;
    request.frame_limit = 0U;
#if SIZE_MAX > UINT32_MAX
    request.frame_limit = (size_t)UINT32_MAX + 1U;
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: frame limit range validation\n", stderr);
    fflush(stderr);
    result = porpoise_project_run(&request, &changed, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics.count != 0U);
    if (diagnostics.count != 0U)
        CHECK(strstr(
            diagnostics.items[diagnostics.count - 1U].message,
            "frame limit") != NULL);
    porpoise_diagnostics_free(&diagnostics);
    request.frame_limit = 0U;
#endif

    request.run_arguments = run_fault;
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: zero-exit guest fault\n", stderr);
    fflush(stderr);
    result = porpoise_project_run(&request, &changed, &diagnostics);
    CHECK(result == PORPOISE_EXIT_TRANSLATION);
    CHECK(changed.process_exit_code == 0);
    CHECK(changed.guest_status_reported);
    CHECK(changed.guest_faulted);
    CHECK(changed.guest_pc == UINT32_C(0x800055E0));
    CHECK(strcmp(changed.guest_status_message, "fixture guest fault") == 0);
    CHECK(changed.status_file_path[0] != '\0');
    CHECK(!porpoise_path_exists(changed.status_file_path));
    CHECK(diagnostics.count != 0U);
    porpoise_diagnostics_free(&diagnostics);

    request.run_arguments = run_missing;
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: zero-exit missing status\n", stderr);
    fflush(stderr);
    result = porpoise_project_run(&request, &changed, &diagnostics);
    CHECK(result == PORPOISE_EXIT_TRANSLATION);
    CHECK(changed.process_exit_code == 0);
    CHECK(!changed.guest_status_reported);
    CHECK(!changed.guest_faulted);
    CHECK(changed.status_file_path[0] != '\0');
    CHECK(!porpoise_path_exists(changed.status_file_path));
    CHECK(diagnostics.count != 0U);
    porpoise_diagnostics_free(&diagnostics);

    callbacks.cancel = false;
    request.run_arguments = run_cancel;
    request.dvd_root = NULL;
    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: cancellation\n", stderr);
    fflush(stderr);
    result = porpoise_project_run(&request, &changed, &diagnostics);
    CHECK(result == PORPOISE_EXIT_CANCELLED);
    CHECK(changed.status_file_path[0] != '\0');
    CHECK(!porpoise_path_exists(changed.status_file_path));
    porpoise_diagnostics_free(&diagnostics);

    porpoise_diagnostics_init(&diagnostics);
    fputs("build-core: cleanup\n", stderr);
    fflush(stderr);
    if (changed_working_directory)
        CHECK(TEST_CHDIR(original_working_directory) == 0);
    CHECK(porpoise_remove_tree(temporary, &diagnostics));
    porpoise_diagnostics_free(&diagnostics);
    if (failures != 0) {
        fprintf(stderr, "%d build-core checks failed\n", failures);
        return 1;
    }
    puts("build core tests passed");
    return 0;
}
