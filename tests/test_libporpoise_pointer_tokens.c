#include "porpoise_libporpoise_adapter.h"

#include <dolphin/os/OSHostAddress.h>
#include <porpoise/stub.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            (void)fprintf(                                                 \
                stderr,                                                    \
                "check failed at %s:%d: %s\n",                            \
                __FILE__,                                                  \
                __LINE__,                                                  \
                #condition);                                               \
            abort();                                                       \
        }                                                                  \
    } while (0)

#define TEST_DIRECT_GUEST_ADDRESS UINT32_C(0x80002000)

int main(void)
{
    PorpoiseHostAdapter host;
    PorpoisePpcState state;
    uint32_t owned_tokens[OS_HOST_ADDRESS_TOKEN_SLOT_COUNT];
    uint32_t first_generation_token;
    uint32_t second_generation_token;
    uint32_t unowned_token;
    uint32_t encoded;
    unsigned int encodes_before;
    unsigned int releases_before;
    unsigned int index;
    void *decoded;
    void *direct_pointer;

    memset(&host, 0, sizeof(host));
    CHECK(OS_HOST_ADDRESS_TOKEN_SLOT_COUNT == 4U);
    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    porpoise_state_init(&state, &host);

    direct_pointer = NULL;
    CHECK(host.decode_pointer(
              host.context,
              TEST_DIRECT_GUEST_ADDRESS,
              &direct_pointer) == PORPOISE_HOST_OK);
    CHECK(direct_pointer != NULL);
    CHECK(porpoise_encode_pointer(&state, direct_pointer) ==
          TEST_DIRECT_GUEST_ADDRESS);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubTokenEncodeCount() == 0U);
    CHECK(PorpoiseStubTokenActiveCount() == 0U);

    first_generation_token = porpoise_encode_pointer(
        &state,
        PorpoiseStubNativePointerAt(0U));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(__OSHostIsAddressToken(first_generation_token));
    CHECK(PorpoiseStubTokenEncodeCount() == 1U);
    CHECK(PorpoiseStubTokenActiveCount() == 1U);

    for (index = 0U; index < 32U; index++) {
        CHECK(porpoise_encode_pointer(
                  &state,
                  PorpoiseStubNativePointerAt(0U)) ==
              first_generation_token);
        CHECK(!porpoise_state_has_fault(&state));
    }
    CHECK(PorpoiseStubTokenEncodeCount() == 1U);
    CHECK(PorpoiseStubTokenActiveCount() == 1U);

    decoded = NULL;
    CHECK(host.decode_pointer(
              host.context,
              first_generation_token,
              &decoded) == PORPOISE_HOST_OK);
    CHECK(decoded == PorpoiseStubNativePointerAt(0U));

    /* A token created outside the adapter is not accepted as guest input and
     * remains the creator's responsibility to release. */
    unowned_token = __OSHostEncodeAddress(PorpoiseStubNativePointerAt(4U));
    CHECK(__OSHostIsAddressToken(unowned_token));
    CHECK(unowned_token != first_generation_token);
    decoded = NULL;
    CHECK(host.decode_pointer(host.context, unowned_token, &decoded) ==
          PORPOISE_HOST_INVALID_POINTER);
    CHECK(decoded == NULL);
    releases_before = PorpoiseStubTokenReleaseCount();
    __OSHostReleaseAddress(unowned_token);
    CHECK(PorpoiseStubTokenReleaseCount() == releases_before + 1U);
    CHECK(PorpoiseStubTokenActiveCount() == 1U);

    /* Cached tokens are validated on both directions. Divergence fails closed
     * and must not allocate a replacement token behind the guest's back. */
    PorpoiseStubSetTokenDecodeBias(1U);
    decoded = NULL;
    CHECK(host.decode_pointer(
              host.context,
              first_generation_token,
              &decoded) == PORPOISE_HOST_INVALID_POINTER);
    CHECK(decoded == NULL);
    encodes_before = PorpoiseStubTokenEncodeCount();
    CHECK(porpoise_encode_pointer(
              &state,
              PorpoiseStubNativePointerAt(0U)) == 0U);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_POINTER);
    CHECK(PorpoiseStubTokenEncodeCount() == encodes_before);
    porpoise_state_clear_fault(&state);
    PorpoiseStubSetTokenDecodeBias(0U);

    owned_tokens[0] = first_generation_token;
    for (index = 1U; index < OS_HOST_ADDRESS_TOKEN_SLOT_COUNT; index++) {
        owned_tokens[index] = porpoise_encode_pointer(
            &state,
            PorpoiseStubNativePointerAt(index));
        CHECK(!porpoise_state_has_fault(&state));
        CHECK(__OSHostIsAddressToken(owned_tokens[index]));
        CHECK(owned_tokens[index] != owned_tokens[index - 1U]);
    }
    CHECK(PorpoiseStubTokenActiveCount() ==
          OS_HOST_ADDRESS_TOKEN_SLOT_COUNT);

    /* Capacity is rejected before calling the host encoder, and the already
     * interned map remains intact. Direct console pointers still encode. */
    encodes_before = PorpoiseStubTokenEncodeCount();
    encoded = porpoise_encode_pointer(
        &state,
        PorpoiseStubNativePointerAt(OS_HOST_ADDRESS_TOKEN_SLOT_COUNT));
    CHECK(encoded == 0U);
    CHECK(state.fault == PORPOISE_FAULT_HOST_IO);
    CHECK(PorpoiseStubTokenEncodeCount() == encodes_before);
    CHECK(PorpoiseStubTokenActiveCount() ==
          OS_HOST_ADDRESS_TOKEN_SLOT_COUNT);
    porpoise_state_clear_fault(&state);
    CHECK(porpoise_encode_pointer(&state, direct_pointer) ==
          TEST_DIRECT_GUEST_ADDRESS);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(PorpoiseStubTokenEncodeCount() == encodes_before);

    releases_before = PorpoiseStubTokenReleaseCount();
    porpoise_libporpoise_adapter_shutdown(&host);
    CHECK(PorpoiseStubTokenReleaseCount() ==
          releases_before + OS_HOST_ADDRESS_TOKEN_SLOT_COUNT);
    CHECK(PorpoiseStubTokenActiveCount() == 0U);
    CHECK(__OSHostDecodeAddress(first_generation_token) == NULL);
    porpoise_libporpoise_adapter_shutdown(&host);
    CHECK(PorpoiseStubTokenReleaseCount() ==
          releases_before + OS_HOST_ADDRESS_TOKEN_SLOT_COUNT);

    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    porpoise_state_init(&state, &host);
    second_generation_token = porpoise_encode_pointer(
        &state,
        PorpoiseStubNativePointerAt(0U));
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(__OSHostIsAddressToken(second_generation_token));
    CHECK(second_generation_token != first_generation_token);
    decoded = NULL;
    CHECK(host.decode_pointer(
              host.context,
              first_generation_token,
              &decoded) == PORPOISE_HOST_INVALID_POINTER);
    CHECK(decoded == NULL);
    CHECK(host.decode_pointer(
              host.context,
              second_generation_token,
              &decoded) == PORPOISE_HOST_OK);
    CHECK(decoded == PorpoiseStubNativePointerAt(0U));
    releases_before = PorpoiseStubTokenReleaseCount();
    porpoise_libporpoise_adapter_shutdown(&host);
    CHECK(PorpoiseStubTokenReleaseCount() == releases_before + 1U);
    CHECK(PorpoiseStubTokenActiveCount() == 0U);

    return 0;
}
