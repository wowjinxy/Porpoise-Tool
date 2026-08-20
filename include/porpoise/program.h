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
    char *section;
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
    char *section;
    bool is_global;
    bool skipped;
    bool data_region;
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

typedef struct PorpoiseDataLocalLabel {
    char *name;
    size_t source_line;
    uint32_t offset;
} PorpoiseDataLocalLabel;

typedef enum PorpoiseDataFixupKind {
    PORPOISE_DATA_FIXUP_ABSOLUTE_32 = 0,
    /* The `.rel BASE, TARGET` dialect stores TARGET's absolute address. */
    PORPOISE_DATA_FIXUP_REL_TARGET_32
} PorpoiseDataFixupKind;

typedef struct PorpoiseDataFixup {
    PorpoiseDataFixupKind kind;
    size_t source_line;
    uint32_t offset;
    uint8_t width;
    char *target_symbol;
    int64_t target_addend;
    char *base_symbol;
    int64_t base_addend;
} PorpoiseDataFixup;

typedef struct PorpoiseDataObject {
    char *name;
    char *section;
    bool is_global;
    size_t metadata_line;
    size_t source_line;
    size_t end_source_line;
    uint32_t section_offset;
    uint32_t address;
    uint32_t size;
    uint8_t *bytes;
    /* One byte per guest byte: nonzero means explicitly initialized. */
    uint8_t *initialized;
    PorpoiseDataLocalLabel *labels;
    size_t label_count;
    size_t label_capacity;
    PorpoiseDataFixup *fixups;
    size_t fixup_count;
    size_t fixup_capacity;
} PorpoiseDataObject;

/*
 * A whole section contribution range used to address byte emitters that are
 * intentionally outside named `.obj` records (normally alignment padding).
 * `storage` owns the bytes, labels, and fixups; `present` distinguishes
 * explicitly emitted bytes from ranges occupied by named objects.
 */
typedef struct PorpoiseAnonymousData {
    PorpoiseDataObject storage;
    uint8_t *present;
} PorpoiseAnonymousData;

typedef enum PorpoiseDataSpanKind {
    PORPOISE_DATA_SPAN_INITIALIZED = 0,
    PORPOISE_DATA_SPAN_ZERO_FILL
} PorpoiseDataSpanKind;

typedef struct PorpoiseDataSpan {
    PorpoiseDataSpanKind kind;
    uint32_t address;
    uint32_t size;
    /* Owned by the span for INITIALIZED spans; NULL for ZERO_FILL spans. */
    uint8_t *bytes;
    size_t source_file_index;
    /* SIZE_MAX denotes legacy data or an explicit anonymous contribution. */
    size_t data_object_index;
    size_t source_line;
    bool contribution_padding;
} PorpoiseDataSpan;

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
    PorpoiseDataObject *data_objects;
    size_t data_object_count;
    size_t data_object_capacity;
    PorpoiseAnonymousData *anonymous_data;
    size_t anonymous_data_count;
    size_t anonymous_data_capacity;
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
    const PorpoiseSourceFile *file;
    const PorpoiseFunction *function;
    uint32_t address;
    size_t instruction_item_index;
} PorpoiseProgramLabelIndexEntry;

typedef struct PorpoiseProgramDataSymbolIndexEntry {
    const char *name;
    const PorpoiseSourceFile *file;
    const PorpoiseDataObject *object;
} PorpoiseProgramDataSymbolIndexEntry;

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
    PorpoiseProgramDataSymbolIndexEntry *data_symbol_index;
    size_t data_symbol_index_count;
    size_t data_symbol_index_capacity;
    PorpoiseDataSpan *data_spans;
    size_t data_span_count;
    size_t data_span_capacity;
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
/* Exact input spelling, including aliases owned by skipped functions. */
const PorpoiseAddressAlias *porpoise_program_find_declared_alias(
    const PorpoiseProgram *program,
    const char *name,
    const PorpoiseFunction **function_out);
/*
 * Resolve an exact function name declared by the input, including a declared
 * function-name alias and including functions excluded by a skip list. This
 * intentionally does not resolve generated C-name spellings, ordinary symbol
 * aliases, or instruction labels.
 */
bool porpoise_program_resolve_declared_function(
    const PorpoiseProgram *program,
    const char *name,
    const PorpoiseFunction **function_out,
    const PorpoiseAddressAlias **alias_out,
    uint32_t *address_out);
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
/*
 * Resolve a lifted symbol from an assembly translation-unit scope. Local
 * declarations in the owning function and then the same file/section take
 * precedence over global declarations. Bare local names that are outside the
 * requested scope are never selected.
 */
bool porpoise_program_resolve_symbol_scoped(
    const PorpoiseProgram *program,
    const PorpoiseSourceFile *scope_file,
    const PorpoiseFunction *scope_function,
    const char *scope_section,
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
/*
 * Resolve an exact assembly address for data fixups. Unlike translation
 * lookups, this includes skipped functions and honors file/object locality.
 */
bool porpoise_program_resolve_raw_address(
    const PorpoiseProgram *program,
    const PorpoiseSourceFile *scope_file,
    const PorpoiseDataObject *scope_object,
    const char *name,
    uint32_t *address_out);
size_t porpoise_program_count_named_function(
    const PorpoiseProgram *program,
    const char *name);

#endif
