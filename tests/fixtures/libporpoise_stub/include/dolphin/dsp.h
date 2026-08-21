#ifndef TEST_LIBPORPOISE_DOLPHIN_DSP_H
#define TEST_LIBPORPOISE_DOLPHIN_DSP_H

#include <dolphin/os/OSTime.h>
#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef volatile u32 vu32;
typedef struct STRUCT_DSP_TASK DSPTaskInfo;
typedef void (*DSPCallback)(void *task);

/* Native host layout: pointer fields intentionally use host pointer width. */
struct STRUCT_DSP_TASK {
    vu32 state;
    vu32 priority;
    vu32 flags;
    u16 *iram_mmem_addr;
    u32 iram_length;
    u32 iram_addr;
    u16 *dram_mmem_addr;
    u32 dram_length;
    u32 dram_addr;
    u16 dsp_init_vector;
    u16 dsp_resume_vector;
    DSPCallback init_cb;
    DSPCallback res_cb;
    DSPCallback done_cb;
    DSPCallback req_cb;
    DSPTaskInfo *next;
    DSPTaskInfo *prev;
    OSTime t_context;
    OSTime t_task;
};

#define DSP_TASK_FLAG_CLEARALL UINT32_C(0x00000000)
#define DSP_TASK_FLAG_ATTACHED UINT32_C(0x00000001)
#define DSP_TASK_FLAG_CANCEL UINT32_C(0x00000002)

#define DSP_TASK_STATE_INIT UINT32_C(0)
#define DSP_TASK_STATE_RUN UINT32_C(1)
#define DSP_TASK_STATE_YIELD UINT32_C(2)
#define DSP_TASK_STATE_DONE UINT32_C(3)

DSPTaskInfo *DSPAddTask(DSPTaskInfo *task);

#ifdef __cplusplus
}
#endif

#endif
