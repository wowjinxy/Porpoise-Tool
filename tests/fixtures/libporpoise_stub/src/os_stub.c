#include <dolphin.h>
#include <porpoise/stub.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    STUB_MEMORY_SIZE = 24 * 1024 * 1024,
    STUB_ARENA_OFFSET = 0x1000,
    STUB_NATIVE_POINTER_SIZE = 16
};

#define STUB_ADDRESS_TOKEN (OS_HOST_ADDRESS_TOKEN_TAG | (1U << OS_HOST_ADDRESS_TOKEN_SLOT_BITS))

static u8 stub_memory[STUB_MEMORY_SIZE];
static u8 native_pointer_bytes[STUB_NATIVE_POINTER_SIZE];
static unsigned int os_init_count;
static unsigned int report_count;
static unsigned int token_release_count;
static unsigned int bootstrap_count;
static u64 host_time_ticks;
static BOOL token_in_use;
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
        if (pointer == &native_pointer_bytes[0]) {
            token_in_use = TRUE;
            return STUB_ADDRESS_TOKEN;
        }
        return 0U;
    }
    return 0x80000000U + (u32)(value - begin);
}

u32 __OSHostEncodePointerWord(const void *pointer) {
    return __OSHostEncodeAddress(pointer);
}

void *__OSHostDecodeAddress(u32 address) {
    u32 offset;

    if (address == STUB_ADDRESS_TOKEN && token_in_use) {
        return &native_pointer_bytes[0];
    }

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
    if (token == STUB_ADDRESS_TOKEN) {
        token_release_count++;
        token_in_use = FALSE;
    }
}

void OSInit(void) {
    ++os_init_count;
    if (!memory_initialized) {
        (void)__OSHostMemoryInit(OS_HOST_MEMORY_PROFILE_GAMECUBE);
    }
}

OSTime OSGetTime(void) {
    host_time_ticks++;
    return (OSTime)host_time_ticks;
}

unsigned int PorpoiseStubOSInitCount(void) {
    return os_init_count;
}

void *PorpoiseStubNativePointer(void) {
    return &native_pointer_bytes[0];
}

uint32_t PorpoiseStubTokenAddress(void) {
    return STUB_ADDRESS_TOKEN;
}

unsigned int PorpoiseStubTokenReleaseCount(void) {
    return token_release_count;
}

unsigned int PorpoiseStubBootstrapCount(void) {
    return bootstrap_count;
}

int PorpoiseStubTitleSentinelsValid(void) {
    static const u8 expected_stack[4] = {0x81U, 0x7FU, 0xF0U, 0x00U};
    static const u8 expected_toc[4] = {0x12U, 0x34U, 0x56U, 0x78U};
    static const u8 expected_sda[4] = {0x89U, 0xABU, 0xCDU, 0xEFU};

    return memcmp(&stub_memory[0x17FEFF0], expected_stack, 4U) == 0 &&
           memcmp(&stub_memory[0x1000], expected_toc, 4U) == 0 &&
           memcmp(&stub_memory[0x2000], expected_sda, 4U) == 0;
}

int PorpoiseHostPrepareTitleEntryV1(
    uint32_t entry_address,
    uint32_t gpr_out[32]) {
    size_t index;

    if (entry_address == 0U || gpr_out == NULL || !memory_initialized) {
        return 1;
    }
    for (index = 0U; index < 32U; index++) {
        gpr_out[index] = 0U;
    }
    gpr_out[1] = UINT32_C(0x817FF000);
    gpr_out[2] = UINT32_C(0x80001000);
    gpr_out[13] = UINT32_C(0x80002000);
    bootstrap_count++;
    return 0;
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
