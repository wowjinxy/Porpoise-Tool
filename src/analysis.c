#include "porpoise/analysis.h"
#include "porpoise/util.h"

#include <stdlib.h>
#include <string.h>

enum {
    PORPOISE_GENERATED_SYMBOL_CAPACITY = PORPOISE_NAME_CAPACITY + 32,
    PORPOISE_BUILTIN_ADAPTER_MAX_ARGUMENTS = 8
};

typedef struct PorpoiseBuiltinAbiValue {
    PorpoiseAbiType type;
    PorpoiseAbiRegisterClass register_class;
    unsigned int register_index;
} PorpoiseBuiltinAbiValue;

typedef struct PorpoiseBuiltinAdapterContract {
    const char *name;
    const char *native_callable;
    PorpoiseBuiltinAbiValue result;
    size_t argument_count;
    PorpoiseBuiltinAbiValue
        arguments[PORPOISE_BUILTIN_ADAPTER_MAX_ARGUMENTS];
} PorpoiseBuiltinAdapterContract;

#define PORPOISE_BUILTIN_VOID \
    { PORPOISE_ABI_VOID, PORPOISE_ABI_REGISTER_NONE, 0U }
#define PORPOISE_BUILTIN_GPR(value_type, index) \
    { (value_type), PORPOISE_ABI_REGISTER_GPR, (index) }
#define PORPOISE_BUILTIN_FPR(value_type, index) \
    { (value_type), PORPOISE_ABI_REGISTER_FPR, (index) }
#define PORPOISE_BUILTIN_ADAPTER_HEADER \
    "porpoise_libporpoise_builtins_private.h"

static const PorpoiseBuiltinAdapterContract builtin_adapter_contracts[] = {
    {
        "porpoise_libporpoise_ai_init_adapter",
        "AIInit",
        PORPOISE_BUILTIN_VOID,
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_ar_alloc_adapter",
        "ARAlloc",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U)}
    },
    {
        "porpoise_libporpoise_ar_free_adapter",
        "ARFree",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_ar_get_size_adapter",
        "ARGetSize",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
        0U,
        {PORPOISE_BUILTIN_VOID}
    },
    {
        "porpoise_libporpoise_ar_init_adapter",
        "ARInit",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U)
        }
    },
    {
        "porpoise_libporpoise_ar_reset_adapter",
        "ARReset",
        PORPOISE_BUILTIN_VOID,
        0U,
        {PORPOISE_BUILTIN_VOID}
    },
    {
        "porpoise_libporpoise_arq_post_request_adapter",
        "ARQPostRequest",
        PORPOISE_BUILTIN_VOID,
        8U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 5U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 6U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 7U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 8U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 9U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 10U)
        }
    },
    {
        "porpoise_libporpoise_card_probe_ex_adapter",
        "CARDProbeEx",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 3U),
        3U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 5U)
        }
    },
    {
        "porpoise_libporpoise_dsp_add_task_adapter",
        "DSPAddTask",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_gx_init_adapter",
        "GXInit",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_set_draw_done_callback_adapter",
        "GXSetDrawDoneCallback",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_gx_set_copy_filter_adapter",
        "GXSetCopyFilter",
        PORPOISE_BUILTIN_VOID,
        4U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U8, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U8, 5U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 6U)
        }
    },
    {
        "porpoise_libporpoise_gx_set_copy_clear_adapter",
        "GXSetCopyClear",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_set_disp_copy_dst_adapter",
        "GXSetDispCopyDst",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U16, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U16, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_set_tex_copy_dst_adapter",
        "GXSetTexCopyDst",
        PORPOISE_BUILTIN_VOID,
        4U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U16, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U16, 4U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 5U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U8, 6U)
        }
    },
    {
        "porpoise_libporpoise_gx_copy_disp_adapter",
        "GXCopyDisp",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U8, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_copy_tex_adapter",
        "GXCopyTex",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U8, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_load_light_obj_imm_adapter",
        "GXLoadLightObjImm",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_set_array_adapter",
        "GXSetArray",
        PORPOISE_BUILTIN_VOID,
        3U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U8, 5U)
        }
    },
    {
        "porpoise_libporpoise_gx_load_tex_obj_adapter",
        "GXLoadTexObj",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_load_tlut_adapter",
        "GXLoadTlut",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_set_chan_amb_color_adapter",
        "GXSetChanAmbColor",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_set_chan_mat_color_adapter",
        "GXSetChanMatColor",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_call_display_list_adapter",
        "GXCallDisplayList",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_set_projection_adapter",
        "GXSetProjection",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_get_projectionv_adapter",
        "GXGetProjectionv",
        PORPOISE_BUILTIN_VOID,
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_gx_load_pos_mtx_imm_adapter",
        "GXLoadPosMtxImm",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_load_nrm_mtx_imm_adapter",
        "GXLoadNrmMtxImm",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_load_tex_mtx_imm_adapter",
        "GXLoadTexMtxImm",
        PORPOISE_BUILTIN_VOID,
        3U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 5U)
        }
    },
    {
        "porpoise_libporpoise_gx_get_viewportv_adapter",
        "GXGetViewportv",
        PORPOISE_BUILTIN_VOID,
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_gx_set_ind_tex_mtx_adapter",
        "GXSetIndTexMtx",
        PORPOISE_BUILTIN_VOID,
        3U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S8, 5U)
        }
    },
    {
        "porpoise_libporpoise_gx_set_tev_color_adapter",
        "GXSetTevColor",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_set_tev_color_s10_adapter",
        "GXSetTevColorS10",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_set_tev_kcolor_adapter",
        "GXSetTevKColor",
        PORPOISE_BUILTIN_VOID,
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_set_fog_adapter",
        "GXSetFog",
        PORPOISE_BUILTIN_VOID,
        6U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
            PORPOISE_BUILTIN_FPR(PORPOISE_ABI_F32, 1U),
            PORPOISE_BUILTIN_FPR(PORPOISE_ABI_F32, 2U),
            PORPOISE_BUILTIN_FPR(PORPOISE_ABI_F32, 3U),
            PORPOISE_BUILTIN_FPR(PORPOISE_ABI_F32, 4U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U)
        }
    },
    {
        "porpoise_libporpoise_gx_set_fog_range_adj_adapter",
        "GXSetFogRangeAdj",
        PORPOISE_BUILTIN_VOID,
        3U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U8, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U16, 4U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 5U)
        }
    },
    {
        "porpoise_libporpoise_gx_set_tev_indirect_adapter",
        "GXSetTevIndirect",
        PORPOISE_BUILTIN_VOID,
        8U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 5U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 6U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 7U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 8U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 9U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U8, 10U)
        }
    },
    {
        "porpoise_libporpoise_dvd_cancel_adapter",
        "DVDCancel",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 3U),
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_dvd_close_adapter",
        "DVDClose",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 3U),
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_dvd_convert_path_to_entry_adapter",
        "DVDConvertPathToEntrynum",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 3U),
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_dvd_fast_open_adapter",
        "DVDFastOpen",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 3U),
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U)
        }
    },
    {
        "porpoise_libporpoise_dvd_get_command_block_status_adapter",
        "DVDGetCommandBlockStatus",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 3U),
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_dvd_open_adapter",
        "DVDOpen",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 3U),
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U)
        }
    },
    {
        "porpoise_libporpoise_dvd_read_prio_adapter",
        "DVDReadPrio",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 3U),
        5U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 5U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 6U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 7U)
        }
    },
    {
        "porpoise_libporpoise_os_alloc_from_arena_hi_adapter",
        "OSAllocFromArenaHi",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U)
        }
    },
    {
        "porpoise_libporpoise_os_alloc_from_arena_lo_adapter",
        "OSAllocFromArenaLo",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
        2U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_U32, 4U)
        }
    },
    {
        "porpoise_libporpoise_os_exit_thread_adapter",
        "OSExitThread",
        PORPOISE_BUILTIN_VOID,
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_os_get_arena_hi_adapter",
        "OSGetArenaHi",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
        0U,
        {PORPOISE_BUILTIN_VOID}
    },
    {
        "porpoise_libporpoise_os_get_arena_lo_adapter",
        "OSGetArenaLo",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
        0U,
        {PORPOISE_BUILTIN_VOID}
    },
    {
        "porpoise_libporpoise_os_get_current_thread_adapter",
        "OSGetCurrentThread",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
        0U,
        {PORPOISE_BUILTIN_VOID}
    },
    {
        "porpoise_libporpoise_os_init_message_queue_adapter",
        "OSInitMessageQueue",
        PORPOISE_BUILTIN_VOID,
        3U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 5U)
        }
    },
    {
        "porpoise_libporpoise_os_receive_message_adapter",
        "OSReceiveMessage",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 3U),
        3U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 5U)
        }
    },
    {
        "porpoise_libporpoise_os_report_adapter",
        "OSReport",
        PORPOISE_BUILTIN_VOID,
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_os_resume_thread_adapter",
        "OSResumeThread",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 3U),
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_os_send_message_adapter",
        "OSSendMessage",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 3U),
        3U,
        {
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 4U),
            PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 5U)
        }
    },
    {
        "porpoise_libporpoise_os_set_arena_hi_adapter",
        "OSSetArenaHi",
        PORPOISE_BUILTIN_VOID,
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_os_set_arena_lo_adapter",
        "OSSetArenaLo",
        PORPOISE_BUILTIN_VOID,
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_os_sleep_thread_adapter",
        "OSSleepThread",
        PORPOISE_BUILTIN_VOID,
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_os_suspend_thread_adapter",
        "OSSuspendThread",
        PORPOISE_BUILTIN_GPR(PORPOISE_ABI_S32, 3U),
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_os_wakeup_thread_adapter",
        "OSWakeupThread",
        PORPOISE_BUILTIN_VOID,
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_vi_configure_adapter",
        "VIConfigure",
        PORPOISE_BUILTIN_VOID,
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    },
    {
        "porpoise_libporpoise_vi_set_next_frame_buffer_adapter",
        "VISetNextFrameBuffer",
        PORPOISE_BUILTIN_VOID,
        1U,
        {PORPOISE_BUILTIN_GPR(PORPOISE_ABI_POINTER, 3U)}
    }
};

#undef PORPOISE_BUILTIN_GPR
#undef PORPOISE_BUILTIN_VOID

static const char *abi_callable_name(const PorpoiseAbiFunction *function) {
    if (function->kind == PORPOISE_ABI_IMPORT && function->adapter != NULL) {
        return function->adapter;
    }
    return function->wrapper;
}

static const char *abi_callable_role(const PorpoiseAbiFunction *function) {
    if (function->kind == PORPOISE_ABI_EXPORT) {
        return "export wrapper";
    }
    return function->adapter != NULL ? "import adapter" : "import wrapper";
}

static bool import_bridge_name(const PorpoiseAbiFunction *function,
                               char *name,
                               size_t capacity) {
    char sanitized[PORPOISE_NAME_CAPACITY];

    porpoise_sanitize_identifier(
        function->symbol,
        sanitized,
        sizeof(sanitized));
    return porpoise_format(
        name,
        capacity,
        "porpoise_import_%s",
        sanitized);
}

static bool lifted_function_name(const PorpoiseFunction *function,
                                 char *name,
                                 size_t capacity) {
    return porpoise_format(
        name,
        capacity,
        "porpoise_lifted_%s",
        function->c_name);
}

static bool abi_callable_name_is_reserved(const char *name) {
    static const char *const reserved_names[] = {
        /* Entry points and public typedef names that do not use our prefixes. */
        "main",
        "DolphinMain",
        "PorpoisePpcState",
        "PorpoiseHostAdapter",
        "PorpoiseFpr",
        "PorpoiseHostResult",
        "PorpoiseFault",
        "PorpoiseExecutionStatus",
        "PorpoiseFpFmaOperation",
        "PorpoiseFpPrecision",
        "PorpoiseTitleHostResultV3",
        "PorpoiseTitleInitialWordV3",
        "PorpoiseTitleRuntimeConfigV1",
        "PorpoiseTitleEntryStateV3",
        "PorpoiseHostPrepareRuntimeV1",
        "PorpoiseHostPrepareTitleEntryV3",
        "PorpoiseHostReadBytesFn",
        "PorpoiseHostWriteBytesFn",
        "PorpoiseHostDecodePointerFn",
        "PorpoiseHostEncodePointerFn",
        "PorpoiseHostReadTimeBaseFn",
        "PorpoiseHostTrapFn",
        "PorpoiseHostSystemCallFn",
        "PorpoiseHostCallGuestFn",
        "PorpoiseHostPollEventsFn",
        "PorpoiseLiftedFunction"
    };
    size_t index;

    /* Own these prefixes so future public helpers/constants cannot drift. */
    if (strncmp(name, "porpoise_", 9U) == 0 ||
        strncmp(name, "PORPOISE_", 9U) == 0) {
        return true;
    }

    for (index = 0U;
         index < sizeof(reserved_names) / sizeof(reserved_names[0]);
         index++) {
        if (strcmp(name, reserved_names[index]) == 0) {
            return true;
        }
    }
    return false;
}

static const PorpoiseBuiltinAdapterContract *builtin_adapter_contract(
    const char *name) {
    size_t index;

    if (name == NULL) return NULL;
    for (index = 0U;
         index < sizeof(builtin_adapter_contracts) /
                     sizeof(builtin_adapter_contracts[0]);
         index++) {
        if (strcmp(name, builtin_adapter_contracts[index].name) == 0) {
            return &builtin_adapter_contracts[index];
        }
    }
    return NULL;
}

static const PorpoiseBuiltinAdapterContract *builtin_native_callable_contract(
    const char *name) {
    size_t index;

    if (name == NULL) return NULL;
    for (index = 0U;
         index < sizeof(builtin_adapter_contracts) /
                     sizeof(builtin_adapter_contracts[0]);
         index++) {
        if (strcmp(
                name,
                builtin_adapter_contracts[index].native_callable) == 0) {
            return &builtin_adapter_contracts[index];
        }
    }
    return NULL;
}

static bool abi_value_matches_builtin(
    const PorpoiseAbiValue *value,
    const PorpoiseBuiltinAbiValue *expected) {
    return value->type == expected->type &&
           value->register_class == expected->register_class &&
           value->register_index == expected->register_index;
}

static const char *abi_register_class_name(
    PorpoiseAbiRegisterClass register_class) {
    if (register_class == PORPOISE_ABI_REGISTER_GPR) return "r";
    if (register_class == PORPOISE_ABI_REGISTER_FPR) return "f";
    return "no register ";
}

static bool validate_builtin_runtime_adapter(
    const PorpoiseAbiFunction *function,
    PorpoiseDiagnostics *diagnostics) {
    const PorpoiseBuiltinAdapterContract *contract =
        builtin_adapter_contract(function->adapter);
    bool valid = true;
    size_t argument_index;

    if (contract == NULL) return true;

    if (function->kind != PORPOISE_ABI_IMPORT) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "built-in adapter %s for %s must be an ABI import",
            contract->name,
            function->symbol);
        valid = false;
    }
    if (function->header == NULL ||
        strcmp(function->header, PORPOISE_BUILTIN_ADAPTER_HEADER) != 0) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "built-in adapter %s for %s must use header %s",
            contract->name,
            function->symbol,
            PORPOISE_BUILTIN_ADAPTER_HEADER);
        valid = false;
    }
    if (!abi_value_matches_builtin(&function->result, &contract->result)) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "built-in adapter %s for %s has return mapping %s %s%u; expected %s %s%u",
            contract->name,
            function->symbol,
            porpoise_abi_type_name(function->result.type),
            abi_register_class_name(function->result.register_class),
            function->result.register_index,
            porpoise_abi_type_name(contract->result.type),
            abi_register_class_name(contract->result.register_class),
            contract->result.register_index);
        valid = false;
    }
    if (function->argument_count != contract->argument_count) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "built-in adapter %s for %s has %lu ABI arguments; expected %lu",
            contract->name,
            function->symbol,
            (unsigned long)function->argument_count,
            (unsigned long)contract->argument_count);
        valid = false;
    }

    for (argument_index = 0U;
         argument_index < function->argument_count &&
         argument_index < contract->argument_count;
         argument_index++) {
        const PorpoiseAbiValue *argument =
            &function->arguments[argument_index];
        const PorpoiseBuiltinAbiValue *expected =
            &contract->arguments[argument_index];

        if (abi_value_matches_builtin(argument, expected)) continue;
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "built-in adapter %s for %s has argument %lu mapping %s %s%u; expected %s %s%u",
            contract->name,
            function->symbol,
            (unsigned long)(argument_index + 1U),
            porpoise_abi_type_name(argument->type),
            abi_register_class_name(argument->register_class),
            argument->register_index,
            porpoise_abi_type_name(expected->type),
            abi_register_class_name(expected->register_class),
            expected->register_index);
        valid = false;
    }
    return valid;
}

static bool validate_builtin_runtime_containment(
    const PorpoiseAbiFunction *function,
    PorpoiseDiagnostics *diagnostics) {
    const PorpoiseBuiltinAdapterContract *contract;
    const char *callable;
    const char *callable_role;

    if (function->kind != PORPOISE_ABI_IMPORT) return true;

    contract = builtin_native_callable_contract(function->symbol);
    if (contract != NULL) {
        if (function->adapter != NULL &&
            strcmp(function->adapter, contract->name) == 0) {
            return true;
        }
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "ABI import %s must use built-in adapter %s; typed wrappers and other adapters bypass required guest ABI marshalling",
            function->symbol,
            contract->name);
        return false;
    }

    callable = function->adapter != NULL
                   ? function->adapter
                   : function->wrapper;
    contract = builtin_native_callable_contract(callable);
    if (contract == NULL) return true;

    callable_role = function->adapter != NULL ? "adapter" : "typed wrapper";
    porpoise_diagnostics_add(
        diagnostics,
        PORPOISE_SEVERITY_ERROR,
        NULL,
        0U,
        0U,
        "ABI import %s cannot use protected native callable %s as its %s; use built-in adapter %s",
        function->symbol,
        contract->native_callable,
        callable_role,
        contract->name);
    return false;
}

static bool abi_uses_builtin_runtime_adapter(
    const PorpoiseAbiFunction *function) {
    return function->kind == PORPOISE_ABI_IMPORT &&
           builtin_adapter_contract(function->adapter) != NULL;
}

static int validate_abi_namespace(const PorpoiseProgram *program,
                                  const PorpoiseAbiManifest *abi,
                                  PorpoiseDiagnostics *diagnostics) {
    size_t function_index;
    bool valid = true;

    for (function_index = 0U;
         function_index < abi->function_count;
         function_index++) {
        const PorpoiseAbiFunction *function =
            &abi->functions[function_index];
        const char *callable = abi_callable_name(function);
        const char *role = abi_callable_role(function);
        size_t previous;
        size_t generated_index;
        size_t file_index;

        if (!validate_builtin_runtime_adapter(function, diagnostics)) {
            valid = false;
        }
        if (!validate_builtin_runtime_containment(function, diagnostics)) {
            valid = false;
        }

        if (function->kind == PORPOISE_ABI_IMPORT) {
            char bridge[PORPOISE_GENERATED_SYMBOL_CAPACITY];
            const PorpoiseAddressAlias *declared_alias =
                porpoise_program_find_declared_alias(
                    program,
                    function->symbol,
                    NULL);

            if (porpoise_program_find_function(
                    program,
                    function->symbol) != NULL) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    NULL,
                    0U,
                    0U,
                    "ABI import %s conflicts with a translated function",
                    function->symbol);
                valid = false;
            }
            if (declared_alias != NULL &&
                !declared_alias->is_function_name) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    declared_alias->source_path,
                    declared_alias->source_line,
                    declared_alias->address,
                    "ABI import %s conflicts with an ordinary input address alias",
                    function->symbol);
                valid = false;
            }
            if (!import_bridge_name(function, bridge, sizeof(bridge))) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    NULL,
                    0U,
                    0U,
                    "cannot construct the generated bridge name for ABI import %s",
                    function->symbol);
                return PORPOISE_EXIT_INTERNAL;
            }
            for (previous = 0U; previous < function_index; previous++) {
                const PorpoiseAbiFunction *candidate =
                    &abi->functions[previous];
                char candidate_bridge[PORPOISE_GENERATED_SYMBOL_CAPACITY];

                if (candidate->kind != PORPOISE_ABI_IMPORT) {
                    continue;
                }
                if (!import_bridge_name(
                        candidate,
                        candidate_bridge,
                        sizeof(candidate_bridge))) {
                    porpoise_diagnostics_add(
                        diagnostics,
                        PORPOISE_SEVERITY_ERROR,
                        NULL,
                        0U,
                        0U,
                        "cannot construct the generated bridge name for ABI import %s",
                        candidate->symbol);
                    return PORPOISE_EXIT_INTERNAL;
                }
                if (strcmp(bridge, candidate_bridge) == 0) {
                    porpoise_diagnostics_add(
                        diagnostics,
                        PORPOISE_SEVERITY_ERROR,
                        NULL,
                        0U,
                        0U,
                        "ABI imports %s and %s collide as generated bridge %s",
                        candidate->symbol,
                        function->symbol,
                        bridge);
                    valid = false;
                }
            }
        }

        if (callable == NULL) {
            porpoise_diagnostics_add(
                diagnostics,
                PORPOISE_SEVERITY_ERROR,
                NULL,
                0U,
                0U,
                "ABI %s for %s has no callable C identifier",
                role,
                function->symbol);
            valid = false;
            continue;
        }

        if (abi_callable_name_is_reserved(callable) &&
            !abi_uses_builtin_runtime_adapter(function)) {
            porpoise_diagnostics_add(
                diagnostics,
                PORPOISE_SEVERITY_ERROR,
                NULL,
                0U,
                0U,
                "ABI %s %s for %s is reserved by the generated project or runtime",
                role,
                callable,
                function->symbol);
            valid = false;
        }

        for (previous = 0U; previous < function_index; previous++) {
            const PorpoiseAbiFunction *candidate = &abi->functions[previous];
            const char *candidate_callable = abi_callable_name(candidate);

            if (candidate_callable != NULL &&
                strcmp(callable, candidate_callable) == 0) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    NULL,
                    0U,
                    0U,
                    "ABI %s for %s and ABI %s for %s both use C identifier %s",
                    abi_callable_role(candidate),
                    candidate->symbol,
                    role,
                    function->symbol,
                    callable);
                valid = false;
            }
        }

        for (generated_index = 0U;
             generated_index < abi->function_count;
             generated_index++) {
            const PorpoiseAbiFunction *generated =
                &abi->functions[generated_index];
            char bridge[PORPOISE_GENERATED_SYMBOL_CAPACITY];

            if (generated->kind != PORPOISE_ABI_IMPORT) {
                continue;
            }
            if (!import_bridge_name(generated, bridge, sizeof(bridge))) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    NULL,
                    0U,
                    0U,
                    "cannot construct the generated bridge name for ABI import %s",
                    generated->symbol);
                return PORPOISE_EXIT_INTERNAL;
            }
            if (strcmp(callable, bridge) == 0) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    NULL,
                    0U,
                    0U,
                    "ABI %s %s for %s collides with generated import bridge %s",
                    role,
                    callable,
                    function->symbol,
                    bridge);
                valid = false;
            }
        }

        for (file_index = 0U;
             file_index < program->file_count;
             file_index++) {
            const PorpoiseSourceFile *file = &program->files[file_index];
            size_t lifted_index;

            for (lifted_index = 0U;
                 lifted_index < file->function_count;
                 lifted_index++) {
                const PorpoiseFunction *lifted =
                    &file->functions[lifted_index];
                char lifted_name[PORPOISE_GENERATED_SYMBOL_CAPACITY];

                if (lifted->skipped) {
                    continue;
                }
                if (!lifted_function_name(
                        lifted,
                        lifted_name,
                        sizeof(lifted_name))) {
                    porpoise_diagnostics_add(
                        diagnostics,
                        PORPOISE_SEVERITY_ERROR,
                        file->relative_path,
                        0U,
                        lifted->start_address,
                        "cannot construct the generated name for lifted function %s",
                        lifted->name);
                    return PORPOISE_EXIT_INTERNAL;
                }
                if (strcmp(callable, lifted_name) == 0) {
                    porpoise_diagnostics_add(
                        diagnostics,
                        PORPOISE_SEVERITY_ERROR,
                        file->relative_path,
                        0U,
                        lifted->start_address,
                        "ABI %s %s for %s collides with lifted function %s",
                        role,
                        callable,
                        function->symbol,
                        lifted_name);
                    valid = false;
                }
            }
        }
    }

    return valid ? PORPOISE_EXIT_OK : PORPOISE_EXIT_USAGE;
}

static bool function_has_global_input_name(
    const PorpoiseFunction *function,
    const char *name) {
    size_t alias_index;
    if (function->is_global && strcmp(function->name, name) == 0) return true;
    for (alias_index = 0U;
         alias_index < function->alias_count;
         alias_index++) {
        const PorpoiseAddressAlias *alias = &function->aliases[alias_index];
        if (alias->is_function_name && alias->is_global &&
            strcmp(alias->name, name) == 0) {
            return true;
        }
    }
    return false;
}

void porpoise_analysis_init(PorpoiseAnalysis *analysis) {
    if (analysis != NULL) memset(analysis, 0, sizeof(*analysis));
}

void porpoise_analysis_free(PorpoiseAnalysis *analysis) {
    if (analysis == NULL) return;
    free(analysis->import_bindings);
    memset(analysis, 0, sizeof(*analysis));
}

static int build_import_bindings(
    const PorpoiseProgram *program,
    const PorpoiseAbiManifest *abi,
    PorpoiseAnalysis *analysis,
    PorpoiseDiagnostics *diagnostics) {
    size_t function_index;
    size_t binding_count = 0U;
    size_t binding_index = 0U;
    bool valid = true;

    for (function_index = 0U;
         function_index < abi->function_count;
         function_index++) {
        const PorpoiseAbiFunction *function = &abi->functions[function_index];
        const PorpoiseFunction *owner = NULL;

        if (function->kind != PORPOISE_ABI_IMPORT) continue;
        if (!porpoise_program_resolve_declared_function(
                program, function->symbol, &owner, NULL, NULL)) {
            continue;
        }
        if (owner == NULL || !owner->skipped) {
            porpoise_diagnostics_add(
                diagnostics,
                PORPOISE_SEVERITY_ERROR,
                NULL,
                0U,
                owner == NULL ? 0U : owner->start_address,
                "ABI import %s conflicts with a translated function",
                function->symbol);
            valid = false;
            continue;
        }
        binding_count++;
    }

    if (!valid) return PORPOISE_EXIT_USAGE;
    if (binding_count == 0U) return PORPOISE_EXIT_OK;
    if (binding_count > SIZE_MAX / sizeof(*analysis->import_bindings)) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "too many skipped ABI import bindings");
        return PORPOISE_EXIT_INTERNAL;
    }
    analysis->import_bindings = (PorpoiseImportBinding *)calloc(
        binding_count, sizeof(*analysis->import_bindings));
    if (analysis->import_bindings == NULL) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "out of memory while binding skipped ABI imports");
        return PORPOISE_EXIT_INTERNAL;
    }

    for (function_index = 0U;
         function_index < abi->function_count;
         function_index++) {
        const PorpoiseAbiFunction *function = &abi->functions[function_index];
        const PorpoiseFunction *owner = NULL;
        const PorpoiseAddressAlias *alias = NULL;
        uint32_t guest_address = 0U;
        size_t previous;

        if (function->kind != PORPOISE_ABI_IMPORT) continue;
        if (!porpoise_program_resolve_declared_function(
                program,
                function->symbol,
                &owner,
                &alias,
                &guest_address) ||
            owner == NULL || !owner->skipped) {
            continue;
        }

        for (previous = 0U; previous < binding_index; previous++) {
            const PorpoiseImportBinding *candidate =
                &analysis->import_bindings[previous];
            if (candidate->guest_address != guest_address) continue;
            porpoise_diagnostics_add(
                diagnostics,
                PORPOISE_SEVERITY_ERROR,
                NULL,
                0U,
                guest_address,
                "ABI imports %s and %s bind to the same skipped guest address",
                candidate->import->symbol,
                function->symbol);
            valid = false;
            break;
        }
        if (!valid) continue;

        analysis->import_bindings[binding_index].import = function;
        analysis->import_bindings[binding_index].owner = owner;
        analysis->import_bindings[binding_index].alias = alias;
        analysis->import_bindings[binding_index].guest_address = guest_address;
        binding_index++;
    }

    if (!valid) {
        free(analysis->import_bindings);
        analysis->import_bindings = NULL;
        return PORPOISE_EXIT_USAGE;
    }
    analysis->import_binding_count = binding_index;
    return PORPOISE_EXIT_OK;
}

int porpoise_analyze_program(
    const PorpoiseProgram *program,
    const PorpoiseAbiManifest *abi,
    const char *requested_entry,
    PorpoiseAnalysis *analysis,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseAnalysis candidate;
    size_t file_index;
    size_t function_index;
    int namespace_result;
    int binding_result;
    bool valid = true;
    if (program == NULL || abi == NULL || analysis == NULL || diagnostics == NULL)
        return PORPOISE_EXIT_INTERNAL;
    porpoise_analysis_init(&candidate);
    namespace_result = validate_abi_namespace(program, abi, diagnostics);
    if (namespace_result != PORPOISE_EXIT_OK) {
        return namespace_result;
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        for (function_index = 0U; function_index < file->function_count; function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            if (function->skipped) continue;
            candidate.translated_function_count++;
        }
    }
    if (candidate.translated_function_count == 0U) {
        porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, NULL, 0U, 0U,
                                 "no functions remain to translate");
        valid = false;
    }
    for (function_index = 0U; function_index < abi->function_count; function_index++) {
        const PorpoiseAbiFunction *function = &abi->functions[function_index];
        if (function->kind == PORPOISE_ABI_EXPORT &&
            porpoise_program_find_function(program, function->symbol) == NULL) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, NULL, 0U, 0U,
                                     "ABI export %s has no translated function", function->symbol);
            valid = false;
        }
    }
    if (!valid) return PORPOISE_EXIT_TRANSLATION;
    if (requested_entry != NULL && requested_entry[0] != '\0') {
        if (strcmp(requested_entry, "__start") == 0) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, requested_entry, 0U, 0U,
                                     "console __start cannot be used as the host entry; select a title function");
            return PORPOISE_EXIT_USAGE;
        }
        candidate.entry = porpoise_program_find_function(program, requested_entry);
        if (candidate.entry == NULL) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, requested_entry, 0U, 0U,
                                     "entry symbol is not a translated input function");
            return PORPOISE_EXIT_USAGE;
        }
    } else {
        size_t main_count = 0U;
        for (file_index = 0U; file_index < program->file_count; file_index++) {
            const PorpoiseSourceFile *file = &program->files[file_index];
            for (function_index = 0U; function_index < file->function_count; function_index++) {
                const PorpoiseFunction *function = &file->functions[function_index];
                if (!function->skipped &&
                    function_has_global_input_name(function, "main")) {
                    candidate.entry = function;
                    main_count++;
                }
            }
        }
        if (main_count != 1U) candidate.entry = NULL;
    }

    binding_result = build_import_bindings(
        program, abi, &candidate, diagnostics);
    if (binding_result != PORPOISE_EXIT_OK) {
        porpoise_analysis_free(&candidate);
        return binding_result;
    }

    porpoise_analysis_free(analysis);
    *analysis = candidate;
    return PORPOISE_EXIT_OK;
}
