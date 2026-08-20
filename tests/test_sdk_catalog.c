#include "porpoise/sdk_catalog.h"

#include <stdio.h>
#include <string.h>

static unsigned int failures = 0U;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                     \
                    __FILE__, __LINE__, #condition);                            \
            failures++;                                                        \
        }                                                                       \
    } while (0)

static bool fixture_path(
    char *output,
    size_t capacity,
    const char *root,
    const char *name) {
    int written = snprintf(
        output, capacity, "%s/tests/fixtures/sdk_catalog/%s", root, name);
    return written >= 0 && (size_t)written < capacity;
}

static void reset_diagnostics(PorpoiseDiagnostics *diagnostics) {
    porpoise_diagnostics_free(diagnostics);
    porpoise_diagnostics_init(diagnostics);
}

static void init_signature(
    PorpoiseFunctionSignature *signature,
    uint8_t digest_seed) {
    size_t index;
    memset(signature, 0, sizeof(*signature));
    signature->algorithm_version = PORPOISE_SIGNATURE_ALGORITHM_VERSION;
    signature->function_size = 32U;
    signature->instruction_count = 8U;
    signature->fixed_instruction_count = 5U;
    signature->meaningful_fixed_instruction_count = 4U;
    signature->relocation_count = 2U;
    signature->internal_branch_count = 1U;
    signature->external_branch_count = 1U;
    signature->external_target_count = 2U;
    for (index = 0U; index < PORPOISE_SHA256_DIGEST_SIZE; index++)
        signature->digest[index] = (uint8_t)(digest_seed + (uint8_t)index);
    porpoise_sha256_hex(signature->digest, signature->digest_hex);
}

static void test_strict_load_and_lookup(const char *root) {
    char path[PORPOISE_PATH_CAPACITY];
    PorpoiseSdkCatalog catalog;
    PorpoiseDiagnostics diagnostics;
    PorpoiseSdkCatalogMatch match;
    PorpoiseFunctionSignature changed;

    CHECK(fixture_path(path, sizeof(path), root, "valid.json"));
    porpoise_sdk_catalog_init(&catalog);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_sdk_catalog_load_builtin(
              &catalog, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(catalog.entry_count == 0U);
    CHECK(porpoise_sdk_catalog_load_json(
              &catalog, path, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    CHECK(catalog.entry_count == 2U);
    if (catalog.entry_count >= 2U) {
        const PorpoiseSdkCatalogEntry *entry = &catalog.entries[0];
        CHECK(strcmp(entry->canonical_identity, "sdk:OSReport") == 0);
        CHECK(entry->category == PORPOISE_SDK_CATEGORY_NINTENDO_DOLPHIN);
        CHECK(entry->contract_name != NULL);
        if (entry->contract_name != NULL)
            CHECK(strcmp(entry->contract_name, "OSReport") == 0);
        CHECK(entry->provenance.source_kind ==
              PORPOISE_SDK_CATALOG_SOURCE_JSON);
        CHECK(entry->provenance.path != NULL);
        if (entry->provenance.path != NULL)
            CHECK(strcmp(entry->provenance.path, path) == 0);
        CHECK(entry->provenance.line == 5U);
        CHECK(strcmp(
                  entry->signature.digest_hex,
                  "000102030405060708090a0b0c0d0e0f"
                  "101112131415161718191a1b1c1d1e1f") == 0);

        match = porpoise_sdk_catalog_lookup_exact(
            &catalog, &entry->signature);
        CHECK(match.status == PORPOISE_SDK_CATALOG_MATCH_UNIQUE);
        CHECK(match.match_count == 1U);
        CHECK(match.entry == entry);

        changed = entry->signature;
        changed.internal_branch_count++;
        match = porpoise_sdk_catalog_lookup_exact(&catalog, &changed);
        CHECK(match.status == PORPOISE_SDK_CATALOG_MATCH_NONE);
        CHECK(match.entry == NULL);
        CHECK(match.match_count == 0U);
    }

    /* Re-loading an identical additive catalog coalesces all identities. */
    CHECK(porpoise_sdk_catalog_load_json(
              &catalog, path, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(catalog.entry_count == 2U);
    porpoise_diagnostics_free(&diagnostics);
    porpoise_sdk_catalog_free(&catalog);
}

static void test_malformed_catalogs(const char *root) {
    static const char *const names[] = {
        "malformed-trailing.json",
        "malformed-unknown.json",
        "malformed-digest.json"
    };
    PorpoiseSdkCatalog catalog;
    PorpoiseDiagnostics diagnostics;
    size_t index;

    porpoise_sdk_catalog_init(&catalog);
    porpoise_diagnostics_init(&diagnostics);
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); index++) {
        char path[PORPOISE_PATH_CAPACITY];
        CHECK(fixture_path(path, sizeof(path), root, names[index]));
        CHECK(porpoise_sdk_catalog_load_json(
                  &catalog, path, &diagnostics) == PORPOISE_EXIT_USAGE);
        CHECK(porpoise_diagnostics_have_errors(&diagnostics));
        CHECK(catalog.entry_count == 0U);
        reset_diagnostics(&diagnostics);
    }
    porpoise_diagnostics_free(&diagnostics);
    porpoise_sdk_catalog_free(&catalog);
}

static void test_conflict_is_transactional(const char *root) {
    char valid_path[PORPOISE_PATH_CAPACITY];
    char conflict_path[PORPOISE_PATH_CAPACITY];
    PorpoiseSdkCatalog catalog;
    PorpoiseDiagnostics diagnostics;

    CHECK(fixture_path(valid_path, sizeof(valid_path), root, "valid.json"));
    CHECK(fixture_path(
        conflict_path, sizeof(conflict_path), root, "conflict.json"));
    porpoise_sdk_catalog_init(&catalog);
    porpoise_diagnostics_init(&diagnostics);
    CHECK(porpoise_sdk_catalog_load_json(
              &catalog, valid_path, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(catalog.entry_count == 2U);
    CHECK(porpoise_sdk_catalog_load_json(
              &catalog, conflict_path, &diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(porpoise_diagnostics_have_errors(&diagnostics));
    CHECK(catalog.entry_count == 2U);
    if (catalog.entry_count != 0U) {
        CHECK(strcmp(catalog.entries[0].canonical_identity,
                     "sdk:OSReport") == 0);
        CHECK(catalog.entries[0].category ==
              PORPOISE_SDK_CATEGORY_NINTENDO_DOLPHIN);
    }
    porpoise_diagnostics_free(&diagnostics);
    porpoise_sdk_catalog_free(&catalog);
}

static void test_programmatic_coalescing_and_ambiguity(void) {
    PorpoiseSdkCatalog catalog;
    PorpoiseDiagnostics diagnostics;
    PorpoiseSdkCatalogEntry first;
    PorpoiseSdkCatalogEntry alias;
    PorpoiseSdkCatalogEntry conflict;
    PorpoiseSdkCatalogEntry invalid;
    PorpoiseSdkCatalogMatch match;

    porpoise_sdk_catalog_init(&catalog);
    porpoise_diagnostics_init(&diagnostics);
    memset(&first, 0, sizeof(first));
    first.canonical_identity = "sdk:first";
    first.category = PORPOISE_SDK_CATEGORY_NINTENDO_DOLPHIN;
    first.contract_name = "first_contract";
    first.provenance.source_kind = PORPOISE_SDK_CATALOG_SOURCE_BUILTIN;
    first.provenance.path = "test:first";
    first.provenance.line = 1U;
    init_signature(&first.signature, 0x40U);

    CHECK(porpoise_sdk_catalog_add(
              &catalog, &first, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(porpoise_sdk_catalog_add(
              &catalog, &first, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(catalog.entry_count == 1U);

    alias = first;
    alias.canonical_identity = "sdk:alias";
    alias.category = PORPOISE_SDK_CATEGORY_DEMO;
    alias.contract_name = NULL;
    alias.provenance.path = "test:alias";
    CHECK(porpoise_sdk_catalog_add(
              &catalog, &alias, &diagnostics) == PORPOISE_EXIT_OK);
    CHECK(catalog.entry_count == 2U);
    match = porpoise_sdk_catalog_lookup_exact(&catalog, &first.signature);
    CHECK(match.status == PORPOISE_SDK_CATALOG_MATCH_AMBIGUOUS);
    CHECK(match.match_count == 2U);
    CHECK(match.entry == NULL);

    conflict = first;
    conflict.category = PORPOISE_SDK_CATEGORY_RUNTIME;
    conflict.provenance.path = "test:conflict";
    CHECK(porpoise_sdk_catalog_add(
              &catalog, &conflict, &diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(catalog.entry_count == 2U);
    CHECK(porpoise_diagnostics_have_errors(&diagnostics));
    reset_diagnostics(&diagnostics);

    invalid = first;
    invalid.canonical_identity = "sdk:invalid";
    invalid.signature.meaningful_fixed_instruction_count = 6U;
    CHECK(porpoise_sdk_catalog_add(
              &catalog, &invalid, &diagnostics) == PORPOISE_EXIT_USAGE);
    CHECK(catalog.entry_count == 2U);
    porpoise_diagnostics_free(&diagnostics);
    porpoise_sdk_catalog_free(&catalog);
}

static void test_category_policy(void) {
    static const struct CategoryCase {
        PorpoiseSdkCategory category;
        const char *name;
        bool automatic;
    } cases[] = {
        { PORPOISE_SDK_CATEGORY_NINTENDO_DOLPHIN,
          "nintendo_dolphin", true },
        { PORPOISE_SDK_CATEGORY_DEMO, "demo", true },
        { PORPOISE_SDK_CATEGORY_CRT_MSL, "crt_msl", false },
        { PORPOISE_SDK_CATEGORY_RUNTIME, "runtime", false },
        { PORPOISE_SDK_CATEGORY_METROTRK, "metrotrk", false },
        { PORPOISE_SDK_CATEGORY_DEBUGGER, "debugger", false },
        { PORPOISE_SDK_CATEGORY_STUB, "stub", false }
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
        PorpoiseSdkCategory parsed = PORPOISE_SDK_CATEGORY_STUB;
        CHECK(strcmp(
                  porpoise_sdk_category_name(cases[index].category),
                  cases[index].name) == 0);
        CHECK(porpoise_sdk_category_from_name(
            cases[index].name, &parsed));
        CHECK(parsed == cases[index].category);
        CHECK(porpoise_sdk_category_is_automatic(cases[index].category) ==
              cases[index].automatic);
        CHECK(porpoise_sdk_category_is_report_only(cases[index].category) ==
              !cases[index].automatic);
    }
    CHECK(!porpoise_sdk_category_from_name("other", NULL));
    CHECK(strcmp(
              porpoise_sdk_category_name((PorpoiseSdkCategory)99),
              "unknown") == 0);
    CHECK(!porpoise_sdk_category_is_report_only(
        (PorpoiseSdkCategory)99));
    CHECK(strcmp(
              porpoise_sdk_catalog_source_kind_name(
                  PORPOISE_SDK_CATALOG_SOURCE_BUILTIN),
              "builtin") == 0);
    CHECK(strcmp(
              porpoise_sdk_catalog_source_kind_name(
                  PORPOISE_SDK_CATALOG_SOURCE_JSON),
              "json") == 0);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: test_sdk_catalog SOURCE_ROOT\n");
        return 2;
    }
    test_strict_load_and_lookup(argv[1]);
    test_malformed_catalogs(argv[1]);
    test_conflict_is_transactional(argv[1]);
    test_programmatic_coalescing_and_ambiguity();
    test_category_policy();

    if (failures != 0U) {
        fprintf(stderr, "%u SDK catalog test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
