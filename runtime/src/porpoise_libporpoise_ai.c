#include "porpoise_libporpoise_builtins_private.h"

#include <dolphin/ai.h>

#include <stddef.h>

void porpoise_libporpoise_ai_init_adapter(PorpoisePpcState *state)
{
    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }

    /* The current libPorpoise host AI implementation intentionally ignores
     * the console callback stack. Keep its uint32_t guest address opaque:
     * converting it to a native pointer would invent ownership and lifetime
     * semantics that the host API neither needs nor promises. */
    if (state->gpr[3] != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            state->gpr[3],
            "AIInit guest callback-stack semantics are unsupported");
        return;
    }
    AIInit(NULL);
}
