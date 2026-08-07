#ifndef PORPOISE_LIBPORPOISE_ADAPTER_H
#define PORPOISE_LIBPORPOISE_ADAPTER_H

#include "porpoise_lifted.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize libPorpoise once and populate adapter with the callbacks used by
 * lifted code. This function is intended to run on the host startup thread,
 * before any lifted function is called.
 */
PorpoiseHostResult porpoise_libporpoise_adapter_init(
    PorpoiseHostAdapter *adapter);

#ifdef __cplusplus
}
#endif

#endif
