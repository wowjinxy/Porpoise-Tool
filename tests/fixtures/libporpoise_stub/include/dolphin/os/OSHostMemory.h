#ifndef TEST_LIBPORPOISE_OS_HOST_MEMORY_H
#define TEST_LIBPORPOISE_OS_HOST_MEMORY_H

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum OSHostMemoryProfile {
    OS_HOST_MEMORY_PROFILE_GAMECUBE = 0,
    OS_HOST_MEMORY_PROFILE_GAMECUBE_EXTENDED = 1
} OSHostMemoryProfile;

typedef struct OSHostMemoryLayout {
    OSHostMemoryProfile profile;
    void *cachedBase;
    void *uncachedBase;
    u32 size;
    u32 consoleSize;
    void *arenaLo;
    void *arenaHi;
    void *consoleArenaHi;
} OSHostMemoryLayout;

const OSHostMemoryLayout *__OSHostMemoryInit(OSHostMemoryProfile profile);
const OSHostMemoryLayout *__OSHostMemoryGetLayout(void);
BOOL __OSHostMemoryContainsAddress(const void *address);
void *__OSHostMemoryResolveArenaHi(void *previous, void *requested);

#ifdef __cplusplus
}
#endif

#endif
