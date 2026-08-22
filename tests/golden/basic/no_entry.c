#include <math.h>
#include <stdint.h>

#include "generated/no_entry.h"
#include "porpoise_dispatch_private.h"
#include "porpoise_imports_private.h"

void porpoise_lifted_add_one(PorpoisePpcState *state)
{
    if (porpoise_state_should_stop(state)) return;
    state->pc = UINT32_C(0x80001000);
    state->gpr[3] = state->gpr[3] + porpoise_sign_extend16(UINT32_C(0x0001));
    state->pc = UINT32_C(0x80001004);
    if (!porpoise_guest_lr_returns_to_caller(state)) { uint32_t target = state->lr; (void)porpoise_branch_address(state, target); }
    return;
}
