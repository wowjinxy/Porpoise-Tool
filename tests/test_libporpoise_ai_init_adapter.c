#include "porpoise_libporpoise_builtins_private.h"

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
    PorpoisePpcState state;
    const uint32_t guest_callback_stack = UINT32_C(0x81234568);

    PorpoiseStubAIReset();
    porpoise_state_init(&state, NULL);

    porpoise_libporpoise_ai_init_adapter(&state);

    CHECK(PorpoiseStubAIInitCount() == 1U);
    CHECK(PorpoiseStubAILastStack() == NULL);
    CHECK(state.gpr[3] == 0U);
    CHECK(!porpoise_state_has_fault(&state));

    porpoise_libporpoise_ai_init_adapter(NULL);
    CHECK(PorpoiseStubAIInitCount() == 1U);

    porpoise_state_init(&state, NULL);
    state.gpr[3] = guest_callback_stack;
    porpoise_libporpoise_ai_init_adapter(&state);
    CHECK(PorpoiseStubAIInitCount() == 1U);
    CHECK(PorpoiseStubAILastStack() == NULL);
    CHECK(state.gpr[3] == guest_callback_stack);
    CHECK(state.fault == PORPOISE_FAULT_UNSUPPORTED_OPERATION);
    CHECK(state.fault_address == guest_callback_stack);

    porpoise_state_init(&state, NULL);
    state.status = PORPOISE_EXECUTION_RETURNED;
    porpoise_libporpoise_ai_init_adapter(&state);
    CHECK(PorpoiseStubAIInitCount() == 1U);
    CHECK(state.gpr[3] == 0U);
    return 0;
}
