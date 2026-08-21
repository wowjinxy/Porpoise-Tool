#ifndef PORPOISE_PROJECT_INTERNAL_H
#define PORPOISE_PROJECT_INTERNAL_H

#include "porpoise/analysis.h"
#include "porpoise/project.h"

#include <stdio.h>

typedef struct ProjectDataChunk {
    uint32_t address;
    size_t size;
    size_t capacity;
    uint8_t *bytes;
} ProjectDataChunk;

typedef struct ProjectContext {
    const PorpoiseProgram *program;
    const PorpoiseAbiManifest *abi;
    const PorpoiseProjectOptions *options;
    PorpoiseReport *report;
    PorpoiseDiagnostics *diagnostics;
    const PorpoiseTranslationPlan *plan;
    const PorpoiseAnalysis *analysis;
    const PorpoiseFunction *entry;
    int failure_code;
    char stage[PORPOISE_PATH_CAPACITY];
    char project_name[PORPOISE_NAME_CAPACITY];
    uint16_t *registry_shards;
    size_t registry_shard_count;
    ProjectDataChunk *data_chunks;
    size_t data_chunk_count;
    size_t data_chunk_capacity;
    bool cancellation_reported;
    bool output_expected_exists;
    bool output_expected_empty;
} ProjectContext;

struct PorpoiseStagedProject {
    char output_path[PORPOISE_PATH_CAPACITY];
    char stage_path[PORPOISE_PATH_CAPACITY];
    PorpoiseOperationCallbacks operation;
    bool force;
    bool output_expected_exists;
    bool output_expected_empty;
    bool published;
};

FILE *porpoise_project_open_generated_file(
    ProjectContext *context,
    const char *relative_path,
    char *full_path);

bool porpoise_project_checked_close(
    FILE *file,
    const char *path,
    PorpoiseDiagnostics *diagnostics);

const PorpoiseFunctionPlanView *porpoise_project_find_function_plan(
    const ProjectContext *context,
    const PorpoiseFunction *function);

PorpoisePlanAction porpoise_project_function_action(
    const ProjectContext *context,
    const PorpoiseFunction *function);

const PorpoiseAbiFunction *porpoise_project_function_import_binding(
    const ProjectContext *context,
    const PorpoiseFunction *function);

size_t porpoise_project_translated_function_count(
    const ProjectContext *context);

size_t porpoise_project_data_word_count(const PorpoiseProgram *program);

bool porpoise_project_write_report(ProjectContext *context);

bool porpoise_project_generation_cancelled(ProjectContext *context);

bool porpoise_project_prepare_generation(ProjectContext *context);

bool porpoise_project_generate_artifacts(ProjectContext *context);

void porpoise_project_release_generation(ProjectContext *context);

int porpoise_project_prepare_output(ProjectContext *context);

int porpoise_project_stage_context(
    ProjectContext *context,
    PorpoiseStagedProject **staged_out);

#endif
