#include "porpoise/symbol_map.h"

#include <stdio.h>
#include <string.h>

static unsigned int failures = 0U;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            failures++;                                                        \
        }                                                                       \
    } while (0)

static bool fixture_path(
    char *path,
    size_t capacity,
    const char *source_root,
    const char *leaf) {
    int written = snprintf(
        path, capacity, "%s/tests/fixtures/symbol_map/%s", source_root, leaf);
    return written >= 0 && (size_t)written < capacity;
}

static bool diagnostics_contain(
    const PorpoiseDiagnostics *diagnostics,
    const char *fragment) {
    size_t index;
    for (index = 0U; index < diagnostics->count; index++) {
        if (strstr(diagnostics->items[index].message, fragment) != NULL)
            return true;
    }
    return false;
}

static const PorpoiseSymbol *find_symbol(
    const PorpoiseSymbolCatalog *catalog,
    const char *module,
    const char *name) {
    size_t index = porpoise_symbol_catalog_find_name(
        catalog, module, name, 0U);
    return index == SIZE_MAX ? NULL : &catalog->symbols[index];
}

static void test_codewarrior(const char *source_root) {
    char path[PORPOISE_PATH_CAPACITY];
    PorpoiseSymbolCatalog catalog;
    PorpoiseSymbolMapLoadOptions options;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseSymbol *symbol;
    size_t index;

    CHECK(fixture_path(
        path, sizeof(path), source_root, "codewarrior.map"));
    porpoise_symbol_catalog_init(&catalog);
    porpoise_symbol_map_load_options_init(&options);
    options.module = "main";
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_symbol_catalog_load_codewarrior(
              &catalog, path, &options, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    CHECK(catalog.symbol_count == 9U);

    symbol = find_symbol(&catalog, "main", "MainFunc");
    CHECK(symbol != NULL);
    if (symbol != NULL) {
        CHECK(symbol->used && symbol->has_address && symbol->has_size);
        CHECK(symbol->address == UINT32_C(0x80001000));
        CHECK(symbol->size == UINT32_C(0x18));
        CHECK(symbol->kind == PORPOISE_SYMBOL_KIND_FUNCTION);
        CHECK(symbol->scope == PORPOISE_SYMBOL_SCOPE_WEAK);
        CHECK(strcmp(symbol->section, ".text") == 0);
        CHECK(strcmp(symbol->library, "sdk.a") == 0);
        CHECK(strcmp(symbol->object, "main.c") == 0);
        CHECK(symbol->provenance.kind ==
              PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP);
        CHECK(symbol->provenance.line == 10U);
    }

    symbol = find_symbol(&catalog, "main", "MainAlias");
    CHECK(symbol != NULL && symbol->kind == PORPOISE_SYMBOL_KIND_LABEL);
    CHECK(symbol != NULL && symbol->size == 0U && symbol->has_size);

    symbol = find_symbol(&catalog, "main", "GoneFunc");
    CHECK(symbol != NULL && !symbol->used && !symbol->has_address);
    CHECK(symbol != NULL && symbol->kind == PORPOISE_SYMBOL_KIND_FUNCTION);

    symbol = find_symbol(&catalog, "main", "GlobalData");
    CHECK(symbol != NULL && symbol->kind == PORPOISE_SYMBOL_KIND_OBJECT);
    CHECK(symbol != NULL && symbol->scope == PORPOISE_SYMBOL_SCOPE_LOCAL);

    symbol = find_symbol(&catalog, "main", ".text");
    CHECK(symbol != NULL && symbol->kind == PORPOISE_SYMBOL_KIND_SECTION);
    index = porpoise_symbol_catalog_find_address(
        &catalog, "main", ".text", UINT32_C(0x80001000), 0U);
    CHECK(index != SIZE_MAX);
    CHECK(index != SIZE_MAX &&
          catalog.symbols[index].address == UINT32_C(0x80001000));
    CHECK(porpoise_symbol_catalog_find_name(
              &catalog, "other", "MainFunc", 0U) == SIZE_MAX);

    symbol = find_symbol(&catalog, "main", "_stack_addr");
    CHECK(symbol != NULL);
    if (symbol != NULL) {
        CHECK(symbol->used && symbol->has_address && !symbol->has_size);
        CHECK(symbol->address == UINT32_C(0x80004000));
        CHECK(symbol->section == NULL);
        CHECK(symbol->kind == PORPOISE_SYMBOL_KIND_LABEL);
        CHECK(symbol->scope == PORPOISE_SYMBOL_SCOPE_GLOBAL);
        CHECK(symbol->provenance.kind ==
              PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP);
        CHECK(symbol->provenance.line == 22U);
    }
    symbol = find_symbol(&catalog, "main", "_SDA_BASE_");
    CHECK(symbol != NULL && symbol->address == UINT32_C(0x80005000));
    symbol = find_symbol(&catalog, "main", "_SDA2_BASE_");
    CHECK(symbol != NULL && symbol->address == UINT32_C(0x80006000));

    CHECK(strcmp(porpoise_symbol_kind_name(
                     PORPOISE_SYMBOL_KIND_FUNCTION), "function") == 0);
    CHECK(strcmp(porpoise_symbol_scope_name(
                     PORPOISE_SYMBOL_SCOPE_WEAK), "weak") == 0);
    CHECK(strcmp(porpoise_symbol_source_kind_name(
                     PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP),
                 "codewarrior-map") == 0);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_symbol_catalog_free(&catalog);
}

static void test_dtk(const char *source_root) {
    char symbols[PORPOISE_PATH_CAPACITY];
    char splits[PORPOISE_PATH_CAPACITY];
    PorpoiseSymbolCatalog catalog;
    PorpoiseSymbolMapLoadOptions options;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseSymbol *symbol;
    size_t alias_index;

    CHECK(fixture_path(
        symbols, sizeof(symbols), source_root, "dtk-symbols.txt"));
    CHECK(fixture_path(
        splits, sizeof(splits), source_root, "dtk-splits.txt"));
    porpoise_symbol_catalog_init(&catalog);
    porpoise_symbol_map_load_options_init(&options);
    options.module = "rel:sample";
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_symbol_catalog_load_dtk(
              &catalog, symbols, splits, &options, &diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    CHECK(catalog.symbol_count == 4U);

    symbol = find_symbol(&catalog, "rel:sample", "RelFunction");
    CHECK(symbol != NULL);
    if (symbol != NULL) {
        CHECK(symbol->address == UINT32_C(0x100));
        CHECK(symbol->size == UINT32_C(0x20));
        CHECK(symbol->kind == PORPOISE_SYMBOL_KIND_FUNCTION);
        CHECK(symbol->scope == PORPOISE_SYMBOL_SCOPE_GLOBAL);
        CHECK(strcmp(symbol->library, "sdk.a") == 0);
        CHECK(strcmp(symbol->object, "rel_code.c") == 0);
        CHECK(strcmp(symbol->provenance.auxiliary_path, splits) == 0);
        CHECK(symbol->provenance.auxiliary_line == 6U);
    }
    alias_index = porpoise_symbol_catalog_find_address(
        &catalog, "rel:sample", ".text", UINT32_C(0x100), 0U);
    CHECK(alias_index != SIZE_MAX);
    alias_index = porpoise_symbol_catalog_find_address(
        &catalog, "rel:sample", ".text", UINT32_C(0x100),
        alias_index == SIZE_MAX ? 0U : alias_index + 1U);
    CHECK(alias_index != SIZE_MAX); /* Address aliases remain distinct. */

    symbol = find_symbol(&catalog, "rel:sample", "LocalObject");
    CHECK(symbol != NULL && strcmp(symbol->object, "rel_data.c") == 0);
    CHECK(symbol != NULL && symbol->library == NULL);
    symbol = find_symbol(&catalog, "rel:sample", "Unowned");
    CHECK(symbol != NULL && symbol->object == NULL);
    CHECK(symbol != NULL && symbol->scope == PORPOISE_SYMBOL_SCOPE_UNKNOWN);
    CHECK(strcmp(porpoise_symbol_source_kind_name(
                     PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS),
                 "dtk-symbols") == 0);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_symbol_catalog_free(&catalog);
}

static void test_partial_and_empty(const char *source_root) {
    char path[PORPOISE_PATH_CAPACITY];
    PorpoiseSymbolCatalog catalog;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseSymbol *symbol;

    porpoise_symbol_catalog_init(&catalog);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(fixture_path(
        path, sizeof(path), source_root, "empty.map"));
    CHECK(porpoise_symbol_catalog_load_codewarrior(
              &catalog, path, NULL, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(catalog.symbol_count == 0U);

    CHECK(fixture_path(
        path, sizeof(path), source_root, "dtk-partial-symbols.txt"));
    CHECK(porpoise_symbol_catalog_load_dtk(
              &catalog, path, NULL, NULL, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(catalog.symbol_count == 1U);
    symbol = find_symbol(&catalog, NULL, "OnlySymbol");
    CHECK(symbol != NULL && symbol->object == NULL);

    CHECK(fixture_path(
        path, sizeof(path), source_root, "empty-symbols.txt"));
    CHECK(porpoise_symbol_catalog_load_dtk(
              &catalog, path, NULL, NULL, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(catalog.symbol_count == 1U);
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    porpoise_diagnostics_free(&diagnostics);
    porpoise_symbol_catalog_free(&catalog);
}

static void test_strict_failures_are_transactional(
    const char *source_root) {
    char symbols[PORPOISE_PATH_CAPACITY];
    char splits[PORPOISE_PATH_CAPACITY];
    PorpoiseSymbolCatalog catalog;
    PorpoiseSymbolMapLoadOptions options;
    PorpoiseDiagnostics diagnostics;
    size_t original_count;

    porpoise_symbol_catalog_init(&catalog);
    porpoise_symbol_map_load_options_init(&options);
    options.module = "rel:sample";
    porpoise_diagnostics_init(&diagnostics);
    CHECK(fixture_path(
        symbols, sizeof(symbols), source_root, "dtk-symbols.txt"));
    CHECK(fixture_path(
        splits, sizeof(splits), source_root, "dtk-splits.txt"));
    CHECK(porpoise_symbol_catalog_load_dtk(
              &catalog, symbols, splits, &options, &diagnostics) ==
          PORPOISE_EXIT_OK);
    original_count = catalog.symbol_count;

    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(fixture_path(
        symbols, sizeof(symbols), source_root, "dtk-conflict-symbols.txt"));
    CHECK(porpoise_symbol_catalog_load_dtk(
              &catalog, symbols, NULL, &options, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    CHECK(catalog.symbol_count == original_count);
    CHECK(diagnostics_contain(&diagnostics, "conflicts"));

    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(fixture_path(
        symbols, sizeof(symbols), source_root, "dtk-malformed-symbols.txt"));
    CHECK(porpoise_symbol_catalog_load_dtk(
              &catalog, symbols, NULL, &options, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    CHECK(catalog.symbol_count == original_count);
    CHECK(diagnostics_contain(&diagnostics, "address"));

    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(fixture_path(
        symbols, sizeof(symbols), source_root, "dtk-symbols.txt"));
    CHECK(fixture_path(
        splits, sizeof(splits), source_root, "dtk-overlap-splits.txt"));
    CHECK(porpoise_symbol_catalog_load_dtk(
              &catalog, symbols, splits, &options, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    CHECK(catalog.symbol_count == original_count);
    CHECK(diagnostics_contain(&diagnostics, "overlaps"));

    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(fixture_path(
        symbols, sizeof(symbols), source_root, "malformed.map"));
    CHECK(porpoise_symbol_catalog_load_codewarrior(
              &catalog, symbols, &options, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    CHECK(catalog.symbol_count == original_count);
    CHECK(diagnostics_contain(&diagnostics, "malformed"));

    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(fixture_path(
        symbols, sizeof(symbols), source_root, "malformed-linker.map"));
    CHECK(porpoise_symbol_catalog_load_codewarrior(
              &catalog, symbols, &options, &diagnostics) ==
          PORPOISE_EXIT_TRANSLATION);
    CHECK(catalog.symbol_count == original_count);
    CHECK(diagnostics_contain(
        &diagnostics, "linker-generated symbol record"));

    porpoise_diagnostics_free(&diagnostics);
    porpoise_symbol_catalog_free(&catalog);
}

static void test_nonstrict_skips_malformed(const char *source_root) {
    char path[PORPOISE_PATH_CAPACITY];
    PorpoiseSymbolCatalog catalog;
    PorpoiseSymbolMapLoadOptions options;
    PorpoiseDiagnostics diagnostics;

    CHECK(fixture_path(
        path, sizeof(path), source_root, "dtk-malformed-symbols.txt"));
    porpoise_symbol_catalog_init(&catalog);
    porpoise_symbol_map_load_options_init(&options);
    options.strict = false;
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_symbol_catalog_load_dtk(
              &catalog, path, NULL, &options, &diagnostics) ==
          PORPOISE_EXIT_OK);
    CHECK(catalog.symbol_count == 0U);
    CHECK(diagnostics.count == 1U);
    CHECK(diagnostics.items[0].severity == PORPOISE_SEVERITY_WARNING);
    porpoise_diagnostics_free(&diagnostics);
    porpoise_symbol_catalog_free(&catalog);

    CHECK(fixture_path(
        path, sizeof(path), source_root, "malformed-linker.map"));
    porpoise_symbol_catalog_init(&catalog);
    porpoise_symbol_map_load_options_init(&options);
    options.strict = false;
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_symbol_catalog_load_codewarrior(
              &catalog, path, &options, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(catalog.symbol_count == 0U);
    CHECK(diagnostics.count == 1U);
    CHECK(diagnostics.items[0].severity == PORPOISE_SEVERITY_WARNING);
    porpoise_diagnostics_free(&diagnostics);
    porpoise_symbol_catalog_free(&catalog);
}

int main(int argc, char **argv) {
    const char *source_root = argc > 1 ? argv[1] : ".";
    test_codewarrior(source_root);
    test_dtk(source_root);
    test_partial_and_empty(source_root);
    test_strict_failures_are_transactional(source_root);
    test_nonstrict_skips_malformed(source_root);

    if (failures != 0U) {
        fprintf(stderr, "%u symbol-map test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
