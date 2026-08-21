#ifndef PORPOISE_ABI_H
#define PORPOISE_ABI_H

#include "porpoise/common.h"

typedef enum PorpoiseAbiKind {
    PORPOISE_ABI_IMPORT = 0,
    PORPOISE_ABI_EXPORT
} PorpoiseAbiKind;

typedef enum PorpoiseAbiType {
    PORPOISE_ABI_VOID = 0,
    PORPOISE_ABI_U8,
    PORPOISE_ABI_U16,
    PORPOISE_ABI_U32,
    PORPOISE_ABI_S8,
    PORPOISE_ABI_S16,
    PORPOISE_ABI_S32,
    PORPOISE_ABI_F32,
    PORPOISE_ABI_F64,
    PORPOISE_ABI_POINTER
} PorpoiseAbiType;

typedef enum PorpoiseAbiRegisterClass {
    PORPOISE_ABI_REGISTER_NONE = 0,
    PORPOISE_ABI_REGISTER_GPR,
    PORPOISE_ABI_REGISTER_FPR
} PorpoiseAbiRegisterClass;

typedef struct PorpoiseAbiValue {
    PorpoiseAbiType type;
    PorpoiseAbiRegisterClass register_class;
    unsigned int register_index;
    char *name;
} PorpoiseAbiValue;

typedef struct PorpoiseAbiFunction {
    PorpoiseAbiKind kind;
    char *symbol;
    char *wrapper;
    char *header;
    char *adapter;
    PorpoiseAbiValue result;
    PorpoiseAbiValue *arguments;
    size_t argument_count;
} PorpoiseAbiFunction;

typedef struct PorpoiseAbiManifest {
    PorpoiseAbiFunction *functions;
    size_t function_count;
} PorpoiseAbiManifest;

void porpoise_abi_init(PorpoiseAbiManifest *manifest);
void porpoise_abi_free(PorpoiseAbiManifest *manifest);
int porpoise_abi_load(
    PorpoiseAbiManifest *manifest,
    const char *path,
    PorpoiseDiagnostics *diagnostics);
/*
 * Add contracts transactionally. Byte-for-byte equivalent declarations
 * coalesce; a repeated symbol with a different contract is rejected.
 */
int porpoise_abi_merge(
    PorpoiseAbiManifest *destination,
    const PorpoiseAbiManifest *source,
    const char *source_identity,
    PorpoiseDiagnostics *diagnostics);
int porpoise_abi_load_additive(
    PorpoiseAbiManifest *manifest,
    const char *path,
    PorpoiseDiagnostics *diagnostics);
const PorpoiseAbiFunction *porpoise_abi_find_import(
    const PorpoiseAbiManifest *manifest,
    const char *symbol);
const char *porpoise_abi_type_name(PorpoiseAbiType type);

#endif
