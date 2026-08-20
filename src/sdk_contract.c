#include "porpoise/sdk_contract.h"

#include <string.h>

struct PorpoiseSdkContract {
    const char *host_callable;
    const char *canonical_name;
    PorpoiseSdkAbiValue result;
    size_t argument_count;
    PorpoiseSdkAbiValue arguments[PORPOISE_SDK_CONTRACT_MAX_ARGUMENTS];
};

#define SDK_VOID \
    { PORPOISE_ABI_VOID, PORPOISE_ABI_REGISTER_NONE, 0U }
#define SDK_GPR(value_type, index) \
    { (value_type), PORPOISE_ABI_REGISTER_GPR, (index) }
#define SDK_FPR(value_type, index) \
    { (value_type), PORPOISE_ABI_REGISTER_FPR, (index) }
#define SDK_CONTRACT(callable, canonical, result_value, count, ...) \
    { (callable), (canonical), result_value, (count), {__VA_ARGS__} }

static const PorpoiseSdkContract sdk_contracts[] = {
    SDK_CONTRACT(
        "porpoise_libporpoise_ai_init_adapter", "AIInit",
        SDK_VOID, 1U, SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_ar_alloc_adapter", "ARAlloc",
        SDK_GPR(PORPOISE_ABI_U32, 3U), 1U,
        SDK_GPR(PORPOISE_ABI_U32, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_ar_free_adapter", "ARFree",
        SDK_GPR(PORPOISE_ABI_U32, 3U), 1U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_ar_get_size_adapter", "ARGetSize",
        SDK_GPR(PORPOISE_ABI_U32, 3U), 0U, SDK_VOID),
    SDK_CONTRACT(
        "porpoise_libporpoise_ar_init_adapter", "ARInit",
        SDK_GPR(PORPOISE_ABI_U32, 3U), 2U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_ar_reset_adapter", "ARReset",
        SDK_VOID, 0U, SDK_VOID),
    SDK_CONTRACT(
        "porpoise_libporpoise_arq_post_request_adapter", "ARQPostRequest",
        SDK_VOID, 8U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U),
        SDK_GPR(PORPOISE_ABI_U32, 5U),
        SDK_GPR(PORPOISE_ABI_U32, 6U),
        SDK_GPR(PORPOISE_ABI_U32, 7U),
        SDK_GPR(PORPOISE_ABI_U32, 8U),
        SDK_GPR(PORPOISE_ABI_U32, 9U),
        SDK_GPR(PORPOISE_ABI_U32, 10U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_card_probe_ex_adapter", "CARDProbeEx",
        SDK_GPR(PORPOISE_ABI_S32, 3U), 3U,
        SDK_GPR(PORPOISE_ABI_S32, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U),
        SDK_GPR(PORPOISE_ABI_POINTER, 5U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_dsp_add_task_adapter", "DSPAddTask",
        SDK_GPR(PORPOISE_ABI_POINTER, 3U), 1U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_init_adapter", "GXInit",
        SDK_GPR(PORPOISE_ABI_POINTER, 3U), 2U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_draw_done_callback_adapter",
        "GXSetDrawDoneCallback", SDK_GPR(PORPOISE_ABI_POINTER, 3U), 1U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_copy_filter_adapter", "GXSetCopyFilter",
        SDK_VOID, 4U,
        SDK_GPR(PORPOISE_ABI_U8, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U),
        SDK_GPR(PORPOISE_ABI_U8, 5U),
        SDK_GPR(PORPOISE_ABI_POINTER, 6U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_copy_clear_adapter", "GXSetCopyClear",
        SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_disp_copy_dst_adapter",
        "GXSetDispCopyDst", SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_U16, 3U),
        SDK_GPR(PORPOISE_ABI_U16, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_tex_copy_dst_adapter", "GXSetTexCopyDst",
        SDK_VOID, 4U,
        SDK_GPR(PORPOISE_ABI_U16, 3U),
        SDK_GPR(PORPOISE_ABI_U16, 4U),
        SDK_GPR(PORPOISE_ABI_U32, 5U),
        SDK_GPR(PORPOISE_ABI_U8, 6U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_copy_disp_adapter", "GXCopyDisp",
        SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_U8, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_copy_tex_adapter", "GXCopyTex",
        SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_U8, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_load_light_obj_imm_adapter",
        "GXLoadLightObjImm", SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_array_adapter", "GXSetArray",
        SDK_VOID, 3U,
        SDK_GPR(PORPOISE_ABI_U32, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U),
        SDK_GPR(PORPOISE_ABI_U8, 5U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_load_tex_obj_adapter", "GXLoadTexObj",
        SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_load_tlut_adapter", "GXLoadTlut",
        SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_chan_amb_color_adapter",
        "GXSetChanAmbColor", SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_U32, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_chan_mat_color_adapter",
        "GXSetChanMatColor", SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_U32, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_call_display_list_adapter",
        "GXCallDisplayList", SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_projection_adapter", "GXSetProjection",
        SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_get_projectionv_adapter", "GXGetProjectionv",
        SDK_VOID, 1U, SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_load_pos_mtx_imm_adapter",
        "GXLoadPosMtxImm", SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_load_nrm_mtx_imm_adapter",
        "GXLoadNrmMtxImm", SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_load_tex_mtx_imm_adapter",
        "GXLoadTexMtxImm", SDK_VOID, 3U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U),
        SDK_GPR(PORPOISE_ABI_U32, 5U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_get_viewportv_adapter", "GXGetViewportv",
        SDK_VOID, 1U, SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_ind_tex_mtx_adapter", "GXSetIndTexMtx",
        SDK_VOID, 3U,
        SDK_GPR(PORPOISE_ABI_U32, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U),
        SDK_GPR(PORPOISE_ABI_S8, 5U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_tev_color_adapter", "GXSetTevColor",
        SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_U32, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_tev_color_s10_adapter",
        "GXSetTevColorS10", SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_U32, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_tev_kcolor_adapter", "GXSetTevKColor",
        SDK_VOID, 2U,
        SDK_GPR(PORPOISE_ABI_U32, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_fog_adapter", "GXSetFog",
        SDK_VOID, 6U,
        SDK_GPR(PORPOISE_ABI_U32, 3U),
        SDK_FPR(PORPOISE_ABI_F32, 1U),
        SDK_FPR(PORPOISE_ABI_F32, 2U),
        SDK_FPR(PORPOISE_ABI_F32, 3U),
        SDK_FPR(PORPOISE_ABI_F32, 4U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_fog_range_adj_adapter",
        "GXSetFogRangeAdj", SDK_VOID, 3U,
        SDK_GPR(PORPOISE_ABI_U8, 3U),
        SDK_GPR(PORPOISE_ABI_U16, 4U),
        SDK_GPR(PORPOISE_ABI_POINTER, 5U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_gx_set_tev_indirect_adapter", "GXSetTevIndirect",
        SDK_VOID, 8U,
        SDK_GPR(PORPOISE_ABI_U32, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U),
        SDK_GPR(PORPOISE_ABI_U32, 5U),
        SDK_GPR(PORPOISE_ABI_U32, 6U),
        SDK_GPR(PORPOISE_ABI_U32, 7U),
        SDK_GPR(PORPOISE_ABI_U32, 8U),
        SDK_GPR(PORPOISE_ABI_U32, 9U),
        SDK_GPR(PORPOISE_ABI_U8, 10U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_dvd_cancel_adapter", "DVDCancel",
        SDK_GPR(PORPOISE_ABI_S32, 3U), 1U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_dvd_close_adapter", "DVDClose",
        SDK_GPR(PORPOISE_ABI_S32, 3U), 1U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_dvd_convert_path_to_entry_adapter",
        "DVDConvertPathToEntrynum", SDK_GPR(PORPOISE_ABI_S32, 3U), 1U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_dvd_fast_open_adapter", "DVDFastOpen",
        SDK_GPR(PORPOISE_ABI_S32, 3U), 2U,
        SDK_GPR(PORPOISE_ABI_S32, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_dvd_get_command_block_status_adapter",
        "DVDGetCommandBlockStatus", SDK_GPR(PORPOISE_ABI_S32, 3U), 1U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_dvd_open_adapter", "DVDOpen",
        SDK_GPR(PORPOISE_ABI_S32, 3U), 2U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_dvd_read_prio_adapter", "DVDReadPrio",
        SDK_GPR(PORPOISE_ABI_S32, 3U), 5U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U),
        SDK_GPR(PORPOISE_ABI_S32, 5U),
        SDK_GPR(PORPOISE_ABI_S32, 6U),
        SDK_GPR(PORPOISE_ABI_S32, 7U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_alloc_from_arena_hi_adapter",
        "OSAllocFromArenaHi", SDK_GPR(PORPOISE_ABI_POINTER, 3U), 2U,
        SDK_GPR(PORPOISE_ABI_U32, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_alloc_from_arena_lo_adapter",
        "OSAllocFromArenaLo", SDK_GPR(PORPOISE_ABI_POINTER, 3U), 2U,
        SDK_GPR(PORPOISE_ABI_U32, 3U),
        SDK_GPR(PORPOISE_ABI_U32, 4U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_exit_thread_adapter", "OSExitThread",
        SDK_VOID, 1U, SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_get_arena_hi_adapter", "OSGetArenaHi",
        SDK_GPR(PORPOISE_ABI_POINTER, 3U), 0U, SDK_VOID),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_get_arena_lo_adapter", "OSGetArenaLo",
        SDK_GPR(PORPOISE_ABI_POINTER, 3U), 0U, SDK_VOID),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_get_current_thread_adapter",
        "OSGetCurrentThread", SDK_GPR(PORPOISE_ABI_POINTER, 3U), 0U,
        SDK_VOID),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_init_message_queue_adapter",
        "OSInitMessageQueue", SDK_VOID, 3U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U),
        SDK_GPR(PORPOISE_ABI_S32, 5U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_receive_message_adapter",
        "OSReceiveMessage", SDK_GPR(PORPOISE_ABI_S32, 3U), 3U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U),
        SDK_GPR(PORPOISE_ABI_S32, 5U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_report_adapter", "OSReport",
        SDK_VOID, 1U, SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_resume_thread_adapter", "OSResumeThread",
        SDK_GPR(PORPOISE_ABI_S32, 3U), 1U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_send_message_adapter", "OSSendMessage",
        SDK_GPR(PORPOISE_ABI_S32, 3U), 3U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U),
        SDK_GPR(PORPOISE_ABI_POINTER, 4U),
        SDK_GPR(PORPOISE_ABI_S32, 5U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_set_arena_hi_adapter", "OSSetArenaHi",
        SDK_VOID, 1U, SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_set_arena_lo_adapter", "OSSetArenaLo",
        SDK_VOID, 1U, SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_sleep_thread_adapter", "OSSleepThread",
        SDK_VOID, 1U, SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_suspend_thread_adapter", "OSSuspendThread",
        SDK_GPR(PORPOISE_ABI_S32, 3U), 1U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_os_wakeup_thread_adapter", "OSWakeupThread",
        SDK_VOID, 1U, SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_vi_configure_adapter", "VIConfigure",
        SDK_VOID, 1U, SDK_GPR(PORPOISE_ABI_POINTER, 3U)),
    SDK_CONTRACT(
        "porpoise_libporpoise_vi_set_next_frame_buffer_adapter",
        "VISetNextFrameBuffer", SDK_VOID, 1U,
        SDK_GPR(PORPOISE_ABI_POINTER, 3U))
};

#undef SDK_CONTRACT
#undef SDK_FPR
#undef SDK_GPR
#undef SDK_VOID

static bool sdk_value_matches(
    const PorpoiseAbiValue *value,
    const PorpoiseSdkAbiValue *expected) {
    return value != NULL && expected != NULL &&
           value->type == expected->type &&
           value->register_class == expected->register_class &&
           value->register_index == expected->register_index;
}

size_t porpoise_sdk_contract_count(void) {
    return sizeof(sdk_contracts) / sizeof(sdk_contracts[0]);
}

const PorpoiseSdkContract *porpoise_sdk_contract_at(size_t index) {
    if (index >= porpoise_sdk_contract_count()) return NULL;
    return &sdk_contracts[index];
}

const PorpoiseSdkContract *porpoise_sdk_contract_find_by_canonical_name(
    const char *canonical_name) {
    size_t index;

    if (canonical_name == NULL) return NULL;
    for (index = 0U; index < porpoise_sdk_contract_count(); index++) {
        if (strcmp(canonical_name, sdk_contracts[index].canonical_name) == 0) {
            return &sdk_contracts[index];
        }
    }
    return NULL;
}

const PorpoiseSdkContract *porpoise_sdk_contract_find_by_host_callable(
    const char *host_callable) {
    size_t index;

    if (host_callable == NULL) return NULL;
    for (index = 0U; index < porpoise_sdk_contract_count(); index++) {
        if (strcmp(host_callable, sdk_contracts[index].host_callable) == 0) {
            return &sdk_contracts[index];
        }
    }
    return NULL;
}

const char *porpoise_sdk_contract_canonical_name(
    const PorpoiseSdkContract *contract) {
    return contract != NULL ? contract->canonical_name : NULL;
}

PorpoiseSdkContractCategory porpoise_sdk_contract_category(
    const PorpoiseSdkContract *contract) {
    return contract != NULL
               ? PORPOISE_SDK_CONTRACT_NINTENDO_DOLPHIN
               : PORPOISE_SDK_CONTRACT_OTHER;
}

PorpoiseSdkHostBindingKind porpoise_sdk_contract_host_binding_kind(
    const PorpoiseSdkContract *contract) {
    return contract != NULL
               ? PORPOISE_SDK_HOST_BINDING_SPECIALIZED_ADAPTER
               : PORPOISE_SDK_HOST_BINDING_NONE;
}

const char *porpoise_sdk_contract_host_callable(
    const PorpoiseSdkContract *contract) {
    return contract != NULL ? contract->host_callable : NULL;
}

const char *porpoise_sdk_contract_host_header(
    const PorpoiseSdkContract *contract) {
    return contract != NULL ? PORPOISE_SDK_CONTRACT_BUILTIN_HEADER : NULL;
}

const PorpoiseSdkAbiValue *porpoise_sdk_contract_result(
    const PorpoiseSdkContract *contract) {
    return contract != NULL ? &contract->result : NULL;
}

size_t porpoise_sdk_contract_argument_count(
    const PorpoiseSdkContract *contract) {
    return contract != NULL ? contract->argument_count : 0U;
}

const PorpoiseSdkAbiValue *porpoise_sdk_contract_argument_at(
    const PorpoiseSdkContract *contract,
    size_t index) {
    if (contract == NULL || index >= contract->argument_count) return NULL;
    return &contract->arguments[index];
}

bool porpoise_sdk_contract_allows_automatic_import(
    const PorpoiseSdkContract *contract) {
    return contract != NULL;
}

bool porpoise_sdk_contract_binding_matches(
    const PorpoiseSdkContract *contract,
    const PorpoiseAbiFunction *function) {
    size_t index;

    if (contract == NULL || function == NULL ||
        function->kind != PORPOISE_ABI_IMPORT ||
        function->adapter == NULL || function->header == NULL ||
        strcmp(function->adapter, contract->host_callable) != 0 ||
        strcmp(function->header, PORPOISE_SDK_CONTRACT_BUILTIN_HEADER) != 0 ||
        !sdk_value_matches(&function->result, &contract->result) ||
        function->argument_count != contract->argument_count) {
        return false;
    }
    if (function->argument_count != 0U && function->arguments == NULL) {
        return false;
    }
    for (index = 0U; index < contract->argument_count; index++) {
        if (!sdk_value_matches(
                &function->arguments[index],
                &contract->arguments[index])) {
            return false;
        }
    }
    return true;
}

bool porpoise_sdk_contract_can_automatic_import(
    const PorpoiseSdkContract *contract,
    const PorpoiseAbiFunction *function) {
    return contract != NULL && function != NULL && function->symbol != NULL &&
           porpoise_sdk_contract_allows_automatic_import(contract) &&
           strcmp(function->symbol, contract->canonical_name) == 0 &&
           porpoise_sdk_contract_binding_matches(contract, function);
}
