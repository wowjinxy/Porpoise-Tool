#ifndef PORPOISE_ASM_DATA_INTERNAL_H
#define PORPOISE_ASM_DATA_INTERNAL_H

#include "porpoise/program.h"

typedef struct PorpoiseAsmDataMetadata {
    char section[PORPOISE_NAME_CAPACITY];
    uint32_t section_offset;
    uint32_t address;
    uint32_t size;
    size_t source_line;
} PorpoiseAsmDataMetadata;

typedef struct PorpoiseAsmDataParser {
    PorpoiseAsmDataMetadata metadata;
    bool have_metadata;
    uint32_t range_address;
    uint32_t range_size;
    size_t range_source_line;
    bool have_range;
    PorpoiseAnonymousData *active_anonymous;
    size_t contribution_offset;
    PorpoiseDataObject *current_object;
    uint8_t *current_presence;
    size_t current_offset;
    char selected_section[PORPOISE_NAME_CAPACITY];
    bool have_selected_section;
    bool selected_section_executable;
    bool executable_symbol_data;
} PorpoiseAsmDataParser;

typedef enum PorpoiseAsmDataLineResult {
    PORPOISE_ASM_DATA_NOT_HANDLED = 0,
    PORPOISE_ASM_DATA_HANDLED,
    PORPOISE_ASM_DATA_ERROR,
    PORPOISE_ASM_DATA_INTERNAL_ERROR
} PorpoiseAsmDataLineResult;

void porpoise_asm_data_parser_init(PorpoiseAsmDataParser *parser);

/* True while an executable-section `.sym` owns following annotated words. */
bool porpoise_asm_data_accepts_annotated_words(
    const PorpoiseAsmDataParser *parser);

/* A real `.fn` always ends any preceding symbol-delimited data region. */
void porpoise_asm_data_begin_function(PorpoiseAsmDataParser *parser);

/*
 * Recognize a decomp-toolkit contribution metadata comment. `recognized` is
 * true for both valid metadata and malformed metadata-looking comments.
 */
PorpoiseAsmDataLineResult porpoise_asm_data_parse_metadata(
    PorpoiseAsmDataParser *parser,
    const PorpoiseSourceFile *file,
    const char *line,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics,
    bool *recognized);

/* Handle object structure/body and fail-closed bare byte emitters. */
PorpoiseAsmDataLineResult porpoise_asm_data_parse_line(
    PorpoiseAsmDataParser *parser,
    PorpoiseSourceFile *file,
    const char *line,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics);

bool porpoise_asm_data_finish_file(
    PorpoiseAsmDataParser *parser,
    const PorpoiseSourceFile *file,
    size_t source_line,
    PorpoiseDiagnostics *diagnostics);

void porpoise_asm_data_free_object(PorpoiseDataObject *object);

/* Resolve fixups, validate ranges, and construct Program::data_spans. */
bool porpoise_asm_data_finalize(
    PorpoiseProgram *program,
    PorpoiseDiagnostics *diagnostics);

#endif
