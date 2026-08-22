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

    CHECK(porpoise_sdk_contract_count() == 91U);
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
        if (strcmp(
                porpoise_sdk_contract_canonical_name(candidate),
                "DEMOPadInit") == 0) {
            CHECK(porpoise_sdk_contract_category(candidate) ==
                  PORPOISE_SDK_CONTRACT_DEMO);
        } else {
            CHECK(porpoise_sdk_contract_category(candidate) ==
                  PORPOISE_SDK_CONTRACT_NINTENDO_DOLPHIN);
        }
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
    CHECK(porpoise_sdk_contract_find_by_canonical_identity(NULL) == NULL);
    CHECK(porpoise_sdk_contract_find_by_canonical_identity("") == NULL);
    CHECK(porpoise_sdk_contract_find_by_canonical_identity(
              "other.a/Other.c/OSInit") == NULL);
    CHECK(!porpoise_sdk_contract_accepts_canonical_identity(NULL, "x"));
    return 0;
}

static int test_exact_startup_identities(void) {
    static const struct {
        const char *contract_name;
        const char *identity;
        PorpoiseSdkContractCategory category;
        size_t argument_count;
    } expected[] = {
        {"OSInit", "os.a/OS.c/OSInit",
         PORPOISE_SDK_CONTRACT_NINTENDO_DOLPHIN, 0U},
        {"DVDInit", "dvd.a/dvd.c/DVDInit",
         PORPOISE_SDK_CONTRACT_NINTENDO_DOLPHIN, 0U},
        {"VIInit", "vi.a/vi.c/VIInit",
         PORPOISE_SDK_CONTRACT_NINTENDO_DOLPHIN, 0U},
        {"DEMOPadInit", "demo.a/DEMOPad.c/DEMOPadInit",
         PORPOISE_SDK_CONTRACT_DEMO, 0U},
        {"VIWaitForRetrace", "vi.a/vi.c/VIWaitForRetrace",
         PORPOISE_SDK_CONTRACT_NINTENDO_DOLPHIN, 0U},
        {"GXDrawDone", "gx.a/GXMisc.c/GXDrawDone",
         PORPOISE_SDK_CONTRACT_NINTENDO_DOLPHIN, 0U},
        {"VISetBlack", "vi.a/vi.c/VISetBlack",
         PORPOISE_SDK_CONTRACT_NINTENDO_DOLPHIN, 1U},
        {"VIFlush", "vi.a/vi.c/VIFlush",
         PORPOISE_SDK_CONTRACT_NINTENDO_DOLPHIN, 0U}
    };
    size_t index;

    for (index = 0U; index < sizeof(expected) / sizeof(expected[0]); index++) {
        const PorpoiseSdkContract *contract =
            porpoise_sdk_contract_find_by_canonical_name(
                expected[index].contract_name);
        const PorpoiseSdkAbiValue *result;

        CHECK(contract != NULL);
        CHECK(porpoise_sdk_contract_category(contract) ==
              expected[index].category);
        CHECK(porpoise_sdk_contract_argument_count(contract) ==
              expected[index].argument_count);
        result = porpoise_sdk_contract_result(contract);
        CHECK(result != NULL && result->type == PORPOISE_ABI_VOID);
        CHECK(porpoise_sdk_contract_accepts_canonical_identity(
            contract, expected[index].identity));
        CHECK(porpoise_sdk_contract_find_by_canonical_identity(
                  expected[index].identity) == contract);
        CHECK(!porpoise_sdk_contract_accepts_canonical_identity(
            contract, expected[index].contract_name));
        CHECK(!porpoise_sdk_contract_accepts_canonical_identity(
            contract, "other.a/Other.c/OSInit"));
        CHECK(!porpoise_sdk_contract_accepts_canonical_identity(
            contract, NULL));
    }

    CHECK(porpoise_sdk_contract_accepts_canonical_identity(
        porpoise_sdk_contract_find_by_canonical_name("GXInit"),
        "gx.a/GXInit.c/GXInit"));
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

static int test_exact_pad_read_identity(void) {
    const PorpoiseSdkContract *contract =
        porpoise_sdk_contract_find_by_canonical_name("PADRead");
    const PorpoiseSdkAbiValue *value;

    CHECK(contract != NULL);
    CHECK(porpoise_sdk_contract_category(contract) ==
          PORPOISE_SDK_CONTRACT_NINTENDO_DOLPHIN);
    CHECK(strcmp(
              porpoise_sdk_contract_host_callable(contract),
              "porpoise_libporpoise_pad_read_adapter") == 0);
    CHECK(porpoise_sdk_contract_argument_count(contract) == 1U);
    value = porpoise_sdk_contract_result(contract);
    CHECK(value != NULL && value->type == PORPOISE_ABI_U32);
    CHECK(value->register_class == PORPOISE_ABI_REGISTER_GPR);
    CHECK(value->register_index == 3U);
    value = porpoise_sdk_contract_argument_at(contract, 0U);
    CHECK(value != NULL && value->type == PORPOISE_ABI_POINTER);
    CHECK(value->register_class == PORPOISE_ABI_REGISTER_GPR);
    CHECK(value->register_index == 3U);
    CHECK(porpoise_sdk_contract_accepts_canonical_identity(
        contract, "pad.a/Pad.c/PADRead"));
    CHECK(porpoise_sdk_contract_find_by_canonical_identity(
              "pad.a/Pad.c/PADRead") == contract);
    CHECK(!porpoise_sdk_contract_accepts_canonical_identity(
        contract, "PADRead"));
    CHECK(!porpoise_sdk_contract_accepts_canonical_identity(
        contract, "other.a/Pad.c/PADRead"));
    return 0;
}

static int test_exact_scalar_gx_identities(void) {
    static const struct {
        const char *name;
        const char *identity;
        size_t argument_count;
        PorpoiseAbiType result_type;
    } expected[] = {
        {"GXBegin", "gx.a/GXGeometry.c/GXBegin", 3U, PORPOISE_ABI_VOID},
        {"GXClearVtxDesc", "gx.a/GXAttr.c/GXClearVtxDesc", 0U,
         PORPOISE_ABI_VOID},
        {"GXSetVtxDesc", "gx.a/GXAttr.c/GXSetVtxDesc", 2U,
         PORPOISE_ABI_VOID},
        {"GXSetVtxAttrFmt", "gx.a/GXAttr.c/GXSetVtxAttrFmt", 5U,
         PORPOISE_ABI_VOID},
        {"GXInvalidateVtxCache", "gx.a/GXAttr.c/GXInvalidateVtxCache", 0U,
         PORPOISE_ABI_VOID},
        {"GXSetNumTexGens", "gx.a/GXAttr.c/GXSetNumTexGens", 1U,
         PORPOISE_ABI_VOID},
        {"GXSetNumChans", "gx.a/GXLight.c/GXSetNumChans", 1U,
         PORPOISE_ABI_VOID},
        {"GXInvalidateTexAll", "gx.a/GXTexture.c/GXInvalidateTexAll", 0U,
         PORPOISE_ABI_VOID},
        {"GXSetTevOp", "gx.a/GXTev.c/GXSetTevOp", 2U,
         PORPOISE_ABI_VOID},
        {"GXSetTevOrder", "gx.a/GXTev.c/GXSetTevOrder", 4U,
         PORPOISE_ABI_VOID},
        {"GXSetNumTevStages", "gx.a/GXTev.c/GXSetNumTevStages", 1U,
         PORPOISE_ABI_VOID},
        {"GXSetColorUpdate", "gx.a/GXPixel.c/GXSetColorUpdate", 1U,
         PORPOISE_ABI_VOID},
        {"GXSetZMode", "gx.a/GXPixel.c/GXSetZMode", 3U,
         PORPOISE_ABI_VOID},
        {"GXSetPixelFmt", "gx.a/GXPixel.c/GXSetPixelFmt", 2U,
         PORPOISE_ABI_VOID},
        {"GXSetViewport", "gx.a/GXTransform.c/GXSetViewport", 6U,
         PORPOISE_ABI_VOID},
        {"GXSetScissor", "gx.a/GXTransform.c/GXSetScissor", 4U,
         PORPOISE_ABI_VOID},
        {"GXSetDispCopySrc", "gx.a/GXFrameBuf.c/GXSetDispCopySrc", 4U,
         PORPOISE_ABI_VOID},
        {"GXGetYScaleFactor", "gx.a/GXFrameBuf.c/GXGetYScaleFactor", 2U,
         PORPOISE_ABI_F32},
        {"GXSetDispCopyYScale", "gx.a/GXFrameBuf.c/GXSetDispCopyYScale", 1U,
         PORPOISE_ABI_U32},
        {"GXSetDispCopyGamma", "gx.a/GXFrameBuf.c/GXSetDispCopyGamma", 1U,
         PORPOISE_ABI_VOID}
    };
    size_t index;

    for (index = 0U; index < sizeof(expected) / sizeof(expected[0]); index++) {
        const PorpoiseSdkContract *contract =
            porpoise_sdk_contract_find_by_canonical_name(expected[index].name);
        const PorpoiseSdkAbiValue *result;

        CHECK(contract != NULL);
        CHECK(porpoise_sdk_contract_category(contract) ==
              PORPOISE_SDK_CONTRACT_NINTENDO_DOLPHIN);
        CHECK(porpoise_sdk_contract_argument_count(contract) ==
              expected[index].argument_count);
        result = porpoise_sdk_contract_result(contract);
        CHECK(result != NULL && result->type == expected[index].result_type);
        CHECK(porpoise_sdk_contract_accepts_canonical_identity(
            contract, expected[index].identity));
        CHECK(porpoise_sdk_contract_find_by_canonical_identity(
                  expected[index].identity) == contract);
        CHECK(!porpoise_sdk_contract_accepts_canonical_identity(
            contract, expected[index].name));
        CHECK(!porpoise_sdk_contract_accepts_canonical_identity(
            contract, "other.a/GX.c/GXBegin"));
    }

    {
        const PorpoiseSdkContract *contract =
            porpoise_sdk_contract_find_by_canonical_name("GXSetViewport");
        const PorpoiseSdkAbiValue *argument =
            porpoise_sdk_contract_argument_at(contract, 5U);
        CHECK(argument != NULL && argument->type == PORPOISE_ABI_F32);
        CHECK(argument->register_class == PORPOISE_ABI_REGISTER_FPR);
        CHECK(argument->register_index == 6U);
    }
    {
        const PorpoiseSdkContract *contract =
            porpoise_sdk_contract_find_by_canonical_name(
                "GXGetYScaleFactor");
        const PorpoiseSdkAbiValue *result =
            porpoise_sdk_contract_result(contract);
        CHECK(result != NULL && result->register_class ==
              PORPOISE_ABI_REGISTER_FPR);
        CHECK(result->register_index == 1U);
    }
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
    if (test_exact_startup_identities() != 0) return 1;
    if (test_representative_contracts() != 0) return 1;
    if (test_exact_pad_read_identity() != 0) return 1;
    if (test_exact_scalar_gx_identities() != 0) return 1;
    if (test_invalid_bindings() != 0) return 1;
    return 0;
}
