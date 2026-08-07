#ifndef PORPOISE_COMMON_H
#define PORPOISE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PORPOISE_PATH_CAPACITY 4096
#define PORPOISE_NAME_CAPACITY 256
#define PORPOISE_MESSAGE_CAPACITY 1024

enum {
    PORPOISE_EXIT_OK = 0,
    PORPOISE_EXIT_USAGE = 2,
    PORPOISE_EXIT_TRANSLATION = 3,
    PORPOISE_EXIT_IO = 4,
    PORPOISE_EXIT_INTERNAL = 5
};

typedef enum PorpoiseSeverity {
    PORPOISE_SEVERITY_INFO = 0,
    PORPOISE_SEVERITY_WARNING,
    PORPOISE_SEVERITY_ERROR
} PorpoiseSeverity;

typedef struct PorpoiseDiagnostic {
    PorpoiseSeverity severity;
    char *file;
    size_t line;
    uint32_t address;
    char *message;
} PorpoiseDiagnostic;

typedef struct PorpoiseDiagnostics {
    PorpoiseDiagnostic *items;
    size_t count;
    size_t capacity;
} PorpoiseDiagnostics;

void porpoise_diagnostics_init(PorpoiseDiagnostics *diagnostics);
void porpoise_diagnostics_free(PorpoiseDiagnostics *diagnostics);
bool porpoise_diagnostics_add(
    PorpoiseDiagnostics *diagnostics,
    PorpoiseSeverity severity,
    const char *file,
    size_t line,
    uint32_t address,
    const char *format,
    ...);
bool porpoise_diagnostics_have_errors(const PorpoiseDiagnostics *diagnostics);

#endif
