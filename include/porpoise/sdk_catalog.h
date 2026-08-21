#ifndef PORPOISE_SDK_CATALOG_H
#define PORPOISE_SDK_CATALOG_H

#include "porpoise/common.h"
#include "porpoise/signature.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PORPOISE_SDK_CATALOG_SCHEMA_VERSION 1U

typedef enum PorpoiseSdkCategory {
    PORPOISE_SDK_CATEGORY_NINTENDO_DOLPHIN = 0,
    PORPOISE_SDK_CATEGORY_DEMO,
    PORPOISE_SDK_CATEGORY_CRT_MSL,
    PORPOISE_SDK_CATEGORY_RUNTIME,
    PORPOISE_SDK_CATEGORY_METROTRK,
    PORPOISE_SDK_CATEGORY_DEBUGGER,
    PORPOISE_SDK_CATEGORY_STUB
} PorpoiseSdkCategory;

typedef enum PorpoiseSdkCatalogSourceKind {
    PORPOISE_SDK_CATALOG_SOURCE_BUILTIN = 0,
    PORPOISE_SDK_CATALOG_SOURCE_JSON
} PorpoiseSdkCatalogSourceKind;

typedef struct PorpoiseSdkCatalogProvenance {
    PorpoiseSdkCatalogSourceKind source_kind;
    char *path;
    size_t line;
} PorpoiseSdkCatalogProvenance;

typedef struct PorpoiseSdkCatalogEntry {
    char *canonical_identity;
    PorpoiseSdkCategory category;
    char *contract_name;
    PorpoiseFunctionSignature signature;
    PorpoiseSdkCatalogProvenance provenance;
} PorpoiseSdkCatalogEntry;

typedef struct PorpoiseSdkCatalogLookupIndex
    PorpoiseSdkCatalogLookupIndex;

typedef struct PorpoiseSdkCatalog {
    PorpoiseSdkCatalogEntry *entries;
    size_t entry_count;
    size_t entry_capacity;
    /* Opaque exact-signature and canonical-identity lookup index. */
    PorpoiseSdkCatalogLookupIndex *lookup_index;
} PorpoiseSdkCatalog;

typedef enum PorpoiseSdkCatalogMatchStatus {
    PORPOISE_SDK_CATALOG_MATCH_NONE = 0,
    PORPOISE_SDK_CATALOG_MATCH_UNIQUE,
    PORPOISE_SDK_CATALOG_MATCH_AMBIGUOUS
} PorpoiseSdkCatalogMatchStatus;

typedef struct PorpoiseSdkCatalogMatch {
    PorpoiseSdkCatalogMatchStatus status;
    const PorpoiseSdkCatalogEntry *entry;
    size_t match_count;
} PorpoiseSdkCatalogMatch;

void porpoise_sdk_catalog_init(PorpoiseSdkCatalog *catalog);
void porpoise_sdk_catalog_free(PorpoiseSdkCatalog *catalog);

/*
 * Clone and add one entry. Semantically identical entries coalesce. Reusing a
 * canonical identity with different metadata is a hard configuration error.
 */
int porpoise_sdk_catalog_add(
    PorpoiseSdkCatalog *catalog,
    const PorpoiseSdkCatalogEntry *entry,
    PorpoiseDiagnostics *diagnostics);

/* Transactionally append source to destination under the same rules. */
int porpoise_sdk_catalog_merge(
    PorpoiseSdkCatalog *destination,
    const PorpoiseSdkCatalog *source,
    PorpoiseDiagnostics *diagnostics);

/* Parse a strict schema-version-1 JSON file and transactionally append it. */
int porpoise_sdk_catalog_load_json(
    PorpoiseSdkCatalog *catalog,
    const char *path,
    PorpoiseDiagnostics *diagnostics);

/*
 * Append the nonrecoverable built-in catalog. The initial catalog is empty,
 * but this versioned entry point keeps callers independent of its contents.
 */
int porpoise_sdk_catalog_load_builtin(
    PorpoiseSdkCatalog *catalog,
    PorpoiseDiagnostics *diagnostics);

/* Match the digest and every structural field exactly. */
PorpoiseSdkCatalogMatch porpoise_sdk_catalog_lookup_exact(
    const PorpoiseSdkCatalog *catalog,
    const PorpoiseFunctionSignature *signature);

/* Find a unique catalog entry by canonical identity, or NULL if absent. */
const PorpoiseSdkCatalogEntry *porpoise_sdk_catalog_find_identity(
    const PorpoiseSdkCatalog *catalog,
    const char *canonical_identity);

/*
 * Resolve identity and prove that signature has exactly one catalog owner.
 * This indexed operation is intended for fully revalidated cache hints and
 * never uses the identity to disambiguate an exact signature collision.
 */
PorpoiseSdkCatalogMatch porpoise_sdk_catalog_lookup_identity_exact(
    const PorpoiseSdkCatalog *catalog,
    const char *canonical_identity,
    const PorpoiseFunctionSignature *signature);

const char *porpoise_sdk_category_name(PorpoiseSdkCategory category);
bool porpoise_sdk_category_from_name(
    const char *name,
    PorpoiseSdkCategory *category_out);
bool porpoise_sdk_category_is_automatic(PorpoiseSdkCategory category);
bool porpoise_sdk_category_is_report_only(PorpoiseSdkCategory category);
const char *porpoise_sdk_catalog_source_kind_name(
    PorpoiseSdkCatalogSourceKind kind);

#ifdef __cplusplus
}
#endif

#endif
