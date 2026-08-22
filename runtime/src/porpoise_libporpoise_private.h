#ifndef PORPOISE_LIBPORPOISE_PRIVATE_H
#define PORPOISE_LIBPORPOISE_PRIVATE_H

#include "porpoise_libporpoise_adapter.h"

typedef struct PorpoiseLibporpoiseThreadRegistry
    PorpoiseLibporpoiseThreadRegistry;

/* Generated into src/ only. This is the private transaction boundary between
 * the libPorpoise owner adapter and focused lifted-ABI modules. */
typedef struct PorpoiseLibporpoiseArenaSnapshot {
    uint32_t configured_base;
    uint32_t configured_limit;
    uint32_t lo;
    uint32_t hi;
} PorpoiseLibporpoiseArenaSnapshot;

typedef struct PorpoiseLibporpoiseGxCopyDestination {
    uint16_t width;
    uint16_t height;
    uint32_t format;
    uint8_t mipmap;
} PorpoiseLibporpoiseGxCopyDestination;

int porpoise_libporpoise_thread_registry_create(
    PorpoiseHostAdapter *owner_adapter,
    PorpoiseLibporpoiseThreadRegistry **registry_out);

/* Stop, join, and destroy every carrier. A zero result means at least one
 * native callback can still observe adapter storage, so its owner must not be
 * freed. */
int porpoise_libporpoise_thread_registry_shutdown(
    PorpoiseLibporpoiseThreadRegistry *registry);

void porpoise_libporpoise_thread_registry_destroy(
    PorpoiseLibporpoiseThreadRegistry *registry);

int porpoise_libporpoise_thread_registry_set_export_binder(
    PorpoiseLibporpoiseThreadRegistry *registry,
    PorpoiseBindExportStateFn binder);

PorpoiseLibporpoiseThreadRegistry *
porpoise_libporpoise_thread_registry_for_state(PorpoisePpcState *state);

/* Release the process-global native AR allocator only when this exact host
 * adapter owns its shadow table. This is called before the adapter context is
 * destroyed so native code can never retain a dangling table pointer. */
void porpoise_libporpoise_ar_shutdown(
    const PorpoiseHostAdapter *adapter);

int porpoise_libporpoise_arena_snapshot(
    PorpoisePpcState *state,
    PorpoiseLibporpoiseArenaSnapshot *snapshot_out);

int porpoise_libporpoise_arena_commit(
    PorpoisePpcState *state,
    const PorpoiseLibporpoiseArenaSnapshot *expected,
    uint32_t new_lo,
    uint32_t new_hi);

/* Validate GXInit's complete guest FIFO span and reserve the one-way native
 * initialization transition. A successful begin leaves the context poisoned
 * until commit proves that the native FIFO result has an owned opaque token. */
int porpoise_libporpoise_gx_init_begin(
    PorpoisePpcState *state,
    uint32_t guest_base,
    uint32_t size,
    void **native_base_out);

int porpoise_libporpoise_gx_init_commit(
    PorpoisePpcState *state,
    const void *native_fifo,
    uint32_t *guest_token_out);

/* Require that native GX initialization completed successfully for this exact
 * active adapter context. Native GX state is process-global and has no reset;
 * an adapter created after its owning context shuts down cannot inherit it. */
int porpoise_libporpoise_gx_require_active(
    PorpoisePpcState *state);

/* Queue canonical write-gather bytes in-order when libPorpoise exposes the
 * version-2 ingress. Version 1 remains a synchronous compatibility fallback.
 * This is used for complete commands such as GXBegin; ordinary WGPIPE stores
 * are coalesced privately and flushed at imported-call ordering boundaries. */
int porpoise_libporpoise_gx_queue_canonical_bytes(
    const uint8_t *bytes,
    size_t size);

int porpoise_libporpoise_gx_flush_pending(PorpoisePpcState *state);

/* Complete all previously submitted GX work without flushing libPorpoise's
 * private SDK shadow state into the shared command processor. Lifted guests
 * own the FIFO state, so native GXDrawDone would overwrite their VCD/VAT
 * registers with unrelated native defaults. */
int porpoise_libporpoise_gx_complete_draw(
    PorpoisePpcState *state);

/* Validate and decode one complete ordinary-RAM guest span for a native GX
 * call. Tokens, MMIO, holes, guest/host misalignment, and overflow fault
 * before the native API sees a pointer. Alignment must be a power of two. */
int porpoise_libporpoise_gx_decode_span(
    PorpoisePpcState *state,
    uint32_t guest_address,
    size_t size,
    size_t alignment,
    void **native_pointer_out,
    const char *null_description);

int porpoise_libporpoise_gx_read_span(
    PorpoisePpcState *state,
    uint32_t guest_address,
    size_t size,
    size_t alignment,
    void *destination,
    const char *null_description);

/* Decode from one guest RAM address through the exact end of its
 * physical/cached/uncached console-visible mapping. */
int porpoise_libporpoise_gx_decode_mapped_tail(
    PorpoisePpcState *state,
    uint32_t guest_address,
    void **native_pointer_out,
    uint32_t *size_out);

int porpoise_libporpoise_gx_set_draw_done_callback(
    PorpoisePpcState *state,
    uint32_t guest_callback,
    uint32_t *previous_guest_callback_out);

int porpoise_libporpoise_gx_record_disp_copy_destination(
    PorpoisePpcState *state,
    uint16_t width,
    uint16_t height);

int porpoise_libporpoise_gx_record_tex_copy_destination(
    PorpoisePpcState *state,
    uint16_t width,
    uint16_t height,
    uint32_t format,
    uint8_t mipmap);

int porpoise_libporpoise_gx_get_disp_copy_destination(
    PorpoisePpcState *state,
    PorpoiseLibporpoiseGxCopyDestination *destination_out);

int porpoise_libporpoise_gx_get_tex_copy_destination(
    PorpoisePpcState *state,
    PorpoiseLibporpoiseGxCopyDestination *destination_out);

#endif
