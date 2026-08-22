#define _POSIX_C_SOURCE 200809L

#include "process_internal.h"

#include "porpoise/util.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int failures;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            failures++;                                                      \
        }                                                                    \
    } while (0)

typedef struct ProcessLogStats {
    size_t standard_output_bytes;
    size_t standard_error_bytes;
    bool saw_output;
} ProcessLogStats;

typedef struct ProcessThreadCase {
    const char *self;
    const char *working_directory;
    char value[32];
    int status;
    bool valid;
} ProcessThreadCase;

static volatile sig_atomic_t child_stop;

static void child_stop_handler(int signal_number) {
    (void)signal_number;
    child_stop = 1;
}

static void process_log(
    void *user_data,
    PorpoiseBuildPhase phase,
    bool standard_error,
    const char *text,
    size_t length) {
    ProcessLogStats *stats = (ProcessLogStats *)user_data;
    (void)phase;
    (void)text;
    if (standard_error) stats->standard_error_bytes += length;
    else {
        stats->standard_output_bytes += length;
        if (length != 0U) stats->saw_output = true;
    }
}

static bool cancel_after_output(void *user_data) {
    return ((ProcessLogStats *)user_data)->saw_output;
}

static int child_inspect(int argc, char **argv) {
    char working_directory[PORPOISE_PATH_CAPACITY];
    const char *value = getenv("PORPOISE_PROCESS_TEST_VALUE");
    if (argc != 3 || getcwd(working_directory, sizeof(working_directory)) == NULL)
        return 2;
    printf("cwd=%s\nenv=%s\narg=%s\n", working_directory,
           value == NULL ? "" : value, argv[2]);
    return fflush(stdout) == 0 ? 0 : 3;
}

static bool child_write_stream(
    FILE *stream,
    size_t total,
    char fill,
    const char *head,
    const char *tail) {
    char block[4096];
    size_t head_length = strlen(head);
    size_t tail_length = strlen(tail);
    size_t remaining;
    memset(block, (unsigned char)fill, sizeof(block));
    if (head_length + tail_length > total ||
        fwrite(head, 1U, head_length, stream) != head_length) return false;
    remaining = total - head_length - tail_length;
    while (remaining != 0U) {
        size_t count = remaining < sizeof(block) ? remaining : sizeof(block);
        if (fwrite(block, 1U, count, stream) != count) return false;
        remaining -= count;
    }
    return fwrite(tail, 1U, tail_length, stream) == tail_length &&
           fflush(stream) == 0;
}

static int child_emit(void) {
    const size_t total =
        (size_t)PORPOISE_PROCESS_CAPTURE_LIMIT_BYTES + 32768U;
    if (!child_write_stream(
            stdout, total, 'o', "stdout-begin\n", "\nstdout-tail\n")) return 2;
    if (!child_write_stream(
            stderr, total, 'e', "stderr-begin\n", "\nstderr-tail\n")) return 3;
    return 0;
}

static int child_wait_tree(void) {
    struct sigaction action;
    pid_t grandchild;
    memset(&action, 0, sizeof(action));
    action.sa_handler = child_stop_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) != 0) return 2;
    grandchild = fork();
    if (grandchild < 0) return 3;
    if (grandchild == 0) {
        while (!child_stop) pause();
        _exit(0);
    }
    printf("ready %ld\n", (long)grandchild);
    if (fflush(stdout) != 0) return 4;
    while (!child_stop) pause();
    (void)kill(grandchild, SIGTERM);
    while (waitpid(grandchild, NULL, 0) < 0 && errno == EINTR) {}
    return 0;
}

static void *process_thread_run(void *user_data) {
    ProcessThreadCase *test = (ProcessThreadCase *)user_data;
    const char *arguments[4];
    PorpoiseBuildEnvironmentEntry environment[2];
    PorpoiseProcessCapture capture;
    PorpoiseDiagnostics diagnostics;
    char expected_environment[64];
    const char expected_argument[] = "arg=space value\n";
    char expected_working_directory[PORPOISE_PATH_CAPACITY + 8U];
    arguments[0] = test->self;
    arguments[1] = "--inspect";
    arguments[2] = "space value";
    arguments[3] = NULL;
    environment[0].name = "PORPOISE_PROCESS_TEST_VALUE";
    environment[0].value = "shadowed";
    environment[1].name = "PORPOISE_PROCESS_TEST_VALUE";
    environment[1].value = test->value;
    porpoise_process_capture_init(&capture);
    porpoise_diagnostics_init(&diagnostics);
    test->status = porpoise_process_run(
        arguments, test->working_directory, environment, 2U,
        PORPOISE_BUILD_PHASE_RUN, NULL, &capture, &diagnostics);
    snprintf(
        expected_environment, sizeof(expected_environment), "env=%s\n",
        test->value);
    snprintf(
        expected_working_directory, sizeof(expected_working_directory),
        "cwd=%s\n", test->working_directory);
    test->valid = test->status == PORPOISE_EXIT_OK &&
                  capture.exit_code == 0 &&
                  !capture.standard_output_truncated &&
                  !capture.standard_error_truncated &&
                  capture.standard_output != NULL &&
                  strstr(capture.standard_output, expected_environment) != NULL &&
                  strstr(capture.standard_output, expected_argument) != NULL &&
                  strstr(
                      capture.standard_output,
                      expected_working_directory) != NULL &&
                  diagnostics.count == 0U;
    porpoise_process_capture_free(&capture);
    porpoise_diagnostics_free(&diagnostics);
    return NULL;
}

static void test_concurrent_spawn(
    const char *self,
    const char *working_directory) {
    enum { THREAD_COUNT = 6 };
    pthread_t threads[THREAD_COUNT];
    ProcessThreadCase cases[THREAD_COUNT];
    size_t created = 0U;
    size_t index;
    memset(cases, 0, sizeof(cases));
    for (index = 0U; index < THREAD_COUNT; index++) {
        cases[index].self = self;
        cases[index].working_directory = working_directory;
        snprintf(cases[index].value, sizeof(cases[index].value),
                 "thread-%zu", index);
        if (pthread_create(
                &threads[index], NULL, process_thread_run,
                &cases[index]) != 0) break;
        created++;
    }
    CHECK(created == THREAD_COUNT);
    for (index = 0U; index < created; index++)
        CHECK(pthread_join(threads[index], NULL) == 0);
    for (index = 0U; index < created; index++) {
        CHECK(cases[index].status == PORPOISE_EXIT_OK);
        CHECK(cases[index].valid);
    }
}

static void test_bounded_capture(const char *self) {
    const char *arguments[] = {self, "--emit", NULL};
    const char output_tail[] = "\nstdout-tail\n";
    const char error_tail[] = "\nstderr-tail\n";
    const size_t total =
        (size_t)PORPOISE_PROCESS_CAPTURE_LIMIT_BYTES + 32768U;
    ProcessLogStats stats;
    PorpoiseBuildCallbacks callbacks;
    PorpoiseProcessCapture capture;
    PorpoiseDiagnostics diagnostics;
    int status;
    memset(&stats, 0, sizeof(stats));
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.log = process_log;
    callbacks.user_data = &stats;
    porpoise_process_capture_init(&capture);
    porpoise_diagnostics_init(&diagnostics);
    status = porpoise_process_run(
        arguments, NULL, NULL, 0U, PORPOISE_BUILD_PHASE_COMPILE,
        &callbacks, &capture, &diagnostics);
    CHECK(status == PORPOISE_EXIT_OK);
    CHECK(capture.exit_code == 0);
    CHECK(capture.standard_output_truncated);
    CHECK(capture.standard_error_truncated);
    CHECK(capture.standard_output != NULL);
    CHECK(capture.standard_error != NULL);
    if (capture.standard_output != NULL) {
        size_t length = strlen(capture.standard_output);
        CHECK(length == (size_t)PORPOISE_PROCESS_CAPTURE_LIMIT_BYTES);
        CHECK(strstr(capture.standard_output, "stdout-begin") == NULL);
        CHECK(length >= strlen(output_tail));
        if (length >= strlen(output_tail))
            CHECK(memcmp(
                capture.standard_output + length - strlen(output_tail),
                output_tail, strlen(output_tail)) == 0);
    }
    if (capture.standard_error != NULL) {
        size_t length = strlen(capture.standard_error);
        CHECK(length == (size_t)PORPOISE_PROCESS_CAPTURE_LIMIT_BYTES);
        CHECK(strstr(capture.standard_error, "stderr-begin") == NULL);
        CHECK(length >= strlen(error_tail));
        if (length >= strlen(error_tail))
            CHECK(memcmp(
                capture.standard_error + length - strlen(error_tail),
                error_tail, strlen(error_tail)) == 0);
    }
    CHECK(stats.standard_output_bytes == total);
    CHECK(stats.standard_error_bytes == total);
    CHECK(diagnostics.count == 0U);
    porpoise_process_capture_free(&capture);
    porpoise_diagnostics_free(&diagnostics);
}

static void test_process_group_cancellation(const char *self) {
    const char *arguments[] = {self, "--wait-tree", NULL};
    ProcessLogStats stats;
    PorpoiseBuildCallbacks callbacks;
    PorpoiseProcessCapture capture;
    PorpoiseDiagnostics diagnostics;
    long grandchild = -1L;
    int status;
    memset(&stats, 0, sizeof(stats));
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.log = process_log;
    callbacks.cancelled = cancel_after_output;
    callbacks.user_data = &stats;
    porpoise_process_capture_init(&capture);
    porpoise_diagnostics_init(&diagnostics);
    status = porpoise_process_run(
        arguments, NULL, NULL, 0U, PORPOISE_BUILD_PHASE_RUN,
        &callbacks, &capture, &diagnostics);
    CHECK(status == PORPOISE_EXIT_CANCELLED);
    CHECK(stats.saw_output);
    CHECK(capture.standard_output != NULL);
    if (capture.standard_output != NULL)
        CHECK(sscanf(capture.standard_output, "ready %ld", &grandchild) == 1);
    if (grandchild > 0L) {
        errno = 0;
        CHECK(kill((pid_t)grandchild, 0) == -1);
        CHECK(errno == ESRCH);
    }
    porpoise_process_capture_free(&capture);
    porpoise_diagnostics_free(&diagnostics);
}

int main(int argc, char **argv) {
    char self[PORPOISE_PATH_CAPACITY];
    char working_directory[PORPOISE_PATH_CAPACITY];
    PorpoiseDiagnostics diagnostics;
    if (argc >= 2 && strcmp(argv[1], "--inspect") == 0)
        return child_inspect(argc, argv);
    if (argc == 2 && strcmp(argv[1], "--emit") == 0) return child_emit();
    if (argc == 2 && strcmp(argv[1], "--wait-tree") == 0)
        return child_wait_tree();
    if (argc != 2) {
        fprintf(stderr, "usage: %s BUILD_ROOT\n", argv[0]);
        return 2;
    }
    if (realpath(argv[0], self) == NULL) {
        fprintf(stderr, "cannot resolve test executable: %s\n", strerror(errno));
        return 2;
    }
    if (!porpoise_path_join(
            working_directory, sizeof(working_directory), argv[1],
            "process test working directory")) return 2;
    porpoise_diagnostics_init(&diagnostics);
    if (porpoise_path_exists(working_directory))
        CHECK(porpoise_remove_tree(working_directory, &diagnostics));
    CHECK(porpoise_make_directories(working_directory, &diagnostics));
    CHECK(diagnostics.count == 0U);
    porpoise_diagnostics_free(&diagnostics);
    test_concurrent_spawn(self, working_directory);
    test_bounded_capture(self);
    test_process_group_cancellation(self);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_remove_tree(working_directory, &diagnostics));
    porpoise_diagnostics_free(&diagnostics);
    if (failures != 0)
        fprintf(stderr, "%d process-service check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
