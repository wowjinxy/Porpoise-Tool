#ifndef TEST_LIBPORPOISE_DOLPHIN_CARD_H
#define TEST_LIBPORPOISE_DOLPHIN_CARD_H

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CARD_RESULT_READY 0
#define CARD_RESULT_FATAL_ERROR (-128)
#define CARD_RESULT_NOCARD (-3)

s32 CARDProbeEx(s32 channel, s32 *mem_size, s32 *sector_size);

#ifdef __cplusplus
}
#endif

#endif
