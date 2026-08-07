#include <math.h>
#include <stdint.h>

#include "porpoise_lifted.h"
#include "porpoise_generated.h"
#include "porpoise_imports.h"

void porpoise_lifted_add_one(PorpoisePpcState *state)
{
    if (state == NULL || porpoise_state_has_fault(state)) return;
    state->pc = UINT32_C(0x80001000);
    state->gpr[3] = state->gpr[3] + (uint32_t)(int32_t)(int16_t)UINT16_C(0x0001);
    state->pc = UINT32_C(0x80001004);
    return;
}
