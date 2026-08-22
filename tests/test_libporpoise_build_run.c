#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "porpoise/build.h"
#include "porpoise/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define SMOKE_MKDIR(path) _mkdir(path)
#define SMOKE_PROCESS_ID() ((unsigned long)_getpid())
#else
#include <sys/stat.h>
#include <unistd.h>
#define SMOKE_MKDIR(path) mkdir((path), 0755)
#define SMOKE_PROCESS_ID() ((unsigned long)getpid())
#endif

static void smoke_log(
    void *user_data,
    PorpoiseBuildPhase phase,
    bool standard_error,
    const char *text,
    size_t length) {
    FILE *stream = standard_error ? stderr : stdout;
    (void)user_data;
    fprintf(stream, "[%s] ", porpoise_build_phase_name(phase));
    if (length != 0U) (void)fwrite(text, 1U, length, stream);
    if (length == 0U || text[length - 1U] != '\n') fputc('\n', stream);
    fflush(stream);
}

static void print_diagnostics(const PorpoiseDiagnostics *diagnostics) {
    size_t index;
    for (index = 0U; index < diagnostics->count; index++) {
        fprintf(
            stderr,
            "diagnostic: %s: %s\n",
            diagnostics->items[index].file,
            diagnostics->items[index].message);
    }
}

static bool write_project(const char *path) {
    FILE *file = fopen(path, "wb");
    bool ok;
    if (file == NULL) return false;
    ok = fputs("{\"schema_version\":2,\"targets\":[]}", file) >= 0;
    if (fclose(file) != 0) ok = false;
    return ok;
}

static bool build_result_is_complete(const PorpoiseBuildResult *result) {
    return result->configured && result->compiled &&
           result->executable_available &&
           result->configuration_digest[0] != '\0' &&
           result->libporpoise_identity[0] != '\0' &&
           result->generated_output_identity[0] != '\0' &&
           porpoise_path_exists(result->executable_path) &&
           porpoise_path_exists(result->manifest_path);
}

static bool run_result_is_ok(const PorpoiseBuildResult *result) {
    return result->process_exit_code == 0 && result->guest_status_reported &&
           !result->guest_faulted &&
           result->guest_pc == UINT32_C(0x80000000) &&
           strcmp(
               result->guest_status_message,
               "real libPorpoise smoke completed") == 0 &&
           result->status_file_path[0] != '\0' &&
           !porpoise_path_exists(result->status_file_path);
}

int main(int argc, char **argv) {
    char fixture[PORPOISE_PATH_CAPACITY];
    char generated[PORPOISE_PATH_CAPACITY];
    char title_host[PORPOISE_PATH_CAPACITY];
    char temporary[PORPOISE_PATH_CAPACITY];
    char project[PORPOISE_PATH_CAPACITY];
    PorpoiseBuildRequest request;
    PorpoiseBuildResult first;
    PorpoiseBuildResult reused;
    PorpoiseDiagnostics diagnostics;
    int status;
    int exit_code = 1;

    if (argc != 7) {
        fprintf(
            stderr,
            "usage: test_libporpoise_build_run SOURCE_ROOT BUILD_ROOT "
            "LIBPORPOISE MESON CC CXX\n");
        return 2;
    }
    if (!porpoise_path_join(
            fixture,
            sizeof(fixture),
            argv[1],
            "tests/fixtures/libporpoise_build_run") ||
        !porpoise_path_join(
            generated, sizeof(generated), fixture, "generated") ||
        !porpoise_path_join(
            title_host, sizeof(title_host), fixture, "title-host") ||
        !porpoise_format(
            temporary,
            sizeof(temporary),
            "%s/lpbr-%lu",
            argv[2],
            SMOKE_PROCESS_ID()) ||
        !porpoise_path_join(
            project, sizeof(project), temporary, "smoke.porpoise.json")) {
        fputs("cannot construct real libPorpoise smoke paths\n", stderr);
        return 2;
    }

    porpoise_diagnostics_init(&diagnostics);
    if (porpoise_path_exists(temporary) &&
        !porpoise_remove_tree(temporary, &diagnostics)) {
        print_diagnostics(&diagnostics);
        porpoise_diagnostics_free(&diagnostics);
        return 1;
    }
    porpoise_diagnostics_free(&diagnostics);
    if (SMOKE_MKDIR(temporary) != 0 || !write_project(project)) {
        fprintf(stderr, "cannot create smoke project at %s\n", temporary);
        return 1;
    }

    porpoise_build_request_init(&request);
    request.project_file = project;
    request.target_id = "real-lib-smoke";
    request.generated_directory = generated;
    request.libporpoise_directory = argv[3];
    request.title_host_directory = title_host;
    request.meson_executable = argv[4];
    request.c_compiler = argv[5];
    request.cpp_compiler = argv[6];
    request.build_type = "debugoptimized";
    request.generated_plan_digest = "synthetic-real-lib-smoke-v1";
    request.allow_copy_fallback = false;
    request.callbacks.log = smoke_log;

    porpoise_diagnostics_init(&diagnostics);
    status = porpoise_project_build(&request, &first, &diagnostics);
    if (status != PORPOISE_EXIT_OK || !build_result_is_complete(&first)) {
        fprintf(stderr, "first real libPorpoise build failed (%d)\n", status);
        print_diagnostics(&diagnostics);
        porpoise_diagnostics_free(&diagnostics);
        goto cleanup;
    }
    porpoise_diagnostics_free(&diagnostics);

    porpoise_diagnostics_init(&diagnostics);
    status = porpoise_project_run(&request, &first, &diagnostics);
    if (status != PORPOISE_EXIT_OK || !run_result_is_ok(&first)) {
        fprintf(stderr, "first real libPorpoise run failed (%d)\n", status);
        print_diagnostics(&diagnostics);
        porpoise_diagnostics_free(&diagnostics);
        goto cleanup;
    }
    porpoise_diagnostics_free(&diagnostics);

    porpoise_diagnostics_init(&diagnostics);
    status = porpoise_project_build(&request, &reused, &diagnostics);
    if (status != PORPOISE_EXIT_OK || !reused.cache_reused ||
        strcmp(
            first.configuration_digest,
            reused.configuration_digest) != 0 ||
        strcmp(first.cache_directory, reused.cache_directory) != 0) {
        fprintf(stderr, "real libPorpoise cache reuse failed (%d)\n", status);
        print_diagnostics(&diagnostics);
        porpoise_diagnostics_free(&diagnostics);
        goto cleanup;
    }
    porpoise_diagnostics_free(&diagnostics);

    porpoise_diagnostics_init(&diagnostics);
    status = porpoise_project_run(&request, &reused, &diagnostics);
    if (status != PORPOISE_EXIT_OK || !run_result_is_ok(&reused)) {
        fprintf(stderr, "cached real libPorpoise run failed (%d)\n", status);
        print_diagnostics(&diagnostics);
        porpoise_diagnostics_free(&diagnostics);
        goto cleanup;
    }
    porpoise_diagnostics_free(&diagnostics);
    exit_code = 0;

cleanup:
    if (getenv("PORPOISE_KEEP_REAL_SMOKE") != NULL) {
        fprintf(stderr, "retained real libPorpoise smoke at %s\n", temporary);
        return exit_code;
    }
    porpoise_diagnostics_init(&diagnostics);
    if (!porpoise_remove_tree(temporary, &diagnostics)) {
        fputs("real libPorpoise smoke cleanup failed\n", stderr);
        print_diagnostics(&diagnostics);
        exit_code = 1;
    }
    porpoise_diagnostics_free(&diagnostics);
    if (exit_code == 0) {
        puts(
            "real libPorpoise shared Build/Run smoke passed "
            "(fresh build, run, cache reuse, cached run)");
    }
    return exit_code;
}
