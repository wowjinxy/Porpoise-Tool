#include "porpoise_libporpoise_builtins_private.h"
#include "porpoise_libporpoise_private.h"

/*
 * Keep these unstable libPorpoise host interfaces confined to this adapter.
 * Generated lifted sources and porpoise_lifted.c must not include them.
 */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include <dolphin/ar.h>
#include <dolphin/demo/DEMOPad.h>
#include <dolphin/dsp.h>
#include <dolphin/dvd.h>
#include <dolphin/os/OSArena.h>
#include <dolphin/os/OSHostAddress.h>
#include <dolphin/os/OSHostMemory.h>
#include <dolphin/os/OSInterrupt.h>
#include <dolphin/os/OSTime.h>
#include <dolphin/pad.h>
#include <dolphin/vi.h>
#include <simulator/sim_gx_CommandProcessor.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "porpoise_dispatch_private.h"
#include "porpoise_libporpoise_gx_headers.h"
#include "porpoise_libporpoise_presentation_private.h"

#if !defined(PORPOISE_HAVE_LIBPORPOISE_PRESENTATION_STATS_BRIDGE)
int porpoise_libporpoise_presentation_snapshot(
    uint64_t *presentation_count_out,
    uint32_t *guest_frame_buffer_out)
{
    if (presentation_count_out != NULL) {
        *presentation_count_out = UINT64_C(0);
    }
    if (guest_frame_buffer_out != NULL) {
        *guest_frame_buffer_out = UINT32_C(0);
    }
    return 0;
}
#endif

/* libPorpoise currently declares OSInit only through its broad os.h facade. */
extern void OSInit(void);

#define PORPOISE_PHYSICAL_EFB_START UINT32_C(0x08000000)
#define PORPOISE_PHYSICAL_MMIO_END UINT32_C(0x10000000)
#define PORPOISE_CACHED_EFB_START UINT32_C(0x88000000)
#define PORPOISE_CACHED_MMIO_END UINT64_C(0x90000000)
#define PORPOISE_EFFECTIVE_EFB_START UINT32_C(0xC8000000)
#define PORPOISE_EFFECTIVE_MMIO_END UINT64_C(0xD0000000)
#define PORPOISE_GX_FIFO_ADDRESS UINT32_C(0xCC008000)
#define PORPOISE_GUEST_SYSTEM_CALL_VECTOR UINT32_C(0x80000C00)
#define PORPOISE_GUEST_SYSTEM_CALL_VECTOR_WORDS 7U

static const uint32_t porpoise_libporpoise_canonical_system_call_vector[
    PORPOISE_GUEST_SYSTEM_CALL_VECTOR_WORDS] = {
    UINT32_C(0x7D30FAA6), /* mfspr r9, HID0 */
    UINT32_C(0x612A0008), /* ori r10, r9, 8 */
    UINT32_C(0x7D50FBA6), /* mtspr HID0, r10 */
    UINT32_C(0x4C00012C), /* isync */
    UINT32_C(0x7C0004AC), /* sync */
    UINT32_C(0x7D30FBA6), /* mtspr HID0, r9 */
    UINT32_C(0x4C000064)  /* rfi */
};

#define PORPOISE_DVD_MIRROR_CAPACITY 64U
#define PORPOISE_DVD_PATH_CAPACITY 1024U
#define PORPOISE_GUEST_DVD_COMMAND_BLOCK_SIZE 0x30U
#define PORPOISE_GUEST_DVD_FILE_INFO_SIZE 0x3CU
#define PORPOISE_GUEST_DVD_STATE_OFFSET 0x0CU
#define PORPOISE_GUEST_DVD_CURRENT_TRANSFER_OFFSET 0x1CU
#define PORPOISE_GUEST_DVD_TRANSFERRED_OFFSET 0x20U
#define PORPOISE_GUEST_DVD_START_ADDRESS_OFFSET 0x30U
#define PORPOISE_GUEST_DVD_LENGTH_OFFSET 0x34U
#define PORPOISE_GUEST_DVD_CALLBACK_OFFSET 0x38U
#define PORPOISE_DVD_TRANSFER_ALIGNMENT 32U
#define PORPOISE_GUEST_GX_RENDER_MODE_SIZE 0x3CU
#define PORPOISE_GUEST_PAD_STATUS_SIZE 0x0CU
#define PORPOISE_GUEST_PAD_STATUS_COUNT 4U
#define PORPOISE_GUEST_PAD_STATUS_ARRAY_SIZE \
    (PORPOISE_GUEST_PAD_STATUS_SIZE * PORPOISE_GUEST_PAD_STATUS_COUNT)
#define PORPOISE_GX_DRAW_DONE_EVENT_CAPACITY 64U
#define PORPOISE_GX_FIFO_PENDING_CAPACITY 4096U
#define PORPOISE_GUEST_ARQ_REQUEST_SIZE 0x20U
#define PORPOISE_GUEST_ARQ_NEXT_OFFSET 0x00U
#define PORPOISE_GUEST_ARQ_OWNER_OFFSET 0x04U
#define PORPOISE_GUEST_ARQ_TYPE_OFFSET 0x08U
#define PORPOISE_GUEST_ARQ_PRIORITY_OFFSET 0x0CU
#define PORPOISE_GUEST_ARQ_SOURCE_OFFSET 0x10U
#define PORPOISE_GUEST_ARQ_DESTINATION_OFFSET 0x14U
#define PORPOISE_GUEST_ARQ_LENGTH_OFFSET 0x18U
#define PORPOISE_GUEST_ARQ_CALLBACK_OFFSET 0x1CU

#define PORPOISE_GUEST_DSP_TASK_SIZE 0x50U
#define PORPOISE_GUEST_DSP_STATE_OFFSET 0x00U
#define PORPOISE_GUEST_DSP_PRIORITY_OFFSET 0x04U
#define PORPOISE_GUEST_DSP_FLAGS_OFFSET 0x08U
#define PORPOISE_GUEST_DSP_IRAM_MEMORY_OFFSET 0x0CU
#define PORPOISE_GUEST_DSP_IRAM_LENGTH_OFFSET 0x10U
#define PORPOISE_GUEST_DSP_IRAM_ADDRESS_OFFSET 0x14U
#define PORPOISE_GUEST_DSP_DRAM_MEMORY_OFFSET 0x18U
#define PORPOISE_GUEST_DSP_DRAM_LENGTH_OFFSET 0x1CU
#define PORPOISE_GUEST_DSP_DRAM_ADDRESS_OFFSET 0x20U
#define PORPOISE_GUEST_DSP_INIT_VECTOR_OFFSET 0x24U
#define PORPOISE_GUEST_DSP_RESUME_VECTOR_OFFSET 0x26U
#define PORPOISE_GUEST_DSP_INIT_CALLBACK_OFFSET 0x28U
#define PORPOISE_GUEST_DSP_RESUME_CALLBACK_OFFSET 0x2CU
#define PORPOISE_GUEST_DSP_DONE_CALLBACK_OFFSET 0x30U
#define PORPOISE_GUEST_DSP_REQUEST_CALLBACK_OFFSET 0x34U
#define PORPOISE_GUEST_DSP_NEXT_OFFSET 0x38U
#define PORPOISE_GUEST_DSP_PREVIOUS_OFFSET 0x3CU
#define PORPOISE_GUEST_DSP_CONTEXT_TIME_OFFSET 0x40U
#define PORPOISE_GUEST_DSP_TASK_TIME_OFFSET 0x48U

typedef struct PorpoiseDvdFileMirror {
    int in_use;
    uint32_t guest_file_info;
    DVDFileInfo native_file_info;
} PorpoiseDvdFileMirror;

typedef struct PorpoiseArqCompletion {
    uint32_t guest_request;
    uint32_t guest_callback;
    uint32_t ready_depth;
} PorpoiseArqCompletion;

typedef struct PorpoiseDspTaskMirror PorpoiseDspTaskMirror;

typedef enum PorpoiseLibporpoiseGxInitLifecycle {
    PORPOISE_LIBPORPOISE_GX_INIT_UNINITIALIZED = 0,
    PORPOISE_LIBPORPOISE_GX_INIT_POISONED,
    PORPOISE_LIBPORPOISE_GX_INIT_ACTIVE
} PorpoiseLibporpoiseGxInitLifecycle;

struct PorpoiseDspTaskMirror {
    DSPTaskInfo native_task;
    uint32_t guest_task;
    PorpoisePpcState submission_state;
    PorpoiseDspTaskMirror *next;
};

typedef struct PorpoiseHostPointerToken {
    const void *native_pointer;
    u32 guest_token;
} PorpoiseHostPointerToken;

typedef struct PorpoiseLibporpoiseContext {
    PorpoiseHostAdapter *owner_adapter;
    PorpoiseLibporpoiseThreadRegistry *thread_registry;
    PorpoiseHostPointerToken *pointer_tokens;
    size_t pointer_token_count;
    size_t pointer_token_capacity;
    PorpoiseHostCallGuestFn guest_dispatch;
    PorpoiseDvdFileMirror dvd_files[PORPOISE_DVD_MIRROR_CAPACITY];
    PorpoiseArqCompletion *arq_completions;
    size_t arq_completion_count;
    size_t arq_completion_capacity;
    int dispatching_events;
    PorpoiseDspTaskMirror *dsp_tasks;
    uint32_t dsp_native_call_depth;
    int dsp_callback_failed;
    PorpoiseFault dsp_callback_fault;
    uint32_t dsp_callback_fault_address;
    char dsp_callback_fault_message[PORPOISE_FAULT_MESSAGE_CAPACITY];
    uint32_t arena_configured_base;
    uint32_t arena_configured_limit;
    uint32_t arena_guest_lo;
    uint32_t arena_guest_hi;
    void *arena_native_base;
    void *arena_previous_native_lo;
    void *arena_previous_native_hi;
    int arena_restore_pending;
    int arena_configuration_poisoned;
    int arena_configured;
    PorpoiseLibporpoiseGuestSdkLayoutV1 guest_sdk_layout;
    int guest_sdk_layout_bound;
    int os_guest_state_mirrored;
    u32 gx_fifo_token;
    uint8_t gx_fifo_pending[PORPOISE_GX_FIFO_PENDING_CAPACITY];
    size_t gx_fifo_pending_size;
    uint32_t gx_draw_done_callback;
    uint32_t gx_draw_done_events[PORPOISE_GX_DRAW_DONE_EVENT_CAPACITY];
    size_t gx_draw_done_event_head;
    size_t gx_draw_done_event_count;
    int gx_draw_done_callback_failed;
    PorpoiseFault gx_draw_done_callback_fault;
    uint32_t gx_draw_done_callback_fault_address;
    char gx_draw_done_callback_fault_message[
        PORPOISE_FAULT_MESSAGE_CAPACITY];
    PorpoiseLibporpoiseGxCopyDestination gx_disp_copy_destination;
    PorpoiseLibporpoiseGxCopyDestination gx_tex_copy_destination;
    int gx_disp_copy_destination_configured;
    int gx_tex_copy_destination_configured;
    uint32_t vi_xfb_span_bytes;
    int vi_xfb_layout_configured;
} PorpoiseLibporpoiseContext;

static int porpoise_libporpoise_os_initialized;
static int porpoise_libporpoise_dvd_initialized;
static int porpoise_libporpoise_vi_initialized;
static int porpoise_libporpoise_demo_pad_initialized;
static int porpoise_libporpoise_dvd_root_is_explicit;
static char *porpoise_libporpoise_dvd_root_directory;
static PorpoiseLibporpoiseContext *porpoise_libporpoise_active_context;
static PorpoiseLibporpoiseGxInitLifecycle
    porpoise_libporpoise_gx_init_lifecycle =
        PORPOISE_LIBPORPOISE_GX_INIT_UNINITIALIZED;
static PorpoiseLibporpoiseContext *porpoise_libporpoise_gx_owner_context;

static int porpoise_libporpoise_owns_token(
    const PorpoiseLibporpoiseContext *context,
    u32 token);
static PorpoiseHostResult porpoise_libporpoise_poll_events(
    void *adapter_context,
    PorpoisePpcState *state);

static const PorpoiseHostPointerToken *
porpoise_libporpoise_find_pointer_token(
    const PorpoiseLibporpoiseContext *context,
    const void *native_pointer)
{
    size_t index;

    if (context == NULL || native_pointer == NULL) {
        return NULL;
    }
    for (index = 0U; index < context->pointer_token_count; index++) {
        if (context->pointer_tokens[index].native_pointer == native_pointer) {
            return &context->pointer_tokens[index];
        }
    }
    return NULL;
}

static const PorpoiseHostPointerToken *
porpoise_libporpoise_find_owned_token(
    const PorpoiseLibporpoiseContext *context,
    u32 token)
{
    size_t index;

    if (context == NULL || !__OSHostIsAddressToken(token)) {
        return NULL;
    }
    for (index = 0U; index < context->pointer_token_count; index++) {
        if (context->pointer_tokens[index].guest_token == token) {
            return &context->pointer_tokens[index];
        }
    }
    return NULL;
}

static PorpoiseHostResult porpoise_libporpoise_reserve_pointer_token(
    PorpoiseLibporpoiseContext *context)
{
    size_t new_capacity;
    PorpoiseHostPointerToken *resized;

    if (context == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (context->pointer_token_count >= OS_HOST_ADDRESS_TOKEN_SLOT_COUNT) {
        return PORPOISE_HOST_IO_ERROR;
    }
    if (context->pointer_token_count < context->pointer_token_capacity) {
        return PORPOISE_HOST_OK;
    }

    new_capacity = context->pointer_token_capacity == 0U
                       ? 16U
                       : context->pointer_token_capacity * 2U;
    if (new_capacity < context->pointer_token_capacity ||
        new_capacity > OS_HOST_ADDRESS_TOKEN_SLOT_COUNT) {
        new_capacity = OS_HOST_ADDRESS_TOKEN_SLOT_COUNT;
    }
    if (new_capacity > SIZE_MAX / sizeof(*context->pointer_tokens)) {
        return PORPOISE_HOST_ADDRESS_OVERFLOW;
    }
    resized = (PorpoiseHostPointerToken *)realloc(
        context->pointer_tokens,
        new_capacity * sizeof(*context->pointer_tokens));
    if (resized == NULL) {
        return PORPOISE_HOST_IO_ERROR;
    }
    context->pointer_tokens = resized;
    context->pointer_token_capacity = new_capacity;
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult porpoise_libporpoise_record_pointer_token(
    PorpoiseLibporpoiseContext *context,
    const void *native_pointer,
    u32 token)
{
    if (context == NULL || native_pointer == NULL ||
        !__OSHostIsAddressToken(token)) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (porpoise_libporpoise_find_pointer_token(context, native_pointer) !=
            NULL ||
        porpoise_libporpoise_find_owned_token(context, token) != NULL) {
        return PORPOISE_HOST_INVALID_POINTER;
    }
    if (context->pointer_token_count >= context->pointer_token_capacity ||
        context->pointer_token_count >= OS_HOST_ADDRESS_TOKEN_SLOT_COUNT) {
        return PORPOISE_HOST_IO_ERROR;
    }

    context->pointer_tokens[context->pointer_token_count].native_pointer =
        native_pointer;
    context->pointer_tokens[context->pointer_token_count].guest_token = token;
    context->pointer_token_count++;
    return PORPOISE_HOST_OK;
}

static int porpoise_libporpoise_owns_token(
    const PorpoiseLibporpoiseContext *context,
    u32 token)
{
    return porpoise_libporpoise_find_owned_token(context, token) != NULL;
}

static PorpoiseHostResult porpoise_libporpoise_read_time_base(
    void *context,
    uint64_t *ticks_out)
{
    (void)context;
    if (ticks_out == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    *ticks_out = (uint64_t)OSGetTime();
    return PORPOISE_HOST_OK;
}

static int porpoise_ranges_overlap(
    uint64_t first_start,
    uint64_t first_end,
    uint64_t second_start,
    uint64_t second_end)
{
    return first_start < second_end && second_start < first_end;
}

static int porpoise_is_unsupported_mmio_span(
    uint32_t guest_address,
    size_t size)
{
    uint64_t start;
    uint64_t end;

    if (size == 0U) {
        return 0;
    }

    start = guest_address;
    end = start + (uint64_t)size;
    return porpoise_ranges_overlap(
               start,
               end,
               PORPOISE_PHYSICAL_EFB_START,
               PORPOISE_PHYSICAL_MMIO_END) ||
           porpoise_ranges_overlap(
               start,
               end,
               PORPOISE_CACHED_EFB_START,
               PORPOISE_CACHED_MMIO_END) ||
           porpoise_ranges_overlap(
               start,
               end,
               PORPOISE_EFFECTIVE_EFB_START,
               PORPOISE_EFFECTIVE_MMIO_END);
}

int porpoise_libporpoise_gx_queue_canonical_bytes(
    const uint8_t *bytes,
    size_t size)
{
    if ((bytes == NULL && size != 0U) || size > (size_t)UINT32_MAX) {
        return 0;
    }
#if defined(SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION) && \
    SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION >= 1
#if SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION >= 2
    return SIM_GX_CommandProcessor_QueueCanonicalBytes(
               (const u8 *)bytes, (u32)size)
        ? 1 : 0;
#else
    return SIM_GX_CommandProcessor_SendCanonicalBytes(
               (const u8 *)bytes, (u32)size)
        ? 1 : 0;
#endif
#else
    (void)bytes;
    (void)size;
    return 0;
#endif
}

static PorpoiseHostResult porpoise_libporpoise_flush_gx_fifo_context(
    PorpoiseLibporpoiseContext *context)
{
    if (context == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (context->gx_fifo_pending_size == 0U) {
        return PORPOISE_HOST_OK;
    }
    if (!porpoise_libporpoise_gx_queue_canonical_bytes(
            context->gx_fifo_pending,
            context->gx_fifo_pending_size)) {
        return PORPOISE_HOST_IO_ERROR;
    }
    context->gx_fifo_pending_size = 0U;
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult porpoise_libporpoise_buffer_gx_fifo(
    PorpoiseLibporpoiseContext *context,
    const void *source,
    size_t size)
{
    PorpoiseHostResult result;

    if (context == NULL || (source == NULL && size != 0U) ||
        size > PORPOISE_GX_FIFO_PENDING_CAPACITY) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (size > PORPOISE_GX_FIFO_PENDING_CAPACITY -
                   context->gx_fifo_pending_size) {
        result = porpoise_libporpoise_flush_gx_fifo_context(context);
        if (result != PORPOISE_HOST_OK) {
            return result;
        }
    }
    memcpy(
        &context->gx_fifo_pending[context->gx_fifo_pending_size],
        source,
        size);
    context->gx_fifo_pending_size += size;
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult porpoise_libporpoise_write_gx_fifo(
    PorpoiseLibporpoiseContext *context,
    const void *source,
    size_t size)
{
    /*
     * Typed SendU16/U32/F32 calls are not a lossless raw-MMIO bridge: packed
     * color and vertex fields need the original Gekko byte stream. Require
     * the versioned canonical-byte API and otherwise keep this MMIO fail-closed.
     */
#if defined(SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION) && \
    SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION >= 1
    if (size != 1U && size != 2U && size != 4U && size != 8U) {
        return PORPOISE_HOST_UNSUPPORTED_MMIO;
    }
    return porpoise_libporpoise_buffer_gx_fifo(context, source, size);
#else
    (void)source;
    (void)size;
    return PORPOISE_HOST_UNSUPPORTED_MMIO;
#endif
}

static PorpoiseHostResult porpoise_libporpoise_write_gx_fifo_u8(
    void *opaque_context,
    uint8_t value)
{
#if defined(SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION) && \
    SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION >= 1
    return porpoise_libporpoise_buffer_gx_fifo(
        (PorpoiseLibporpoiseContext *)opaque_context,
        &value,
        sizeof(value));
#else
    (void)opaque_context;
    (void)value;
    return PORPOISE_HOST_UNSUPPORTED_MMIO;
#endif
}

static int porpoise_span_overlaps_address_tokens(
    uint32_t guest_address,
    size_t size)
{
    uint64_t start;
    uint64_t end;
    uint64_t token_start;
    uint64_t token_end;

    if (size == 0U) {
        return 0;
    }
    start = guest_address;
    end = start + (uint64_t)size;
    token_start = (uint64_t)(
        OS_HOST_ADDRESS_TOKEN_TAG & OS_HOST_ADDRESS_TOKEN_MASK);
    token_end = token_start +
                (uint64_t)(UINT32_MAX - OS_HOST_ADDRESS_TOKEN_MASK) +
                UINT64_C(1);
    return porpoise_ranges_overlap(start, end, token_start, token_end);
}

static PorpoiseHostResult porpoise_libporpoise_decode_span(
    const void *context,
    uint32_t guest_address,
    size_t size,
    void **pointer_out)
{
    uint32_t last_address;
    void *first_pointer;

    if (context == NULL || pointer_out == NULL || size == 0U) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    *pointer_out = NULL;

    if (size - 1U > (size_t)(UINT32_MAX - guest_address)) {
        return PORPOISE_HOST_ADDRESS_OVERFLOW;
    }
    last_address = guest_address + (uint32_t)(size - 1U);
    if (porpoise_is_unsupported_mmio_span(guest_address, size)) {
        return PORPOISE_HOST_UNSUPPORTED_MMIO;
    }

    /* Host tokens are opaque ABI handles, never guest memory ranges. */
    if (porpoise_span_overlaps_address_tokens(guest_address, size)) {
        return PORPOISE_HOST_INVALID_POINTER;
    }

    first_pointer = __OSHostDecodeAddress((u32)guest_address);
    if (first_pointer == NULL) {
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    }

    if (size > 1U) {
        const void *last_pointer = __OSHostDecodeAddress((u32)last_address);
        uintptr_t expected_last_pointer;
        if ((uintptr_t)first_pointer > UINTPTR_MAX - (size - 1U)) {
            return PORPOISE_HOST_ADDRESS_OVERFLOW;
        }
        expected_last_pointer = (uintptr_t)first_pointer + (size - 1U);
        if (last_pointer == NULL ||
            (uintptr_t)last_pointer != expected_last_pointer) {
            return PORPOISE_HOST_UNMAPPED_ADDRESS;
        }
    }

    *pointer_out = first_pointer;
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult porpoise_libporpoise_read_bytes(
    // cppcheck-suppress constParameterCallback
    void *context,
    uint32_t guest_address,
    void *destination,
    size_t size)
{
    PorpoiseHostResult result;
    void *source;

    if (destination == NULL && size != 0U) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (size == 0U) {
        return PORPOISE_HOST_OK;
    }

    source = NULL;
    result = porpoise_libporpoise_decode_span(
        context,
        guest_address,
        size,
        &source);
    if (result != PORPOISE_HOST_OK) {
        return result;
    }

    memcpy(destination, source, size);
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult porpoise_libporpoise_write_bytes(
    // cppcheck-suppress constParameterCallback
    void *context,
    uint32_t guest_address,
    const void *source,
    size_t size)
{
    PorpoiseHostResult result;
    void *destination;

    if (source == NULL && size != 0U) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (size == 0U) {
        return PORPOISE_HOST_OK;
    }
    if (guest_address == PORPOISE_GX_FIFO_ADDRESS) {
        return porpoise_libporpoise_write_gx_fifo(
            (PorpoiseLibporpoiseContext *)context,
            source,
            size);
    }

    destination = NULL;
    result = porpoise_libporpoise_decode_span(
        context,
        guest_address,
        size,
        &destination);
    if (result != PORPOISE_HOST_OK) {
        return result;
    }

    memcpy(destination, source, size);
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult porpoise_libporpoise_decode_pointer(
    /* The public callback ABI keeps context mutable for stateful adapters. */
    // cppcheck-suppress constParameterCallback
    void *context,
    uint32_t guest_address,
    void **pointer_out)
{
    const PorpoiseLibporpoiseContext *adapter_context =
        (const PorpoiseLibporpoiseContext *)context;
    const PorpoiseHostPointerToken *owned_token;
    void *pointer;

    if (adapter_context == NULL || pointer_out == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    *pointer_out = NULL;

    if (guest_address == 0U) {
        return PORPOISE_HOST_OK;
    }
    if (porpoise_is_unsupported_mmio_span(guest_address, 1U)) {
        return PORPOISE_HOST_UNSUPPORTED_MMIO;
    }

    owned_token = NULL;
    if (__OSHostIsAddressToken((u32)guest_address)) {
        owned_token = porpoise_libporpoise_find_owned_token(
            adapter_context,
            (u32)guest_address);
        if (owned_token == NULL) {
            return PORPOISE_HOST_INVALID_POINTER;
        }
    }

    pointer = __OSHostDecodeAddress((u32)guest_address);
    if (owned_token != NULL && pointer != owned_token->native_pointer) {
        return PORPOISE_HOST_INVALID_POINTER;
    }
    if (pointer == NULL) {
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    }

    *pointer_out = pointer;
    return PORPOISE_HOST_OK;
}

static PorpoiseHostResult porpoise_libporpoise_encode_pointer(
    void *context,
    const void *pointer,
    uint32_t *guest_address_out)
{
    PorpoiseLibporpoiseContext *adapter_context =
        (PorpoiseLibporpoiseContext *)context;
    const PorpoiseHostPointerToken *cached;
    PorpoiseHostResult result;
    u32 guest_address;

    if (adapter_context == NULL || guest_address_out == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    *guest_address_out = 0U;

    if (pointer == NULL) {
        return PORPOISE_HOST_OK;
    }

    /* libPorpoise allocates a fresh opaque token on every encode. Intern the
     * adapter-owned pair first so repeated native pointers keep stable guest
     * identity and cannot consume the host token table. A stale or externally
     * replaced token is never silently refreshed. */
    cached = porpoise_libporpoise_find_pointer_token(
        adapter_context,
        pointer);
    if (cached != NULL) {
        if (__OSHostDecodeAddress(cached->guest_token) != pointer) {
            return PORPOISE_HOST_INVALID_POINTER;
        }
        *guest_address_out = (uint32_t)cached->guest_token;
        return PORPOISE_HOST_OK;
    }

    /* Console-memory pointers encode directly and need no adapter ownership.
     * For every other pointer, reserve bookkeeping before asking libPorpoise
     * to allocate a token. Thus allocation failure cannot create an untracked
     * host token or partially publish a pointer/token pair. */
    if (!__OSHostMemoryContainsAddress(pointer)) {
        result = porpoise_libporpoise_reserve_pointer_token(adapter_context);
        if (result != PORPOISE_HOST_OK) {
            return result;
        }
    }

    /* This adapter needs a value that __OSHostDecodeAddress can round-trip. */
    guest_address = __OSHostEncodeAddress(pointer);
    if (guest_address == 0U) {
        return PORPOISE_HOST_INVALID_POINTER;
    }
    if (porpoise_is_unsupported_mmio_span((uint32_t)guest_address, 1U)) {
        return PORPOISE_HOST_UNSUPPORTED_MMIO;
    }

    if (__OSHostIsAddressToken(guest_address)) {
        if (__OSHostDecodeAddress(guest_address) != pointer) {
            __OSHostReleaseAddress(guest_address);
            return PORPOISE_HOST_INVALID_POINTER;
        }
        result = porpoise_libporpoise_record_pointer_token(
            adapter_context,
            pointer,
            guest_address);
        if (result != PORPOISE_HOST_OK) {
            __OSHostReleaseAddress(guest_address);
            return result;
        }
    }

    *guest_address_out = (uint32_t)guest_address;
    return PORPOISE_HOST_OK;
}

static PorpoiseFault porpoise_libporpoise_fault_from_host_result(
    PorpoiseHostResult result)
{
    switch (result) {
        case PORPOISE_HOST_OK:
            return PORPOISE_FAULT_NONE;
        case PORPOISE_HOST_INVALID_ARGUMENT:
            return PORPOISE_FAULT_INVALID_ARGUMENT;
        case PORPOISE_HOST_INVALID_POINTER:
            return PORPOISE_FAULT_INVALID_POINTER;
        case PORPOISE_HOST_UNMAPPED_ADDRESS:
            return PORPOISE_FAULT_UNMAPPED_ADDRESS;
        case PORPOISE_HOST_UNSUPPORTED_MMIO:
            return PORPOISE_FAULT_UNSUPPORTED_MMIO;
        case PORPOISE_HOST_ADDRESS_OVERFLOW:
            return PORPOISE_FAULT_ADDRESS_OVERFLOW;
        case PORPOISE_HOST_IO_ERROR:
        default:
            return PORPOISE_FAULT_HOST_IO;
    }
}

static void porpoise_libporpoise_set_host_fault(
    PorpoisePpcState *state,
    PorpoiseHostResult result,
    uint32_t guest_address)
{
    porpoise_state_set_fault(
        state,
        porpoise_libporpoise_fault_from_host_result(result),
        guest_address,
        porpoise_host_result_string(result));
}

static PorpoiseLibporpoiseContext *
porpoise_libporpoise_context_for_state(PorpoisePpcState *state)
{
    PorpoiseHostAdapter *adapter;
    PorpoiseLibporpoiseContext *context;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return NULL;
    }
    adapter = state->host;
    if (adapter == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_NO_HOST_ADAPTER,
            state->pc,
            "libPorpoise ABI adapter requires its host adapter");
        return NULL;
    }
    if (adapter->read_bytes != porpoise_libporpoise_read_bytes ||
        adapter->write_bytes != porpoise_libporpoise_write_bytes ||
        adapter->decode_pointer != porpoise_libporpoise_decode_pointer ||
        adapter->encode_pointer != porpoise_libporpoise_encode_pointer) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "PPC state is not attached to a libPorpoise adapter");
        return NULL;
    }

    context = (PorpoiseLibporpoiseContext *)adapter->context;
    if (context == NULL || context != porpoise_libporpoise_active_context ||
        context->owner_adapter != adapter) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "libPorpoise adapter is not active");
        return NULL;
    }
    return context;
}

int porpoise_libporpoise_gx_flush_pending(PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseHostResult result;

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return 0;
    }
    result = porpoise_libporpoise_flush_gx_fifo_context(context);
    if (result != PORPOISE_HOST_OK) {
        porpoise_libporpoise_set_host_fault(state, result, state->pc);
        return 0;
    }
    return 1;
}

PorpoiseLibporpoiseThreadRegistry *
porpoise_libporpoise_thread_registry_for_state(PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context =
        porpoise_libporpoise_context_for_state(state);

    return context != NULL ? context->thread_registry : NULL;
}

int porpoise_libporpoise_gx_init_begin(
    PorpoisePpcState *state,
    uint32_t guest_base,
    uint32_t size,
    void **native_base_out)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseHostResult result;
    void *native_base;

    if (native_base_out != NULL) {
        *native_base_out = NULL;
    }
    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return 0;
    }
    if (native_base_out == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "GXInit adapter transaction has no native-base output");
        return 0;
    }
    if (porpoise_libporpoise_gx_init_lifecycle ==
        PORPOISE_LIBPORPOISE_GX_INIT_POISONED) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            guest_base,
            "GXInit host state is poisoned after an incomplete initialization");
        return 0;
    }
    if (porpoise_libporpoise_gx_init_lifecycle ==
        PORPOISE_LIBPORPOISE_GX_INIT_ACTIVE) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            guest_base,
            "GXInit may only initialize the active host adapter once");
        return 0;
    }
    if (guest_base == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_base,
            "GXInit guest FIFO base is NULL");
        return 0;
    }
    if ((guest_base & UINT32_C(31)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_base,
            "GXInit guest FIFO base must be 32-byte aligned");
        return 0;
    }
    if (size < UINT32_C(0x10000) || (size & UINT32_C(31)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            size,
            "GXInit FIFO size must be at least 0x10000 and a multiple of 32");
        return 0;
    }
    if (size > UINT32_MAX - guest_base) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            guest_base,
            "GXInit guest FIFO span overflows the 32-bit address space");
        return 0;
    }

    native_base = NULL;
    result = porpoise_libporpoise_decode_span(
        context, guest_base, (size_t)size, &native_base);
    if (result != PORPOISE_HOST_OK) {
        porpoise_libporpoise_set_host_fault(state, result, guest_base);
        return 0;
    }
    if (((uintptr_t)native_base & (uintptr_t)31U) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_base,
            "GXInit decoded host FIFO base is not 32-byte aligned");
        return 0;
    }

    /* Native GXInit is not transactional. Poison before entering it so a
     * null result, token failure, reentrant call, or other incomplete return
     * can never make a second native initialization attempt. */
    porpoise_libporpoise_gx_init_lifecycle =
        PORPOISE_LIBPORPOISE_GX_INIT_POISONED;
    porpoise_libporpoise_gx_owner_context = context;
    *native_base_out = native_base;
    return 1;
}

int porpoise_libporpoise_gx_init_commit(
    PorpoisePpcState *state,
    const void *native_fifo,
    uint32_t *guest_token_out)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseHostResult result;
    uint32_t token;
    uint32_t guest_base;

    if (guest_token_out != NULL) {
        *guest_token_out = 0U;
    }
    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return 0;
    }
    guest_base = state->gpr[3];
    if (guest_token_out == NULL ||
        porpoise_libporpoise_gx_init_lifecycle !=
            PORPOISE_LIBPORPOISE_GX_INIT_POISONED ||
        porpoise_libporpoise_gx_owner_context != context) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            guest_base,
            "GXInit adapter commit has no matching poisoned transaction");
        return 0;
    }
    if (native_fifo == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_HOST_IO,
            guest_base,
            "native GXInit returned NULL after initialization began");
        return 0;
    }

    token = 0U;
    result = porpoise_libporpoise_encode_pointer(
        context, native_fifo, &token);
    if (result != PORPOISE_HOST_OK) {
        porpoise_libporpoise_set_host_fault(state, result, guest_base);
        return 0;
    }
    if (!__OSHostIsAddressToken((u32)token) ||
        !porpoise_libporpoise_owns_token(context, (u32)token)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_base,
            "native GXInit result did not encode as an owned opaque host token");
        return 0;
    }

    context->gx_fifo_token = (u32)token;
    porpoise_libporpoise_gx_init_lifecycle =
        PORPOISE_LIBPORPOISE_GX_INIT_ACTIVE;
    *guest_token_out = token;
    return 1;
}

int porpoise_libporpoise_gx_require_active(PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context =
        porpoise_libporpoise_context_for_state(state);

    if (context == NULL) {
        return 0;
    }
    if (porpoise_libporpoise_gx_init_lifecycle !=
            PORPOISE_LIBPORPOISE_GX_INIT_ACTIVE ||
        porpoise_libporpoise_gx_owner_context != context) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "native GX is not active for this adapter context");
        return 0;
    }
    return 1;
}

int porpoise_libporpoise_gx_decode_span(
    PorpoisePpcState *state,
    uint32_t guest_address,
    size_t size,
    size_t alignment,
    void **native_pointer_out,
    const char *null_description)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseHostResult result;
    void *native_pointer;

    if (native_pointer_out != NULL) {
        *native_pointer_out = NULL;
    }
    if (!porpoise_libporpoise_gx_require_active(state)) {
        return 0;
    }
    if (native_pointer_out == NULL || null_description == NULL || size == 0U ||
        alignment == 0U || (alignment & (alignment - 1U)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "invalid private GX span-decoding request");
        return 0;
    }
    if (guest_address == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_address,
            null_description);
        return 0;
    }
    if (((size_t)guest_address & (alignment - 1U)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_address,
            "guest GX span does not meet its required alignment");
        return 0;
    }

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return 0;
    }
    native_pointer = NULL;
    result = porpoise_libporpoise_decode_span(
        context, guest_address, size, &native_pointer);
    if (result != PORPOISE_HOST_OK) {
        porpoise_libporpoise_set_host_fault(state, result, guest_address);
        return 0;
    }
    if (((uintptr_t)native_pointer & (alignment - 1U)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_address,
            "decoded host GX span does not meet its required alignment");
        return 0;
    }
    *native_pointer_out = native_pointer;
    return 1;
}

int porpoise_libporpoise_gx_read_span(
    PorpoisePpcState *state,
    uint32_t guest_address,
    size_t size,
    size_t alignment,
    void *destination,
    const char *null_description)
{
    void *native_pointer;

    if (destination == NULL) {
        if (state != NULL && !porpoise_state_should_stop(state)) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_INVALID_STATE,
                state->pc,
                "private GX span read has no destination");
        }
        return 0;
    }
    native_pointer = NULL;
    if (!porpoise_libporpoise_gx_decode_span(
            state,
            guest_address,
            size,
            alignment,
            &native_pointer,
            null_description)) {
        return 0;
    }
    memcpy(destination, native_pointer, size);
    return 1;
}

int porpoise_libporpoise_gx_decode_mapped_tail(
    PorpoisePpcState *state,
    uint32_t guest_address,
    void **native_pointer_out,
    uint32_t *size_out)
{
    const OSHostMemoryLayout *layout;
    void *first_pointer;
    uint32_t offset;
    uint32_t size;

    if (native_pointer_out != NULL) {
        *native_pointer_out = NULL;
    }
    if (size_out != NULL) {
        *size_out = 0U;
    }
    if (!porpoise_libporpoise_gx_require_active(state)) {
        return 0;
    }
    if (native_pointer_out == NULL || size_out == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "invalid private GX mapped-tail request");
        return 0;
    }

    /* Preserve decode_span's precise MMIO/token/unmapped fault classes before
     * the alias-to-layout calculation below. */
    first_pointer = NULL;
    if (!porpoise_libporpoise_gx_decode_span(
            state,
            guest_address,
            1U,
            1U,
            &first_pointer,
            "guest GX mapped-tail address is NULL")) {
        return 0;
    }

    layout = __OSHostMemoryGetLayout();
    if (layout == NULL || layout->consoleSize == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            guest_address,
            "libPorpoise has no console-visible memory layout");
        return 0;
    }
    if (guest_address < layout->consoleSize) {
        offset = guest_address;
    } else if (guest_address >= UINT32_C(0x80000000) &&
               guest_address - UINT32_C(0x80000000) <
                   layout->consoleSize) {
        offset = guest_address - UINT32_C(0x80000000);
    } else if (guest_address >= UINT32_C(0xC0000000) &&
               guest_address - UINT32_C(0xC0000000) <
                   layout->consoleSize) {
        offset = guest_address - UINT32_C(0xC0000000);
    } else {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_UNMAPPED_ADDRESS,
            guest_address,
            "guest GX address is outside console-visible main RAM");
        return 0;
    }
    size = layout->consoleSize - offset;
    if (!porpoise_libporpoise_gx_decode_span(
            state,
            guest_address,
            (size_t)size,
            1U,
            native_pointer_out,
            "guest GX mapped-tail address is NULL")) {
        return 0;
    }
    *size_out = size;
    return 1;
}

static void porpoise_libporpoise_record_gx_draw_done_failure(
    PorpoiseLibporpoiseContext *context,
    PorpoiseFault fault,
    uint32_t fault_address,
    const char *message)
{
    if (context == NULL || context->gx_draw_done_callback_failed) {
        return;
    }
    if (fault == PORPOISE_FAULT_NONE) {
        fault = PORPOISE_FAULT_INVALID_STATE;
    }
    context->gx_draw_done_callback_failed = 1;
    context->gx_draw_done_callback_fault = fault;
    context->gx_draw_done_callback_fault_address = fault_address;
    (void)snprintf(
        context->gx_draw_done_callback_fault_message,
        sizeof(context->gx_draw_done_callback_fault_message),
        "%s",
        message != NULL ? message : porpoise_fault_string(fault));
}

static void porpoise_libporpoise_gx_draw_done_callback(void)
{
    PorpoiseLibporpoiseContext *context;
    BOOL interrupts_were_enabled;
    size_t event_index;

    interrupts_were_enabled = OSDisableInterrupts();
    context = porpoise_libporpoise_active_context;
    if (context == NULL ||
        context != porpoise_libporpoise_gx_owner_context ||
        porpoise_libporpoise_gx_init_lifecycle !=
            PORPOISE_LIBPORPOISE_GX_INIT_ACTIVE) {
        OSRestoreInterrupts(interrupts_were_enabled);
        return;
    }
    if (context->gx_draw_done_callback == 0U) {
        porpoise_libporpoise_record_gx_draw_done_failure(
            context,
            PORPOISE_FAULT_INVALID_STATE,
            0U,
            "native GX invoked an unregistered draw-done callback");
        OSRestoreInterrupts(interrupts_were_enabled);
        return;
    }
    if (context->gx_draw_done_event_count >=
        PORPOISE_GX_DRAW_DONE_EVENT_CAPACITY) {
        porpoise_libporpoise_record_gx_draw_done_failure(
            context,
            PORPOISE_FAULT_HOST_IO,
            context->gx_draw_done_callback,
            "native GX draw-done event queue overflowed");
        OSRestoreInterrupts(interrupts_were_enabled);
        return;
    }
    event_index =
        (context->gx_draw_done_event_head +
         context->gx_draw_done_event_count) %
        PORPOISE_GX_DRAW_DONE_EVENT_CAPACITY;
    context->gx_draw_done_events[event_index] =
        context->gx_draw_done_callback;
    context->gx_draw_done_event_count++;
    OSRestoreInterrupts(interrupts_were_enabled);
}

int porpoise_libporpoise_gx_complete_draw(PorpoisePpcState *state)
{
    static const u8 draw_done_command[] = {
        UINT8_C(0x61),
        UINT8_C(0x45),
        UINT8_C(0x00),
        UINT8_C(0x00),
        UINT8_C(0x02)
    };
    PorpoiseLibporpoiseContext *context;

    if (!porpoise_libporpoise_gx_require_active(state)) {
        return 0;
    }
    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return 0;
    }

    /*
     * GXDrawDone normally writes the PE-finish BP command and then GXFlushes
     * native SDK dirty state. The command processor consumes canonical bytes
     * synchronously, and its 0x45 handler waits for host GPU completion. Send
     * only that exact finish command so native dirty VCD/VAT state cannot
     * replace the lifted guest's active vertex layout.
     */
#if defined(SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION) && \
    SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION >= 1
    if (!SIM_GX_CommandProcessor_SendCanonicalBytes(
            draw_done_command, (u32)sizeof(draw_done_command))) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_HOST_IO,
            state->pc,
            "libPorpoise rejected the canonical GX draw-done command");
        return 0;
    }
#else
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        state->pc,
        "GXDrawDone requires libPorpoise canonical FIFO bytes API v1");
    return 0;
#endif

    /* The command-processor finish handler waits synchronously but does not
     * invoke the SDK draw-done callback. Mirror that event explicitly while
     * retaining the guest callback address registered at completion time. */
    if (context->gx_draw_done_callback != 0U) {
        porpoise_libporpoise_gx_draw_done_callback();
    }
    return 1;
}

int porpoise_libporpoise_gx_set_draw_done_callback(
    PorpoisePpcState *state,
    uint32_t guest_callback,
    uint32_t *previous_guest_callback_out)
{
    PorpoiseLibporpoiseContext *context;
    GXDrawDoneCallback expected_previous_native;
    GXDrawDoneCallback new_native;
    GXDrawDoneCallback previous_native;
    GXDrawDoneCallback rollback_result;
    uint32_t previous_guest_callback;
    BOOL interrupts_were_enabled;

    if (previous_guest_callback_out != NULL) {
        *previous_guest_callback_out = 0U;
    }
    if (!porpoise_libporpoise_gx_require_active(state)) {
        return 0;
    }
    if (previous_guest_callback_out == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "GX draw-done callback transaction has no result output");
        return 0;
    }
    if (guest_callback != 0U &&
        (guest_callback & UINT32_C(3)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_callback,
            "guest GX draw-done callback is not four-byte aligned");
        return 0;
    }
    if (guest_callback != 0U &&
        (state->host->call_guest == NULL ||
         state->host->poll_events != porpoise_libporpoise_poll_events)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            guest_callback,
            "GX draw-done callback requires guest dispatch and event polling");
        return 0;
    }
    if (guest_callback != 0U &&
        !porpoise_dispatch_available(guest_callback)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            guest_callback,
            "guest GX draw-done callback is not present in generated dispatch");
        return 0;
    }

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return 0;
    }
    interrupts_were_enabled = OSDisableInterrupts();
    previous_guest_callback = context->gx_draw_done_callback;
    expected_previous_native = previous_guest_callback != 0U
                                   ? porpoise_libporpoise_gx_draw_done_callback
                                   : NULL;
    new_native = guest_callback != 0U
                     ? porpoise_libporpoise_gx_draw_done_callback
                     : NULL;
    previous_native = GXSetDrawDoneCallback(new_native);
    if (previous_native != expected_previous_native) {
        rollback_result = GXSetDrawDoneCallback(previous_native);
        OSRestoreInterrupts(interrupts_were_enabled);
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            guest_callback,
            rollback_result == new_native
                ? "native GX draw-done callback state diverged from its guest mirror"
                : "native GX draw-done callback state diverged and rollback failed");
        return 0;
    }

    context->gx_draw_done_callback = guest_callback;
    *previous_guest_callback_out = previous_guest_callback;
    OSRestoreInterrupts(interrupts_were_enabled);
    return 1;
}

int porpoise_libporpoise_gx_record_disp_copy_destination(
    PorpoisePpcState *state,
    uint16_t width,
    uint16_t height)
{
    PorpoiseLibporpoiseContext *context;

    if (!porpoise_libporpoise_gx_require_active(state)) {
        return 0;
    }
    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return 0;
    }
    context->gx_disp_copy_destination.width = width;
    context->gx_disp_copy_destination.height = height;
    context->gx_disp_copy_destination.format = 0U;
    context->gx_disp_copy_destination.mipmap = 0U;
    context->gx_disp_copy_destination_configured = 1;
    return 1;
}

int porpoise_libporpoise_gx_record_tex_copy_destination(
    PorpoisePpcState *state,
    uint16_t width,
    uint16_t height,
    uint32_t format,
    uint8_t mipmap)
{
    PorpoiseLibporpoiseContext *context;

    if (!porpoise_libporpoise_gx_require_active(state)) {
        return 0;
    }
    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return 0;
    }
    context->gx_tex_copy_destination.width = width;
    context->gx_tex_copy_destination.height = height;
    context->gx_tex_copy_destination.format = format;
    context->gx_tex_copy_destination.mipmap = mipmap;
    context->gx_tex_copy_destination_configured = 1;
    return 1;
}

static int porpoise_libporpoise_gx_get_copy_destination(
    PorpoisePpcState *state,
    const PorpoiseLibporpoiseGxCopyDestination *destination,
    int configured,
    const char *message,
    PorpoiseLibporpoiseGxCopyDestination *destination_out)
{
    if (!porpoise_libporpoise_gx_require_active(state)) {
        return 0;
    }
    if (destination_out == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "GX copy-destination query has no output");
        return 0;
    }
    if (!configured) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            message);
        return 0;
    }
    *destination_out = *destination;
    return 1;
}

int porpoise_libporpoise_gx_get_disp_copy_destination(
    PorpoisePpcState *state,
    PorpoiseLibporpoiseGxCopyDestination *destination_out)
{
    PorpoiseLibporpoiseContext *context;

    if (!porpoise_libporpoise_gx_require_active(state)) {
        return 0;
    }
    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return 0;
    }
    return porpoise_libporpoise_gx_get_copy_destination(
        state,
        &context->gx_disp_copy_destination,
        context->gx_disp_copy_destination_configured,
        "GXCopyDisp requires a preceding GXSetDispCopyDst",
        destination_out);
}

int porpoise_libporpoise_gx_get_tex_copy_destination(
    PorpoisePpcState *state,
    PorpoiseLibporpoiseGxCopyDestination *destination_out)
{
    PorpoiseLibporpoiseContext *context;

    if (!porpoise_libporpoise_gx_require_active(state)) {
        return 0;
    }
    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return 0;
    }
    return porpoise_libporpoise_gx_get_copy_destination(
        state,
        &context->gx_tex_copy_destination,
        context->gx_tex_copy_destination_configured,
        "GXCopyTex requires a preceding GXSetTexCopyDst",
        destination_out);
}

static int porpoise_libporpoise_adapter_is_active(
    const PorpoiseHostAdapter *adapter)
{
    const PorpoiseLibporpoiseContext *context;

    if (adapter == NULL || adapter->context == NULL) {
        return 0;
    }
    context = (const PorpoiseLibporpoiseContext *)adapter->context;
    return adapter->read_bytes == porpoise_libporpoise_read_bytes &&
           adapter->write_bytes == porpoise_libporpoise_write_bytes &&
           adapter->decode_pointer == porpoise_libporpoise_decode_pointer &&
           adapter->encode_pointer == porpoise_libporpoise_encode_pointer &&
           context == porpoise_libporpoise_active_context &&
           context->owner_adapter == adapter;
}

static int porpoise_libporpoise_arena_native_pointer(
    const PorpoiseLibporpoiseContext *context,
    uint32_t guest_address,
    void **pointer_out)
{
    uintptr_t native_base;
    uintptr_t offset;

    if (context == NULL || pointer_out == NULL ||
        !context->arena_configured ||
        guest_address < context->arena_configured_base ||
        guest_address > context->arena_configured_limit) {
        return 0;
    }
    native_base = (uintptr_t)context->arena_native_base;
    offset = (uintptr_t)(
        guest_address - context->arena_configured_base);
    if (native_base > UINTPTR_MAX - offset) {
        return 0;
    }
    *pointer_out = (void *)(native_base + offset);
    return 1;
}

static int porpoise_libporpoise_restore_native_arena(
    PorpoiseLibporpoiseContext *context)
{
    if (context == NULL || !context->arena_restore_pending) {
        return 1;
    }

    OSSetArenaLo(context->arena_previous_native_lo);
    OSSetArenaHi(context->arena_previous_native_hi);
    if (OSGetArenaLo() != context->arena_previous_native_lo ||
        OSGetArenaHi() != context->arena_previous_native_hi) {
        return 0;
    }

    context->arena_restore_pending = 0;
    return 1;
}

int porpoise_libporpoise_arena_snapshot(
    PorpoisePpcState *state,
    PorpoiseLibporpoiseArenaSnapshot *snapshot_out)
{
    PorpoiseLibporpoiseContext *context;
    void *expected_lo;
    void *expected_hi;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return 0;
    }
    if (snapshot_out == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "guest arena snapshot output is NULL");
        return 0;
    }
    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return 0;
    }
    if (!context->arena_configured) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "guest arena bounds were not configured");
        return 0;
    }
    if (!porpoise_libporpoise_arena_native_pointer(
            context, context->arena_guest_lo, &expected_lo) ||
        !porpoise_libporpoise_arena_native_pointer(
            context, context->arena_guest_hi, &expected_hi)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "guest arena mirror cannot represent its native bounds");
        return 0;
    }
    if (OSGetArenaLo() != expected_lo || OSGetArenaHi() != expected_hi) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "native libPorpoise arena diverged from the guest arena mirror");
        return 0;
    }

    snapshot_out->configured_base = context->arena_configured_base;
    snapshot_out->configured_limit = context->arena_configured_limit;
    snapshot_out->lo = context->arena_guest_lo;
    snapshot_out->hi = context->arena_guest_hi;
    return 1;
}

int porpoise_libporpoise_arena_commit(
    PorpoisePpcState *state,
    const PorpoiseLibporpoiseArenaSnapshot *expected,
    uint32_t new_lo,
    uint32_t new_hi)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseLibporpoiseArenaSnapshot current;
    void *native_lo;
    void *native_hi;
    void *previous_lo;
    void *previous_hi;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return 0;
    }
    if (expected == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->pc,
            "guest arena transaction has no expected snapshot");
        return 0;
    }
    if (!porpoise_libporpoise_arena_snapshot(state, &current)) {
        return 0;
    }
    if (current.configured_base != expected->configured_base ||
        current.configured_limit != expected->configured_limit ||
        current.lo != expected->lo || current.hi != expected->hi) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "guest arena changed during an adapter transaction");
        return 0;
    }
    if (new_lo < current.configured_base ||
        new_hi > current.configured_limit || new_lo > new_hi) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            new_lo > new_hi ? new_lo : new_hi,
            "guest arena transaction leaves its configured address span");
        return 0;
    }

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL ||
        !porpoise_libporpoise_arena_native_pointer(
            context, new_lo, &native_lo) ||
        !porpoise_libporpoise_arena_native_pointer(
            context, new_hi, &native_hi)) {
        if (!porpoise_state_has_fault(state)) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_INVALID_STATE,
                state->pc,
                "guest arena transaction cannot map its native bounds");
        }
        return 0;
    }

    previous_lo = OSGetArenaLo();
    previous_hi = OSGetArenaHi();
    if (new_lo != current.lo) {
        OSSetArenaLo(native_lo);
    }
    if (new_hi != current.hi) {
        OSSetArenaHi(native_hi);
    }
    if (OSGetArenaLo() != native_lo || OSGetArenaHi() != native_hi) {
        int rollback_succeeded;

        OSSetArenaLo(previous_lo);
        OSSetArenaHi(previous_hi);
        rollback_succeeded = OSGetArenaLo() == previous_lo &&
                             OSGetArenaHi() == previous_hi;
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            rollback_succeeded
                ? "native libPorpoise rejected a guest arena transaction"
                : "native libPorpoise rejected a guest arena transaction and rollback failed");
        return 0;
    }

    context->arena_guest_lo = new_lo;
    context->arena_guest_hi = new_hi;
    return 1;
}

static PorpoiseHostResult porpoise_libporpoise_bind_guest_runtime_internal(
    PorpoiseHostAdapter *adapter,
    PorpoiseHostCallGuestFn dispatcher,
    PorpoiseBindExportStateFn export_state_binder,
    int require_export_state_binder)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseHostResult result;
    BOOL interrupts_were_enabled;

    if (adapter == NULL || dispatcher == NULL ||
        (require_export_state_binder && export_state_binder == NULL) ||
        dispatcher == porpoise_libporpoise_run_guest) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }

    interrupts_were_enabled = OSDisableInterrupts();
    if (!porpoise_libporpoise_adapter_is_active(adapter)) {
        result = PORPOISE_HOST_INVALID_ARGUMENT;
    } else {
        context = (PorpoiseLibporpoiseContext *)adapter->context;
        if ((context->guest_dispatch != NULL &&
             context->guest_dispatch != dispatcher) ||
            (adapter->call_guest != NULL &&
             adapter->call_guest != porpoise_libporpoise_run_guest) ||
            !porpoise_libporpoise_thread_registry_set_export_binder(
                context->thread_registry,
                export_state_binder)) {
            result = PORPOISE_HOST_INVALID_ARGUMENT;
        } else {
            context->guest_dispatch = dispatcher;
            adapter->call_guest = porpoise_libporpoise_run_guest;
            result = PORPOISE_HOST_OK;
        }
    }
    OSRestoreInterrupts(interrupts_were_enabled);
    return result;
}

PorpoiseHostResult porpoise_libporpoise_bind_guest_dispatch(
    PorpoiseHostAdapter *adapter,
    PorpoiseHostCallGuestFn dispatcher)
{
    return porpoise_libporpoise_bind_guest_runtime_internal(
        adapter, dispatcher, NULL, 0);
}

PorpoiseHostResult porpoise_libporpoise_bind_guest_runtime(
    PorpoiseHostAdapter *adapter,
    PorpoiseHostCallGuestFn dispatcher,
    PorpoiseBindExportStateFn export_state_binder)
{
    return porpoise_libporpoise_bind_guest_runtime_internal(
        adapter, dispatcher, export_state_binder, 1);
}

PorpoiseHostResult porpoise_libporpoise_bind_guest_sdk_layout_v1(
    PorpoiseHostAdapter *adapter,
    const PorpoiseLibporpoiseGuestSdkLayoutV1 *layout)
{
    PorpoiseLibporpoiseContext *context;
    const uint32_t addresses[] = {
        layout != NULL ? layout->os_arena_lo_address : 0U,
        layout != NULL ? layout->os_arena_hi_address : 0U,
        layout != NULL ? layout->os_initialized_address : 0U,
        layout != NULL ? layout->os_boot_info_address : 0U,
        layout != NULL ? layout->os_bi2_debug_flag_address : 0U,
        layout != NULL ? layout->dvd_long_file_name_flag_address : 0U
    };
    size_t index;
    BOOL interrupts_were_enabled;
    PorpoiseHostResult result = PORPOISE_HOST_OK;

    if (adapter == NULL || layout == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    for (index = 0U;
         index < sizeof(addresses) / sizeof(addresses[0]);
         index++) {
        size_t earlier;
        if (addresses[index] == 0U ||
            (addresses[index] & UINT32_C(3)) != 0U) {
            return PORPOISE_HOST_INVALID_ARGUMENT;
        }
        for (earlier = 0U; earlier < index; earlier++) {
            if (addresses[index] == addresses[earlier]) {
                return PORPOISE_HOST_INVALID_ARGUMENT;
            }
        }
    }

    interrupts_were_enabled = OSDisableInterrupts();
    if (!porpoise_libporpoise_adapter_is_active(adapter)) {
        result = PORPOISE_HOST_INVALID_ARGUMENT;
    } else {
        context = (PorpoiseLibporpoiseContext *)adapter->context;
        if (context->guest_sdk_layout_bound) {
            result = memcmp(
                         &context->guest_sdk_layout,
                         layout,
                         sizeof(*layout)) == 0
                         ? PORPOISE_HOST_OK
                         : PORPOISE_HOST_INVALID_ARGUMENT;
        } else {
            for (index = 0U;
                 index < sizeof(addresses) / sizeof(addresses[0]);
                 index++) {
                void *mapped = NULL;
                result = porpoise_libporpoise_decode_span(
                    context, addresses[index], sizeof(uint32_t), &mapped);
                if (result != PORPOISE_HOST_OK) break;
            }
            if (result == PORPOISE_HOST_OK) {
                context->guest_sdk_layout = *layout;
                context->guest_sdk_layout_bound = 1;
            }
        }
    }
    OSRestoreInterrupts(interrupts_were_enabled);
    return result;
}

int porpoise_libporpoise_run_guest(
    PorpoisePpcState *state,
    uint32_t guest_function_address)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseHostCallGuestFn dispatcher;
    BOOL interrupts_were_enabled;
    int dispatched;

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return 0;
    }

    interrupts_were_enabled = OSDisableInterrupts();
    dispatcher = context->guest_dispatch;
    if (!porpoise_libporpoise_adapter_is_active(state->host)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            guest_function_address,
            "guest dispatch is not attached to the active libPorpoise adapter");
        OSRestoreInterrupts(interrupts_were_enabled);
        return 0;
    }
    if (dispatcher == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            guest_function_address,
            "guest address dispatcher has not been bound");
        OSRestoreInterrupts(interrupts_were_enabled);
        return 0;
    }
    if (state->host->call_guest != porpoise_libporpoise_run_guest) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            guest_function_address,
            "active libPorpoise adapter guest dispatcher was replaced");
        OSRestoreInterrupts(interrupts_were_enabled);
        return 0;
    }

    dispatched = dispatcher(state, guest_function_address);
    OSRestoreInterrupts(interrupts_were_enabled);
    return dispatched && !porpoise_state_should_stop(state);
}

static int porpoise_libporpoise_validate_guest_span(
    PorpoisePpcState *state,
    const PorpoiseLibporpoiseContext *context,
    uint32_t guest_address,
    size_t size)
{
    PorpoiseHostResult result;
    void *pointer;

    if (size == 0U) {
        return 1;
    }
    pointer = NULL;
    result = porpoise_libporpoise_decode_span(
        context, guest_address, size, &pointer);
    if (result != PORPOISE_HOST_OK) {
        porpoise_libporpoise_set_host_fault(
            state, result, guest_address);
        return 0;
    }
    return 1;
}

static int porpoise_libporpoise_validate_guest_structure(
    PorpoisePpcState *state,
    const PorpoiseLibporpoiseContext *context,
    uint32_t guest_address,
    size_t size,
    const char *description)
{
    if (guest_address == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_address,
            description);
        return 0;
    }
    if ((guest_address & UINT32_C(3)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_address,
            "guest structure is not four-byte aligned");
        return 0;
    }
    return porpoise_libporpoise_validate_guest_span(
        state, context, guest_address, size);
}

static void porpoise_libporpoise_write_be32(
    uint8_t *destination,
    uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24U);
    destination[1] = (uint8_t)(value >> 16U);
    destination[2] = (uint8_t)(value >> 8U);
    destination[3] = (uint8_t)value;
}

static void porpoise_libporpoise_write_be16(
    uint8_t *destination,
    uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8U);
    destination[1] = (uint8_t)value;
}

static uint16_t porpoise_libporpoise_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8U) |
                      (uint16_t)source[1]);
}

static uint32_t porpoise_libporpoise_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24U) |
           ((uint32_t)source[1] << 16U) |
           ((uint32_t)source[2] << 8U) |
           (uint32_t)source[3];
}

static PorpoiseHostResult
porpoise_libporpoise_initialize_system_call_vector(
    PorpoiseLibporpoiseContext *context)
{
    uint8_t guest_vector[
        sizeof(porpoise_libporpoise_canonical_system_call_vector)];
    size_t word_index;

    if (context == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }

    for (word_index = 0U;
         word_index < PORPOISE_GUEST_SYSTEM_CALL_VECTOR_WORDS;
         word_index++) {
        porpoise_libporpoise_write_be32(
            &guest_vector[word_index * sizeof(uint32_t)],
            porpoise_libporpoise_canonical_system_call_vector[word_index]);
    }
    return porpoise_libporpoise_write_bytes(
        context,
        PORPOISE_GUEST_SYSTEM_CALL_VECTOR,
        guest_vector,
        sizeof(guest_vector));
}

static PorpoiseHostResult porpoise_libporpoise_system_call(
    void *adapter_context,
    PorpoisePpcState *state,
    uint32_t instruction_address)
{
    uint8_t guest_vector[
        sizeof(porpoise_libporpoise_canonical_system_call_vector)];
    PorpoiseHostResult result;
    size_t word_index;

    if (adapter_context == NULL || state == NULL || state->host == NULL ||
        state->host->context != adapter_context ||
        state->host->read_bytes == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }

    result = state->host->read_bytes(
        adapter_context,
        PORPOISE_GUEST_SYSTEM_CALL_VECTOR,
        guest_vector,
        sizeof(guest_vector));
    if (result != PORPOISE_HOST_OK) {
        return result;
    }

    for (word_index = 0U;
         word_index < PORPOISE_GUEST_SYSTEM_CALL_VECTOR_WORDS;
         word_index++) {
        uint32_t actual_word = porpoise_libporpoise_read_be32(
            &guest_vector[word_index * sizeof(uint32_t)]);

        if (actual_word !=
            porpoise_libporpoise_canonical_system_call_vector[word_index]) {
            char fault_message[PORPOISE_FAULT_MESSAGE_CAPACITY];
            uint32_t word_address =
                PORPOISE_GUEST_SYSTEM_CALL_VECTOR +
                (uint32_t)(word_index * sizeof(uint32_t));

            (void)snprintf(
                fault_message,
                sizeof(fault_message),
                "guest system-call vector word %zu at 0x%08" PRIX32
                " is 0x%08" PRIX32 "; expected 0x%08" PRIX32,
                word_index,
                word_address,
                actual_word,
                porpoise_libporpoise_canonical_system_call_vector[
                    word_index]);
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_UNSUPPORTED_OPERATION,
                instruction_address,
                fault_message);
            return PORPOISE_HOST_OK;
        }
    }

    /* Lifted guest memory is already coherent host memory. The validated SDK
     * vector's HID0/cache barriers therefore have no additional host effect. */
    return PORPOISE_HOST_OK;
}

static uint64_t porpoise_libporpoise_read_be64(const uint8_t *source)
{
    return ((uint64_t)porpoise_libporpoise_read_be32(source) << 32U) |
           (uint64_t)porpoise_libporpoise_read_be32(source + 4U);
}

static PorpoiseDspTaskMirror *porpoise_libporpoise_find_dsp_guest_task(
    PorpoiseLibporpoiseContext *context,
    uint32_t guest_task)
{
    PorpoiseDspTaskMirror *mirror;

    if (context == NULL) {
        return NULL;
    }
    for (mirror = context->dsp_tasks;
         mirror != NULL;
         mirror = mirror->next) {
        if (mirror->guest_task == guest_task) {
            return mirror;
        }
    }
    return NULL;
}

static PorpoiseDspTaskMirror *porpoise_libporpoise_find_dsp_native_task(
    PorpoiseLibporpoiseContext *context,
    const DSPTaskInfo *native_task)
{
    PorpoiseDspTaskMirror *mirror;

    if (context == NULL || native_task == NULL) {
        return NULL;
    }
    for (mirror = context->dsp_tasks;
         mirror != NULL;
         mirror = mirror->next) {
        if (&mirror->native_task == native_task) {
            return mirror;
        }
    }
    return NULL;
}

static void porpoise_libporpoise_record_dsp_callback_failure(
    PorpoiseLibporpoiseContext *context,
    PorpoiseFault fault,
    uint32_t fault_address,
    const char *message)
{
    const char *fault_message;

    if (context == NULL || context->dsp_callback_failed) {
        return;
    }
    if (fault == PORPOISE_FAULT_NONE) {
        fault = PORPOISE_FAULT_INVALID_STATE;
    }
    fault_message = message != NULL ? message : porpoise_fault_string(fault);
    context->dsp_callback_failed = 1;
    context->dsp_callback_fault = fault;
    context->dsp_callback_fault_address = fault_address;
    (void)snprintf(
        context->dsp_callback_fault_message,
        sizeof(context->dsp_callback_fault_message),
        "%s",
        fault_message);
}

static void porpoise_libporpoise_record_dsp_host_failure(
    PorpoiseLibporpoiseContext *context,
    PorpoiseHostResult result,
    uint32_t fault_address)
{
    porpoise_libporpoise_record_dsp_callback_failure(
        context,
        porpoise_libporpoise_fault_from_host_result(result),
        fault_address,
        porpoise_host_result_string(result));
}

static int porpoise_libporpoise_dsp_native_link_to_guest(
    PorpoiseLibporpoiseContext *context,
    const DSPTaskInfo *native_task,
    uint32_t *guest_task_out)
{
    PorpoiseDspTaskMirror *mirror;

    if (guest_task_out == NULL) {
        return 0;
    }
    *guest_task_out = 0U;
    if (native_task == NULL) {
        return 1;
    }
    mirror = porpoise_libporpoise_find_dsp_native_task(
        context, native_task);
    if (mirror == NULL) {
        return 0;
    }
    *guest_task_out = mirror->guest_task;
    return 1;
}

static int porpoise_libporpoise_write_dsp_guest_word(
    PorpoiseLibporpoiseContext *context,
    uint32_t guest_address,
    uint32_t value)
{
    uint8_t bytes[4];
    PorpoiseHostResult result;

    porpoise_libporpoise_write_be32(bytes, value);
    result = porpoise_libporpoise_write_bytes(
        context, guest_address, bytes, sizeof(bytes));
    if (result != PORPOISE_HOST_OK) {
        porpoise_libporpoise_record_dsp_host_failure(
            context, result, guest_address);
        return 0;
    }
    return 1;
}

static int porpoise_libporpoise_sync_dsp_task(
    PorpoiseLibporpoiseContext *context,
    PorpoiseDspTaskMirror *mirror)
{
    uint32_t guest_next;
    uint32_t guest_previous;
    int synchronized;

    if (context == NULL || mirror == NULL) {
        porpoise_libporpoise_record_dsp_callback_failure(
            context,
            PORPOISE_FAULT_INVALID_STATE,
            0U,
            "native DSP callback has no task mirror");
        return 0;
    }
    if (!porpoise_libporpoise_dsp_native_link_to_guest(
            context, mirror->native_task.next, &guest_next) ||
        !porpoise_libporpoise_dsp_native_link_to_guest(
            context, mirror->native_task.prev, &guest_previous)) {
        porpoise_libporpoise_record_dsp_callback_failure(
            context,
            PORPOISE_FAULT_INVALID_STATE,
            mirror->guest_task,
            "native DSP queue contains an unknown task pointer");
        return 0;
    }

    synchronized = 1;
    synchronized &= porpoise_libporpoise_write_dsp_guest_word(
        context,
        mirror->guest_task + PORPOISE_GUEST_DSP_STATE_OFFSET,
        (uint32_t)mirror->native_task.state);
    synchronized &= porpoise_libporpoise_write_dsp_guest_word(
        context,
        mirror->guest_task + PORPOISE_GUEST_DSP_FLAGS_OFFSET,
        (uint32_t)mirror->native_task.flags);
    synchronized &= porpoise_libporpoise_write_dsp_guest_word(
        context,
        mirror->guest_task + PORPOISE_GUEST_DSP_NEXT_OFFSET,
        guest_next);
    synchronized &= porpoise_libporpoise_write_dsp_guest_word(
        context,
        mirror->guest_task + PORPOISE_GUEST_DSP_PREVIOUS_OFFSET,
        guest_previous);
    return synchronized;
}

static int porpoise_libporpoise_sync_all_dsp_tasks(
    PorpoiseLibporpoiseContext *context)
{
    PorpoiseDspTaskMirror *mirror;
    int synchronized = 1;

    if (context == NULL) {
        return 0;
    }
    for (mirror = context->dsp_tasks;
         mirror != NULL;
         mirror = mirror->next) {
        if (!porpoise_libporpoise_sync_dsp_task(context, mirror)) {
            synchronized = 0;
        }
    }
    return synchronized;
}

static void porpoise_libporpoise_remove_dsp_mirror(
    PorpoiseLibporpoiseContext *context,
    PorpoiseDspTaskMirror *mirror)
{
    PorpoiseDspTaskMirror **link;

    if (context == NULL || mirror == NULL) {
        return;
    }
    link = &context->dsp_tasks;
    while (*link != NULL && *link != mirror) {
        link = &(*link)->next;
    }
    if (*link == mirror) {
        *link = mirror->next;
        free(mirror);
    }
}

static void porpoise_libporpoise_collect_completed_dsp_tasks(
    PorpoiseLibporpoiseContext *context)
{
    PorpoiseDspTaskMirror **link;

    if (context == NULL || context->dsp_native_call_depth != 0U) {
        return;
    }
    link = &context->dsp_tasks;
    while (*link != NULL) {
        PorpoiseDspTaskMirror *mirror = *link;
        if (mirror->native_task.state == DSP_TASK_STATE_DONE &&
            mirror->native_task.flags == DSP_TASK_FLAG_CLEARALL &&
            mirror->native_task.next == NULL &&
            mirror->native_task.prev == NULL) {
            *link = mirror->next;
            free(mirror);
        } else {
            link = &mirror->next;
        }
    }
}

static void porpoise_libporpoise_free_all_dsp_tasks(
    PorpoiseLibporpoiseContext *context)
{
    PorpoiseDspTaskMirror *mirror;

    if (context == NULL) {
        return;
    }
    mirror = context->dsp_tasks;
    while (mirror != NULL) {
        PorpoiseDspTaskMirror *next = mirror->next;
        free(mirror);
        mirror = next;
    }
    context->dsp_tasks = NULL;
}

static int porpoise_libporpoise_read_dsp_callback_address(
    PorpoiseLibporpoiseContext *context,
    const PorpoiseDspTaskMirror *mirror,
    uint32_t callback_offset,
    uint32_t *callback_out)
{
    PorpoiseHostResult result;
    uint8_t bytes[4];
    uint32_t address;

    if (context == NULL || mirror == NULL || callback_out == NULL) {
        porpoise_libporpoise_record_dsp_callback_failure(
            context,
            PORPOISE_FAULT_INVALID_STATE,
            0U,
            "native DSP callback has no task mirror");
        return 0;
    }
    address = mirror->guest_task + callback_offset;
    result = porpoise_libporpoise_read_bytes(
        context, address, bytes, sizeof(bytes));
    if (result != PORPOISE_HOST_OK) {
        porpoise_libporpoise_record_dsp_host_failure(
            context, result, address);
        return 0;
    }
    *callback_out = porpoise_libporpoise_read_be32(bytes);
    return 1;
}

static void porpoise_libporpoise_dsp_callback(
    void *native_task_pointer,
    uint32_t callback_offset)
{
    PorpoiseLibporpoiseContext *context =
        porpoise_libporpoise_active_context;
    DSPTaskInfo *native_task = (DSPTaskInfo *)native_task_pointer;
    PorpoiseDspTaskMirror *mirror;
    PorpoisePpcState callback_state;
    PorpoiseHostCallGuestFn call_guest;
    uint32_t guest_callback;
    int dispatched;

    if (context == NULL || context->dsp_callback_failed) {
        return;
    }
    mirror = porpoise_libporpoise_find_dsp_native_task(
        context, native_task);
    if (mirror == NULL) {
        porpoise_libporpoise_record_dsp_callback_failure(
            context,
            PORPOISE_FAULT_INVALID_STATE,
            0U,
            "libPorpoise invoked DSP callback for an unknown task");
        return;
    }

    (void)porpoise_libporpoise_sync_all_dsp_tasks(context);
    if (context->dsp_callback_failed ||
        !porpoise_libporpoise_read_dsp_callback_address(
            context, mirror, callback_offset, &guest_callback)) {
        return;
    }
    if (guest_callback == 0U) {
        return;
    }
    if ((guest_callback & UINT32_C(3)) != 0U) {
        porpoise_libporpoise_record_dsp_callback_failure(
            context,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_callback,
            "guest DSP callback address is not four-byte aligned");
        return;
    }
    if (!porpoise_dispatch_available(guest_callback)) {
        porpoise_libporpoise_record_dsp_callback_failure(
            context,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            guest_callback,
            "guest DSP callback address is not present in generated dispatch");
        return;
    }

    callback_state = mirror->submission_state;
    call_guest = callback_state.host != NULL
                     ? callback_state.host->call_guest
                     : NULL;
    if (call_guest == NULL) {
        porpoise_libporpoise_record_dsp_callback_failure(
            context,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            guest_callback,
            "DSP callback requires guest address dispatch");
        return;
    }
    if (callback_state.host_event_delivery_depth == UINT32_MAX) {
        porpoise_libporpoise_record_dsp_callback_failure(
            context,
            PORPOISE_FAULT_INVALID_STATE,
            guest_callback,
            "DSP callback event-delivery depth overflow");
        return;
    }
    callback_state.host_event_delivery_depth++;
    callback_state.gpr[3] = mirror->guest_task;
    dispatched = call_guest(&callback_state, guest_callback);
    if (!dispatched || porpoise_state_should_stop(&callback_state)) {
        if (callback_state.fault != PORPOISE_FAULT_NONE) {
            porpoise_libporpoise_record_dsp_callback_failure(
                context,
                callback_state.fault,
                callback_state.fault_address,
                porpoise_state_fault_message(&callback_state));
        } else {
            porpoise_libporpoise_record_dsp_callback_failure(
                context,
                PORPOISE_FAULT_INVALID_STATE,
                guest_callback,
                "guest DSP callback dispatch did not complete normally");
        }
    }
}

static void porpoise_libporpoise_dsp_init_callback(void *native_task)
{
    porpoise_libporpoise_dsp_callback(
        native_task, PORPOISE_GUEST_DSP_INIT_CALLBACK_OFFSET);
}

static void porpoise_libporpoise_dsp_resume_callback(void *native_task)
{
    porpoise_libporpoise_dsp_callback(
        native_task, PORPOISE_GUEST_DSP_RESUME_CALLBACK_OFFSET);
}

static void porpoise_libporpoise_dsp_done_callback(void *native_task)
{
    porpoise_libporpoise_dsp_callback(
        native_task, PORPOISE_GUEST_DSP_DONE_CALLBACK_OFFSET);
}

static void porpoise_libporpoise_dsp_request_callback(void *native_task)
{
    porpoise_libporpoise_dsp_callback(
        native_task, PORPOISE_GUEST_DSP_REQUEST_CALLBACK_OFFSET);
}

static int porpoise_libporpoise_validate_dsp_callback(
    PorpoisePpcState *state,
    uint32_t guest_callback)
{
    if (guest_callback == 0U) {
        return 1;
    }
    if ((guest_callback & UINT32_C(3)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_callback,
            "guest DSP callback address is not four-byte aligned");
        return 0;
    }
    if (state->host->call_guest == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            guest_callback,
            "DSP callback requires guest address dispatch");
        return 0;
    }
    if (!porpoise_dispatch_available(guest_callback)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            guest_callback,
            "guest DSP callback address is not present in generated dispatch");
        return 0;
    }
    return 1;
}

void porpoise_libporpoise_dsp_add_task_adapter(PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseDspTaskMirror *mirror;
    PorpoiseHostResult host_result;
    DSPTaskInfo *native_result;
    uint8_t guest_bytes[PORPOISE_GUEST_DSP_TASK_SIZE];
    uint32_t guest_task;
    uint32_t iram_memory;
    uint32_t iram_length;
    uint32_t dram_memory;
    uint32_t dram_length;
    uint32_t callback_offsets[4];
    void *iram_pointer;
    void *dram_pointer;
    size_t callback_index;
    int new_mirror;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    guest_task = state->gpr[3];
    state->gpr[3] = 0U;
    if (guest_task == 0U) {
        return;
    }
    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return;
    }
    if (!porpoise_libporpoise_validate_guest_structure(
            state,
            context,
            guest_task,
            PORPOISE_GUEST_DSP_TASK_SIZE,
            "DSPTaskInfo pointer is NULL")) {
        return;
    }
    host_result = porpoise_libporpoise_read_bytes(
        context, guest_task, guest_bytes, sizeof(guest_bytes));
    if (host_result != PORPOISE_HOST_OK) {
        porpoise_libporpoise_set_host_fault(
            state, host_result, guest_task);
        return;
    }

    callback_offsets[0] = PORPOISE_GUEST_DSP_INIT_CALLBACK_OFFSET;
    callback_offsets[1] = PORPOISE_GUEST_DSP_RESUME_CALLBACK_OFFSET;
    callback_offsets[2] = PORPOISE_GUEST_DSP_DONE_CALLBACK_OFFSET;
    callback_offsets[3] = PORPOISE_GUEST_DSP_REQUEST_CALLBACK_OFFSET;
    for (callback_index = 0U; callback_index < 4U; callback_index++) {
        uint32_t guest_callback = porpoise_libporpoise_read_be32(
            &guest_bytes[callback_offsets[callback_index]]);
        if (!porpoise_libporpoise_validate_dsp_callback(
                state, guest_callback)) {
            return;
        }
    }

    iram_memory = porpoise_libporpoise_read_be32(
        &guest_bytes[PORPOISE_GUEST_DSP_IRAM_MEMORY_OFFSET]);
    iram_length = porpoise_libporpoise_read_be32(
        &guest_bytes[PORPOISE_GUEST_DSP_IRAM_LENGTH_OFFSET]);
    dram_memory = porpoise_libporpoise_read_be32(
        &guest_bytes[PORPOISE_GUEST_DSP_DRAM_MEMORY_OFFSET]);
    dram_length = porpoise_libporpoise_read_be32(
        &guest_bytes[PORPOISE_GUEST_DSP_DRAM_LENGTH_OFFSET]);
    if ((iram_memory == 0U && iram_length != 0U) ||
        (dram_memory == 0U && dram_length != 0U)) {
        uint32_t invalid_address = iram_memory == 0U && iram_length != 0U
                                       ? iram_memory
                                       : dram_memory;
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            invalid_address,
            "DSP memory image has nonzero length and a NULL guest address");
        return;
    }
    iram_pointer = NULL;
    host_result = iram_length != 0U
                      ? porpoise_libporpoise_decode_span(
                            context,
                            iram_memory,
                            (size_t)iram_length,
                            &iram_pointer)
                      : porpoise_libporpoise_decode_pointer(
                            context, iram_memory, &iram_pointer);
    if (host_result != PORPOISE_HOST_OK) {
        porpoise_libporpoise_set_host_fault(
            state, host_result, iram_memory);
        return;
    }
    dram_pointer = NULL;
    host_result = dram_length != 0U
                      ? porpoise_libporpoise_decode_span(
                            context,
                            dram_memory,
                            (size_t)dram_length,
                            &dram_pointer)
                      : porpoise_libporpoise_decode_pointer(
                            context, dram_memory, &dram_pointer);
    if (host_result != PORPOISE_HOST_OK) {
        porpoise_libporpoise_set_host_fault(
            state, host_result, dram_memory);
        return;
    }

    mirror = porpoise_libporpoise_find_dsp_guest_task(
        context, guest_task);
    new_mirror = mirror == NULL;
    if (new_mirror) {
        mirror = (PorpoiseDspTaskMirror *)calloc(1U, sizeof(*mirror));
        if (mirror == NULL) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_HOST_IO,
                guest_task,
                "could not allocate native DSP task mirror");
            return;
        }
        mirror->guest_task = guest_task;
        mirror->submission_state = *state;
        mirror->submission_state.gpr[3] = guest_task;
        mirror->native_task.state = porpoise_libporpoise_read_be32(
            &guest_bytes[PORPOISE_GUEST_DSP_STATE_OFFSET]);
        mirror->native_task.priority = porpoise_libporpoise_read_be32(
            &guest_bytes[PORPOISE_GUEST_DSP_PRIORITY_OFFSET]);
        mirror->native_task.flags = porpoise_libporpoise_read_be32(
            &guest_bytes[PORPOISE_GUEST_DSP_FLAGS_OFFSET]);
        mirror->native_task.iram_mmem_addr = (u16 *)iram_pointer;
        mirror->native_task.iram_length = iram_length;
        mirror->native_task.iram_addr = porpoise_libporpoise_read_be32(
            &guest_bytes[PORPOISE_GUEST_DSP_IRAM_ADDRESS_OFFSET]);
        mirror->native_task.dram_mmem_addr = (u16 *)dram_pointer;
        mirror->native_task.dram_length = dram_length;
        mirror->native_task.dram_addr = porpoise_libporpoise_read_be32(
            &guest_bytes[PORPOISE_GUEST_DSP_DRAM_ADDRESS_OFFSET]);
        mirror->native_task.dsp_init_vector =
            porpoise_libporpoise_read_be16(
                &guest_bytes[PORPOISE_GUEST_DSP_INIT_VECTOR_OFFSET]);
        mirror->native_task.dsp_resume_vector =
            porpoise_libporpoise_read_be16(
                &guest_bytes[PORPOISE_GUEST_DSP_RESUME_VECTOR_OFFSET]);
        mirror->native_task.init_cb =
            porpoise_libporpoise_dsp_init_callback;
        mirror->native_task.res_cb =
            porpoise_libporpoise_dsp_resume_callback;
        mirror->native_task.done_cb =
            porpoise_libporpoise_dsp_done_callback;
        mirror->native_task.req_cb =
            porpoise_libporpoise_dsp_request_callback;
        mirror->native_task.next = NULL;
        mirror->native_task.prev = NULL;
        mirror->native_task.t_context = (OSTime)
            porpoise_libporpoise_read_be64(
                &guest_bytes[PORPOISE_GUEST_DSP_CONTEXT_TIME_OFFSET]);
        mirror->native_task.t_task = (OSTime)
            porpoise_libporpoise_read_be64(
                &guest_bytes[PORPOISE_GUEST_DSP_TASK_TIME_OFFSET]);
        mirror->next = context->dsp_tasks;
        context->dsp_tasks = mirror;
    }

    if (context->dsp_native_call_depth == 0U) {
        context->dsp_callback_failed = 0;
        context->dsp_callback_fault = PORPOISE_FAULT_NONE;
        context->dsp_callback_fault_address = 0U;
        context->dsp_callback_fault_message[0] = '\0';
    }
    if (context->dsp_native_call_depth == UINT32_MAX) {
        if (new_mirror) {
            porpoise_libporpoise_remove_dsp_mirror(context, mirror);
        }
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            guest_task,
            "native DSP scheduler recursion depth overflow");
        return;
    }

    context->dsp_native_call_depth++;
    native_result = DSPAddTask(&mirror->native_task);
    context->dsp_native_call_depth--;
    if (native_result != &mirror->native_task) {
        if (new_mirror) {
            porpoise_libporpoise_remove_dsp_mirror(context, mirror);
        }
        if (context->dsp_native_call_depth == 0U &&
            context->dsp_callback_failed) {
            porpoise_state_set_fault(
                state,
                context->dsp_callback_fault,
                context->dsp_callback_fault_address,
                context->dsp_callback_fault_message);
            context->dsp_callback_failed = 0;
        }
        return;
    }

    state->gpr[3] = guest_task;
    (void)porpoise_libporpoise_sync_all_dsp_tasks(context);
    if (context->dsp_native_call_depth == 0U) {
        porpoise_libporpoise_collect_completed_dsp_tasks(context);
        if (context->dsp_callback_failed) {
            porpoise_state_set_fault(
                state,
                context->dsp_callback_fault,
                context->dsp_callback_fault_address,
                context->dsp_callback_fault_message);
            context->dsp_callback_failed = 0;
        }
    }
}

static PorpoiseHostResult porpoise_libporpoise_reserve_arq_completion(
    PorpoiseLibporpoiseContext *context)
{
    size_t new_capacity;
    PorpoiseArqCompletion *resized;

    if (context == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (context->arq_completion_count <
        context->arq_completion_capacity) {
        return PORPOISE_HOST_OK;
    }
    if (context->arq_completion_count == SIZE_MAX) {
        return PORPOISE_HOST_ADDRESS_OVERFLOW;
    }

    if (context->arq_completion_capacity == 0U) {
        new_capacity = 8U;
    } else {
        if (context->arq_completion_capacity > SIZE_MAX / 2U) {
            return PORPOISE_HOST_ADDRESS_OVERFLOW;
        }
        new_capacity = context->arq_completion_capacity * 2U;
    }
    if (new_capacity > SIZE_MAX / sizeof(*context->arq_completions)) {
        return PORPOISE_HOST_ADDRESS_OVERFLOW;
    }

    resized = (PorpoiseArqCompletion *)realloc(
        context->arq_completions,
        new_capacity * sizeof(*context->arq_completions));
    if (resized == NULL) {
        return PORPOISE_HOST_IO_ERROR;
    }
    context->arq_completions = resized;
    context->arq_completion_capacity = new_capacity;
    return PORPOISE_HOST_OK;
}

static void porpoise_libporpoise_set_arq_dma_fault(
    PorpoisePpcState *state,
    ARDMAResult result,
    uint32_t guest_request)
{
    PorpoiseFault fault;
    const char *message;

    switch (result) {
        case AR_DMA_RESULT_INVALID_DIRECTION:
            fault = PORPOISE_FAULT_INVALID_ARGUMENT;
            message = "libPorpoise rejected the ARQ DMA direction";
            break;
        case AR_DMA_RESULT_INVALID_ALIGNMENT:
            fault = PORPOISE_FAULT_INVALID_ARGUMENT;
            message = "libPorpoise rejected the ARQ DMA alignment";
            break;
        case AR_DMA_RESULT_INVALID_ARAM_RANGE:
            fault = PORPOISE_FAULT_INVALID_ARGUMENT;
            message = "libPorpoise rejected the ARQ ARAM range";
            break;
        case AR_DMA_RESULT_INVALID_MAIN_MEMORY_RANGE:
            fault = PORPOISE_FAULT_INVALID_POINTER;
            message = "libPorpoise rejected the ARQ main-memory range";
            break;
        case AR_DMA_RESULT_BUSY:
            fault = PORPOISE_FAULT_HOST_IO;
            message = "libPorpoise reported an AR DMA already in progress";
            break;
        case AR_DMA_RESULT_NOT_STARTED:
        default:
            fault = PORPOISE_FAULT_HOST_IO;
            message = "libPorpoise did not complete the ARQ DMA";
            break;
    }
    porpoise_state_set_fault(state, fault, guest_request, message);
}

static void porpoise_libporpoise_propagate_guest_callback_failure(
    PorpoisePpcState *state,
    const PorpoisePpcState *callback_state,
    uint32_t guest_callback,
    const char *message)
{
    if (callback_state->fault != PORPOISE_FAULT_NONE) {
        porpoise_state_set_fault(
            state,
            callback_state->fault,
            callback_state->fault_address,
            porpoise_state_fault_message(callback_state));
        return;
    }
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_INVALID_STATE,
        guest_callback,
        message);
}

static PorpoiseHostResult porpoise_libporpoise_poll_events(
    void *adapter_context,
    PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context =
        (PorpoiseLibporpoiseContext *)adapter_context;
    size_t completions_to_drain;
    size_t gx_events_to_drain;
    BOOL interrupts_were_enabled;
    int gx_failure_pending;
    PorpoiseFault gx_failure_fault;
    uint32_t gx_failure_address;
    char gx_failure_message[PORPOISE_FAULT_MESSAGE_CAPACITY];

    if (context == NULL || state == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (context != porpoise_libporpoise_active_context ||
        state->host == NULL || state->host->context != context ||
        state->host->poll_events != porpoise_libporpoise_poll_events) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "host event poll is not attached to the active libPorpoise adapter");
        return PORPOISE_HOST_OK;
    }
    gx_failure_pending = 0;
    gx_failure_fault = PORPOISE_FAULT_NONE;
    gx_failure_address = 0U;
    gx_failure_message[0] = '\0';
    interrupts_were_enabled = OSDisableInterrupts();
    if (context->gx_draw_done_callback_failed) {
        gx_failure_pending = 1;
        gx_failure_fault = context->gx_draw_done_callback_fault;
        gx_failure_address =
            context->gx_draw_done_callback_fault_address;
        (void)snprintf(
            gx_failure_message,
            sizeof(gx_failure_message),
            "%s",
            context->gx_draw_done_callback_fault_message);
        context->gx_draw_done_callback_failed = 0;
        context->gx_draw_done_callback_fault = PORPOISE_FAULT_NONE;
        context->gx_draw_done_callback_fault_address = 0U;
        context->gx_draw_done_callback_fault_message[0] = '\0';
    }
    OSRestoreInterrupts(interrupts_were_enabled);
    if (gx_failure_pending) {
        porpoise_state_set_fault(
            state,
            gx_failure_fault,
            gx_failure_address,
            gx_failure_message);
        return PORPOISE_HOST_OK;
    }
    if (context->dispatching_events ||
        (state->msr & PORPOISE_MSR_EE) == 0U) {
        return PORPOISE_HOST_OK;
    }

    context->dispatching_events = 1;
    completions_to_drain = context->arq_completion_count;
    while (completions_to_drain != 0U &&
           context->arq_completion_count != 0U &&
           !porpoise_state_should_stop(state)) {
        PorpoiseArqCompletion completion;
        PorpoisePpcState callback_state;
        PorpoiseHostCallGuestFn call_guest;
        int dispatched;

        completion = context->arq_completions[0];
        if (state->lifted_call_depth > completion.ready_depth) {
            break;
        }
        if ((state->msr & PORPOISE_MSR_EE) == 0U) {
            break;
        }

        if (context->arq_completion_count > 1U) {
            memmove(
                &context->arq_completions[0],
                &context->arq_completions[1],
                (context->arq_completion_count - 1U) *
                    sizeof(*context->arq_completions));
        }
        context->arq_completion_count--;
        completions_to_drain--;

        call_guest = state->host->call_guest;
        if (call_guest == NULL) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_MISSING_HOST_CALLBACK,
                completion.guest_callback,
                "queued ARQ completion has no guest address dispatcher");
            break;
        }

        callback_state = *state;
        callback_state.gpr[3] = completion.guest_request;
        dispatched = call_guest(
            &callback_state, completion.guest_callback);
        if (!dispatched || porpoise_state_should_stop(&callback_state)) {
            porpoise_libporpoise_propagate_guest_callback_failure(
                state,
                &callback_state,
                completion.guest_callback,
                "guest ARQ callback dispatch did not complete normally");
            break;
        }
    }

    interrupts_were_enabled = OSDisableInterrupts();
    gx_events_to_drain = context->gx_draw_done_event_count;
    OSRestoreInterrupts(interrupts_were_enabled);
    while (gx_events_to_drain != 0U &&
           !porpoise_state_should_stop(state)) {
        PorpoisePpcState callback_state;
        PorpoiseHostCallGuestFn call_guest;
        uint32_t guest_callback;
        int dispatched;

        if ((state->msr & PORPOISE_MSR_EE) == 0U) {
            break;
        }
        interrupts_were_enabled = OSDisableInterrupts();
        if (context->gx_draw_done_event_count == 0U) {
            OSRestoreInterrupts(interrupts_were_enabled);
            break;
        }
        guest_callback = context->gx_draw_done_events[
            context->gx_draw_done_event_head];
        context->gx_draw_done_event_head =
            (context->gx_draw_done_event_head + 1U) %
            PORPOISE_GX_DRAW_DONE_EVENT_CAPACITY;
        context->gx_draw_done_event_count--;
        gx_events_to_drain--;
        OSRestoreInterrupts(interrupts_were_enabled);

        call_guest = state->host->call_guest;
        if (call_guest == NULL) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_MISSING_HOST_CALLBACK,
                guest_callback,
                "queued GX draw-done event has no guest address dispatcher");
            break;
        }
        callback_state = *state;
        if (callback_state.host_event_delivery_depth == UINT32_MAX) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_INVALID_STATE,
                guest_callback,
                "GX draw-done event-delivery depth overflow");
            break;
        }

        callback_state.host_event_delivery_depth++;
        dispatched = call_guest(&callback_state, guest_callback);
        if (!dispatched || porpoise_state_should_stop(&callback_state)) {
            porpoise_libporpoise_propagate_guest_callback_failure(
                state,
                &callback_state,
                guest_callback,
                "guest GX draw-done callback dispatch did not complete normally");
            break;
        }
    }
    context->dispatching_events = 0;
    return PORPOISE_HOST_OK;
}

void porpoise_libporpoise_arq_post_request_adapter(
    PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseHostResult host_result;
    uint8_t guest_bytes[PORPOISE_GUEST_ARQ_REQUEST_SIZE];
    uint32_t guest_request;
    uint32_t owner;
    uint32_t type;
    uint32_t priority;
    uint32_t source;
    uint32_t destination;
    uint32_t length;
    uint32_t callback;
    uint32_t stored_callback;
    uint32_t native_main_memory;
    uint32_t native_aram;
    ARDMAResult dma_result;

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return;
    }
    guest_request = state->gpr[3];
    owner = state->gpr[4];
    type = state->gpr[5];
    priority = state->gpr[6];
    source = state->gpr[7];
    destination = state->gpr[8];
    length = state->gpr[9];
    callback = state->gpr[10];

    if (!porpoise_libporpoise_validate_guest_structure(
            state,
            context,
            guest_request,
            PORPOISE_GUEST_ARQ_REQUEST_SIZE,
            "ARQRequest pointer is NULL")) {
        return;
    }
    if (state->lifted_call_depth == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            guest_request,
            "ARQPostRequest must run inside lifted address dispatch");
        return;
    }
    if (priority != ARQ_PRIORITY_HIGH) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            guest_request,
            "only high-priority ARQ requests are supported");
        return;
    }
    if (type != ARQ_TYPE_MRAM_TO_ARAM &&
        type != ARQ_TYPE_ARAM_TO_MRAM) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_request,
            "ARQ request has an invalid DMA direction");
        return;
    }
    if (callback != 0U && (callback & UINT32_C(3)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            callback,
            "ARQ callback address is not four-byte aligned");
        return;
    }
    if (callback != 0U &&
        (state->host->call_guest == NULL ||
         state->host->poll_events != porpoise_libporpoise_poll_events)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            callback,
            "ARQ callback requires guest dispatch and event polling");
        return;
    }

    if (callback == 0U) {
#if defined(PORPOISE_GUEST_ARQ_CALLBACK_HACK_ADDRESS)
        stored_callback =
            (uint32_t)PORPOISE_GUEST_ARQ_CALLBACK_HACK_ADDRESS;
        if (stored_callback == 0U ||
            (stored_callback & UINT32_C(3)) != 0U) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_INVALID_STATE,
                stored_callback,
                "generated __ARQCallbackHack address is invalid");
            return;
        }
#else
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            guest_request,
            "generated project does not resolve __ARQCallbackHack");
        return;
#endif
    } else {
        stored_callback = callback;
        host_result = porpoise_libporpoise_reserve_arq_completion(
            context);
        if (host_result != PORPOISE_HOST_OK) {
            porpoise_libporpoise_set_host_fault(
                state, host_result, guest_request);
            return;
        }
    }

    host_result = state->host->read_bytes(
        state->host->context,
        guest_request,
        guest_bytes,
        sizeof(guest_bytes));
    if (host_result != PORPOISE_HOST_OK) {
        porpoise_libporpoise_set_host_fault(
            state, host_result, guest_request);
        return;
    }
    porpoise_libporpoise_write_be32(
        &guest_bytes[PORPOISE_GUEST_ARQ_NEXT_OFFSET], 0U);
    porpoise_libporpoise_write_be32(
        &guest_bytes[PORPOISE_GUEST_ARQ_OWNER_OFFSET], owner);
    porpoise_libporpoise_write_be32(
        &guest_bytes[PORPOISE_GUEST_ARQ_TYPE_OFFSET], type);
    /* The SDK leaves the request's priority word at +0x0C untouched. */
    (void)guest_bytes[PORPOISE_GUEST_ARQ_PRIORITY_OFFSET];
    porpoise_libporpoise_write_be32(
        &guest_bytes[PORPOISE_GUEST_ARQ_SOURCE_OFFSET], source);
    porpoise_libporpoise_write_be32(
        &guest_bytes[PORPOISE_GUEST_ARQ_DESTINATION_OFFSET], destination);
    porpoise_libporpoise_write_be32(
        &guest_bytes[PORPOISE_GUEST_ARQ_LENGTH_OFFSET], length);
    porpoise_libporpoise_write_be32(
        &guest_bytes[PORPOISE_GUEST_ARQ_CALLBACK_OFFSET], stored_callback);

    host_result = state->host->write_bytes(
        state->host->context,
        guest_request,
        guest_bytes,
        sizeof(guest_bytes));
    if (host_result != PORPOISE_HOST_OK) {
        porpoise_libporpoise_set_host_fault(
            state, host_result, guest_request);
        return;
    }

    if (type == ARQ_TYPE_MRAM_TO_ARAM) {
        native_main_memory = source;
        native_aram = destination;
    } else {
        native_main_memory = destination;
        native_aram = source;
    }
    dma_result = ARStartDMAEx(
        type, native_main_memory, native_aram, length);
    if (dma_result != AR_DMA_RESULT_SUCCESS) {
        porpoise_libporpoise_set_arq_dma_fault(
            state, dma_result, guest_request);
        return;
    }

    if (callback != 0U) {
        PorpoiseArqCompletion *completion =
            &context->arq_completions[context->arq_completion_count];
        completion->guest_request = guest_request;
        completion->guest_callback = callback;
        completion->ready_depth = state->lifted_call_depth - 1U;
        context->arq_completion_count++;
    }
}

void porpoise_libporpoise_vi_configure_adapter(PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context;
    GXRenderModeObj native_mode;
    uint64_t padded_width;
    uint64_t span_bytes;
    uint32_t guest_mode;
    size_t index;

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return;
    }
    guest_mode = state->gpr[3];
    if (!porpoise_libporpoise_validate_guest_structure(
            state,
            context,
            guest_mode,
            PORPOISE_GUEST_GX_RENDER_MODE_SIZE,
            "GXRenderModeObj pointer is NULL")) {
        return;
    }

    memset(&native_mode, 0, sizeof(native_mode));
    native_mode.viTVmode = (VITVMode)porpoise_load_u32(
        state, guest_mode + UINT32_C(0x00));
    native_mode.fbWidth = porpoise_load_u16(
        state, guest_mode + UINT32_C(0x04));
    native_mode.efbHeight = porpoise_load_u16(
        state, guest_mode + UINT32_C(0x06));
    native_mode.xfbHeight = porpoise_load_u16(
        state, guest_mode + UINT32_C(0x08));
    native_mode.viXOrigin = porpoise_load_u16(
        state, guest_mode + UINT32_C(0x0A));
    native_mode.viYOrigin = porpoise_load_u16(
        state, guest_mode + UINT32_C(0x0C));
    native_mode.viWidth = porpoise_load_u16(
        state, guest_mode + UINT32_C(0x0E));
    native_mode.viHeight = porpoise_load_u16(
        state, guest_mode + UINT32_C(0x10));
    native_mode.xFBmode = (VIXFBMode)porpoise_load_u32(
        state, guest_mode + UINT32_C(0x14));
    native_mode.field_rendering = porpoise_load_u8(
        state, guest_mode + UINT32_C(0x18));
    native_mode.aa = porpoise_load_u8(
        state, guest_mode + UINT32_C(0x19));
    for (index = 0U; index < 24U; index++) {
        native_mode.sample_pattern[index / 2U][index % 2U] =
            porpoise_load_u8(
                state,
                guest_mode + UINT32_C(0x1A) + (uint32_t)index);
    }
    for (index = 0U; index < 7U; index++) {
        native_mode.vfilter[index] = porpoise_load_u8(
            state,
            guest_mode + UINT32_C(0x32) + (uint32_t)index);
    }
    if (porpoise_state_has_fault(state)) {
        return;
    }
    if (native_mode.fbWidth == 0U || native_mode.xfbHeight == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_mode,
            "VIConfigure requires nonzero XFB dimensions");
        return;
    }
    padded_width = ((uint64_t)native_mode.fbWidth + UINT64_C(15)) &
        ~UINT64_C(15);
    span_bytes = padded_width * UINT64_C(2) *
        (uint64_t)native_mode.xfbHeight;
    if (span_bytes == 0U || span_bytes > UINT32_MAX) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_mode,
            "VIConfigure XFB span is outside the supported guest range");
        return;
    }
    VIConfigure(&native_mode);
    context->vi_xfb_span_bytes = (uint32_t)span_bytes;
    context->vi_xfb_layout_configured = 1;
}

void porpoise_libporpoise_os_init_adapter(PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context =
        porpoise_libporpoise_context_for_state(state);

    if (context == NULL) {
        return;
    }
    if (!porpoise_libporpoise_os_initialized) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "OSInit import requires native runtime initialization");
        return;
    }
    if (context->guest_sdk_layout_bound &&
        !context->os_guest_state_mirrored) {
        const uint32_t addresses[] = {
            context->guest_sdk_layout.os_arena_lo_address,
            context->guest_sdk_layout.os_arena_hi_address,
            context->guest_sdk_layout.os_initialized_address,
            context->guest_sdk_layout.os_boot_info_address,
            context->guest_sdk_layout.os_bi2_debug_flag_address,
            context->guest_sdk_layout.dvd_long_file_name_flag_address
        };
        const uint32_t values[] = {
            context->arena_guest_lo,
            context->arena_guest_hi,
            UINT32_C(1),
            UINT32_C(0x80000000),
            UINT32_C(0),
            UINT32_C(1)
        };
        uint8_t *destinations[
            sizeof(addresses) / sizeof(addresses[0])];
        size_t index;

        if (!context->arena_configured) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_INVALID_STATE,
                state->pc,
                "exact OSInit import requires configured guest arena bounds");
            return;
        }
        for (index = 0U;
             index < sizeof(addresses) / sizeof(addresses[0]);
             index++) {
            void *destination = NULL;
            PorpoiseHostResult result =
                porpoise_libporpoise_decode_span(
                    context,
                    addresses[index],
                    sizeof(uint32_t),
                    &destination);
            if (result != PORPOISE_HOST_OK) {
                porpoise_libporpoise_set_host_fault(
                    state, result, addresses[index]);
                return;
            }
            destinations[index] = (uint8_t *)destination;
        }
        for (index = 0U;
             index < sizeof(addresses) / sizeof(addresses[0]);
             index++) {
            porpoise_libporpoise_write_be32(
                destinations[index], values[index]);
        }
        context->os_guest_state_mirrored = 1;
    }
}

void porpoise_libporpoise_dvd_init_adapter(PorpoisePpcState *state)
{
    if (porpoise_libporpoise_context_for_state(state) == NULL) {
        return;
    }
    if (!porpoise_libporpoise_dvd_initialized ||
        DVDGetFSTLocation() == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            state->pc,
            "DVDInit import requires title-host native DVD initialization");
    }
}

void porpoise_libporpoise_vi_init_adapter(PorpoisePpcState *state)
{
    if (porpoise_libporpoise_context_for_state(state) == NULL) {
        return;
    }
    if (!porpoise_libporpoise_vi_initialized) {
        VIInit();
        porpoise_libporpoise_vi_initialized = 1;
    }
}

void porpoise_libporpoise_demo_pad_init_adapter(PorpoisePpcState *state)
{
    if (porpoise_libporpoise_context_for_state(state) == NULL) {
        return;
    }
    if (!porpoise_libporpoise_demo_pad_initialized) {
        DEMOPadInit();
        porpoise_libporpoise_demo_pad_initialized = 1;
    }
}

void porpoise_libporpoise_pad_read_adapter(PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseHostResult result;
    PADStatus native_status[PORPOISE_GUEST_PAD_STATUS_COUNT];
    uint8_t encoded_status[PORPOISE_GUEST_PAD_STATUS_ARRAY_SIZE];
    uint8_t *guest_status;
    void *guest_status_span;
    uint32_t guest_address;
    uint32_t motor_mask;
    size_t index;

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return;
    }
    guest_address = state->gpr[3];
    if (guest_address == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_address,
            "PADRead status array is NULL");
        return;
    }
    if ((guest_address & UINT32_C(1)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_address,
            "PADRead status array is not two-byte aligned");
        return;
    }

    guest_status_span = NULL;
    result = porpoise_libporpoise_decode_span(
        context,
        guest_address,
        sizeof(encoded_status),
        &guest_status_span);
    if (result != PORPOISE_HOST_OK) {
        porpoise_libporpoise_set_host_fault(state, result, guest_address);
        return;
    }
    guest_status = (uint8_t *)guest_status_span;

    /* PADRead produces four fixed 12-byte console records. Native layout and
     * host endianness are deliberately kept behind this copy boundary. Keep
     * the guest's byte 0x0B padding intact, matching the SDK implementation,
     * which writes only the semantic fields through err at byte 0x0A. The
     * complete destination is preflighted before the stateful native read and
     * committed once, so no rejected call can partially update guest RAM. */
    memcpy(encoded_status, guest_status, sizeof(encoded_status));
    memset(native_status, 0, sizeof(native_status));
    motor_mask = (uint32_t)PADRead(native_status);
    for (index = 0U;
         index < PORPOISE_GUEST_PAD_STATUS_COUNT;
         index++) {
        uint8_t *destination =
            encoded_status + index * PORPOISE_GUEST_PAD_STATUS_SIZE;
        const PADStatus *source = &native_status[index];

        porpoise_libporpoise_write_be16(destination, source->button);
        destination[2U] = (uint8_t)source->stickX;
        destination[3U] = (uint8_t)source->stickY;
        destination[4U] = (uint8_t)source->substickX;
        destination[5U] = (uint8_t)source->substickY;
        destination[6U] = source->triggerLeft;
        destination[7U] = source->triggerRight;
        destination[8U] = source->analogA;
        destination[9U] = source->analogB;
        destination[10U] = (uint8_t)source->err;
    }
    memcpy(guest_status, encoded_status, sizeof(encoded_status));
    state->gpr[3] = motor_mask;
}

void porpoise_libporpoise_vi_set_black_adapter(PorpoisePpcState *state)
{
    uint32_t value;

    if (porpoise_libporpoise_context_for_state(state) == NULL) {
        return;
    }
    value = state->gpr[3];
    if (value > 1U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            value,
            "VISetBlack requires a Boolean value");
        return;
    }
    VISetBlack(value != 0U ? TRUE : FALSE);
}

void porpoise_libporpoise_vi_flush_adapter(PorpoisePpcState *state)
{
    if (porpoise_libporpoise_context_for_state(state) == NULL) {
        return;
    }
    VIFlush();
}

void porpoise_libporpoise_vi_set_next_frame_buffer_adapter(
    PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseHostResult result;
    uint32_t guest_address;
    void *mapped_probe;

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return;
    }
    guest_address = state->gpr[3];
    if (guest_address == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_address,
            "VISetNextFrameBuffer destination is NULL");
        return;
    }
    if ((guest_address & UINT32_C(31)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_address,
            "VISetNextFrameBuffer destination is not 32-byte aligned");
        return;
    }

    /* Configuration can change after this call in the same pre-retrace
     * callback. Validate only that the aligned base names ordinary guest RAM;
     * the versioned native endpoint must validate the complete final-mode XFB
     * span when selection is latched or presented. */
    mapped_probe = NULL;
    result = porpoise_libporpoise_decode_span(
        context, guest_address, 32U, &mapped_probe);
    if (result != PORPOISE_HOST_OK) {
        porpoise_libporpoise_set_host_fault(state, result, guest_address);
        return;
    }
    (void)mapped_probe;

#if defined(LIBPORPOISE_VI_SET_NEXT_FRAME_BUFFER_GUEST_ADDRESS_API_VERSION) && \
    LIBPORPOISE_VI_SET_NEXT_FRAME_BUFFER_GUEST_ADDRESS_API_VERSION >= 1
    if (VIHostSetNextFrameBufferGuestAddress((u32)guest_address) == FALSE) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_HOST_IO,
            guest_address,
            "libPorpoise rejected the VI next-framebuffer guest address");
    }
#else
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        guest_address,
        "libPorpoise does not advertise the VI next-framebuffer guest-address contract");
#endif
}

void porpoise_libporpoise_vi_wait_for_retrace_adapter(
    PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context;
    uint64_t presentations_before;
    uint64_t presentations_after;
    uint32_t guest_frame_buffer_before;
    uint32_t guest_frame_buffer_after;

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return;
    }
    if (!porpoise_libporpoise_presentation_snapshot(
            &presentations_before,
            &guest_frame_buffer_before)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            state->pc,
            "libPorpoise does not expose the host-XFB presentation statistics required by the VIWaitForRetrace contract");
        return;
    }

    (void)guest_frame_buffer_before;
    VIWaitForRetrace();

    if (!porpoise_libporpoise_presentation_snapshot(
            &presentations_after,
            &guest_frame_buffer_after)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "libPorpoise host-XFB presentation statistics became unavailable during VIWaitForRetrace");
        return;
    }
    if (presentations_after < presentations_before) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "libPorpoise host-XFB presentation counter moved backwards during VIWaitForRetrace");
        return;
    }
    if (presentations_after - presentations_before > UINT64_C(1)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "more than one host-XFB presentation occurred during one VIWaitForRetrace call");
        return;
    }
    if (presentations_after > presentations_before) {
        if (state->trace_file != NULL &&
            context->vi_xfb_layout_configured != 0) {
            void *native_frame_buffer = NULL;
            PorpoiseHostResult result = porpoise_libporpoise_decode_span(
                context,
                guest_frame_buffer_after,
                context->vi_xfb_span_bytes,
                &native_frame_buffer);
            const uint8_t *bytes;
            uint64_t content_hash;
            size_t index;
            int content_varied;

            if (result != PORPOISE_HOST_OK) {
                porpoise_libporpoise_set_host_fault(
                    state, result, guest_frame_buffer_after);
                return;
            }
            bytes = (const uint8_t *)native_frame_buffer;
            content_hash = UINT64_C(14695981039346656037);
            content_varied = 0;
            for (index = 0U;
                 index < (size_t)context->vi_xfb_span_bytes;
                 index++) {
                content_hash ^= (uint64_t)bytes[index];
                content_hash *= UINT64_C(1099511628211);
                if (index >= 4U && bytes[index] != bytes[index % 4U]) {
                    content_varied = 1;
                }
            }
            porpoise_trace_frame_observed(
                state,
                guest_frame_buffer_after,
                content_hash,
                content_varied);
        } else {
            porpoise_trace_frame(state, guest_frame_buffer_after);
        }
    }
}

static int porpoise_libporpoise_copy_guest_path(
    PorpoisePpcState *state,
    PorpoiseLibporpoiseContext *context,
    uint32_t guest_address,
    char path[PORPOISE_DVD_PATH_CAPACITY])
{
    size_t index;

    if (guest_address == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_address,
            "DVD path pointer is NULL");
        return 0;
    }

    for (index = 0U; index < PORPOISE_DVD_PATH_CAPACITY; index++) {
        PorpoiseHostResult result;
        uint32_t current_address;

        if (index > (size_t)(UINT32_MAX - guest_address)) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_ADDRESS_OVERFLOW,
                guest_address,
                "DVD path crosses the 32-bit guest address boundary");
            return 0;
        }
        current_address = guest_address + (uint32_t)index;
        result = porpoise_libporpoise_read_bytes(
            context, current_address, &path[index], 1U);
        if (result != PORPOISE_HOST_OK) {
            porpoise_libporpoise_set_host_fault(
                state, result, current_address);
            return 0;
        }
        if (path[index] == '\0') {
            return 1;
        }
    }

    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        guest_address,
        "DVD path is not NUL-terminated within 1024 bytes");
    return 0;
}

static PorpoiseDvdFileMirror *porpoise_libporpoise_find_dvd_mirror(
    PorpoiseLibporpoiseContext *context,
    uint32_t guest_file_info)
{
    size_t index;

    for (index = 0U; index < PORPOISE_DVD_MIRROR_CAPACITY; index++) {
        PorpoiseDvdFileMirror *mirror = &context->dvd_files[index];
        if (mirror->in_use &&
            mirror->guest_file_info == guest_file_info) {
            return mirror;
        }
    }
    return NULL;
}

static PorpoiseDvdFileMirror *porpoise_libporpoise_reserve_dvd_mirror(
    PorpoiseLibporpoiseContext *context)
{
    size_t index;

    for (index = 0U; index < PORPOISE_DVD_MIRROR_CAPACITY; index++) {
        if (!context->dvd_files[index].in_use) {
            return &context->dvd_files[index];
        }
    }
    return NULL;
}

static int porpoise_libporpoise_copy_dvd_open_fields(
    PorpoisePpcState *state,
    const PorpoiseDvdFileMirror *mirror)
{
    uint32_t address = mirror->guest_file_info;

    porpoise_store_u32(
        state,
        address + PORPOISE_GUEST_DVD_STATE_OFFSET,
        (uint32_t)mirror->native_file_info.cBlock.state);
    porpoise_store_u32(
        state,
        address + PORPOISE_GUEST_DVD_START_ADDRESS_OFFSET,
        mirror->native_file_info.startAddr);
    porpoise_store_u32(
        state,
        address + PORPOISE_GUEST_DVD_LENGTH_OFFSET,
        mirror->native_file_info.length);
    porpoise_store_u32(
        state,
        address + PORPOISE_GUEST_DVD_CALLBACK_OFFSET,
        0U);
    return !porpoise_state_has_fault(state);
}

static int porpoise_libporpoise_copy_dvd_transfer_fields(
    PorpoisePpcState *state,
    const PorpoiseDvdFileMirror *mirror)
{
    uint32_t address = mirror->guest_file_info;

    porpoise_store_u32(
        state,
        address + PORPOISE_GUEST_DVD_STATE_OFFSET,
        (uint32_t)mirror->native_file_info.cBlock.state);
    porpoise_store_u32(
        state,
        address + PORPOISE_GUEST_DVD_CURRENT_TRANSFER_OFFSET,
        mirror->native_file_info.cBlock.currTransferSize);
    porpoise_store_u32(
        state,
        address + PORPOISE_GUEST_DVD_TRANSFERRED_OFFSET,
        mirror->native_file_info.cBlock.transferredSize);
    return !porpoise_state_has_fault(state);
}

static void porpoise_libporpoise_release_dvd_mirror(
    PorpoiseDvdFileMirror *mirror)
{
    if (mirror != NULL && mirror->in_use) {
        (void)DVDClose(&mirror->native_file_info);
        memset(mirror, 0, sizeof(*mirror));
    }
}

void porpoise_libporpoise_dvd_convert_path_to_entry_adapter(
    PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context;
    char path[PORPOISE_DVD_PATH_CAPACITY];
    uint32_t guest_path;

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return;
    }
    guest_path = state->gpr[3];
    state->gpr[3] = UINT32_MAX;
    if (!porpoise_libporpoise_copy_guest_path(
            state, context, guest_path, path)) {
        return;
    }
    state->gpr[3] = (uint32_t)DVDConvertPathToEntrynum(path);
}

static void porpoise_libporpoise_dvd_open_common(
    PorpoisePpcState *state,
    int fast_open)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseDvdFileMirror *mirror;
    uint32_t first_argument;
    uint32_t guest_file_info;
    char path[PORPOISE_DVD_PATH_CAPACITY];
    BOOL opened;

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return;
    }
    first_argument = state->gpr[3];
    guest_file_info = state->gpr[4];
    state->gpr[3] = 0U;

    if (!porpoise_libporpoise_validate_guest_structure(
            state,
            context,
            guest_file_info,
            PORPOISE_GUEST_DVD_FILE_INFO_SIZE,
            "DVDFileInfo pointer is NULL")) {
        return;
    }
    if (porpoise_libporpoise_find_dvd_mirror(
            context, guest_file_info) != NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            guest_file_info,
            "DVDFileInfo is already open");
        return;
    }
    if (!fast_open &&
        !porpoise_libporpoise_copy_guest_path(
            state, context, first_argument, path)) {
        return;
    }

    mirror = porpoise_libporpoise_reserve_dvd_mirror(context);
    if (mirror == NULL) {
        return;
    }
    memset(mirror, 0, sizeof(*mirror));
    opened = fast_open
                 ? DVDFastOpen((s32)first_argument, &mirror->native_file_info)
                 : DVDOpen(path, &mirror->native_file_info);
    if (!opened) {
        memset(mirror, 0, sizeof(*mirror));
        return;
    }

    mirror->in_use = 1;
    mirror->guest_file_info = guest_file_info;
    if (!porpoise_libporpoise_copy_dvd_open_fields(state, mirror)) {
        porpoise_libporpoise_release_dvd_mirror(mirror);
        return;
    }
    state->gpr[3] = 1U;
}

void porpoise_libporpoise_dvd_open_adapter(PorpoisePpcState *state)
{
    porpoise_libporpoise_dvd_open_common(state, 0);
}

void porpoise_libporpoise_dvd_fast_open_adapter(PorpoisePpcState *state)
{
    porpoise_libporpoise_dvd_open_common(state, 1);
}

void porpoise_libporpoise_dvd_read_prio_adapter(PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseDvdFileMirror *mirror;
    uint32_t guest_file_info;
    uint32_t guest_destination;
    s32 length;
    s32 offset;
    s32 priority;
    size_t length_size;
    size_t buffer_size;
    size_t allocation_size;
    size_t copy_size;
    uint8_t *allocation;
    uint8_t *aligned_buffer;
    uintptr_t aligned_address;
    s32 result;

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return;
    }
    guest_file_info = state->gpr[3];
    guest_destination = state->gpr[4];
    length = (s32)state->gpr[5];
    offset = (s32)state->gpr[6];
    priority = (s32)state->gpr[7];
    state->gpr[3] = UINT32_MAX;

    mirror = porpoise_libporpoise_find_dvd_mirror(
        context, guest_file_info);
    if (mirror == NULL || length < 0 || offset < 0) {
        return;
    }
    if (!porpoise_libporpoise_validate_guest_structure(
            state,
            context,
            guest_file_info,
            PORPOISE_GUEST_DVD_FILE_INFO_SIZE,
            "DVDFileInfo pointer is NULL")) {
        return;
    }

    length_size = (size_t)(u32)length;
    if (length_size != 0U) {
        if (guest_destination == 0U) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_INVALID_POINTER,
                guest_destination,
                "DVD read destination is NULL");
            return;
        }
        if (!porpoise_libporpoise_validate_guest_span(
                state, context, guest_destination, length_size)) {
            return;
        }
    }

    buffer_size = length_size != 0U ? length_size : 1U;
    if (buffer_size > SIZE_MAX - (PORPOISE_DVD_TRANSFER_ALIGNMENT - 1U)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            guest_destination,
            "DVD read host buffer size overflows");
        return;
    }
    allocation_size =
        buffer_size + (PORPOISE_DVD_TRANSFER_ALIGNMENT - 1U);
    allocation = (uint8_t *)malloc(allocation_size);
    if (allocation == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_HOST_IO,
            guest_destination,
            "could not allocate aligned DVD read buffer");
        return;
    }
    if ((uintptr_t)allocation >
        UINTPTR_MAX - (PORPOISE_DVD_TRANSFER_ALIGNMENT - 1U)) {
        free(allocation);
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            guest_destination,
            "DVD read host buffer address overflows");
        return;
    }
    aligned_address =
        ((uintptr_t)allocation + (PORPOISE_DVD_TRANSFER_ALIGNMENT - 1U)) &
        ~((uintptr_t)PORPOISE_DVD_TRANSFER_ALIGNMENT - 1U);
    aligned_buffer = (uint8_t *)aligned_address;
    if (length_size != 0U) {
        memset(aligned_buffer, 0, length_size);
    }

    /* A native validation/fseek failure may otherwise retain an old count. */
    mirror->native_file_info.cBlock.currTransferSize = 0U;
    mirror->native_file_info.cBlock.transferredSize = 0U;
    result = DVDReadPrio(
        &mirror->native_file_info,
        aligned_buffer,
        length,
        offset,
        priority);
    state->gpr[3] = (uint32_t)result;

    if (result >= 0) {
        copy_size = (size_t)(u32)result;
    } else {
        copy_size =
            (size_t)mirror->native_file_info.cBlock.currTransferSize;
    }
    if (copy_size > length_size) {
        free(allocation);
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_HOST_IO,
            guest_destination,
            "native DVD read reported more bytes than requested");
        return;
    }
    if (copy_size != 0U &&
        !porpoise_store_bytes(
            state, guest_destination, aligned_buffer, copy_size)) {
        free(allocation);
        return;
    }
    free(allocation);
    (void)porpoise_libporpoise_copy_dvd_transfer_fields(state, mirror);
}

void porpoise_libporpoise_dvd_close_adapter(PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseDvdFileMirror *mirror;
    uint32_t guest_file_info;
    BOOL result;

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return;
    }
    guest_file_info = state->gpr[3];
    state->gpr[3] = 0U;
    mirror = porpoise_libporpoise_find_dvd_mirror(
        context, guest_file_info);
    if (mirror == NULL) {
        return;
    }
    if (!porpoise_libporpoise_validate_guest_structure(
            state,
            context,
            guest_file_info,
            PORPOISE_GUEST_DVD_FILE_INFO_SIZE,
            "DVDFileInfo pointer is NULL")) {
        return;
    }

    result = DVDClose(&mirror->native_file_info);
    state->gpr[3] = result ? 1U : 0U;
    if (!result) {
        return;
    }
    (void)porpoise_libporpoise_copy_dvd_transfer_fields(state, mirror);
    memset(mirror, 0, sizeof(*mirror));
}

void porpoise_libporpoise_dvd_get_command_block_status_adapter(
    PorpoisePpcState *state)
{
    const PorpoiseLibporpoiseContext *context;
    uint32_t guest_command_block;
    s32 dvd_state;

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return;
    }
    guest_command_block = state->gpr[3];
    if (!porpoise_libporpoise_validate_guest_structure(
            state,
            context,
            guest_command_block,
            PORPOISE_GUEST_DVD_COMMAND_BLOCK_SIZE,
            "DVDCommandBlock pointer is NULL")) {
        return;
    }
    dvd_state = (s32)porpoise_load_u32(
        state,
        guest_command_block + PORPOISE_GUEST_DVD_STATE_OFFSET);
    if (porpoise_state_has_fault(state)) {
        return;
    }
    state->gpr[3] = (uint32_t)(
        dvd_state == DVD_STATE_COVER_CLOSED
            ? DVD_STATE_BUSY
            : dvd_state);
}

void porpoise_libporpoise_dvd_cancel_adapter(PorpoisePpcState *state)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseDvdFileMirror *mirror;
    uint32_t guest_command_block;
    s32 dvd_state;

    context = porpoise_libporpoise_context_for_state(state);
    if (context == NULL) {
        return;
    }
    guest_command_block = state->gpr[3];
    state->gpr[3] = UINT32_MAX;
    if (!porpoise_libporpoise_validate_guest_structure(
            state,
            context,
            guest_command_block,
            PORPOISE_GUEST_DVD_COMMAND_BLOCK_SIZE,
            "DVDCommandBlock pointer is NULL")) {
        return;
    }

    mirror = porpoise_libporpoise_find_dvd_mirror(
        context, guest_command_block);
    if (mirror != NULL) {
        s32 result = DVDCancel(&mirror->native_file_info.cBlock);
        state->gpr[3] = (uint32_t)result;
        (void)porpoise_libporpoise_copy_dvd_transfer_fields(state, mirror);
        return;
    }

    dvd_state = (s32)porpoise_load_u32(
        state,
        guest_command_block + PORPOISE_GUEST_DVD_STATE_OFFSET);
    if (porpoise_state_has_fault(state)) {
        return;
    }
    if (dvd_state == DVD_STATE_FATAL_ERROR ||
        dvd_state == DVD_STATE_END ||
        dvd_state == DVD_STATE_CANCELED) {
        state->gpr[3] = 0U;
        return;
    }

    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        guest_command_block,
        "cannot cancel an active guest DVD command without an async mirror");
}

static int porpoise_libporpoise_dvd_configuration_matches(
    const char *dvd_root_directory)
{
    if ((dvd_root_directory != NULL) !=
        porpoise_libporpoise_dvd_root_is_explicit) {
        return 0;
    }
    return dvd_root_directory == NULL ||
           strcmp(
               dvd_root_directory,
               porpoise_libporpoise_dvd_root_directory) == 0;
}

static PorpoiseHostResult porpoise_libporpoise_adapter_init_internal(
    PorpoiseHostAdapter *adapter,
    const char *dvd_root_directory,
    int initialize_dvd)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseHostResult result;
    char *root_copy;

    if (adapter == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if ((initialize_dvd != 0 && initialize_dvd != 1) ||
        (dvd_root_directory != NULL &&
         (dvd_root_directory[0] == '\0' || !initialize_dvd))) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (porpoise_libporpoise_active_context != NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }

    if (initialize_dvd && porpoise_libporpoise_dvd_initialized &&
        !porpoise_libporpoise_dvd_configuration_matches(
            dvd_root_directory)) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }

    /* Keep optional callbacks deterministic as the adapter contract grows. */
    memset(adapter, 0, sizeof(*adapter));

    /* OSInit is internally idempotent; this guard keeps the adapter call once. */
    if (!porpoise_libporpoise_os_initialized) {
        OSInit();
        porpoise_libporpoise_os_initialized = 1;
    }

    context = (PorpoiseLibporpoiseContext *)calloc(1U, sizeof(*context));
    if (context == NULL) {
        return PORPOISE_HOST_IO_ERROR;
    }
    result = porpoise_libporpoise_initialize_system_call_vector(context);
    if (result != PORPOISE_HOST_OK) {
        free(context);
        memset(adapter, 0, sizeof(*adapter));
        return result;
    }

    root_copy = NULL;
    if (initialize_dvd && !porpoise_libporpoise_dvd_initialized &&
        dvd_root_directory != NULL) {
        size_t root_length = strlen(dvd_root_directory);
        if (root_length == SIZE_MAX) {
            free(context);
            return PORPOISE_HOST_ADDRESS_OVERFLOW;
        }
        root_copy = (char *)malloc(root_length + 1U);
        if (root_copy == NULL) {
            free(context);
            return PORPOISE_HOST_IO_ERROR;
        }
        memcpy(root_copy, dvd_root_directory, root_length + 1U);
        if (!DVDSetRootDirectory(root_copy)) {
            free(root_copy);
            free(context);
            return PORPOISE_HOST_INVALID_ARGUMENT;
        }
    }

    if (initialize_dvd && !porpoise_libporpoise_dvd_initialized) {
        porpoise_libporpoise_dvd_root_directory = root_copy;
        porpoise_libporpoise_dvd_root_is_explicit =
            dvd_root_directory != NULL;
        porpoise_libporpoise_dvd_initialized = 1;
        DVDInit();
    } else {
        free(root_copy);
    }
    if (initialize_dvd && DVDGetFSTLocation() == NULL) {
        free(context);
        return PORPOISE_HOST_IO_ERROR;
    }

    adapter->context = context;
    adapter->read_bytes = porpoise_libporpoise_read_bytes;
    adapter->write_bytes = porpoise_libporpoise_write_bytes;
    adapter->decode_pointer = porpoise_libporpoise_decode_pointer;
    adapter->encode_pointer = porpoise_libporpoise_encode_pointer;
    adapter->read_time_base = porpoise_libporpoise_read_time_base;
    adapter->system_call = porpoise_libporpoise_system_call;
    adapter->poll_events = porpoise_libporpoise_poll_events;
    adapter->write_gx_fifo_u8 =
        porpoise_libporpoise_write_gx_fifo_u8;
    context->owner_adapter = adapter;
    if (!porpoise_libporpoise_thread_registry_create(
            adapter, &context->thread_registry)) {
        memset(adapter, 0, sizeof(*adapter));
        free(context);
        return PORPOISE_HOST_IO_ERROR;
    }
    porpoise_libporpoise_active_context = context;
    return PORPOISE_HOST_OK;
}

PorpoiseHostResult porpoise_libporpoise_adapter_init(
    PorpoiseHostAdapter *adapter)
{
    return porpoise_libporpoise_adapter_init_internal(
        adapter, NULL, 0);
}

PorpoiseHostResult porpoise_libporpoise_adapter_init_for_title(
    PorpoiseHostAdapter *adapter,
    const char *dvd_root_directory,
    int initialize_dvd)
{
    return porpoise_libporpoise_adapter_init_internal(
        adapter, dvd_root_directory, initialize_dvd);
}

PorpoiseHostResult porpoise_libporpoise_configure_title_arena(
    const PorpoiseHostAdapter *adapter,
    uint32_t guest_arena_lo,
    uint32_t guest_arena_hi)
{
    PorpoiseLibporpoiseContext *context;
    PorpoiseHostResult result;
    void *arena_lo;
    void *arena_hi;
    void *expected_lo;
    void *expected_hi;
    void *previous_lo;
    void *previous_hi;
    uint32_t span;

    if (adapter == NULL ||
        adapter->read_bytes != porpoise_libporpoise_read_bytes ||
        adapter->write_bytes != porpoise_libporpoise_write_bytes ||
        adapter->decode_pointer != porpoise_libporpoise_decode_pointer ||
        adapter->encode_pointer != porpoise_libporpoise_encode_pointer) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    context = (PorpoiseLibporpoiseContext *)adapter->context;
    if (context == NULL || context != porpoise_libporpoise_active_context ||
        context->owner_adapter != adapter) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (context->arena_configuration_poisoned) {
        return PORPOISE_HOST_IO_ERROR;
    }
    if (context->arena_configured) {
        if (guest_arena_lo != context->arena_configured_base ||
            guest_arena_hi != context->arena_configured_limit ||
            !porpoise_libporpoise_arena_native_pointer(
                context, context->arena_guest_lo, &expected_lo) ||
            !porpoise_libporpoise_arena_native_pointer(
                context, context->arena_guest_hi, &expected_hi) ||
            OSGetArenaLo() != expected_lo || OSGetArenaHi() != expected_hi) {
            return PORPOISE_HOST_INVALID_ARGUMENT;
        }
        return PORPOISE_HOST_OK;
    }
    if (guest_arena_lo == 0U && guest_arena_hi == 0U) {
        return PORPOISE_HOST_OK;
    }
    if (guest_arena_lo == 0U || guest_arena_hi <= guest_arena_lo) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }

    span = guest_arena_hi - guest_arena_lo;
    arena_lo = NULL;
    result = porpoise_libporpoise_decode_span(
        context, guest_arena_lo, (size_t)span, &arena_lo);
    if (result != PORPOISE_HOST_OK) {
        return result;
    }
    if ((uintptr_t)arena_lo > UINTPTR_MAX - (uintptr_t)span) {
        return PORPOISE_HOST_ADDRESS_OVERFLOW;
    }
    arena_hi = (void *)((uintptr_t)arena_lo + (uintptr_t)span);
    previous_lo = OSGetArenaLo();
    previous_hi = OSGetArenaHi();
    context->arena_previous_native_lo = previous_lo;
    context->arena_previous_native_hi = previous_hi;
    context->arena_restore_pending = 1;
    OSSetArenaLo(arena_lo);
    OSSetArenaHi(arena_hi);
    if (OSGetArenaLo() != arena_lo || OSGetArenaHi() != arena_hi) {
        if (porpoise_libporpoise_restore_native_arena(context)) {
            context->arena_previous_native_lo = NULL;
            context->arena_previous_native_hi = NULL;
            return PORPOISE_HOST_INVALID_ARGUMENT;
        }
        context->arena_configuration_poisoned = 1;
        return PORPOISE_HOST_IO_ERROR;
    }
    context->arena_configured_base = guest_arena_lo;
    context->arena_configured_limit = guest_arena_hi;
    context->arena_guest_lo = guest_arena_lo;
    context->arena_guest_hi = guest_arena_hi;
    context->arena_native_base = arena_lo;
    context->arena_configured = 1;
    return PORPOISE_HOST_OK;
}

void porpoise_libporpoise_adapter_shutdown(
    PorpoiseHostAdapter *adapter)
{
    PorpoiseLibporpoiseContext *context;
    BOOL interrupts_were_enabled;
    int gx_callback_diverged;

    if (adapter == NULL ||
        adapter->read_bytes != porpoise_libporpoise_read_bytes ||
        adapter->write_bytes != porpoise_libporpoise_write_bytes ||
        adapter->decode_pointer != porpoise_libporpoise_decode_pointer ||
        adapter->encode_pointer != porpoise_libporpoise_encode_pointer) {
        return;
    }

    context = (PorpoiseLibporpoiseContext *)adapter->context;
    if (context != porpoise_libporpoise_active_context) {
        memset(adapter, 0, sizeof(*adapter));
        return;
    }

    if (context != NULL &&
        porpoise_libporpoise_flush_gx_fifo_context(context) !=
            PORPOISE_HOST_OK) {
        (void)fprintf(
            stderr,
            "Porpoise: failed to submit pending GX FIFO bytes during adapter shutdown\n");
    }

    interrupts_were_enabled = OSDisableInterrupts();
    if (context != NULL &&
        !porpoise_libporpoise_thread_registry_shutdown(
            context->thread_registry)) {
        OSRestoreInterrupts(interrupts_were_enabled);
        (void)fprintf(
            stderr,
            "Porpoise: host-thread carriers did not stop; adapter remains active\n");
        return;
    }
    OSRestoreInterrupts(interrupts_were_enabled);
    porpoise_libporpoise_ar_shutdown(adapter);
    gx_callback_diverged = 0;
    if (context != NULL &&
        context == porpoise_libporpoise_gx_owner_context &&
        porpoise_libporpoise_gx_init_lifecycle ==
            PORPOISE_LIBPORPOISE_GX_INIT_ACTIVE) {
        GXDrawDoneCallback expected =
            context->gx_draw_done_callback != 0U
                ? porpoise_libporpoise_gx_draw_done_callback
                : NULL;
        GXDrawDoneCallback previous;

        interrupts_were_enabled = OSDisableInterrupts();
        previous = GXSetDrawDoneCallback(NULL);
        if (previous != expected) {
            gx_callback_diverged = 1;
        }
        context->gx_draw_done_callback = 0U;
        context->gx_draw_done_event_head = 0U;
        context->gx_draw_done_event_count = 0U;
        porpoise_libporpoise_active_context = NULL;
        OSRestoreInterrupts(interrupts_were_enabled);
    }
    if (gx_callback_diverged) {
        (void)fprintf(
            stderr,
            "Porpoise: native GX draw-done callback diverged during adapter shutdown\n");
    }
    porpoise_libporpoise_active_context = NULL;
    if (context != NULL) {
        size_t index;
        if (!porpoise_libporpoise_restore_native_arena(context)) {
            (void)fprintf(
                stderr,
                "Porpoise: failed to restore native arena bounds during adapter shutdown\n");
        }
        for (index = 0U;
             index < PORPOISE_DVD_MIRROR_CAPACITY;
             index++) {
            porpoise_libporpoise_release_dvd_mirror(
                &context->dvd_files[index]);
        }
        for (index = 0U; index < context->pointer_token_count; index++) {
            __OSHostReleaseAddress(
                context->pointer_tokens[index].guest_token);
        }
        if (porpoise_libporpoise_gx_owner_context == context) {
            porpoise_libporpoise_gx_owner_context = NULL;
        }
        free(context->pointer_tokens);
        free(context->arq_completions);
        porpoise_libporpoise_free_all_dsp_tasks(context);
        porpoise_libporpoise_thread_registry_destroy(
            context->thread_registry);
        free(context);
    }
    memset(adapter, 0, sizeof(*adapter));
}
