#ifndef PORPOISE_TITLE_HOST_H
#define PORPOISE_TITLE_HOST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORPOISE_TITLE_HOST_GPR_COUNT 32U

typedef enum PorpoiseTitleHostResultV1 {
    PORPOISE_TITLE_HOST_OK = 0,
    PORPOISE_TITLE_HOST_UNAVAILABLE = 1,
    PORPOISE_TITLE_HOST_INVALID_STATE = 2
} PorpoiseTitleHostResultV1;

/*
 * Supply the complete initial GPR image after libPorpoise has initialized its
 * console memory. The caller zeroes gpr_out first. In particular, r1 must be
 * an aligned guest stack address and r2/r13 must be the title's TOC/SDA bases.
 */
int PorpoiseHostPrepareTitleEntryV1(
    uint32_t entry_address,
    uint32_t gpr_out[PORPOISE_TITLE_HOST_GPR_COUNT]);

#ifdef __cplusplus
}
#endif

#endif
