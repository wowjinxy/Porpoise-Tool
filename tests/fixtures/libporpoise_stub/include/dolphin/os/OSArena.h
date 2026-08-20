#ifndef TEST_LIBPORPOISE_DOLPHIN_OS_OSARENA_H
#define TEST_LIBPORPOISE_DOLPHIN_OS_OSARENA_H

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void *OSGetArenaHi(void);
void *OSGetArenaLo(void);
void OSSetArenaHi(void *address);
void OSSetArenaLo(void *address);
void *OSAllocFromArenaLo(u32 size, u32 alignment);
void *OSAllocFromArenaHi(u32 size, u32 alignment);

#ifdef __cplusplus
}
#endif

#endif
