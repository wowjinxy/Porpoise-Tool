#include "porpoise_libporpoise_adapter.h"

#include <porpoise/stub.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

int main(void)
{
    PorpoiseHostAdapter host;
    PorpoiseHostAdapter second_host;
    PorpoisePpcState state;
    void *decoded = NULL;
    void *native_pointer = PorpoiseStubNativePointer();
    uint8_t boundary_bytes[2];
    uint32_t token;
    uint32_t repeated_token;
    unsigned int releases_before = PorpoiseStubTokenReleaseCount();

    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    CHECK(host.context != NULL);
    CHECK(host.read_bytes != NULL);
    CHECK(host.write_bytes != NULL);
    CHECK(host.decode_pointer != NULL);
    CHECK(host.encode_pointer != NULL);
    CHECK(host.read_time_base != NULL);
    CHECK(host.trap == NULL);
    CHECK(host.system_call == NULL);
    CHECK(porpoise_libporpoise_adapter_init(&second_host) ==
          PORPOISE_HOST_INVALID_ARGUMENT);
    CHECK(porpoise_libporpoise_adapter_init(&host) ==
          PORPOISE_HOST_INVALID_ARGUMENT);

    porpoise_state_init(&state, &host);
    state.gpr[1] = UINT32_C(0x817FF000);
    state.gpr[2] = UINT32_C(0x80001000);
    state.gpr[13] = UINT32_C(0x80002000);
    CHECK(porpoise_state_prepare_title_entry(&state));
    CHECK(state.gpr[1] == UINT32_C(0x817FF000));
    CHECK(state.gpr[2] == UINT32_C(0x80001000));
    CHECK(state.gpr[13] == UINT32_C(0x80002000));
    token = porpoise_encode_pointer(&state, native_pointer);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(token == PorpoiseStubTokenAddress());
    CHECK(host.decode_pointer(host.context, token, &decoded) == PORPOISE_HOST_OK);
    CHECK(decoded == native_pointer);

    repeated_token = porpoise_encode_pointer(&state, native_pointer);
    CHECK(!porpoise_state_has_fault(&state));
    CHECK(repeated_token == token);

    (void)porpoise_load_u8(&state, token);
    CHECK(state.fault == PORPOISE_FAULT_INVALID_POINTER);
    porpoise_state_clear_fault(&state);

    (void)porpoise_load_u8(&state, token + UINT32_C(4));
    CHECK(state.fault == PORPOISE_FAULT_INVALID_POINTER);
    porpoise_state_clear_fault(&state);

    decoded = NULL;
    CHECK(host.decode_pointer(host.context, token + UINT32_C(4), &decoded) ==
          PORPOISE_HOST_INVALID_POINTER);
    CHECK(decoded == NULL);

    CHECK(host.read_bytes(
              host.context,
              UINT32_C(0xAFFFFFFF),
              boundary_bytes,
              sizeof(boundary_bytes)) == PORPOISE_HOST_INVALID_POINTER);

    (void)porpoise_load_u8(&state, UINT32_C(0x88000000));
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_MMIO);
    porpoise_state_clear_fault(&state);

    porpoise_libporpoise_adapter_shutdown(&host);
    CHECK(PorpoiseStubTokenReleaseCount() == releases_before + 1U);
    CHECK(host.context == NULL);
    CHECK(host.read_bytes == NULL);
    CHECK(host.decode_pointer == NULL);
    porpoise_libporpoise_adapter_shutdown(&host);
    CHECK(PorpoiseStubTokenReleaseCount() == releases_before + 1U);

    CHECK(porpoise_libporpoise_adapter_init(&host) == PORPOISE_HOST_OK);
    porpoise_state_init(&state, &host);
    CHECK(porpoise_encode_pointer(&state, native_pointer) == token);
    CHECK(!porpoise_state_has_fault(&state));
    porpoise_libporpoise_adapter_shutdown(&host);
    CHECK(PorpoiseStubTokenReleaseCount() == releases_before + 2U);

    return 0;
}
