#include <dolphin.h>
#include <porpoise/stub.h>

#include <stddef.h>
#include <stdint.h>

enum {
    STUB_MEMORY_SIZE = 24 * 1024 * 1024,
    STUB_ARENA_OFFSET = 0x1000
};

static u8 stub_memory[STUB_MEMORY_SIZE];
static unsigned int os_init_count;
static unsigned int report_count;
static OSHostMemoryLayout memory_layout;
static BOOL memory_initialized;

static BOOL pointer_is_in_memory(const void *pointer) {
    const uintptr_t value = (uintptr_t)pointer;
    const uintptr_t begin = (uintptr_t)&stub_memory[0];
    const uintptr_t end = begin + (uintptr_t)sizeof(stub_memory);

    return value >= begin && value < end;
}

const OSHostMemoryLayout *__OSHostMemoryInit(OSHostMemoryProfile profile) {
    memory_layout.profile = profile;
    memory_layout.cachedBase = &stub_memory[0];
    memory_layout.uncachedBase = &stub_memory[0];
    memory_layout.size = (u32)sizeof(stub_memory);
    memory_layout.consoleSize = (u32)sizeof(stub_memory);
    memory_layout.arenaLo = &stub_memory[STUB_ARENA_OFFSET];
    memory_layout.arenaHi = &stub_memory[sizeof(stub_memory)];
    memory_layout.consoleArenaHi = memory_layout.arenaHi;
    memory_initialized = TRUE;
    return &memory_layout;
}

const OSHostMemoryLayout *__OSHostMemoryGetLayout(void) {
    return memory_initialized ? &memory_layout : NULL;
}

BOOL __OSHostMemoryContainsAddress(const void *address) {
    return pointer_is_in_memory(address);
}

void *__OSHostMemoryResolveArenaHi(void *previous, void *requested) {
    (void)previous;
    if (pointer_is_in_memory(requested)) {
        return requested;
    }
    return memory_initialized ? memory_layout.arenaHi : NULL;
}

BOOL __OSHostIsAddressToken(u32 address) {
    return (address & OS_HOST_ADDRESS_TOKEN_MASK) == OS_HOST_ADDRESS_TOKEN_TAG;
}

BOOL __OSHostIsFileBackedImageAddress(const void *pointer) {
    (void)pointer;
    return FALSE;
}

u32 __OSHostEncodeAddress(const void *pointer) {
    const uintptr_t value = (uintptr_t)pointer;
    const uintptr_t begin = (uintptr_t)&stub_memory[0];

    if (!pointer_is_in_memory(pointer)) {
        return 0;
    }
    return 0x80000000U + (u32)(value - begin);
}

u32 __OSHostEncodePointerWord(const void *pointer) {
    return __OSHostEncodeAddress(pointer);
}

void *__OSHostDecodeAddress(u32 address) {
    u32 offset;

    if (address < (u32)sizeof(stub_memory)) {
        offset = address;
    } else if (address >= 0x80000000U &&
               address < 0x80000000U + (u32)sizeof(stub_memory)) {
        offset = address - 0x80000000U;
    } else if (address >= 0xC0000000U &&
               address < 0xC0000000U + (u32)sizeof(stub_memory)) {
        offset = address - 0xC0000000U;
    } else {
        return NULL;
    }

    return &stub_memory[offset];
}

void __OSHostReleaseAddress(u32 token) {
    (void)token;
}

void OSInit(void) {
    ++os_init_count;
    if (!memory_initialized) {
        (void)__OSHostMemoryInit(OS_HOST_MEMORY_PROFILE_GAMECUBE);
    }
}

unsigned int PorpoiseStubOSInitCount(void) {
    return os_init_count;
}

uint32_t PorpoiseStubAdd(uint32_t left, uint32_t right) {
    return left + right;
}

void *PorpoiseStubIdentity(void *pointer) {
    return pointer;
}

double PorpoiseStubFloatMix(float left, double right) {
    return (double)left + right;
}

void PorpoiseStubReportAdapter(struct PorpoisePpcState *state) {
    (void)state;
    ++report_count;
}

unsigned int PorpoiseStubReportCount(void) {
    return report_count;
}
