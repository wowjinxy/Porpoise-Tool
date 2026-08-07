#ifndef TEST_LIBPORPOISE_DOLPHIN_H
#define TEST_LIBPORPOISE_DOLPHIN_H

#if defined(LIBPORPOISE_PORT) && !defined(LIBPORPOISE_MAIN_HANDLED)
#define main DolphinMain
#endif

#include <dolphin/types.h>
#include <dolphin/os.h>

#endif
