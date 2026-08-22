#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build-break.h"

int porpoise_build_fixture_host_value(void);
int porpoise_build_fixture_cpp_value(void);

static int write_status(
    const char *status,
    unsigned int guest_pc,
    const char *message) {
    const char *path = getenv("PORPOISE_STATUS_FILE");
    FILE *file;
    int ok;
    if (path == NULL || path[0] == '\0') return 0;
    file = fopen(path, "wb");
    if (file == NULL) return 0;
    ok = fprintf(
             file, "PORPOISE_STATUS_V1\t%s\t%08X\t%s\n",
             status, guest_pc, message) > 0;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int write_trace_marker(void) {
    const char *path = getenv("PORPOISE_TRACE");
    const char *frame_limit = getenv("PORPOISE_FRAME_LIMIT");
    const char *reject_approximations =
        getenv("PORPOISE_REJECT_APPROXIMATIONS");
    FILE *file;
    int ok;
    if (path == NULL || path[0] == '\0') return 1;
    if (frame_limit == NULL || frame_limit[0] == '\0') return 0;
    file = fopen(path, "wb");
    if (file == NULL) return 0;
    ok = fprintf(file, "frame_limit=%s\n", frame_limit) > 0;
    if (ok && reject_approximations != NULL) {
        ok = fprintf(
                 file,
                 "reject_approximations=%s\n",
                 reject_approximations) > 0;
    }
    if (fclose(file) != 0) ok = 0;
    return ok;
}

int main(int argc, char **argv) {
    const char *dvd_root = getenv("PORPOISE_DVD_ROOT");
    if (argc == 2 && strcmp(argv[1], "cancel") == 0) {
        volatile unsigned long spin = 0UL;
        fputs("ready-to-cancel\n", stdout);
        fflush(stdout);
        for (;;) spin++;
    }
    if (argc == 2 && strcmp(argv[1], "fault") == 0) {
        return write_status("FAULT", 0x800055E0U, "fixture guest fault") ?
            0 : 10;
    }
    if (argc == 2 && strcmp(argv[1], "missing-status") == 0) return 0;
    printf("dvd=%s\n", dvd_root == NULL ? "" : dvd_root);
    if (argc == 2 && strcmp(argv[1], "run-ok") == 0 &&
        dvd_root != NULL && strcmp(dvd_root, "fixture-dvd") == 0 &&
        porpoise_build_fixture_host_value() == 7 &&
        porpoise_build_fixture_cpp_value() == 11 && write_trace_marker()) {
        return write_status("OK", 0x800055E0U, "fixture completed") ? 0 : 10;
    }
    return 9;
}
