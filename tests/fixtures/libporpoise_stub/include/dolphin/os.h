#ifndef TEST_LIBPORPOISE_DOLPHIN_OS_H
#define TEST_LIBPORPOISE_DOLPHIN_OS_H

#include <dolphin/os/OSHostAddress.h>
#include <dolphin/os/OSHostMemory.h>
#include <dolphin/os/OSInterrupt.h>
#include <dolphin/os/OSTime.h>
#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void OSInit(void);
void OSReport(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif
