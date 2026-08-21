#ifndef PORPOISE_SYMBOL_MAP_H
#define PORPOISE_SYMBOL_MAP_H

#include "porpoise/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PorpoiseSymbolKind {
    PORPOISE_SYMBOL_KIND_UNKNOWN = 0,
    PORPOISE_SYMBOL_KIND_FUNCTION,
    PORPOISE_SYMBOL_KIND_OBJECT,
    PORPOISE_SYMBOL_KIND_SECTION,
    PORPOISE_SYMBOL_KIND_LABEL
} PorpoiseSymbolKind;

typedef enum PorpoiseSymbolScope {
    PORPOISE_SYMBOL_SCOPE_UNKNOWN = 0,
    PORPOISE_SYMBOL_SCOPE_LOCAL,
    PORPOISE_SYMBOL_SCOPE_GLOBAL,
    PORPOISE_SYMBOL_SCOPE_WEAK
} PorpoiseSymbolScope;

typedef enum PorpoiseSymbolSourceKind {
    PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP = 0,
    PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS
} PorpoiseSymbolSourceKind;

typedef struct PorpoiseSymbolProvenance {
    PorpoiseSymbolSourceKind kind;
    char *path;
    size_t line;

    /* Set when DTK splits.txt supplied the translation-unit ownership. */
    char *auxiliary_path;
    size_t auxiliary_line;
} PorpoiseSymbolProvenance;

typedef struct PorpoiseSymbol {
    char *name;
    char *section;
    char *module;
    char *object;
    char *library;

    uint32_t address;
    uint32_t size;
    bool has_address;
    bool has_size;
    bool used;

    PorpoiseSymbolKind kind;
    PorpoiseSymbolScope scope;
    PorpoiseSymbolProvenance provenance;
} PorpoiseSymbol;

typedef struct PorpoiseSymbolCatalog {
    PorpoiseSymbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
} PorpoiseSymbolCatalog;

typedef struct PorpoiseSymbolMapLoadOptions {
    /* NULL and the empty string both mean the main/unnamed module. */
    const char *module;

    /* Malformed records are errors when strict and warnings otherwise. */
    bool strict;
} PorpoiseSymbolMapLoadOptions;

void porpoise_symbol_catalog_init(PorpoiseSymbolCatalog *catalog);
void porpoise_symbol_catalog_free(PorpoiseSymbolCatalog *catalog);
void porpoise_symbol_map_load_options_init(
    PorpoiseSymbolMapLoadOptions *options);

/*
 * Load the section-layout portion of a CodeWarrior linker map. Call-tree
 * metadata is used, when present, to recover symbol kind and scope. Empty or
 * partial maps are valid. The catalog is unchanged when loading fails.
 */
int porpoise_symbol_catalog_load_codewarrior(
    PorpoiseSymbolCatalog *catalog,
    const char *map_path,
    const PorpoiseSymbolMapLoadOptions *options,
    PorpoiseDiagnostics *diagnostics);

/*
 * Load DTK symbols.txt and optionally pair it with splits.txt. An empty or
 * NULL splits_path leaves object ownership unknown. Empty files are valid.
 * The catalog is unchanged when loading fails.
 */
int porpoise_symbol_catalog_load_dtk(
    PorpoiseSymbolCatalog *catalog,
    const char *symbols_path,
    const char *splits_path,
    const PorpoiseSymbolMapLoadOptions *options,
    PorpoiseDiagnostics *diagnostics);

/* Return the first exact match at or after start_index, or SIZE_MAX. */
size_t porpoise_symbol_catalog_find_name(
    const PorpoiseSymbolCatalog *catalog,
    const char *module,
    const char *name,
    size_t start_index);
size_t porpoise_symbol_catalog_find_address(
    const PorpoiseSymbolCatalog *catalog,
    const char *module,
    const char *section,
    uint32_t address,
    size_t start_index);

const char *porpoise_symbol_kind_name(PorpoiseSymbolKind kind);
const char *porpoise_symbol_scope_name(PorpoiseSymbolScope scope);
const char *porpoise_symbol_source_kind_name(
    PorpoiseSymbolSourceKind kind);

#ifdef __cplusplus
}
#endif

#endif
