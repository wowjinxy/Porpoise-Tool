#include <dolphin/os/OSArena.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int porpoise_real_smoke_cpp_value(void);
int porpoise_real_smoke_title_host_value(void);

static int write_status(
    const char *status,
    uint32_t guest_pc,
    const char *message) {
    const char *path = getenv("PORPOISE_STATUS_FILE");
    FILE *file;
    int ok;
    if (path == NULL || path[0] == '\0') return 0;
    file = fopen(path, "wb");
    if (file == NULL) return 0;
    ok = fprintf(
             file,
             "PORPOISE_STATUS_V1\t%s\t%08X\t%s\n",
             status,
             (unsigned int)guest_pc,
             message) > 0;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

int main(void) {
    const uintptr_t arena_marker = UINT32_C(0x81234560);
    OSSetArenaLo((void *)arena_marker);
    if ((uintptr_t)OSGetArenaLo() != arena_marker ||
        porpoise_real_smoke_cpp_value() != 23 ||
        porpoise_real_smoke_title_host_value() != 19) {
        return write_status(
                   "FAULT",
                   UINT32_C(0x80000000),
                   "real libPorpoise smoke contract failed") ? 0 : 10;
    }
    return write_status(
               "OK",
               UINT32_C(0x80000000),
               "real libPorpoise smoke completed") ? 0 : 10;
}
