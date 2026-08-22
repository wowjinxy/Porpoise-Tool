#ifndef TEST_LIBPORPOISE_DOLPHIN_PAD_H
#define TEST_LIBPORPOISE_DOLPHIN_PAD_H

#include <dolphin/types.h>
#include <porpoise/sdk_import_contract.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PADStatus {
    u16 button;
    s8 stickX;
    s8 stickY;
    s8 substickX;
    s8 substickY;
    u8 triggerLeft;
    u8 triggerRight;
    u8 analogA;
    u8 analogB;
    s8 err;
} PADStatus;

#define PAD_MAX_CONTROLLERS 4

u32 PADRead(PADStatus *status);

#ifdef __cplusplus
}
#endif

#endif
