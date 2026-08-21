#ifndef TEST_LIBPORPOISE_DOLPHIN_AR_H
#define TEST_LIBPORPOISE_DOLPHIN_AR_H

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ARDMAResult {
    AR_DMA_RESULT_SUCCESS = 0,
    AR_DMA_RESULT_NOT_STARTED = -1,
    AR_DMA_RESULT_BUSY = -2,
    AR_DMA_RESULT_INVALID_DIRECTION = -3,
    AR_DMA_RESULT_INVALID_ALIGNMENT = -4,
    AR_DMA_RESULT_INVALID_ARAM_RANGE = -5,
    AR_DMA_RESULT_INVALID_MAIN_MEMORY_RANGE = -6
} ARDMAResult;

#define ARAM_DIR_MRAM_TO_ARAM 0U
#define ARAM_DIR_ARAM_TO_MRAM 1U
#define ARQ_TYPE_MRAM_TO_ARAM ARAM_DIR_MRAM_TO_ARAM
#define ARQ_TYPE_ARAM_TO_MRAM ARAM_DIR_ARAM_TO_MRAM
#define ARQ_PRIORITY_LOW 0U
#define ARQ_PRIORITY_HIGH 1U

ARDMAResult ARStartDMAEx(
    u32 type,
    u32 mainmem_addr,
    u32 aram_addr,
    u32 length);

u32 ARInit(u32 *stack_index_addr, u32 num_entries);
BOOL ARCheckInit(void);
u32 ARAlloc(u32 length);
u32 ARFree(u32 *length);
void ARReset(void);
u32 ARGetBaseAddress(void);
u32 ARGetSize(void);

#ifdef __cplusplus
}
#endif

#endif
