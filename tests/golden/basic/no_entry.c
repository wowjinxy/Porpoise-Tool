#include <math.h>
#include <stdint.h>

#include "porpoise/generated/no_entry.h"
#include "porpoise_dispatch.h"
#include "porpoise_imports.h"

void porpoise_lifted_add_one(PorpoisePpcState *state)
{
    if (porpoise_state_should_stop(state)) return;
    state->pc = UINT32_C(0x80001000);
    state->gpr[3] = state->gpr[3] + porpoise_sign_extend16(UINT32_C(0x0001));
    state->pc = UINT32_C(0x80001004);
    return;
}
