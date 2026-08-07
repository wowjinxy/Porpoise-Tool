#ifndef PORPOISE_PROGRAM_H
#define PORPOISE_PROGRAM_H

#include "porpoise/common.h"

typedef enum PorpoiseAsmItemKind {
    PORPOISE_ASM_INSTRUCTION = 0,
    PORPOISE_ASM_LABEL
} PorpoiseAsmItemKind;

typedef struct PorpoiseAsmItem {
    PorpoiseAsmItemKind kind;
    size_t source_line;
    uint32_t address;
    uint32_t word;
    char *mnemonic;
    char *operands;
    char *label;
} PorpoiseAsmItem;

typedef struct PorpoiseFunction {
    char *name;
    char *c_name;
    bool is_global;
    bool skipped;
    uint32_t start_address;
    uint32_t size;
    PorpoiseAsmItem *items;
    size_t item_count;
    size_t item_capacity;
    size_t instruction_count;
} PorpoiseFunction;

typedef struct PorpoiseDataWord {
    size_t source_line;
    uint32_t address;
    uint32_t word;
    char *directive;
} PorpoiseDataWord;

typedef struct PorpoiseSourceFile {
    char *path;
    char *relative_path;
    char *output_stem;
    PorpoiseFunction *functions;
    size_t function_count;
    size_t function_capacity;
    PorpoiseDataWord *data_words;
    size_t data_word_count;
    size_t data_word_capacity;
} PorpoiseSourceFile;

typedef struct PorpoiseProgram {
    PorpoiseSourceFile *files;
    size_t file_count;
    size_t file_capacity;
} PorpoiseProgram;

void porpoise_program_init(PorpoiseProgram *program);
void porpoise_program_free(PorpoiseProgram *program);
int porpoise_program_load(
    PorpoiseProgram *program,
    const char *input_path,
    PorpoiseDiagnostics *diagnostics);
int porpoise_program_apply_skip_list(
    PorpoiseProgram *program,
    const char *path,
    PorpoiseDiagnostics *diagnostics);
const PorpoiseFunction *porpoise_program_find_function(
    const PorpoiseProgram *program,
    const char *name);
size_t porpoise_program_count_named_function(
    const PorpoiseProgram *program,
    const char *name);

#endif
