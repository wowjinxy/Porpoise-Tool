#ifndef PORPOISE_REPORT_H
#define PORPOISE_REPORT_H

#include "porpoise/common.h"

typedef enum PorpoiseLoweringStatus {
    PORPOISE_LOWERED = 0,
    PORPOISE_HOST_NOOP,
    PORPOISE_APPROXIMATE,
    PORPOISE_UNSUPPORTED
} PorpoiseLoweringStatus;

typedef struct PorpoiseInstructionReport {
    const char *file;
    size_t line;
    uint32_t address;
    const char *mnemonic;
    PorpoiseLoweringStatus status;
    bool semantic_test;
    const char *detail;
} PorpoiseInstructionReport;

typedef struct PorpoiseReport {
    PorpoiseInstructionReport *instructions;
    size_t instruction_count;
    size_t instruction_capacity;
    size_t status_counts[4];
    size_t source_count;
    size_t function_count;
} PorpoiseReport;

void porpoise_report_init(PorpoiseReport *report);
void porpoise_report_free(PorpoiseReport *report);
bool porpoise_report_add(
    PorpoiseReport *report,
    const char *file,
    size_t line,
    uint32_t address,
    const char *mnemonic,
    PorpoiseLoweringStatus status,
    bool semantic_test,
    const char *detail);

/*
 * Report records borrow file, mnemonic, and detail strings. Callers must keep
 * those immutable strings alive until porpoise_report_free(). The normal
 * project pipeline satisfies this through the program IR and opcode registry.
 */
const char *porpoise_lowering_status_name(PorpoiseLoweringStatus status);

#endif
