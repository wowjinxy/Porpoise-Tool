#include "porpoise/report.h"
#include "porpoise/util.h"

#include <stdlib.h>
#include <string.h>

void porpoise_report_init(PorpoiseReport *report) {
    memset(report, 0, sizeof(*report));
}

void porpoise_report_free(PorpoiseReport *report) {
    if (report == NULL) return;
    free(report->instructions);
    memset(report, 0, sizeof(*report));
}

bool porpoise_report_add(
    PorpoiseReport *report,
    const char *file,
    size_t line,
    uint32_t address,
    const char *mnemonic,
    PorpoiseLoweringStatus status,
    bool semantic_test,
    const char *detail) {
    PorpoiseInstructionReport *item;
    if (status < PORPOISE_LOWERED || status > PORPOISE_UNSUPPORTED) return false;
    if (!porpoise_grow_array((void **)&report->instructions, &report->instruction_capacity,
                             sizeof(*report->instructions), report->instruction_count + 1U)) {
        return false;
    }
    item = &report->instructions[report->instruction_count];
    memset(item, 0, sizeof(*item));
    item->file = file == NULL ? "" : file;
    item->line = line;
    item->address = address;
    item->mnemonic = mnemonic == NULL ? "" : mnemonic;
    item->status = status;
    item->semantic_test = semantic_test;
    item->detail = detail == NULL ? "" : detail;
    report->instruction_count++;
    report->status_counts[(size_t)status]++;
    return true;
}

const char *porpoise_lowering_status_name(PorpoiseLoweringStatus status) {
    switch (status) {
    case PORPOISE_LOWERED: return "lowered";
    case PORPOISE_HOST_NOOP: return "host-equivalent-no-op";
    case PORPOISE_APPROXIMATE: return "approximate";
    case PORPOISE_UNSUPPORTED: return "unsupported";
    default: return "invalid";
    }
}
