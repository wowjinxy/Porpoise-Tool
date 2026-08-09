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

typedef struct PorpoiseAddressAlias {
    char *name;
    char *c_name;
    bool is_global;
    bool is_function_name;
    size_t source_line;
    const char *source_path;
    uint32_t address;
    size_t instruction_item_index;
} PorpoiseAddressAlias;

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
    PorpoiseAddressAlias *aliases;
    size_t alias_count;
    size_t alias_capacity;
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

/* Borrowed-pointer lookup entries built once after parsing. */
typedef struct PorpoiseProgramSymbolIndexEntry {
    const char *name;
    const PorpoiseSourceFile *file;
    const PorpoiseFunction *function;
    const PorpoiseAddressAlias *alias;
} PorpoiseProgramSymbolIndexEntry;

typedef struct PorpoiseProgramLabelIndexEntry {
    const char *name;
    const PorpoiseFunction *function;
    uint32_t address;
    size_t instruction_item_index;
} PorpoiseProgramLabelIndexEntry;

typedef struct PorpoiseProgram {
    PorpoiseSourceFile *files;
    size_t file_count;
    size_t file_capacity;
    PorpoiseProgramSymbolIndexEntry *symbol_index;
    size_t symbol_index_count;
    size_t symbol_index_capacity;
    PorpoiseProgramLabelIndexEntry *label_index;
    size_t label_index_count;
    size_t label_index_capacity;
} PorpoiseProgram;

void porpoise_program_init(PorpoiseProgram *program);
void porpoise_program_free(PorpoiseProgram *program);
/* Load requires an initialized, empty program and never appends to existing IR. */
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
const PorpoiseAddressAlias *porpoise_program_find_alias(
    const PorpoiseProgram *program,
    const char *name,
    const PorpoiseFunction **function_out);
size_t porpoise_program_count_aliases(const PorpoiseProgram *program);
const PorpoiseAddressAlias *porpoise_program_alias_at(
    const PorpoiseProgram *program,
    size_t index,
    const PorpoiseSourceFile **file_out,
    const PorpoiseFunction **function_out);
bool porpoise_program_resolve_symbol(
    const PorpoiseProgram *program,
    const char *name,
    const PorpoiseFunction **function_out,
    const PorpoiseAddressAlias **alias_out,
    uint32_t *address_out);
bool porpoise_program_resolve_unique_label(
    const PorpoiseProgram *program,
    const char *name,
    const PorpoiseFunction **function_out,
    uint32_t *address_out,
    size_t *instruction_item_index_out);
size_t porpoise_program_count_named_function(
    const PorpoiseProgram *program,
    const char *name);

#endif
