#include "porpoise_libporpoise_builtins_private.h"
#include "porpoise_libporpoise_private.h"

#if defined(PORPOISE_AUTODETECT_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
#ifdef __has_include
#if __has_include(<porpoise/host_thread_carrier.h>)
#include <porpoise/host_thread_carrier.h>
#define PORPOISE_LIBPORPOISE_HOST_THREAD_CARRIER_HEADER_INCLUDED 1
#if defined(LIBPORPOISE_HOST_THREAD_CARRIER_API_VERSION) && \
    LIBPORPOISE_HOST_THREAD_CARRIER_API_VERSION == 1U
#define PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1 1
#endif
#endif
#endif
#endif

#if defined(PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
#include "porpoise_dispatch_private.h"
#if !defined(PORPOISE_LIBPORPOISE_HOST_THREAD_CARRIER_HEADER_INCLUDED)
#include <porpoise/host_thread_carrier.h>
#endif
#if defined(LIBPORPOISE_HOST_THREAD_CARRIER_API_VERSION) && \
    LIBPORPOISE_HOST_THREAD_CARRIER_API_VERSION >= 1
#if LIBPORPOISE_HOST_THREAD_CARRIER_API_VERSION != 1U
#error "Porpoise host-thread carrier feature requires API version 1"
#endif
#else
#error "Porpoise host-thread carrier feature requires API version 1"
#endif
#endif

#undef PORPOISE_LIBPORPOISE_HOST_THREAD_CARRIER_HEADER_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PORPOISE_GUEST_THREAD_QUEUE_SIZE = 8,
    PORPOISE_GUEST_THREAD_QUEUE_ALIGNMENT = 4,
    PORPOISE_GUEST_THREAD_SIZE = 0x318,
    PORPOISE_GUEST_THREAD_ALIGNMENT = 4,
    PORPOISE_GUEST_CONTEXT_SIZE = 0x2C8,
    PORPOISE_GUEST_CONTEXT_CR_OFFSET = 0x080,
    PORPOISE_GUEST_CONTEXT_LR_OFFSET = 0x084,
    PORPOISE_GUEST_CONTEXT_CTR_OFFSET = 0x088,
    PORPOISE_GUEST_CONTEXT_XER_OFFSET = 0x08C,
    PORPOISE_GUEST_CONTEXT_FPR_OFFSET = 0x090,
    PORPOISE_GUEST_CONTEXT_FPSCR_OFFSET = 0x194,
    PORPOISE_GUEST_CONTEXT_SRR0_OFFSET = 0x198,
    PORPOISE_GUEST_CONTEXT_SRR1_OFFSET = 0x19C,
    PORPOISE_GUEST_CONTEXT_MODE_OFFSET = 0x1A0,
    PORPOISE_GUEST_CONTEXT_STATE_OFFSET = 0x1A2,
    PORPOISE_GUEST_CONTEXT_GQR_OFFSET = 0x1A4,
    PORPOISE_GUEST_CONTEXT_PSF_OFFSET = 0x1C8,
    PORPOISE_GUEST_THREAD_STATE_OFFSET = 0x2C8,
    PORPOISE_GUEST_THREAD_ATTR_OFFSET = 0x2CA,
    PORPOISE_GUEST_THREAD_SUSPEND_OFFSET = 0x2CC,
    PORPOISE_GUEST_THREAD_PRIORITY_OFFSET = 0x2D0,
    PORPOISE_GUEST_THREAD_BASE_PRIORITY_OFFSET = 0x2D4,
    PORPOISE_GUEST_THREAD_VALUE_OFFSET = 0x2D8,
    PORPOISE_GUEST_THREAD_QUEUE_OFFSET = 0x2DC,
    PORPOISE_GUEST_THREAD_LINK_NEXT_OFFSET = 0x2E0,
    PORPOISE_GUEST_THREAD_LINK_PREVIOUS_OFFSET = 0x2E4,
    PORPOISE_GUEST_THREAD_JOIN_HEAD_OFFSET = 0x2E8,
    PORPOISE_GUEST_THREAD_JOIN_TAIL_OFFSET = 0x2EC,
    PORPOISE_GUEST_THREAD_MUTEX_OFFSET = 0x2F0,
    PORPOISE_GUEST_THREAD_MUTEX_HEAD_OFFSET = 0x2F4,
    PORPOISE_GUEST_THREAD_MUTEX_TAIL_OFFSET = 0x2F8,
    PORPOISE_GUEST_THREAD_STACK_BASE_OFFSET = 0x304,
    PORPOISE_GUEST_THREAD_STACK_END_OFFSET = 0x308,
    PORPOISE_GUEST_THREAD_STATE_NULL = 0,
    PORPOISE_GUEST_THREAD_STATE_READY = 1,
    PORPOISE_GUEST_THREAD_STATE_RUNNING = 2,
    PORPOISE_GUEST_THREAD_STATE_WAITING = 4,
    PORPOISE_GUEST_THREAD_STATE_MORIBUND = 8,
    PORPOISE_GUEST_CONTEXT_STATE_FP_SAVED = 1
};

#define PORPOISE_GUEST_CURRENT_CONTEXT_ADDRESS UINT32_C(0x800000D4)
#define PORPOISE_GUEST_CURRENT_THREAD_ADDRESS UINT32_C(0x800000E4)
#define PORPOISE_GUEST_THREAD_STACK_MAGIC UINT32_C(0xDEADBABE)

typedef struct PorpoiseGuestThreadQueue {
    uint32_t head;
    uint32_t tail;
} PorpoiseGuestThreadQueue;

typedef struct PorpoiseGuestThreadLifecycle {
    uint8_t bytes[PORPOISE_GUEST_THREAD_SIZE];
    uint16_t state;
    uint16_t attr;
    int32_t suspend_count;
    int32_t priority;
    int32_t base_priority;
    uint32_t value;
    uint32_t queue;
    uint32_t link_next;
    uint32_t link_previous;
    uint32_t join_head;
    uint32_t join_tail;
    uint32_t mutex;
    uint32_t mutex_head;
    uint32_t mutex_tail;
    uint32_t stack_base;
    uint32_t stack_end;
} PorpoiseGuestThreadLifecycle;

typedef enum PorpoiseGuestCarrierLifecycle {
    PORPOISE_GUEST_CARRIER_CREATED_PAUSED = 0,
    PORPOISE_GUEST_CARRIER_RUNNING,
    PORPOISE_GUEST_CARRIER_PARKED,
    PORPOISE_GUEST_CARRIER_EXITED,
    PORPOISE_GUEST_CARRIER_STOPPING
} PorpoiseGuestCarrierLifecycle;

typedef struct PorpoiseGuestThreadMirror PorpoiseGuestThreadMirror;

struct PorpoiseLibporpoiseThreadRegistry {
    PorpoiseHostAdapter *owner_adapter;
    PorpoiseBindExportStateFn export_state_binder;
    uint32_t main_guest_thread;
    PorpoisePpcState *main_state;
    int poisoned;
#if defined(PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
    PorpoiseGuestThreadMirror *threads;
#endif
};

#if defined(PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
struct PorpoiseGuestThreadMirror {
    PorpoiseLibporpoiseThreadRegistry *registry;
    PorpoiseGuestThreadMirror *next;
    LibPorpoiseHostThreadCarrier *carrier;
    PorpoisePpcState state;
    uint32_t guest_thread;
    uint32_t entry_address;
    int32_t priority;
    PorpoiseGuestCarrierLifecycle lifecycle;
    uint8_t saved_context[PORPOISE_GUEST_CONTEXT_SIZE];
    char name[32];
};
#endif

static __thread PorpoiseLibporpoiseThreadRegistry *
    porpoise_current_thread_registry;
static __thread PorpoiseGuestThreadMirror *porpoise_current_thread_mirror;
static __thread uint32_t porpoise_current_guest_thread;

int porpoise_libporpoise_thread_registry_create(
    PorpoiseHostAdapter *owner_adapter,
    PorpoiseLibporpoiseThreadRegistry **registry_out)
{
    PorpoiseLibporpoiseThreadRegistry *registry;

    if (registry_out != NULL) {
        *registry_out = NULL;
    }
    if (owner_adapter == NULL || registry_out == NULL) {
        return 0;
    }
    registry = (PorpoiseLibporpoiseThreadRegistry *)calloc(
        1U, sizeof(*registry));
    if (registry == NULL) {
        return 0;
    }
    registry->owner_adapter = owner_adapter;
    *registry_out = registry;
    return 1;
}

int porpoise_libporpoise_thread_registry_set_export_binder(
    PorpoiseLibporpoiseThreadRegistry *registry,
    PorpoiseBindExportStateFn binder)
{
    if (registry == NULL) {
        return 0;
    }
    /* The legacy dispatcher-only binding must not clear a binder installed by
     * the generated runtime transaction. */
    if (binder == NULL) {
        return 1;
    }
    if (registry->export_state_binder != NULL &&
        registry->export_state_binder != binder) {
        return 0;
    }
    registry->export_state_binder = binder;
    return 1;
}

#if defined(PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
static int porpoise_thread_carrier_stop_and_destroy(
    PorpoiseGuestThreadMirror *mirror)
{
    LibPorpoiseHostThreadCarrierResultV1 result;

    if (mirror == NULL || mirror->carrier == NULL) {
        return 0;
    }
    mirror->lifecycle = PORPOISE_GUEST_CARRIER_STOPPING;
    if (!porpoise_state_should_stop(&mirror->state)) {
        mirror->state.status = PORPOISE_EXECUTION_RETURNED;
    }
    result = LibPorpoiseHostThreadCarrierRequestStopV1(mirror->carrier);
    if (result != LIBPORPOISE_HOST_THREAD_CARRIER_OK) {
        return 0;
    }
    result = LibPorpoiseHostThreadCarrierJoinV1(
        mirror->carrier, UINT32_MAX);
    if (result != LIBPORPOISE_HOST_THREAD_CARRIER_OK) {
        return 0;
    }
    result = LibPorpoiseHostThreadCarrierDestroyV1(mirror->carrier);
    if (result != LIBPORPOISE_HOST_THREAD_CARRIER_OK) {
        return 0;
    }
    mirror->carrier = NULL;
    return 1;
}
#endif

int porpoise_libporpoise_thread_registry_shutdown(
    PorpoiseLibporpoiseThreadRegistry *registry)
{
    if (registry == NULL) {
        return 1;
    }
#if defined(PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
    {
        PorpoiseGuestThreadMirror *mirror;

        for (mirror = registry->threads;
             mirror != NULL;
             mirror = mirror->next) {
            if (mirror->carrier != NULL &&
                !porpoise_thread_carrier_stop_and_destroy(mirror)) {
                return 0;
            }
        }
    }
#endif
    if (porpoise_current_thread_registry == registry) {
        porpoise_current_thread_registry = NULL;
        porpoise_current_thread_mirror = NULL;
        porpoise_current_guest_thread = 0U;
    }
    registry->main_guest_thread = 0U;
    registry->main_state = NULL;
    return 1;
}

void porpoise_libporpoise_thread_registry_destroy(
    PorpoiseLibporpoiseThreadRegistry *registry)
{
    if (registry == NULL) {
        return;
    }
#if defined(PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
    while (registry->threads != NULL) {
        PorpoiseGuestThreadMirror *next = registry->threads->next;
        if (registry->threads->carrier != NULL) {
            /* Shutdown's success is a required precondition. Leaking is safer
             * than freeing storage still reachable from a native callback. */
            return;
        }
        free(registry->threads);
        registry->threads = next;
    }
#endif
    free(registry);
}

static uint16_t porpoise_read_be_u16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8U) |
                      (uint16_t)bytes[1]);
}

static uint32_t porpoise_read_be_u32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24U) |
           ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) |
           (uint32_t)bytes[3];
}

static int porpoise_guest_spans_overlap(
    uint32_t left_address,
    size_t left_size,
    uint32_t right_address,
    size_t right_size)
{
    uint64_t left_end = (uint64_t)left_address + (uint64_t)left_size;
    uint64_t right_end = (uint64_t)right_address + (uint64_t)right_size;

    return left_size != 0U && right_size != 0U &&
           (uint64_t)left_address < right_end &&
           (uint64_t)right_address < left_end;
}

#if defined(PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
static uint64_t porpoise_read_be_u64(const uint8_t *bytes)
{
    return ((uint64_t)porpoise_read_be_u32(bytes) << 32U) |
           (uint64_t)porpoise_read_be_u32(bytes + 4U);
}

static void porpoise_write_be_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8U);
    bytes[1] = (uint8_t)value;
}

static void porpoise_write_be_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
}

static void porpoise_write_be_u64(uint8_t *bytes, uint64_t value)
{
    porpoise_write_be_u32(bytes, (uint32_t)(value >> 32U));
    porpoise_write_be_u32(bytes + 4U, (uint32_t)value);
}
#endif

static PorpoiseFault porpoise_guest_os_fault_from_host_result(
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

static int porpoise_read_guest_thread_queue(
    PorpoisePpcState *state,
    PorpoiseGuestThreadQueue *queue)
{
    uint8_t bytes[PORPOISE_GUEST_THREAD_QUEUE_SIZE];
    uint32_t guest_address;
    PorpoiseHostResult result;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return 0;
    }

    guest_address = state->gpr[3];
    if (guest_address == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_address,
            "OSThreadQueue guest pointer is null");
        return 0;
    }
    if ((guest_address &
         (uint32_t)(PORPOISE_GUEST_THREAD_QUEUE_ALIGNMENT - 1)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_address,
            "OSThreadQueue guest pointer is not 4-byte aligned");
        return 0;
    }
    if (state->host == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_NO_HOST_ADAPTER,
            guest_address,
            "OSThreadQueue access requires a host adapter");
        return 0;
    }
    if (state->host->read_bytes == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            guest_address,
            "OSThreadQueue access requires the host read callback");
        return 0;
    }

    /* One exact-width read validates the complete guest queue span before
     * either link is interpreted. The host adapter remains responsible for
     * rejecting unmapped, overflowing, or MMIO-backed address ranges. */
    result = state->host->read_bytes(
        state->host->context,
        guest_address,
        bytes,
        sizeof(bytes));
    if (result != PORPOISE_HOST_OK) {
        porpoise_state_set_fault(
            state,
            porpoise_guest_os_fault_from_host_result(result),
            guest_address,
            porpoise_host_result_string(result));
        return 0;
    }

    queue->head = porpoise_read_be_u32(bytes);
    queue->tail = porpoise_read_be_u32(bytes + sizeof(uint32_t));
    if ((queue->head == 0U) != (queue->tail == 0U)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            guest_address,
            "guest OSThreadQueue head and tail are inconsistent");
        return 0;
    }
    return 1;
}

static int porpoise_guest_thread_state_is_valid(uint16_t thread_state)
{
    return thread_state == PORPOISE_GUEST_THREAD_STATE_NULL ||
           thread_state == PORPOISE_GUEST_THREAD_STATE_READY ||
           thread_state == PORPOISE_GUEST_THREAD_STATE_RUNNING ||
           thread_state == PORPOISE_GUEST_THREAD_STATE_WAITING ||
           thread_state == PORPOISE_GUEST_THREAD_STATE_MORIBUND;
}

static void porpoise_parse_guest_thread(
    PorpoiseGuestThreadLifecycle *thread)
{
    thread->state = porpoise_read_be_u16(
        thread->bytes + PORPOISE_GUEST_THREAD_STATE_OFFSET);
    thread->attr = porpoise_read_be_u16(
        thread->bytes + PORPOISE_GUEST_THREAD_ATTR_OFFSET);
    thread->suspend_count = (int32_t)porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_THREAD_SUSPEND_OFFSET);
    thread->priority = (int32_t)porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_THREAD_PRIORITY_OFFSET);
    thread->base_priority = (int32_t)porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_THREAD_BASE_PRIORITY_OFFSET);
    thread->value = porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_THREAD_VALUE_OFFSET);
    thread->queue = porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_THREAD_QUEUE_OFFSET);
    thread->link_next = porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_THREAD_LINK_NEXT_OFFSET);
    thread->link_previous = porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_THREAD_LINK_PREVIOUS_OFFSET);
    thread->join_head = porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_THREAD_JOIN_HEAD_OFFSET);
    thread->join_tail = porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_THREAD_JOIN_TAIL_OFFSET);
    thread->mutex = porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_THREAD_MUTEX_OFFSET);
    thread->mutex_head = porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_THREAD_MUTEX_HEAD_OFFSET);
    thread->mutex_tail = porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_THREAD_MUTEX_TAIL_OFFSET);
    thread->stack_base = porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_THREAD_STACK_BASE_OFFSET);
    thread->stack_end = porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_THREAD_STACK_END_OFFSET);
}

static int porpoise_read_guest_thread_at(
    PorpoisePpcState *state,
    uint32_t guest_address,
    PorpoiseGuestThreadLifecycle *thread)
{
    PorpoiseHostResult result;

    if (state == NULL || thread == NULL || porpoise_state_should_stop(state)) {
        return 0;
    }
    if (guest_address == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_address,
            "OSThread guest pointer is null");
        return 0;
    }
    if ((guest_address &
         (uint32_t)(PORPOISE_GUEST_THREAD_ALIGNMENT - 1)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_address,
            "OSThread guest pointer is not 4-byte aligned");
        return 0;
    }
    if (state->host == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_NO_HOST_ADAPTER,
            guest_address,
            "OSThread access requires a host adapter");
        return 0;
    }
    if (state->host->read_bytes == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            guest_address,
            "OSThread access requires the host read callback");
        return 0;
    }

    /* Validate the complete guest SDK object in one exact-width read. Native
     * libPorpoise appends host-only fields and therefore must never receive a
     * pointer to this 0x318-byte guest allocation. */
    result = state->host->read_bytes(
        state->host->context,
        guest_address,
        thread->bytes,
        sizeof(thread->bytes));
    if (result != PORPOISE_HOST_OK) {
        porpoise_state_set_fault(
            state,
            porpoise_guest_os_fault_from_host_result(result),
            guest_address,
            porpoise_host_result_string(result));
        return 0;
    }

    porpoise_parse_guest_thread(thread);
    if (!porpoise_guest_thread_state_is_valid(thread->state)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            guest_address,
            "guest OSThread has an invalid lifecycle state");
        return 0;
    }
    return 1;
}

#if !defined(PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
static int porpoise_read_guest_thread(
    PorpoisePpcState *state,
    PorpoiseGuestThreadLifecycle *thread)
{
    if (state == NULL) {
        return 0;
    }
    return porpoise_read_guest_thread_at(state, state->gpr[3], thread);
}
#endif

static PorpoiseHostResult porpoise_guest_read_raw(
    PorpoisePpcState *state,
    uint32_t guest_address,
    void *destination,
    size_t size)
{
    if (state == NULL || state->host == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (state->host->read_bytes == NULL) {
        return PORPOISE_HOST_IO_ERROR;
    }
    return state->host->read_bytes(
        state->host->context, guest_address, destination, size);
}

#if defined(PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
static PorpoiseHostResult porpoise_guest_write_raw(
    PorpoisePpcState *state,
    uint32_t guest_address,
    const void *source,
    size_t size)
{
    if (state == NULL || state->host == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (state->host->write_bytes == NULL) {
        return PORPOISE_HOST_IO_ERROR;
    }
    return state->host->write_bytes(
        state->host->context, guest_address, source, size);
}
#endif

static PorpoiseHostResult porpoise_read_guest_word_result(
    PorpoisePpcState *state,
    uint32_t guest_address,
    uint32_t *value_out)
{
    uint8_t bytes[4];
    PorpoiseHostResult result;

    if (value_out == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    *value_out = 0U;
    result = porpoise_guest_read_raw(
        state, guest_address, bytes, sizeof(bytes));
    if (result == PORPOISE_HOST_OK) {
        *value_out = porpoise_read_be_u32(bytes);
    }
    return result;
}

PorpoiseHostResult porpoise_libporpoise_bind_guest_main_thread(
    PorpoisePpcState *state)
{
    PorpoiseLibporpoiseThreadRegistry *registry;
    PorpoiseGuestThreadLifecycle thread;
    PorpoiseHostResult result;
    uint32_t current_context;
    uint32_t current_thread;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    registry = porpoise_libporpoise_thread_registry_for_state(state);
    if (registry == NULL || registry->poisoned) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    result = porpoise_read_guest_word_result(
        state,
        PORPOISE_GUEST_CURRENT_CONTEXT_ADDRESS,
        &current_context);
    if (result != PORPOISE_HOST_OK) {
        return result;
    }
    result = porpoise_read_guest_word_result(
        state,
        PORPOISE_GUEST_CURRENT_THREAD_ADDRESS,
        &current_thread);
    if (result != PORPOISE_HOST_OK) {
        return result;
    }
    if (current_thread == 0U || current_context != current_thread ||
        (current_thread & (PORPOISE_GUEST_THREAD_ALIGNMENT - 1U)) != 0U) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (porpoise_guest_spans_overlap(
            current_thread,
            PORPOISE_GUEST_THREAD_SIZE,
            PORPOISE_GUEST_CURRENT_CONTEXT_ADDRESS,
            sizeof(uint32_t)) ||
        porpoise_guest_spans_overlap(
            current_thread,
            PORPOISE_GUEST_THREAD_SIZE,
            PORPOISE_GUEST_CURRENT_THREAD_ADDRESS,
            sizeof(uint32_t))) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    result = porpoise_guest_read_raw(
        state,
        current_thread,
        thread.bytes,
        sizeof(thread.bytes));
    if (result != PORPOISE_HOST_OK) {
        return result;
    }
    porpoise_parse_guest_thread(&thread);
    if (thread.state != PORPOISE_GUEST_THREAD_STATE_RUNNING ||
        thread.suspend_count != 0 || thread.priority < 0 ||
        thread.priority > 31 || thread.base_priority < 0 ||
        thread.base_priority > 31) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if ((registry->main_guest_thread != 0U &&
         registry->main_guest_thread != current_thread) ||
        (registry->main_state != NULL && registry->main_state != state) ||
        (porpoise_current_thread_registry != NULL &&
         porpoise_current_thread_registry != registry) ||
        (porpoise_current_guest_thread != 0U &&
         porpoise_current_guest_thread != current_thread)) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }

    registry->main_guest_thread = current_thread;
    registry->main_state = state;
    porpoise_current_thread_registry = registry;
    porpoise_current_thread_mirror = NULL;
    porpoise_current_guest_thread = current_thread;
    return PORPOISE_HOST_OK;
}

void porpoise_libporpoise_os_get_current_thread_adapter(
    PorpoisePpcState *state)
{
    PorpoiseLibporpoiseThreadRegistry *registry;
    uint32_t mirrored_address;
    PorpoiseHostResult result;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    state->gpr[3] = 0U;
    registry = porpoise_libporpoise_thread_registry_for_state(state);
    if (registry == NULL) {
        return;
    }
    if (registry->poisoned || porpoise_current_thread_registry != registry ||
        porpoise_current_guest_thread == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            state->pc,
            "OSGetCurrentThread requires an established guest thread identity");
        return;
    }
    result = porpoise_read_guest_word_result(
        state,
        PORPOISE_GUEST_CURRENT_THREAD_ADDRESS,
        &mirrored_address);
    if (result != PORPOISE_HOST_OK) {
        porpoise_state_set_fault(
            state,
            porpoise_guest_os_fault_from_host_result(result),
            PORPOISE_GUEST_CURRENT_THREAD_ADDRESS,
            porpoise_host_result_string(result));
        return;
    }
    if (mirrored_address != porpoise_current_guest_thread) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            PORPOISE_GUEST_CURRENT_THREAD_ADDRESS,
            "guest current-thread word diverged from the Tool carrier identity");
        return;
    }
    state->gpr[3] = porpoise_current_guest_thread;
}

#if defined(PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
typedef struct PorpoiseGuestWriteSpan {
    uint32_t address;
    size_t size;
    const uint8_t *old_bytes;
    const uint8_t *new_bytes;
} PorpoiseGuestWriteSpan;

static void porpoise_guest_carrier_fault(
    PorpoisePpcState *state,
    PorpoiseFault fault,
    uint32_t address,
    const char *message)
{
    if (state != NULL) {
        porpoise_state_set_fault(state, fault, address, message);
    }
}

static void porpoise_guest_carrier_result_fault(
    PorpoisePpcState *state,
    uint32_t address,
    LibPorpoiseHostThreadCarrierResultV1 result,
    const char *operation)
{
    PorpoiseFault fault = PORPOISE_FAULT_HOST_IO;

    if (result == LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_ARGUMENT) {
        fault = PORPOISE_FAULT_INVALID_ARGUMENT;
    } else if (result == LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_STATE) {
        fault = PORPOISE_FAULT_INVALID_STATE;
    }
    porpoise_guest_carrier_fault(state, fault, address, operation);
}

static int porpoise_guest_commit_spans(
    PorpoisePpcState *state,
    PorpoiseLibporpoiseThreadRegistry *registry,
    const PorpoiseGuestWriteSpan *spans,
    size_t span_count,
    const char *message)
{
    size_t index;
    PorpoiseHostResult result = PORPOISE_HOST_OK;

    if (state == NULL || registry == NULL || spans == NULL ||
        span_count == 0U) {
        return 0;
    }

    for (index = 0U; index < span_count; index++) {
        size_t other_index;
        for (other_index = index + 1U;
             other_index < span_count;
             other_index++) {
            if (porpoise_guest_spans_overlap(
                    spans[index].address,
                    spans[index].size,
                    spans[other_index].address,
                    spans[other_index].size)) {
                porpoise_guest_carrier_fault(
                    state,
                    PORPOISE_FAULT_INVALID_ARGUMENT,
                    spans[other_index].address,
                    "guest thread transaction spans overlap");
                return 0;
            }
        }
    }

    /* The adapter callback validates the entire span before mutation. An
     * idempotent write therefore preflights every destination while the old
     * bytes are still authoritative. */
    for (index = 0U; index < span_count; index++) {
        result = porpoise_guest_write_raw(
            state,
            spans[index].address,
            spans[index].old_bytes,
            spans[index].size);
        if (result != PORPOISE_HOST_OK) {
            porpoise_guest_carrier_fault(
                state,
                porpoise_guest_os_fault_from_host_result(result),
                spans[index].address,
                message);
            return 0;
        }
    }

    for (index = 0U; index < span_count; index++) {
        result = porpoise_guest_write_raw(
            state,
            spans[index].address,
            spans[index].new_bytes,
            spans[index].size);
        if (result != PORPOISE_HOST_OK) {
            size_t rollback_index;
            int rollback_succeeded = 1;

            /* Include the failed span because a host callback is allowed to
             * discover an I/O failure only after attempting the copy. */
            for (rollback_index = 0U;
                 rollback_index < span_count;
                 rollback_index++) {
                if (porpoise_guest_write_raw(
                        state,
                        spans[rollback_index].address,
                        spans[rollback_index].old_bytes,
                        spans[rollback_index].size) != PORPOISE_HOST_OK) {
                    rollback_succeeded = 0;
                }
            }
            if (!rollback_succeeded) {
                registry->poisoned = 1;
            }
            porpoise_guest_carrier_fault(
                state,
                porpoise_guest_os_fault_from_host_result(result),
                spans[index].address,
                rollback_succeeded
                    ? message
                    : "guest thread transaction failed and rollback was incomplete");
            return 0;
        }
    }
    return 1;
}

static int porpoise_guest_restore_span(
    PorpoisePpcState *state,
    PorpoiseLibporpoiseThreadRegistry *registry,
    uint32_t address,
    const uint8_t *bytes,
    size_t size,
    const char *message)
{
    PorpoiseHostResult result = porpoise_guest_write_raw(
        state, address, bytes, size);
    if (result == PORPOISE_HOST_OK) {
        return 1;
    }
    registry->poisoned = 1;
    porpoise_guest_carrier_fault(
        state,
        porpoise_guest_os_fault_from_host_result(result),
        address,
        message);
    return 0;
}

static void porpoise_guest_thread_set_state(
    uint8_t *bytes,
    uint16_t thread_state,
    int32_t suspend_count)
{
    porpoise_write_be_u16(
        bytes + PORPOISE_GUEST_THREAD_STATE_OFFSET, thread_state);
    porpoise_write_be_u32(
        bytes + PORPOISE_GUEST_THREAD_SUSPEND_OFFSET,
        (uint32_t)suspend_count);
}

static int porpoise_guest_read_current_words(
    PorpoisePpcState *state,
    uint8_t context_bytes[4],
    uint8_t thread_bytes[4],
    uint32_t *context_out,
    uint32_t *thread_out)
{
    PorpoiseHostResult result;

    result = porpoise_guest_read_raw(
        state,
        PORPOISE_GUEST_CURRENT_CONTEXT_ADDRESS,
        context_bytes,
        4U);
    if (result != PORPOISE_HOST_OK) {
        porpoise_guest_carrier_fault(
            state,
            porpoise_guest_os_fault_from_host_result(result),
            PORPOISE_GUEST_CURRENT_CONTEXT_ADDRESS,
            "could not read the guest current-context word");
        return 0;
    }
    result = porpoise_guest_read_raw(
        state,
        PORPOISE_GUEST_CURRENT_THREAD_ADDRESS,
        thread_bytes,
        4U);
    if (result != PORPOISE_HOST_OK) {
        porpoise_guest_carrier_fault(
            state,
            porpoise_guest_os_fault_from_host_result(result),
            PORPOISE_GUEST_CURRENT_THREAD_ADDRESS,
            "could not read the guest current-thread word");
        return 0;
    }
    *context_out = porpoise_read_be_u32(context_bytes);
    *thread_out = porpoise_read_be_u32(thread_bytes);
    return 1;
}

static int porpoise_guest_thread_has_narrow_scheduler_state(
    PorpoisePpcState *state,
    uint32_t guest_address,
    const PorpoiseGuestThreadLifecycle *thread)
{
    uint8_t magic_bytes[4];
    PorpoiseHostResult result;

    if (thread->attr != 0U || thread->priority < 0 ||
        thread->priority > 31 || thread->base_priority != thread->priority ||
        thread->queue != 0U || thread->link_next != 0U ||
        thread->link_previous != 0U || thread->join_head != 0U ||
        thread->join_tail != 0U || thread->mutex != 0U ||
        thread->mutex_head != 0U || thread->mutex_tail != 0U) {
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            guest_address,
            "guest OSThread uses scheduler state outside carrier v1");
        return 0;
    }
    if (thread->stack_base == 0U || thread->stack_end == 0U ||
        thread->stack_base <= thread->stack_end ||
        (thread->stack_base & UINT32_C(7)) != 0U ||
        (thread->stack_end & UINT32_C(3)) != 0U) {
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            guest_address,
            "guest OSThread has invalid stack bounds");
        return 0;
    }
    result = porpoise_guest_read_raw(
        state, thread->stack_end, magic_bytes, sizeof(magic_bytes));
    if (result != PORPOISE_HOST_OK) {
        porpoise_guest_carrier_fault(
            state,
            porpoise_guest_os_fault_from_host_result(result),
            thread->stack_end,
            "could not validate the guest thread stack sentinel");
        return 0;
    }
    if (porpoise_read_be_u32(magic_bytes) !=
        PORPOISE_GUEST_THREAD_STACK_MAGIC) {
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            thread->stack_end,
            "guest OSThread stack sentinel is missing");
        return 0;
    }
    return 1;
}

static void porpoise_copy_machine_global_state(
    PorpoisePpcState *destination,
    const PorpoisePpcState *source)
{
    memcpy(destination->sprg, source->sprg, sizeof(destination->sprg));
    destination->srr0 = source->srr0;
    destination->srr1 = source->srr1;
    destination->dar = source->dar;
    destination->dsisr = source->dsisr;
    destination->sdr1 = source->sdr1;
    destination->ear = source->ear;
    destination->pvr = source->pvr;
    memcpy(
        destination->segment_register,
        source->segment_register,
        sizeof(destination->segment_register));
    memcpy(destination->ibat_upper, source->ibat_upper,
           sizeof(destination->ibat_upper));
    memcpy(destination->ibat_lower, source->ibat_lower,
           sizeof(destination->ibat_lower));
    memcpy(destination->dbat_upper, source->dbat_upper,
           sizeof(destination->dbat_upper));
    memcpy(destination->dbat_lower, source->dbat_lower,
           sizeof(destination->dbat_lower));
    destination->hid0 = source->hid0;
    destination->hid1 = source->hid1;
    destination->hid2 = source->hid2;
    destination->hid4 = source->hid4;
    destination->l2cr = source->l2cr;
    destination->ictc = source->ictc;
    destination->wpar = source->wpar;
    destination->dma_upper = source->dma_upper;
    destination->dma_lower = source->dma_lower;
    destination->iabr = source->iabr;
    destination->dabr = source->dabr;
    memcpy(destination->mmcr, source->mmcr, sizeof(destination->mmcr));
    memcpy(destination->pmc, source->pmc, sizeof(destination->pmc));
    destination->sia = source->sia;
    destination->sda = source->sda;
    memcpy(
        destination->thermal_management,
        source->thermal_management,
        sizeof(destination->thermal_management));
    memcpy(
        destination->opaque_spr,
        source->opaque_spr,
        sizeof(destination->opaque_spr));
    destination->time_base_bias = source->time_base_bias;
    destination->decrementer_value = source->decrementer_value;
    destination->decrementer_anchor = source->decrementer_anchor;
    destination->decrementer_valid = source->decrementer_valid;
}

static void porpoise_load_guest_context(
    PorpoisePpcState *state,
    const uint8_t *bytes)
{
    size_t index;

    for (index = 0U; index < 32U; index++) {
        state->gpr[index] = porpoise_read_be_u32(bytes + index * 4U);
        state->fpr[index].lane_bits[0] = porpoise_read_be_u64(
            bytes + PORPOISE_GUEST_CONTEXT_FPR_OFFSET + index * 8U);
        state->fpr[index].lane_bits[1] = porpoise_read_be_u64(
            bytes + PORPOISE_GUEST_CONTEXT_PSF_OFFSET + index * 8U);
    }
    state->cr = porpoise_read_be_u32(
        bytes + PORPOISE_GUEST_CONTEXT_CR_OFFSET);
    state->lr = porpoise_read_be_u32(
        bytes + PORPOISE_GUEST_CONTEXT_LR_OFFSET);
    state->ctr = porpoise_read_be_u32(
        bytes + PORPOISE_GUEST_CONTEXT_CTR_OFFSET);
    state->xer = porpoise_read_be_u32(
        bytes + PORPOISE_GUEST_CONTEXT_XER_OFFSET);
    state->fpscr = porpoise_read_be_u32(
        bytes + PORPOISE_GUEST_CONTEXT_FPSCR_OFFSET);
    state->srr0 = porpoise_read_be_u32(
        bytes + PORPOISE_GUEST_CONTEXT_SRR0_OFFSET);
    state->srr1 = porpoise_read_be_u32(
        bytes + PORPOISE_GUEST_CONTEXT_SRR1_OFFSET);
    state->pc = state->srr0;
    state->msr = state->srr1;
    for (index = 0U; index < 8U; index++) {
        state->gqr[index] = porpoise_read_be_u32(
            bytes + PORPOISE_GUEST_CONTEXT_GQR_OFFSET + index * 4U);
    }
}

static void porpoise_save_guest_context(
    uint8_t *bytes,
    const PorpoisePpcState *state,
    uint32_t resume_address)
{
    size_t index;

    for (index = 0U; index < 32U; index++) {
        porpoise_write_be_u32(bytes + index * 4U, state->gpr[index]);
        porpoise_write_be_u64(
            bytes + PORPOISE_GUEST_CONTEXT_FPR_OFFSET + index * 8U,
            state->fpr[index].lane_bits[0]);
        porpoise_write_be_u64(
            bytes + PORPOISE_GUEST_CONTEXT_PSF_OFFSET + index * 8U,
            state->fpr[index].lane_bits[1]);
    }
    porpoise_write_be_u32(
        bytes + PORPOISE_GUEST_CONTEXT_CR_OFFSET, state->cr);
    porpoise_write_be_u32(
        bytes + PORPOISE_GUEST_CONTEXT_LR_OFFSET, state->lr);
    porpoise_write_be_u32(
        bytes + PORPOISE_GUEST_CONTEXT_CTR_OFFSET, state->ctr);
    porpoise_write_be_u32(
        bytes + PORPOISE_GUEST_CONTEXT_XER_OFFSET, state->xer);
    porpoise_write_be_u32(
        bytes + PORPOISE_GUEST_CONTEXT_FPSCR_OFFSET, state->fpscr);
    porpoise_write_be_u32(
        bytes + PORPOISE_GUEST_CONTEXT_SRR0_OFFSET, resume_address);
    porpoise_write_be_u32(
        bytes + PORPOISE_GUEST_CONTEXT_SRR1_OFFSET, state->msr);
    porpoise_write_be_u16(
        bytes + PORPOISE_GUEST_CONTEXT_MODE_OFFSET, 0U);
    porpoise_write_be_u16(
        bytes + PORPOISE_GUEST_CONTEXT_STATE_OFFSET,
        PORPOISE_GUEST_CONTEXT_STATE_FP_SAVED);
    for (index = 0U; index < 8U; index++) {
        porpoise_write_be_u32(
            bytes + PORPOISE_GUEST_CONTEXT_GQR_OFFSET + index * 4U,
            state->gqr[index]);
    }
}

static PorpoiseGuestThreadMirror *porpoise_find_guest_thread_mirror(
    PorpoiseLibporpoiseThreadRegistry *registry,
    uint32_t guest_thread)
{
    PorpoiseGuestThreadMirror *mirror;

    for (mirror = registry->threads; mirror != NULL; mirror = mirror->next) {
        if (mirror->guest_thread == guest_thread) {
            return mirror;
        }
    }
    return NULL;
}

static int porpoise_require_guest_main_caller(
    PorpoisePpcState *state,
    PorpoiseLibporpoiseThreadRegistry **registry_out)
{
    PorpoiseLibporpoiseThreadRegistry *registry =
        porpoise_libporpoise_thread_registry_for_state(state);

    if (registry_out != NULL) {
        *registry_out = NULL;
    }
    if (registry == NULL || registry->poisoned ||
        registry->main_state != state || registry->main_guest_thread == 0U ||
        porpoise_current_thread_registry != registry ||
        porpoise_current_thread_mirror != NULL ||
        porpoise_current_guest_thread != registry->main_guest_thread) {
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            state != NULL ? state->pc : 0U,
            "guest thread operation requires the canonical main thread");
        return 0;
    }
    if (registry_out != NULL) {
        *registry_out = registry;
    }
    return 1;
}

static int porpoise_guest_handoff_to_child(
    PorpoiseGuestThreadMirror *mirror)
{
    PorpoiseLibporpoiseThreadRegistry *registry = mirror->registry;
    PorpoiseGuestThreadLifecycle main_thread;
    PorpoiseGuestThreadLifecycle target_thread;
    uint8_t new_main[PORPOISE_GUEST_THREAD_SIZE];
    uint8_t old_context_word[4];
    uint8_t old_thread_word[4];
    uint8_t new_context_word[4];
    uint8_t new_thread_word[4];
    uint32_t current_context;
    uint32_t current_thread;
    PorpoiseGuestWriteSpan spans[3];

    if (!porpoise_read_guest_thread_at(
            &mirror->state, registry->main_guest_thread, &main_thread) ||
        !porpoise_read_guest_thread_at(
            &mirror->state, mirror->guest_thread, &target_thread) ||
        !porpoise_guest_read_current_words(
            &mirror->state,
            old_context_word,
            old_thread_word,
            &current_context,
            &current_thread)) {
        return 0;
    }
    if (main_thread.state != PORPOISE_GUEST_THREAD_STATE_RUNNING ||
        main_thread.suspend_count != 0 ||
        target_thread.state != PORPOISE_GUEST_THREAD_STATE_RUNNING ||
        target_thread.suspend_count != 0 ||
        current_context != registry->main_guest_thread ||
        current_thread != registry->main_guest_thread) {
        porpoise_guest_carrier_fault(
            &mirror->state,
            PORPOISE_FAULT_INVALID_STATE,
            mirror->guest_thread,
            "guest scheduler state diverged before carrier handoff");
        return 0;
    }

    memcpy(new_main, main_thread.bytes, sizeof(new_main));
    porpoise_guest_thread_set_state(
        new_main, PORPOISE_GUEST_THREAD_STATE_READY, 0);
    porpoise_write_be_u32(new_context_word, mirror->guest_thread);
    porpoise_write_be_u32(new_thread_word, mirror->guest_thread);
    spans[0].address = registry->main_guest_thread;
    spans[0].size = sizeof(new_main);
    spans[0].old_bytes = main_thread.bytes;
    spans[0].new_bytes = new_main;
    spans[1].address = PORPOISE_GUEST_CURRENT_CONTEXT_ADDRESS;
    spans[1].size = sizeof(new_context_word);
    spans[1].old_bytes = old_context_word;
    spans[1].new_bytes = new_context_word;
    spans[2].address = PORPOISE_GUEST_CURRENT_THREAD_ADDRESS;
    spans[2].size = sizeof(new_thread_word);
    spans[2].old_bytes = old_thread_word;
    spans[2].new_bytes = new_thread_word;
    if (!porpoise_guest_commit_spans(
            &mirror->state,
            registry,
            spans,
            sizeof(spans) / sizeof(spans[0]),
            "could not commit guest carrier handoff")) {
        return 0;
    }
    porpoise_copy_machine_global_state(
        &mirror->state, registry->main_state);
    mirror->state.srr0 = porpoise_read_be_u32(
        mirror->saved_context + PORPOISE_GUEST_CONTEXT_SRR0_OFFSET);
    mirror->state.srr1 = porpoise_read_be_u32(
        mirror->saved_context + PORPOISE_GUEST_CONTEXT_SRR1_OFFSET);
    return 1;
}

static int porpoise_guest_handoff_to_main(
    PorpoiseGuestThreadMirror *mirror,
    uint16_t target_state,
    int32_t target_suspend_count,
    uint32_t target_value,
    int save_context)
{
    PorpoiseLibporpoiseThreadRegistry *registry = mirror->registry;
    PorpoiseGuestThreadLifecycle main_thread;
    PorpoiseGuestThreadLifecycle target_thread;
    uint8_t new_main[PORPOISE_GUEST_THREAD_SIZE];
    uint8_t new_target[PORPOISE_GUEST_THREAD_SIZE];
    uint8_t old_context_word[4];
    uint8_t old_thread_word[4];
    uint8_t new_context_word[4];
    uint8_t new_thread_word[4];
    uint32_t current_context;
    uint32_t current_thread;
    PorpoiseGuestWriteSpan spans[4];

    if (!porpoise_read_guest_thread_at(
            &mirror->state, registry->main_guest_thread, &main_thread) ||
        !porpoise_read_guest_thread_at(
            &mirror->state, mirror->guest_thread, &target_thread) ||
        !porpoise_guest_read_current_words(
            &mirror->state,
            old_context_word,
            old_thread_word,
            &current_context,
            &current_thread)) {
        return 0;
    }
    if (main_thread.state != PORPOISE_GUEST_THREAD_STATE_READY ||
        main_thread.suspend_count != 0 ||
        target_thread.state != PORPOISE_GUEST_THREAD_STATE_RUNNING ||
        target_thread.suspend_count != 0 ||
        current_context != mirror->guest_thread ||
        current_thread != mirror->guest_thread) {
        porpoise_guest_carrier_fault(
            &mirror->state,
            PORPOISE_FAULT_INVALID_STATE,
            mirror->guest_thread,
            "guest scheduler state diverged while returning to the main thread");
        return 0;
    }
    if (!porpoise_guest_thread_has_narrow_scheduler_state(
            &mirror->state, mirror->guest_thread, &target_thread)) {
        return 0;
    }

    memcpy(new_main, main_thread.bytes, sizeof(new_main));
    memcpy(new_target, target_thread.bytes, sizeof(new_target));
    porpoise_guest_thread_set_state(
        new_main, PORPOISE_GUEST_THREAD_STATE_RUNNING, 0);
    porpoise_guest_thread_set_state(
        new_target, target_state, target_suspend_count);
    if (save_context) {
        porpoise_save_guest_context(
            new_target, &mirror->state, mirror->state.lr);
    }
    if (target_state == PORPOISE_GUEST_THREAD_STATE_MORIBUND) {
        porpoise_write_be_u32(
            new_target + PORPOISE_GUEST_THREAD_VALUE_OFFSET,
            target_value);
        porpoise_write_be_u16(
            new_target + PORPOISE_GUEST_CONTEXT_MODE_OFFSET, 0U);
        porpoise_write_be_u16(
            new_target + PORPOISE_GUEST_CONTEXT_STATE_OFFSET, 0U);
    }
    porpoise_write_be_u32(
        new_context_word, registry->main_guest_thread);
    porpoise_write_be_u32(
        new_thread_word, registry->main_guest_thread);
    spans[0].address = mirror->guest_thread;
    spans[0].size = sizeof(new_target);
    spans[0].old_bytes = target_thread.bytes;
    spans[0].new_bytes = new_target;
    spans[1].address = registry->main_guest_thread;
    spans[1].size = sizeof(new_main);
    spans[1].old_bytes = main_thread.bytes;
    spans[1].new_bytes = new_main;
    spans[2].address = PORPOISE_GUEST_CURRENT_CONTEXT_ADDRESS;
    spans[2].size = sizeof(new_context_word);
    spans[2].old_bytes = old_context_word;
    spans[2].new_bytes = new_context_word;
    spans[3].address = PORPOISE_GUEST_CURRENT_THREAD_ADDRESS;
    spans[3].size = sizeof(new_thread_word);
    spans[3].old_bytes = old_thread_word;
    spans[3].new_bytes = new_thread_word;
    if (!porpoise_guest_commit_spans(
            &mirror->state,
            registry,
            spans,
            sizeof(spans) / sizeof(spans[0]),
            "could not commit guest carrier return")) {
        return 0;
    }
    if (save_context) {
        memcpy(
            mirror->saved_context,
            new_target,
            sizeof(mirror->saved_context));
    }
    porpoise_copy_machine_global_state(
        registry->main_state, &mirror->state);
    return 1;
}

static int porpoise_guest_force_return_to_main(
    PorpoiseGuestThreadMirror *mirror)
{
    PorpoiseLibporpoiseThreadRegistry *registry = mirror->registry;
    PorpoiseGuestThreadLifecycle main_thread;
    PorpoiseGuestThreadLifecycle target_thread;
    uint8_t current_context_bytes[4];
    uint8_t current_thread_bytes[4];
    uint8_t new_context_bytes[4];
    uint8_t new_thread_bytes[4];
    uint8_t new_main[PORPOISE_GUEST_THREAD_SIZE];
    uint8_t new_target[PORPOISE_GUEST_THREAD_SIZE];
    PorpoiseGuestWriteSpan spans[4];
    int child_owned_guest_cpu;

    if (porpoise_guest_read_raw(
            &mirror->state,
            registry->main_guest_thread,
            main_thread.bytes,
            sizeof(main_thread.bytes)) != PORPOISE_HOST_OK ||
        porpoise_guest_read_raw(
            &mirror->state,
            mirror->guest_thread,
            target_thread.bytes,
            sizeof(target_thread.bytes)) != PORPOISE_HOST_OK ||
        porpoise_guest_read_raw(
            &mirror->state,
            PORPOISE_GUEST_CURRENT_CONTEXT_ADDRESS,
            current_context_bytes,
            sizeof(current_context_bytes)) != PORPOISE_HOST_OK ||
        porpoise_guest_read_raw(
            &mirror->state,
            PORPOISE_GUEST_CURRENT_THREAD_ADDRESS,
            current_thread_bytes,
            sizeof(current_thread_bytes)) != PORPOISE_HOST_OK) {
        registry->poisoned = 1;
        return 0;
    }
    child_owned_guest_cpu =
        porpoise_read_be_u32(current_context_bytes) ==
            mirror->guest_thread &&
        porpoise_read_be_u32(current_thread_bytes) ==
            mirror->guest_thread;
    if (!((porpoise_read_be_u32(current_context_bytes) ==
               registry->main_guest_thread &&
           porpoise_read_be_u32(current_thread_bytes) ==
               registry->main_guest_thread) ||
          child_owned_guest_cpu)) {
        registry->poisoned = 1;
        return 0;
    }
    memcpy(new_main, main_thread.bytes, sizeof(new_main));
    memcpy(new_target, target_thread.bytes, sizeof(new_target));
    porpoise_guest_thread_set_state(
        new_main, PORPOISE_GUEST_THREAD_STATE_RUNNING, 0);
    porpoise_guest_thread_set_state(
        new_target, PORPOISE_GUEST_THREAD_STATE_MORIBUND, 0);
    porpoise_write_be_u32(
        new_target + PORPOISE_GUEST_THREAD_VALUE_OFFSET,
        mirror->state.gpr[3]);
    porpoise_write_be_u16(
        new_target + PORPOISE_GUEST_CONTEXT_MODE_OFFSET, 0U);
    porpoise_write_be_u16(
        new_target + PORPOISE_GUEST_CONTEXT_STATE_OFFSET, 0U);
    porpoise_write_be_u32(
        new_context_bytes, registry->main_guest_thread);
    porpoise_write_be_u32(
        new_thread_bytes, registry->main_guest_thread);
    spans[0].address = mirror->guest_thread;
    spans[0].size = sizeof(new_target);
    spans[0].old_bytes = target_thread.bytes;
    spans[0].new_bytes = new_target;
    spans[1].address = registry->main_guest_thread;
    spans[1].size = sizeof(new_main);
    spans[1].old_bytes = main_thread.bytes;
    spans[1].new_bytes = new_main;
    spans[2].address = PORPOISE_GUEST_CURRENT_CONTEXT_ADDRESS;
    spans[2].size = sizeof(new_context_bytes);
    spans[2].old_bytes = current_context_bytes;
    spans[2].new_bytes = new_context_bytes;
    spans[3].address = PORPOISE_GUEST_CURRENT_THREAD_ADDRESS;
    spans[3].size = sizeof(new_thread_bytes);
    spans[3].old_bytes = current_thread_bytes;
    spans[3].new_bytes = new_thread_bytes;
    if (!porpoise_guest_commit_spans(
            &mirror->state,
            registry,
            spans,
            sizeof(spans) / sizeof(spans[0]),
            "could not restore the guest main thread after a carrier fault")) {
        registry->poisoned = 1;
        return 0;
    }
    if (child_owned_guest_cpu) {
        porpoise_copy_machine_global_state(
            registry->main_state, &mirror->state);
    }
    return 1;
}

static void porpoise_guest_propagate_carrier_fault(
    PorpoisePpcState *destination,
    const PorpoisePpcState *carrier_state)
{
    if (destination == NULL || carrier_state == NULL ||
        !porpoise_state_has_fault(carrier_state)) {
        return;
    }
    porpoise_state_set_fault(
        destination,
        carrier_state->fault,
        carrier_state->fault_address,
        porpoise_state_fault_message(carrier_state));
}

static int porpoise_guest_exit_current_carrier(
    PorpoiseGuestThreadMirror *mirror,
    uint32_t value)
{
    if (!porpoise_guest_handoff_to_main(
            mirror,
            PORPOISE_GUEST_THREAD_STATE_MORIBUND,
            0,
            value,
            0)) {
        return 0;
    }
    mirror->state.status = PORPOISE_EXECUTION_RETURNED;
    mirror->lifecycle = PORPOISE_GUEST_CARRIER_EXITED;
    return 1;
}

static void porpoise_guest_carrier_entry(void *context)
{
    PorpoiseGuestThreadMirror *mirror =
        (PorpoiseGuestThreadMirror *)context;
    PorpoiseLibporpoiseThreadRegistry *registry;

    if (mirror == NULL || mirror->registry == NULL) {
        return;
    }
    registry = mirror->registry;
    porpoise_current_thread_registry = registry;
    porpoise_current_thread_mirror = mirror;
    porpoise_current_guest_thread = mirror->guest_thread;
    registry->export_state_binder(&mirror->state);

    if (porpoise_guest_handoff_to_child(mirror)) {
        mirror->state.status = PORPOISE_EXECUTION_RUNNING;
        (void)porpoise_libporpoise_run_guest(
            &mirror->state, mirror->entry_address);
        if (mirror->lifecycle == PORPOISE_GUEST_CARRIER_RUNNING &&
            !porpoise_state_should_stop(&mirror->state)) {
            (void)porpoise_guest_exit_current_carrier(
                mirror, mirror->state.gpr[3]);
        }
    }

    if (porpoise_state_has_fault(&mirror->state)) {
        (void)porpoise_guest_force_return_to_main(mirror);
    }
    if (mirror->lifecycle == PORPOISE_GUEST_CARRIER_RUNNING) {
        mirror->lifecycle = porpoise_state_should_stop(&mirror->state)
                                ? PORPOISE_GUEST_CARRIER_STOPPING
                                : PORPOISE_GUEST_CARRIER_EXITED;
    }
    registry->export_state_binder(NULL);
    porpoise_current_guest_thread = 0U;
    porpoise_current_thread_mirror = NULL;
    porpoise_current_thread_registry = NULL;
}

static PorpoiseGuestThreadMirror *porpoise_register_guest_thread(
    PorpoisePpcState *state,
    PorpoiseLibporpoiseThreadRegistry *registry,
    uint32_t guest_address,
    const PorpoiseGuestThreadLifecycle *thread)
{
    PorpoiseGuestThreadMirror *mirror;
    LibPorpoiseHostThreadCarrierConfigV1 config;
    LibPorpoiseHostThreadCarrierResultV1 result;
    uint32_t entry_address = porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_CONTEXT_SRR0_OFFSET);
    uint32_t exit_address = porpoise_read_be_u32(
        thread->bytes + PORPOISE_GUEST_CONTEXT_LR_OFFSET);

    if (registry->export_state_binder == NULL ||
        state->host == NULL || state->host->call_guest == NULL) {
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            guest_address,
            "guest carrier requires bound dispatch and export-state callbacks");
        return NULL;
    }
    if (entry_address == 0U || (entry_address & UINT32_C(3)) != 0U ||
        exit_address == 0U || (exit_address & UINT32_C(3)) != 0U ||
        !porpoise_dispatch_available(entry_address) ||
        !porpoise_dispatch_available(exit_address)) {
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            guest_address,
            "guest OSThread entry or exit address is not dispatchable");
        return NULL;
    }
    mirror = (PorpoiseGuestThreadMirror *)calloc(1U, sizeof(*mirror));
    if (mirror == NULL) {
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_HOST_IO,
            guest_address,
            "could not allocate a guest carrier mirror");
        return NULL;
    }
    mirror->registry = registry;
    mirror->guest_thread = guest_address;
    mirror->entry_address = entry_address;
    mirror->priority = thread->priority;
    mirror->lifecycle = PORPOISE_GUEST_CARRIER_CREATED_PAUSED;
    memcpy(
        mirror->saved_context,
        thread->bytes,
        sizeof(mirror->saved_context));
    porpoise_state_init(&mirror->state, state->host);
    porpoise_copy_machine_global_state(&mirror->state, state);
    porpoise_load_guest_context(&mirror->state, thread->bytes);
    if (snprintf(
            mirror->name,
            sizeof(mirror->name),
            "guest-%08lX",
            (unsigned long)guest_address) < 0) {
        free(mirror);
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_HOST_IO,
            guest_address,
            "could not name a guest carrier mirror");
        return NULL;
    }
    memset(&config, 0, sizeof(config));
    config.struct_size = (uint32_t)sizeof(config);
    config.entry = porpoise_guest_carrier_entry;
    config.entry_context = mirror;
    config.priority = mirror->priority;
    config.name = mirror->name;
    result = LibPorpoiseHostThreadCarrierCreatePausedV1(
        &config, &mirror->carrier);
    if (result != LIBPORPOISE_HOST_THREAD_CARRIER_OK ||
        mirror->carrier == NULL) {
        free(mirror);
        porpoise_guest_carrier_result_fault(
            state,
            guest_address,
            result,
            "libPorpoise could not create a paused host-thread carrier");
        return NULL;
    }
    mirror->next = registry->threads;
    registry->threads = mirror;
    return mirror;
}

static int porpoise_guest_precommit_target_running(
    PorpoisePpcState *state,
    PorpoiseLibporpoiseThreadRegistry *registry,
    uint32_t guest_address,
    const PorpoiseGuestThreadLifecycle *thread,
    uint8_t new_thread[PORPOISE_GUEST_THREAD_SIZE])
{
    PorpoiseGuestWriteSpan span;

    memcpy(new_thread, thread->bytes, PORPOISE_GUEST_THREAD_SIZE);
    porpoise_guest_thread_set_state(
        new_thread, PORPOISE_GUEST_THREAD_STATE_RUNNING, 0);
    span.address = guest_address;
    span.size = PORPOISE_GUEST_THREAD_SIZE;
    span.old_bytes = thread->bytes;
    span.new_bytes = new_thread;
    return porpoise_guest_commit_spans(
        state,
        registry,
        &span,
        1U,
        "could not stage the guest thread resume");
}

static void porpoise_guest_carrier_invariant_failure(
    PorpoisePpcState *state,
    PorpoiseGuestThreadMirror *mirror,
    const char *message)
{
    mirror->registry->poisoned = 1;
    if (!porpoise_state_should_stop(&mirror->state)) {
        mirror->state.status = PORPOISE_EXECUTION_RETURNED;
    }
    (void)LibPorpoiseHostThreadCarrierRequestStopV1(mirror->carrier);
    porpoise_guest_carrier_fault(
        state,
        PORPOISE_FAULT_INVALID_STATE,
        mirror->guest_thread,
        message);
}

static void porpoise_guest_resume_thread(
    PorpoisePpcState *state)
{
    PorpoiseLibporpoiseThreadRegistry *registry;
    PorpoiseGuestThreadLifecycle main_thread;
    PorpoiseGuestThreadLifecycle target_thread;
    PorpoiseGuestThreadMirror *mirror;
    PorpoiseGuestCarrierLifecycle old_lifecycle;
    uint8_t new_target[PORPOISE_GUEST_THREAD_SIZE];
    uint8_t context_word[4];
    uint8_t thread_word[4];
    uint32_t current_context;
    uint32_t current_thread;
    uint32_t guest_address;
    int32_t previous_suspend_count = -1;
    LibPorpoiseHostThreadCarrierResultV1 carrier_result;

    if (state == NULL || porpoise_state_should_stop(state) ||
        !porpoise_require_guest_main_caller(state, &registry)) {
        return;
    }
    guest_address = state->gpr[3];
    if (guest_address == registry->main_guest_thread) {
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            guest_address,
            "carrier v1 does not externally resume the guest main thread");
        return;
    }
    if (porpoise_guest_spans_overlap(
            guest_address,
            PORPOISE_GUEST_THREAD_SIZE,
            registry->main_guest_thread,
            PORPOISE_GUEST_THREAD_SIZE) ||
        porpoise_guest_spans_overlap(
            guest_address,
            PORPOISE_GUEST_THREAD_SIZE,
            PORPOISE_GUEST_CURRENT_CONTEXT_ADDRESS,
            sizeof(uint32_t)) ||
        porpoise_guest_spans_overlap(
            guest_address,
            PORPOISE_GUEST_THREAD_SIZE,
            PORPOISE_GUEST_CURRENT_THREAD_ADDRESS,
            sizeof(uint32_t))) {
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_address,
            "guest OSThread overlaps another scheduler transaction span");
        return;
    }
    if (!porpoise_read_guest_thread_at(
            state, registry->main_guest_thread, &main_thread) ||
        !porpoise_read_guest_thread_at(
            state, guest_address, &target_thread) ||
        !porpoise_guest_read_current_words(
            state,
            context_word,
            thread_word,
            &current_context,
            &current_thread)) {
        return;
    }
    if (main_thread.state != PORPOISE_GUEST_THREAD_STATE_RUNNING ||
        main_thread.suspend_count != 0 || main_thread.priority < 0 ||
        main_thread.priority > 31 ||
        current_context != registry->main_guest_thread ||
        current_thread != registry->main_guest_thread) {
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            registry->main_guest_thread,
            "guest main thread is not the active running scheduler owner");
        return;
    }
    if (target_thread.state != PORPOISE_GUEST_THREAD_STATE_READY ||
        target_thread.suspend_count != 1 ||
        !porpoise_guest_thread_has_narrow_scheduler_state(
            state, guest_address, &target_thread)) {
        if (!porpoise_state_should_stop(state)) {
            porpoise_guest_carrier_fault(
                state,
                PORPOISE_FAULT_UNSUPPORTED_OPERATION,
                guest_address,
                "carrier v1 resumes only a READY guest thread with suspend count one");
        }
        return;
    }
    if (target_thread.priority >= main_thread.priority) {
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            guest_address,
            "carrier v1 requires a resumed guest thread to outrank the main thread");
        return;
    }

    mirror = porpoise_find_guest_thread_mirror(registry, guest_address);
    if (mirror == NULL) {
        mirror = porpoise_register_guest_thread(
            state, registry, guest_address, &target_thread);
        if (mirror == NULL) {
            return;
        }
    } else if ((mirror->lifecycle !=
                    PORPOISE_GUEST_CARRIER_CREATED_PAUSED &&
                mirror->lifecycle != PORPOISE_GUEST_CARRIER_PARKED) ||
               mirror->priority != target_thread.priority ||
               memcmp(
                   mirror->saved_context,
                   target_thread.bytes,
                   sizeof(mirror->saved_context)) != 0) {
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            guest_address,
            "guest OSThread context changed outside its parked carrier");
        return;
    }

    old_lifecycle = mirror->lifecycle;
    if (!porpoise_guest_precommit_target_running(
            state,
            registry,
            guest_address,
            &target_thread,
            new_target)) {
        return;
    }
    mirror->lifecycle = PORPOISE_GUEST_CARRIER_RUNNING;
    carrier_result = LibPorpoiseHostThreadCarrierResumeV1(
        mirror->carrier, &previous_suspend_count);
    if (carrier_result != LIBPORPOISE_HOST_THREAD_CARRIER_OK) {
        mirror->lifecycle = old_lifecycle;
        (void)porpoise_guest_restore_span(
            state,
            registry,
            guest_address,
            target_thread.bytes,
            sizeof(target_thread.bytes),
            "libPorpoise rejected a carrier resume and guest rollback failed");
        porpoise_guest_carrier_result_fault(
            state,
            guest_address,
            carrier_result,
            "libPorpoise rejected the host-thread carrier resume");
        return;
    }
    state->gpr[3] = 1U;
    if (previous_suspend_count != 1) {
        porpoise_guest_carrier_invariant_failure(
            state,
            mirror,
            "libPorpoise carrier suspend count diverged from the guest mirror");
        return;
    }
    if (mirror->lifecycle != PORPOISE_GUEST_CARRIER_PARKED &&
        mirror->lifecycle != PORPOISE_GUEST_CARRIER_EXITED &&
        mirror->lifecycle != PORPOISE_GUEST_CARRIER_STOPPING) {
        porpoise_guest_carrier_invariant_failure(
            state,
            mirror,
            "higher-priority carrier returned control without yielding");
        return;
    }
    porpoise_guest_propagate_carrier_fault(state, &mirror->state);
}

static int porpoise_guest_rollback_failed_suspend(
    PorpoiseGuestThreadMirror *mirror)
{
    PorpoiseGuestThreadLifecycle target_thread;
    uint8_t new_target[PORPOISE_GUEST_THREAD_SIZE];

    if (!porpoise_read_guest_thread_at(
            &mirror->state, mirror->guest_thread, &target_thread) ||
        target_thread.state != PORPOISE_GUEST_THREAD_STATE_READY ||
        target_thread.suspend_count != 1 ||
        !porpoise_guest_precommit_target_running(
            &mirror->state,
            mirror->registry,
            mirror->guest_thread,
            &target_thread,
            new_target)) {
        mirror->registry->poisoned = 1;
        return 0;
    }
    mirror->lifecycle = PORPOISE_GUEST_CARRIER_RUNNING;
    if (!porpoise_guest_handoff_to_child(mirror)) {
        mirror->registry->poisoned = 1;
        return 0;
    }
    return 1;
}

static void porpoise_guest_suspend_thread(
    PorpoisePpcState *state)
{
    PorpoiseLibporpoiseThreadRegistry *registry;
    PorpoiseGuestThreadMirror *mirror;
    PorpoiseGuestThreadLifecycle target_thread;
    uint32_t guest_address;
    int32_t previous_suspend_count = -1;
    LibPorpoiseHostThreadCarrierResultV1 carrier_result;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    registry = porpoise_libporpoise_thread_registry_for_state(state);
    mirror = porpoise_current_thread_mirror;
    guest_address = state->gpr[3];
    if (registry == NULL || registry->poisoned || mirror == NULL ||
        mirror->registry != registry || &mirror->state != state ||
        mirror->lifecycle != PORPOISE_GUEST_CARRIER_RUNNING ||
        porpoise_current_thread_registry != registry ||
        porpoise_current_guest_thread != mirror->guest_thread ||
        guest_address != mirror->guest_thread) {
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            guest_address,
            "carrier v1 supports only self-suspension by the active guest thread");
        return;
    }
    if (!porpoise_read_guest_thread_at(
            state, guest_address, &target_thread) ||
        target_thread.state != PORPOISE_GUEST_THREAD_STATE_RUNNING ||
        target_thread.suspend_count != 0 ||
        !porpoise_guest_thread_has_narrow_scheduler_state(
            state, guest_address, &target_thread)) {
        if (!porpoise_state_should_stop(state)) {
            porpoise_guest_carrier_fault(
                state,
                PORPOISE_FAULT_INVALID_STATE,
                guest_address,
                "active guest carrier is not in a suspendable state");
        }
        return;
    }

    state->gpr[3] = 0U;
    if (!porpoise_guest_handoff_to_main(
            mirror,
            PORPOISE_GUEST_THREAD_STATE_READY,
            1,
            target_thread.value,
            1)) {
        return;
    }
    mirror->lifecycle = PORPOISE_GUEST_CARRIER_PARKED;
    carrier_result = LibPorpoiseHostThreadCarrierSuspendCurrentV1(
        mirror->carrier, &previous_suspend_count);
    if (carrier_result != LIBPORPOISE_HOST_THREAD_CARRIER_OK) {
        (void)porpoise_guest_rollback_failed_suspend(mirror);
        porpoise_guest_carrier_result_fault(
            state,
            guest_address,
            carrier_result,
            "libPorpoise rejected the carrier self-suspend");
        return;
    }
    if (previous_suspend_count != 0) {
        porpoise_guest_carrier_invariant_failure(
            state,
            mirror,
            "libPorpoise self-suspend count diverged from the guest mirror");
        return;
    }
    if (porpoise_state_should_stop(state) ||
        mirror->lifecycle == PORPOISE_GUEST_CARRIER_STOPPING) {
        return;
    }
    if (mirror->lifecycle != PORPOISE_GUEST_CARRIER_RUNNING ||
        !porpoise_guest_handoff_to_child(mirror)) {
        if (!porpoise_state_should_stop(state)) {
            porpoise_guest_carrier_invariant_failure(
                state,
                mirror,
                "resumed carrier did not have a staged running guest state");
        }
        return;
    }
    state->gpr[3] = 0U;
}

static void porpoise_guest_exit_thread(PorpoisePpcState *state)
{
    PorpoiseLibporpoiseThreadRegistry *registry;
    PorpoiseGuestThreadMirror *mirror;
    uint32_t value;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    value = state->gpr[3];
    registry = porpoise_libporpoise_thread_registry_for_state(state);
    mirror = porpoise_current_thread_mirror;
    if (registry == NULL || registry->poisoned || mirror == NULL ||
        mirror->registry != registry || &mirror->state != state ||
        mirror->lifecycle != PORPOISE_GUEST_CARRIER_RUNNING ||
        porpoise_current_thread_registry != registry ||
        porpoise_current_guest_thread != mirror->guest_thread) {
        porpoise_guest_carrier_fault(
            state,
            PORPOISE_FAULT_UNSUPPORTED_OPERATION,
            state->pc,
            "carrier v1 supports OSExitThread only for the active guest thread");
        return;
    }
    (void)porpoise_guest_exit_current_carrier(mirror, value);
}
#endif

#if !defined(PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
static void porpoise_guest_thread_scheduler_fault(
    PorpoisePpcState *state,
    const char *message)
{
    PorpoiseGuestThreadLifecycle thread;

    if (!porpoise_read_guest_thread(state, &thread)) {
        return;
    }

    (void)thread;
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        state->gpr[3],
        message);
}
#endif

void porpoise_libporpoise_os_wakeup_thread_adapter(
    PorpoisePpcState *state)
{
    PorpoiseGuestThreadQueue queue;

    if (!porpoise_read_guest_thread_queue(state, &queue)) {
        return;
    }

    /* An empty queue has the same observable result as OSWakeupThread without
     * requiring a native queue mirror. A nonempty queue needs a guest thread
     * scheduler; passing its 8-byte storage to the wider native structure
     * would corrupt adjacent guest memory. */
    if (queue.head == 0U) {
        return;
    }
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        state->gpr[3],
        "OSWakeupThread requires guest thread scheduler support for a "
        "nonempty queue");
}

void porpoise_libporpoise_os_sleep_thread_adapter(
    PorpoisePpcState *state)
{
    PorpoiseGuestThreadQueue queue;

    if (!porpoise_read_guest_thread_queue(state, &queue)) {
        return;
    }

    (void)queue;
    /* Sleeping cannot be emulated safely by blocking the host startup thread.
     * Fail immediately until lifted guest scheduling is available. */
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        state->gpr[3],
        "OSSleepThread requires guest thread scheduler support");
}

void porpoise_libporpoise_os_resume_thread_adapter(
    PorpoisePpcState *state)
{
#if defined(PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
    porpoise_guest_resume_thread(state);
#else
    porpoise_guest_thread_scheduler_fault(
        state,
        "OSResumeThread requires a guest thread scheduler mirror");
#endif
}

void porpoise_libporpoise_os_suspend_thread_adapter(
    PorpoisePpcState *state)
{
#if defined(PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
    porpoise_guest_suspend_thread(state);
#else
    porpoise_guest_thread_scheduler_fault(
        state,
        "OSSuspendThread requires a guest thread scheduler mirror");
#endif
}

void porpoise_libporpoise_os_exit_thread_adapter(
    PorpoisePpcState *state)
{
#if defined(PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1)
    porpoise_guest_exit_thread(state);
#else
    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }

    /* r3 is the opaque guest thread return value, not an address to decode.
     * Exiting requires selecting another guest PPC state, so it cannot be
     * delegated to a native host thread implementation. */
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        state->pc,
        "OSExitThread requires a guest thread scheduler mirror");
#endif
}
