#ifndef TEST_LIBPORPOISE_DOLPHIN_OS_INTERRUPT_H
#define TEST_LIBPORPOISE_DOLPHIN_OS_INTERRUPT_H

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL OSDisableInterrupts(void);
BOOL OSRestoreInterrupts(BOOL enabled);

#ifdef __cplusplus
}
#endif

#endif
