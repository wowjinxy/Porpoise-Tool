#include "porpoise/report.h"
#include "porpoise/util.h"

#include <stdlib.h>
#include <string.h>

void porpoise_report_init(PorpoiseReport *report) {
    memset(report, 0, sizeof(*report));
}

void porpoise_report_free(PorpoiseReport *report) {
    size_t index;
    if (report == NULL) return;
    for (index = 0U; index < report->instruction_count; index++) {
        free(report->instructions[index].file);
        free(report->instructions[index].mnemonic);
        free(report->instructions[index].detail);
    }
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
    char *file_copy;
    char *mnemonic_copy;
    char *detail_copy;
    if (status < PORPOISE_LOWERED || status > PORPOISE_UNSUPPORTED) return false;
    if (!porpoise_grow_array((void **)&report->instructions, &report->instruction_capacity,
                             sizeof(*report->instructions), report->instruction_count + 1U)) {
        return false;
    }
    file_copy = porpoise_strdup(file == NULL ? "" : file);
    mnemonic_copy = porpoise_strdup(mnemonic == NULL ? "" : mnemonic);
    detail_copy = porpoise_strdup(detail == NULL ? "" : detail);
    if (file_copy == NULL || mnemonic_copy == NULL || detail_copy == NULL) {
        free(file_copy);
        free(mnemonic_copy);
        free(detail_copy);
        return false;
    }
    item = &report->instructions[report->instruction_count];
    memset(item, 0, sizeof(*item));
    item->file = file_copy;
    item->line = line;
    item->address = address;
    item->mnemonic = mnemonic_copy;
    item->status = status;
    item->semantic_test = semantic_test;
    item->detail = detail_copy;
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
