#include "porpoise/sdk_contract.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",               \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static PorpoiseAbiValue abi_value(
    PorpoiseAbiType type,
    PorpoiseAbiRegisterClass register_class,
    unsigned int register_index) {
    PorpoiseAbiValue value;
    value.type = type;
    value.register_class = register_class;
    value.register_index = register_index;
    value.name = NULL;
    return value;
}

static PorpoiseAbiFunction function_for_contract(
    const PorpoiseSdkContract *contract,
    PorpoiseAbiValue *arguments) {
    PorpoiseAbiFunction function;
    const PorpoiseSdkAbiValue *expected;
    size_t index;

    memset(&function, 0, sizeof(function));
    function.kind = PORPOISE_ABI_IMPORT;
    function.symbol = (char *)porpoise_sdk_contract_canonical_name(contract);
    function.header = (char *)porpoise_sdk_contract_host_header(contract);
    function.adapter = (char *)porpoise_sdk_contract_host_callable(contract);
    expected = porpoise_sdk_contract_result(contract);
    function.result = abi_value(
        expected->type,
        expected->register_class,
        expected->register_index);
    function.argument_count = porpoise_sdk_contract_argument_count(contract);
    function.arguments = arguments;
    for (index = 0U; index < function.argument_count; index++) {
        expected = porpoise_sdk_contract_argument_at(contract, index);
        arguments[index] = abi_value(
            expected->type,
            expected->register_class,
            expected->register_index);
    }
    return function;
}

static int test_enumeration_and_lookup(void) {
    const PorpoiseSdkContract *first;
    const PorpoiseSdkContract *candidate;
    size_t index;
    size_t previous;

    CHECK(porpoise_sdk_contract_count() == 62U);
    first = porpoise_sdk_contract_at(0U);
    CHECK(first != NULL);
    CHECK(strcmp(
              porpoise_sdk_contract_canonical_name(first),
              "AIInit") == 0);
    CHECK(strcmp(
              porpoise_sdk_contract_host_callable(first),
              "porpoise_libporpoise_ai_init_adapter") == 0);
    CHECK(strcmp(
              porpoise_sdk_contract_host_header(first),
              PORPOISE_SDK_CONTRACT_BUILTIN_HEADER) == 0);
    CHECK(porpoise_sdk_contract_at(porpoise_sdk_contract_count()) == NULL);

    for (index = 0U; index < porpoise_sdk_contract_count(); index++) {
        candidate = porpoise_sdk_contract_at(index);
        CHECK(candidate != NULL);
        CHECK(porpoise_sdk_contract_category(candidate) ==
              PORPOISE_SDK_CONTRACT_NINTENDO_DOLPHIN);
        CHECK(porpoise_sdk_contract_host_binding_kind(candidate) ==
              PORPOISE_SDK_HOST_BINDING_SPECIALIZED_ADAPTER);
        CHECK(porpoise_sdk_contract_allows_automatic_import(candidate));
        CHECK(porpoise_sdk_contract_find_by_canonical_name(
                  porpoise_sdk_contract_canonical_name(candidate)) ==
              candidate);
        CHECK(porpoise_sdk_contract_find_by_host_callable(
                  porpoise_sdk_contract_host_callable(candidate)) ==
              candidate);
        for (previous = 0U; previous < index; previous++) {
            const PorpoiseSdkContract *other =
                porpoise_sdk_contract_at(previous);
            CHECK(strcmp(
                      porpoise_sdk_contract_canonical_name(candidate),
                      porpoise_sdk_contract_canonical_name(other)) != 0);
            CHECK(strcmp(
                      porpoise_sdk_contract_host_callable(candidate),
                      porpoise_sdk_contract_host_callable(other)) != 0);
        }
    }

    CHECK(porpoise_sdk_contract_find_by_canonical_name(NULL) == NULL);
    CHECK(porpoise_sdk_contract_find_by_canonical_name("NotAnSdkCall") == NULL);
    CHECK(porpoise_sdk_contract_find_by_host_callable(NULL) == NULL);
    CHECK(porpoise_sdk_contract_find_by_host_callable("host_unknown") == NULL);
    return 0;
}

static int test_representative_contracts(void) {
    const PorpoiseSdkContract *contract;
    const PorpoiseSdkAbiValue *value;
    PorpoiseAbiValue arguments[PORPOISE_SDK_CONTRACT_MAX_ARGUMENTS];
    PorpoiseAbiFunction function;

    contract = porpoise_sdk_contract_find_by_canonical_name("GXSetFog");
    CHECK(contract != NULL);
    CHECK(porpoise_sdk_contract_argument_count(contract) == 6U);
    value = porpoise_sdk_contract_result(contract);
    CHECK(value->type == PORPOISE_ABI_VOID);
    CHECK(value->register_class == PORPOISE_ABI_REGISTER_NONE);
    value = porpoise_sdk_contract_argument_at(contract, 0U);
    CHECK(value->type == PORPOISE_ABI_U32);
    CHECK(value->register_class == PORPOISE_ABI_REGISTER_GPR);
    CHECK(value->register_index == 3U);
    value = porpoise_sdk_contract_argument_at(contract, 4U);
    CHECK(value->type == PORPOISE_ABI_F32);
    CHECK(value->register_class == PORPOISE_ABI_REGISTER_FPR);
    CHECK(value->register_index == 4U);
    value = porpoise_sdk_contract_argument_at(contract, 5U);
    CHECK(value->type == PORPOISE_ABI_POINTER);
    CHECK(value->register_class == PORPOISE_ABI_REGISTER_GPR);
    CHECK(value->register_index == 4U);
    CHECK(porpoise_sdk_contract_argument_at(contract, 6U) == NULL);
    function = function_for_contract(contract, arguments);
    CHECK(porpoise_sdk_contract_binding_matches(contract, &function));
    CHECK(porpoise_sdk_contract_can_automatic_import(contract, &function));

    contract = porpoise_sdk_contract_find_by_canonical_name("ARQPostRequest");
    CHECK(contract != NULL);
    CHECK(porpoise_sdk_contract_argument_count(contract) == 8U);
    value = porpoise_sdk_contract_argument_at(contract, 7U);
    CHECK(value->type == PORPOISE_ABI_U32);
    CHECK(value->register_class == PORPOISE_ABI_REGISTER_GPR);
    CHECK(value->register_index == 10U);

    contract = porpoise_sdk_contract_find_by_canonical_name("OSGetArenaHi");
    CHECK(contract != NULL);
    CHECK(porpoise_sdk_contract_argument_count(contract) == 0U);
    value = porpoise_sdk_contract_result(contract);
    CHECK(value->type == PORPOISE_ABI_POINTER);
    CHECK(value->register_index == 3U);
    return 0;
}

static int test_invalid_bindings(void) {
    const PorpoiseSdkContract *contract =
        porpoise_sdk_contract_find_by_canonical_name("GXInit");
    PorpoiseAbiValue arguments[PORPOISE_SDK_CONTRACT_MAX_ARGUMENTS];
    PorpoiseAbiFunction function;

    CHECK(contract != NULL);
    function = function_for_contract(contract, arguments);

    function.symbol = (char *)"GXInitAlias";
    CHECK(porpoise_sdk_contract_binding_matches(contract, &function));
    CHECK(!porpoise_sdk_contract_can_automatic_import(contract, &function));
    function.symbol = (char *)porpoise_sdk_contract_canonical_name(contract);

    function.header = (char *)"host_api.h";
    CHECK(!porpoise_sdk_contract_binding_matches(contract, &function));
    function.header = (char *)porpoise_sdk_contract_host_header(contract);

    function.result.type = PORPOISE_ABI_U32;
    CHECK(!porpoise_sdk_contract_binding_matches(contract, &function));
    function = function_for_contract(contract, arguments);

    function.arguments[1].register_index = 5U;
    CHECK(!porpoise_sdk_contract_binding_matches(contract, &function));
    function = function_for_contract(contract, arguments);

    function.arguments = NULL;
    CHECK(!porpoise_sdk_contract_binding_matches(contract, &function));
    function = function_for_contract(contract, arguments);

    function.kind = PORPOISE_ABI_EXPORT;
    CHECK(!porpoise_sdk_contract_binding_matches(contract, &function));
    CHECK(!porpoise_sdk_contract_binding_matches(NULL, &function));
    CHECK(!porpoise_sdk_contract_binding_matches(contract, NULL));
    CHECK(!porpoise_sdk_contract_can_automatic_import(NULL, &function));
    CHECK(!porpoise_sdk_contract_can_automatic_import(contract, NULL));
    return 0;
}

int main(void) {
    if (test_enumeration_and_lookup() != 0) return 1;
    if (test_representative_contracts() != 0) return 1;
    if (test_invalid_bindings() != 0) return 1;
    return 0;
}
