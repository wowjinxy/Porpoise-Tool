#include <dolphin/ai.h>
#include <porpoise/stub.h>

#include <stddef.h>

static unsigned int ai_init_count;
static u8 *ai_last_stack;

void AIInit(u8 *stack)
{
    ai_init_count++;
    ai_last_stack = stack;
}

void PorpoiseStubAIReset(void)
{
    ai_init_count = 0U;
    ai_last_stack = NULL;
}

unsigned int PorpoiseStubAIInitCount(void)
{
    return ai_init_count;
}

const void *PorpoiseStubAILastStack(void)
{
    return ai_last_stack;
}
