#ifndef PORPOISE_LIBPORPOISE_BUILTINS_PRIVATE_H
#define PORPOISE_LIBPORPOISE_BUILTINS_PRIVATE_H

#include "porpoise_libporpoise_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize libPorpoise's host AI control model. The console ABI passes a
 * callback-stack address in r3, but the current host model intentionally does
 * not use that stack. A null guest stack is passed as native NULL without
 * decoding, tokenizing, or casting it. A non-null guest stack faults before
 * the native call because those callback-stack semantics are not implemented.
 * The adapter preserves r3. This is a control-surface approximation and does
 * not provide audio output.
 */
void porpoise_libporpoise_ai_init_adapter(
    PorpoisePpcState *state);

/*
 * Coherent AR stack-allocator boundary. Native libPorpoise receives only an
 * adapter-owned, host-endian block table. The guest table remains a checked,
 * four-byte-aligned uint32 address range and stores allocation lengths in
 * big-endian order. The exact owner must reset active or poisoned state.
 */
void porpoise_libporpoise_ar_init_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_ar_alloc_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_ar_free_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_ar_reset_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_ar_get_size_adapter(
    PorpoisePpcState *state);

/*
 * Initialize native GX exactly once for the active adapter. The guest FIFO
 * byte span in r3/r4 is decoded only after complete range and alignment
 * validation. The native GXFifoObj remains opaque and is returned in r3 only
 * as an adapter-owned host token; guest/native object layouts are never mixed.
 */
void porpoise_libporpoise_gx_init_adapter(
    PorpoisePpcState *state);

/*
 * GX frame-buffer boundary adapters require the exact active GX owner.
 * Draw-done callbacks are represented as guest addresses and delivered only
 * through the deferred host-event dispatcher. Copy descriptors are mirrored
 * in the adapter so complete, aligned guest destination spans can be checked
 * before a native copy. Fixed-size guest arrays/objects are copied and
 * endian-decoded instead of being cast to native SDK layouts.
 */
void porpoise_libporpoise_gx_set_draw_done_callback_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_set_copy_filter_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_set_copy_clear_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_set_disp_copy_dst_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_set_tex_copy_dst_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_copy_disp_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_copy_tex_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_load_light_obj_imm_adapter(
    PorpoisePpcState *state);

/*
 * GX data/object calls never cast fixed-size guest descriptors to native
 * host objects. Vertex arrays are registered as canonical big-endian bytes
 * through the exact console-visible mapped tail. Texture and TLUT loads
 * validate their complete descriptors and payloads, rebuild native objects
 * with full-width host pointers, and only then enter native GX. The two
 * channel-color adapters preserve KAR's hidden pointer-bearing ABI.
 */
void porpoise_libporpoise_gx_set_array_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_load_tex_obj_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_load_tlut_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_set_chan_amb_color_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_set_chan_mat_color_adapter(
    PorpoisePpcState *state);

/*
 * Stateless/value GX calls marshal complete big-endian guest objects before
 * entering native GX. Getter outputs are range-preflighted, produced in local
 * native storage, and committed to guest memory as one encoded byte span.
 * GXSetTevIndirect additionally reads its ninth and tenth EABI arguments from
 * the caller's guest stack overflow area at old r1+8 and old r1+12.
 */
void porpoise_libporpoise_gx_call_display_list_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_set_projection_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_get_projectionv_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_load_pos_mtx_imm_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_load_nrm_mtx_imm_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_load_tex_mtx_imm_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_get_viewportv_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_set_ind_tex_mtx_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_set_tev_color_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_set_tev_color_s10_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_set_tev_kcolor_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_set_fog_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_set_fog_range_adj_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_gx_set_tev_indirect_adapter(
    PorpoisePpcState *state);

/*
 * Canonical guest-arena ABI adapters. Getters and allocation results return
 * only mirrored uint32_t guest addresses. Setters and allocations update the
 * private mirror and native libPorpoise arena as one verified transaction;
 * no native pointer or host-address token is returned to lifted code.
 */
void porpoise_libporpoise_os_get_arena_lo_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_os_get_arena_hi_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_os_set_arena_lo_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_os_set_arena_hi_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_os_alloc_from_arena_lo_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_os_alloc_from_arena_hi_adapter(
    PorpoisePpcState *state);

/*
 * Dedicated synchronous DVD ABI adapters. Guest DVD structures retain their
 * 32-bit, big-endian SDK layout; the implementation mirrors open files in
 * stable native DVDFileInfo objects instead of passing guest storage to
 * libPorpoise.
 */
void porpoise_libporpoise_dvd_convert_path_to_entry_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_dvd_open_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_dvd_fast_open_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_dvd_read_prio_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_dvd_close_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_dvd_get_command_block_status_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_dvd_cancel_adapter(
    PorpoisePpcState *state);

/* Copy a big-endian guest GXRenderModeObj into native host layout before
 * calling libPorpoise's synchronous VIConfigure implementation. */
void porpoise_libporpoise_vi_configure_adapter(
    PorpoisePpcState *state);

/* Preserve VISetNextFrameBuffer's exact 32-bit guest XFB address. The native
 * versioned endpoint owns final-mode span validation and VI selection; no
 * decoded host-pointer fallback is permitted. */
void porpoise_libporpoise_vi_set_next_frame_buffer_adapter(
    PorpoisePpcState *state);

/*
 * Marshal the 32-bit guest ARQRequest layout and execute the current
 * libPorpoise high-priority DMA synchronously. A non-null guest callback is
 * queued until the submitting lifted frame has returned and guest EE permits
 * delivery; callback execution receives the original guest request in r3.
 */
void porpoise_libporpoise_arq_post_request_adapter(
    PorpoisePpcState *state);

/*
 * Mirror the exact 32-bit, big-endian guest DSPTaskInfo layout into a stable
 * native task and synchronously run libPorpoise's host DSP scheduler. Guest
 * callbacks receive the original guest task address in r3.
 */
void porpoise_libporpoise_dsp_add_task_adapter(
    PorpoisePpcState *state);

/*
 * Marshal CARDProbeEx's nullable s32 outputs through complete, aligned guest
 * spans. Existing guest values are preloaded and write-preflighted so a
 * native no-write result is preserved, and no guest pointer is exposed to
 * native CARD/EXI code.
 */
void porpoise_libporpoise_card_probe_ex_adapter(
    PorpoisePpcState *state);

/*
 * Guest OSThreadQueue is an exact 8-byte, big-endian structure and is not
 * layout-compatible with libPorpoise's native host queue. Wake is supported
 * only for an empty guest queue; sleep and nonempty wake fail closed until a
 * lifted guest-thread scheduler can suspend and resume PPC execution states.
 */
void porpoise_libporpoise_os_wakeup_thread_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_os_sleep_thread_adapter(
    PorpoisePpcState *state);

/*
 * Resume and suspend validate and transactionally mirror the complete exact
 * 0x318-byte, big-endian guest OSThread. When libPorpoise advertises the exact
 * host-thread carrier v1 contract, the initial narrow lifecycle supports a
 * higher-priority first resume, self-suspend, later resume, and exit. Without
 * that contract the same operations fail closed. Exit treats r3 as an opaque
 * guest return value and never dereferences it. None of these adapters cast
 * guest thread or context storage to native host objects.
 */
void porpoise_libporpoise_os_resume_thread_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_os_suspend_thread_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_os_exit_thread_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_os_get_current_thread_adapter(
    PorpoisePpcState *state);

/*
 * Operate on the exact 0x20-byte, big-endian guest OSMessageQueue layout.
 * Ready and nonblocking circular-buffer operations are supported without
 * casting guest storage to native host structures. An operation that would
 * sleep or wake a queued guest thread fails until guest scheduling exists.
 */
void porpoise_libporpoise_os_init_message_queue_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_os_send_message_adapter(
    PorpoisePpcState *state);
void porpoise_libporpoise_os_receive_message_adapter(
    PorpoisePpcState *state);

/*
 * Format OSReport directly from the guest PPC argument state. The adapter
 * implements the 32-bit big-endian EABI register/overflow-area rules,
 * validates bounded guest strings, and passes only a sanitized host string
 * to libPorpoise. Unsupported or unsafe printf features fault explicitly.
 */
void porpoise_libporpoise_os_report_adapter(
    PorpoisePpcState *state);

#ifdef __cplusplus
}
#endif

#endif
