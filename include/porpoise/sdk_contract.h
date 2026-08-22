#ifndef PORPOISE_SDK_CONTRACT_H
#define PORPOISE_SDK_CONTRACT_H

#include "porpoise/abi.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORPOISE_SDK_CONTRACT_MAX_ARGUMENTS = 8
};

#define PORPOISE_SDK_CONTRACT_BUILTIN_HEADER \
    "porpoise_libporpoise_builtins_private.h"

typedef enum PorpoiseSdkContractCategory {
    PORPOISE_SDK_CONTRACT_NINTENDO_DOLPHIN = 0,
    PORPOISE_SDK_CONTRACT_DEMO,
    PORPOISE_SDK_CONTRACT_CRT_MSL,
    PORPOISE_SDK_CONTRACT_RUNTIME,
    PORPOISE_SDK_CONTRACT_METROTRK,
    PORPOISE_SDK_CONTRACT_DEBUGGER,
    PORPOISE_SDK_CONTRACT_STUB,
    PORPOISE_SDK_CONTRACT_OTHER
} PorpoiseSdkContractCategory;

typedef enum PorpoiseSdkHostBindingKind {
    PORPOISE_SDK_HOST_BINDING_NONE = 0,
    /* A built-in adapter that performs specialized or stateful marshalling. */
    PORPOISE_SDK_HOST_BINDING_SPECIALIZED_ADAPTER,
    /* An ordinary typed host call described entirely by register mappings. */
    PORPOISE_SDK_HOST_BINDING_DIRECT_CALL
} PorpoiseSdkHostBindingKind;

typedef struct PorpoiseSdkAbiValue {
    PorpoiseAbiType type;
    PorpoiseAbiRegisterClass register_class;
    unsigned int register_index;
} PorpoiseSdkAbiValue;

typedef struct PorpoiseSdkContract PorpoiseSdkContract;

/*
 * The returned registry entries have static lifetime and are read-only.
 * Enumeration order is stable within a Porpoise release.
 */
size_t porpoise_sdk_contract_count(void);
const PorpoiseSdkContract *porpoise_sdk_contract_at(size_t index);
const PorpoiseSdkContract *porpoise_sdk_contract_find_by_canonical_name(
    const char *canonical_name);
const PorpoiseSdkContract *porpoise_sdk_contract_find_by_host_callable(
    const char *host_callable);
const PorpoiseSdkContract *porpoise_sdk_contract_find_by_canonical_identity(
    const char *canonical_identity);
const char *porpoise_sdk_contract_canonical_name(
    const PorpoiseSdkContract *contract);
PorpoiseSdkContractCategory porpoise_sdk_contract_category(
    const PorpoiseSdkContract *contract);
PorpoiseSdkHostBindingKind porpoise_sdk_contract_host_binding_kind(
    const PorpoiseSdkContract *contract);
const char *porpoise_sdk_contract_host_callable(
    const PorpoiseSdkContract *contract);
const char *porpoise_sdk_contract_host_header(
    const PorpoiseSdkContract *contract);
const PorpoiseSdkAbiValue *porpoise_sdk_contract_result(
    const PorpoiseSdkContract *contract);
size_t porpoise_sdk_contract_argument_count(
    const PorpoiseSdkContract *contract);
const PorpoiseSdkAbiValue *porpoise_sdk_contract_argument_at(
    const PorpoiseSdkContract *contract,
    size_t index);
bool porpoise_sdk_contract_allows_automatic_import(
    const PorpoiseSdkContract *contract);

/*
 * Return whether an exact catalog identity is permitted to select this
 * built-in contract automatically. Most established contracts accept any
 * exact catalog owner with the audited canonical symbol. Stateful startup
 * contracts instead carry one exact archive/object/symbol identity and fail
 * closed for every other catalog entry.
 */
bool porpoise_sdk_contract_accepts_canonical_identity(
    const PorpoiseSdkContract *contract,
    const char *canonical_identity);

/*
 * This compatibility predicate checks the host binding, header, and register
 * mappings. It deliberately does not require function->symbol to equal the
 * canonical name so existing explicitly named ABI aliases remain valid.
 */
bool porpoise_sdk_contract_binding_matches(
    const PorpoiseSdkContract *contract,
    const PorpoiseAbiFunction *function);

/*
 * Automatic import is stricter: the canonical symbol, audited binding, and
 * automatic-import flag must all match exactly.
 */
bool porpoise_sdk_contract_can_automatic_import(
    const PorpoiseSdkContract *contract,
    const PorpoiseAbiFunction *function);

#ifdef __cplusplus
}
#endif

#endif
