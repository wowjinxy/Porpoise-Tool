#include <dolphin/ar.h>
#include <porpoise/stub.h>

#include <stddef.h>
#include <stdint.h>

#define STUB_AR_BASE UINT32_C(0x00004000)
#define STUB_AR_DEFAULT_SIZE UINT32_C(0x01000000)
#define STUB_AR_ALIGNMENT UINT32_C(32)

static BOOL stub_ar_initialized;
static u32 *stub_ar_table;
static u32 stub_ar_capacity;
static u32 stub_ar_allocated;
static u32 stub_ar_stack_pointer = STUB_AR_BASE;
static u32 stub_ar_size;
static u32 stub_ar_configured_size = STUB_AR_DEFAULT_SIZE;
static unsigned int stub_ar_init_count;
static unsigned int stub_ar_alloc_count;
static unsigned int stub_ar_free_count;
static unsigned int stub_ar_reset_count;

u32 ARInit(u32 *stack_index_addr, u32 num_entries)
{
    stub_ar_init_count++;
    if (stub_ar_initialized) {
        return STUB_AR_BASE;
    }
    stub_ar_initialized = TRUE;
    stub_ar_table = stack_index_addr;
    stub_ar_capacity = num_entries;
    stub_ar_allocated = 0U;
    stub_ar_stack_pointer = STUB_AR_BASE;
    stub_ar_size = stub_ar_configured_size;
    return STUB_AR_BASE;
}

BOOL ARCheckInit(void)
{
    return stub_ar_initialized;
}

u32 ARAlloc(u32 length)
{
    u32 address;

    stub_ar_alloc_count++;
    if (!stub_ar_initialized ||
        (length & (STUB_AR_ALIGNMENT - UINT32_C(1))) != 0U ||
        stub_ar_allocated >= stub_ar_capacity ||
        stub_ar_table == NULL ||
        stub_ar_stack_pointer > stub_ar_size ||
        length > stub_ar_size - stub_ar_stack_pointer) {
        return 0U;
    }
    address = stub_ar_stack_pointer;
    stub_ar_stack_pointer += length;
    stub_ar_table[stub_ar_allocated++] = length;
    return address;
}

u32 ARFree(u32 *length)
{
    u32 block_length;

    stub_ar_free_count++;
    if (!stub_ar_initialized || stub_ar_allocated == 0U ||
        stub_ar_table == NULL) {
        if (length != NULL) {
            *length = 0U;
        }
        return 0U;
    }
    block_length = stub_ar_table[stub_ar_allocated - 1U];
    if (block_length > stub_ar_stack_pointer - STUB_AR_BASE) {
        if (length != NULL) {
            *length = 0U;
        }
        return 0U;
    }
    stub_ar_allocated--;
    stub_ar_stack_pointer -= block_length;
    if (length != NULL) {
        *length = block_length;
    }
    return stub_ar_stack_pointer;
}

void ARReset(void)
{
    stub_ar_reset_count++;
    stub_ar_initialized = FALSE;
    stub_ar_table = NULL;
    stub_ar_capacity = 0U;
    stub_ar_allocated = 0U;
    stub_ar_stack_pointer = STUB_AR_BASE;
}

u32 ARGetBaseAddress(void)
{
    return STUB_AR_BASE;
}

u32 ARGetSize(void)
{
    return stub_ar_size;
}

void PorpoiseStubARAllocatorResetState(void)
{
    stub_ar_initialized = FALSE;
    stub_ar_table = NULL;
    stub_ar_capacity = 0U;
    stub_ar_allocated = 0U;
    stub_ar_stack_pointer = STUB_AR_BASE;
    stub_ar_size = 0U;
    stub_ar_configured_size = STUB_AR_DEFAULT_SIZE;
    stub_ar_init_count = 0U;
    stub_ar_alloc_count = 0U;
    stub_ar_free_count = 0U;
    stub_ar_reset_count = 0U;
}

unsigned int PorpoiseStubARAllocatorInitCount(void)
{
    return stub_ar_init_count;
}

unsigned int PorpoiseStubARAllocatorAllocCount(void)
{
    return stub_ar_alloc_count;
}

unsigned int PorpoiseStubARAllocatorFreeCount(void)
{
    return stub_ar_free_count;
}

unsigned int PorpoiseStubARAllocatorResetCount(void)
{
    return stub_ar_reset_count;
}

const uint32_t *PorpoiseStubARAllocatorBlockTable(void)
{
    return (const uint32_t *)stub_ar_table;
}

uint32_t PorpoiseStubARAllocatorBlockValue(unsigned int index)
{
    if (stub_ar_table == NULL || index >= stub_ar_capacity) {
        return 0U;
    }
    return (uint32_t)stub_ar_table[index];
}

void PorpoiseStubARAllocatorSetSize(uint32_t size)
{
    stub_ar_configured_size = (u32)size;
    if (stub_ar_initialized) {
        stub_ar_size = (u32)size;
    }
}
