#include "porpoise_libporpoise_builtins_private.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    PORPOISE_GUEST_MESSAGE_QUEUE_SIZE = 0x20,
    PORPOISE_GUEST_THREAD_QUEUE_SIZE = 0x08,
    PORPOISE_GUEST_MESSAGE_SIZE = 0x04
};

enum {
    PORPOISE_MESSAGE_SEND_HEAD = 0x00,
    PORPOISE_MESSAGE_SEND_TAIL = 0x04,
    PORPOISE_MESSAGE_RECEIVE_HEAD = 0x08,
    PORPOISE_MESSAGE_RECEIVE_TAIL = 0x0C,
    PORPOISE_MESSAGE_ARRAY = 0x10,
    PORPOISE_MESSAGE_CAPACITY = 0x14,
    PORPOISE_MESSAGE_FIRST = 0x18,
    PORPOISE_MESSAGE_USED = 0x1C
};

typedef struct PorpoiseGuestMessageQueue {
    uint32_t send_head;
    uint32_t send_tail;
    uint32_t receive_head;
    uint32_t receive_tail;
    uint32_t message_array;
    int32_t capacity;
    int32_t first;
    int32_t used;
} PorpoiseGuestMessageQueue;

static int porpoise_message_state_should_stop(
    const PorpoisePpcState *state)
{
    return porpoise_state_should_stop(state);
}

static PorpoiseFault porpoise_message_fault_from_host_result(
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

static void porpoise_message_set_host_fault(
    PorpoisePpcState *state,
    PorpoiseHostResult result,
    uint32_t guest_address)
{
    porpoise_state_set_fault(
        state,
        porpoise_message_fault_from_host_result(result),
        guest_address,
        porpoise_host_result_string(result));
}

static int porpoise_message_validate_span(
    PorpoisePpcState *state,
    uint32_t guest_address,
    size_t size,
    int require_alignment,
    const char *description)
{
    if (porpoise_message_state_should_stop(state)) {
        return 0;
    }
    if (size == 0U) {
        return 1;
    }
    if (guest_address == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_address,
            description);
        return 0;
    }
    if (require_alignment &&
        (guest_address & UINT32_C(3)) != UINT32_C(0)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_address,
            "guest message-queue word span is not four-byte aligned");
        return 0;
    }
    if (size - 1U > (size_t)(UINT32_MAX - guest_address)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            guest_address,
            "guest message-queue span crosses the 32-bit address boundary");
        return 0;
    }
    if (state->host == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_NO_HOST_ADAPTER,
            guest_address,
            NULL);
        return 0;
    }
    return 1;
}

static int porpoise_message_read_span(
    PorpoisePpcState *state,
    uint32_t guest_address,
    uint8_t *destination,
    size_t size,
    int require_alignment,
    const char *description)
{
    PorpoiseHostResult result;

    if (!porpoise_message_validate_span(
            state,
            guest_address,
            size,
            require_alignment,
            description)) {
        return 0;
    }
    if (destination == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_address,
            "guest message-queue read destination is NULL");
        return 0;
    }
    if (state->host->read_bytes == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            guest_address,
            "host adapter has no read callback");
        return 0;
    }
    result = state->host->read_bytes(
        state->host->context,
        guest_address,
        destination,
        size);
    if (result != PORPOISE_HOST_OK) {
        porpoise_message_set_host_fault(state, result, guest_address);
        return 0;
    }
    return 1;
}

static int porpoise_message_preflight_write(
    PorpoisePpcState *state,
    uint32_t guest_address,
    uint8_t *old_bytes,
    size_t size,
    int require_alignment,
    const char *description)
{
    PorpoiseHostResult result;
    void *first_pointer;
    void *last_pointer;

    if (!porpoise_message_read_span(
            state,
            guest_address,
            old_bytes,
            size,
            require_alignment,
            description)) {
        return 0;
    }
    if (state->host->write_bytes == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            guest_address,
            "host adapter has no write callback");
        return 0;
    }

    /* The adapter ABI has no write-probe callback. The current libPorpoise
     * mapping uses the same RAM span for reads, writes, and pointer decode;
     * checking both endpoints therefore preflights the entire word span
     * without changing guest memory. */
    if (state->host->decode_pointer == NULL) {
        return 1;
    }
    first_pointer = NULL;
    result = state->host->decode_pointer(
        state->host->context,
        guest_address,
        &first_pointer);
    if (result != PORPOISE_HOST_OK || first_pointer == NULL) {
        if (result == PORPOISE_HOST_OK) {
            result = PORPOISE_HOST_INVALID_POINTER;
        }
        porpoise_message_set_host_fault(state, result, guest_address);
        return 0;
    }
    last_pointer = NULL;
    result = state->host->decode_pointer(
        state->host->context,
        guest_address + (uint32_t)(size - 1U),
        &last_pointer);
    if (result != PORPOISE_HOST_OK || last_pointer == NULL) {
        if (result == PORPOISE_HOST_OK) {
            result = PORPOISE_HOST_INVALID_POINTER;
        }
        porpoise_message_set_host_fault(state, result, guest_address);
        return 0;
    }
    if ((uintptr_t)first_pointer >
            UINTPTR_MAX - (uintptr_t)(size - 1U) ||
        (uintptr_t)last_pointer !=
            (uintptr_t)first_pointer + (uintptr_t)(size - 1U)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_address,
            "decoded guest message-queue span is not host-contiguous");
        return 0;
    }
    return 1;
}

static int porpoise_message_write_span(
    PorpoisePpcState *state,
    uint32_t guest_address,
    const uint8_t *source,
    size_t size)
{
    PorpoiseHostResult result;

    if (porpoise_message_state_should_stop(state)) {
        return 0;
    }
    result = state->host->write_bytes(
        state->host->context,
        guest_address,
        source,
        size);
    if (result != PORPOISE_HOST_OK) {
        porpoise_message_set_host_fault(state, result, guest_address);
        return 0;
    }
    return 1;
}

static void porpoise_message_best_effort_restore(
    PorpoisePpcState *state,
    uint32_t guest_address,
    const uint8_t *old_bytes,
    size_t size)
{
    if (state == NULL || state->host == NULL ||
        state->host->write_bytes == NULL || old_bytes == NULL ||
        size == 0U) {
        return;
    }
    /* Bypass the sticky-fault gate deliberately. A failed compensating write
     * must not replace the original commit fault or its diagnostic. */
    (void)state->host->write_bytes(
        state->host->context,
        guest_address,
        old_bytes,
        size);
}

static uint32_t porpoise_message_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24U) |
           ((uint32_t)source[1] << 16U) |
           ((uint32_t)source[2] << 8U) |
           (uint32_t)source[3];
}

static int32_t porpoise_message_read_be_s32(const uint8_t *source)
{
    uint32_t bits;
    int32_t value;

    bits = porpoise_message_read_be32(source);
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void porpoise_message_write_be32(
    uint8_t *destination,
    uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24U);
    destination[1] = (uint8_t)(value >> 16U);
    destination[2] = (uint8_t)(value >> 8U);
    destination[3] = (uint8_t)value;
}

static int porpoise_message_spans_overlap(
    uint32_t first_address,
    size_t first_size,
    uint32_t second_address,
    size_t second_size)
{
    uint64_t first_begin;
    uint64_t first_end;
    uint64_t second_begin;
    uint64_t second_end;

    first_begin = first_address;
    first_end = first_begin + first_size;
    second_begin = second_address;
    second_end = second_begin + second_size;
    return first_begin < second_end && second_begin < first_end;
}

static int porpoise_message_decode_span(
    PorpoisePpcState *state,
    uint32_t guest_address,
    size_t size,
    uintptr_t *begin_out,
    uintptr_t *end_out,
    int unavailable_is_not_an_alias)
{
    PorpoiseHostResult result;
    void *first_pointer;
    void *last_pointer;
    uintptr_t begin;
    uintptr_t end;

    if (state == NULL || begin_out == NULL || end_out == NULL || size == 0U) {
        if (state != NULL) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_INVALID_ARGUMENT,
                guest_address,
                "guest message-queue alias check received an invalid span");
        }
        return 0;
    }
    if (size - 1U > (size_t)(UINT32_MAX - guest_address)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            guest_address,
            "guest message-queue alias span crosses the 32-bit address boundary");
        return 0;
    }
    if (state->host == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_NO_HOST_ADAPTER,
            guest_address,
            NULL);
        return 0;
    }
    if (state->host->decode_pointer == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            guest_address,
            "message-queue alias validation requires host pointer decoding");
        return 0;
    }

    first_pointer = NULL;
    result = state->host->decode_pointer(
        state->host->context,
        guest_address,
        &first_pointer);
    if (result != PORPOISE_HOST_OK || first_pointer == NULL) {
        if (unavailable_is_not_an_alias &&
            (result == PORPOISE_HOST_UNMAPPED_ADDRESS ||
             result == PORPOISE_HOST_UNSUPPORTED_MMIO)) {
            return -1;
        }
        if (result == PORPOISE_HOST_OK) {
            result = PORPOISE_HOST_INVALID_POINTER;
        }
        porpoise_message_set_host_fault(state, result, guest_address);
        return 0;
    }
    last_pointer = NULL;
    result = state->host->decode_pointer(
        state->host->context,
        guest_address + (uint32_t)(size - 1U),
        &last_pointer);
    if (result != PORPOISE_HOST_OK || last_pointer == NULL) {
        if (unavailable_is_not_an_alias &&
            (result == PORPOISE_HOST_UNMAPPED_ADDRESS ||
             result == PORPOISE_HOST_UNSUPPORTED_MMIO)) {
            return -1;
        }
        if (result == PORPOISE_HOST_OK) {
            result = PORPOISE_HOST_INVALID_POINTER;
        }
        porpoise_message_set_host_fault(state, result, guest_address);
        return 0;
    }

    begin = (uintptr_t)first_pointer;
    if (begin > UINTPTR_MAX - (uintptr_t)size) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            guest_address,
            "decoded guest message-queue span crosses the host address boundary");
        return 0;
    }
    end = begin + (uintptr_t)size;
    if ((uintptr_t)last_pointer != end - (uintptr_t)1U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_address,
            "decoded guest message-queue span is not host-contiguous");
        return 0;
    }
    *begin_out = begin;
    *end_out = end;
    return 1;
}

static int porpoise_message_spans_alias(
    PorpoisePpcState *state,
    uint32_t first_address,
    size_t first_size,
    uint32_t second_address,
    size_t second_size,
    int *overlap_out)
{
    int decode_result;
    uintptr_t first_begin;
    uintptr_t first_end;
    uintptr_t second_begin;
    uintptr_t second_end;

    if (overlap_out == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            first_address,
            "guest message-queue alias result is NULL");
        return 0;
    }
    if (porpoise_message_spans_overlap(
            first_address,
            first_size,
            second_address,
            second_size)) {
        *overlap_out = 1;
        return 1;
    }
    if (!porpoise_message_decode_span(
            state,
            first_address,
            first_size,
            &first_begin,
            &first_end,
            0)) {
        return 0;
    }
    decode_result = porpoise_message_decode_span(
            state,
            second_address,
            second_size,
            &second_begin,
            &second_end,
            1);
    if (decode_result < 0) {
        *overlap_out = 0;
        return 1;
    }
    if (decode_result == 0) {
        return 0;
    }
    *overlap_out = first_begin < second_end && second_begin < first_end;
    return 1;
}

static int porpoise_message_validate_array(
    PorpoisePpcState *state,
    uint32_t queue_address,
    uint32_t array_address,
    int32_t capacity)
{
    uint64_t byte_size;
    uint64_t last_address;
    int overlaps_queue;

    if (capacity < 0) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            queue_address + PORPOISE_MESSAGE_CAPACITY,
            "guest message queue has a negative capacity");
        return 0;
    }
    if (capacity == 0) {
        if (array_address != 0U &&
            (array_address & UINT32_C(3)) != UINT32_C(0)) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_INVALID_ARGUMENT,
                array_address,
                "zero-capacity guest message array is not four-byte aligned");
            return 0;
        }
        return 1;
    }
    if (array_address == 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            array_address,
            "nonempty guest message queue has a NULL message array");
        return 0;
    }
    if ((array_address & UINT32_C(3)) != UINT32_C(0)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            array_address,
            "guest message array is not four-byte aligned");
        return 0;
    }
    byte_size = (uint64_t)(uint32_t)capacity *
                (uint64_t)PORPOISE_GUEST_MESSAGE_SIZE;
    last_address = (uint64_t)array_address + byte_size - UINT64_C(1);
    if (last_address > UINT32_MAX) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            array_address,
            "guest message array crosses the 32-bit address boundary");
        return 0;
    }
    if (!porpoise_message_spans_alias(
            state,
            queue_address,
            PORPOISE_GUEST_MESSAGE_QUEUE_SIZE,
            array_address,
            (size_t)byte_size,
            &overlaps_queue)) {
        return 0;
    }
    if (overlaps_queue) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            array_address,
            "guest message array overlaps its queue structure");
        return 0;
    }
    return 1;
}

static int porpoise_message_read_queue(
    PorpoisePpcState *state,
    uint32_t queue_address,
    PorpoiseGuestMessageQueue *queue,
    uint8_t raw[PORPOISE_GUEST_MESSAGE_QUEUE_SIZE])
{
    if (queue == NULL || raw == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            queue_address,
            "guest message-queue snapshot destination is NULL");
        return 0;
    }
    if (!porpoise_message_read_span(
            state,
            queue_address,
            raw,
            PORPOISE_GUEST_MESSAGE_QUEUE_SIZE,
            1,
            "guest message queue is NULL")) {
        return 0;
    }
    queue->send_head = porpoise_message_read_be32(
        raw + PORPOISE_MESSAGE_SEND_HEAD);
    queue->send_tail = porpoise_message_read_be32(
        raw + PORPOISE_MESSAGE_SEND_TAIL);
    queue->receive_head = porpoise_message_read_be32(
        raw + PORPOISE_MESSAGE_RECEIVE_HEAD);
    queue->receive_tail = porpoise_message_read_be32(
        raw + PORPOISE_MESSAGE_RECEIVE_TAIL);
    queue->message_array = porpoise_message_read_be32(
        raw + PORPOISE_MESSAGE_ARRAY);
    queue->capacity = porpoise_message_read_be_s32(
        raw + PORPOISE_MESSAGE_CAPACITY);
    queue->first = porpoise_message_read_be_s32(
        raw + PORPOISE_MESSAGE_FIRST);
    queue->used = porpoise_message_read_be_s32(
        raw + PORPOISE_MESSAGE_USED);
    return 1;
}

static int porpoise_message_validate_thread_queue(
    PorpoisePpcState *state,
    uint32_t queue_address,
    uint32_t head,
    uint32_t tail,
    const char *description)
{
    if ((head == 0U) != (tail == 0U)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            queue_address,
            description);
        return 0;
    }
    if ((head != 0U && (head & UINT32_C(3)) != UINT32_C(0)) ||
        (tail != 0U && (tail & UINT32_C(3)) != UINT32_C(0))) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            queue_address,
            "guest message waiter queue contains an unaligned thread pointer");
        return 0;
    }
    return 1;
}

static int porpoise_message_validate_queue_state(
    PorpoisePpcState *state,
    uint32_t queue_address,
    const PorpoiseGuestMessageQueue *queue)
{
    if (!porpoise_message_validate_thread_queue(
            state,
            queue_address + PORPOISE_MESSAGE_SEND_HEAD,
            queue->send_head,
            queue->send_tail,
            "guest message send-wait queue has only one endpoint") ||
        !porpoise_message_validate_thread_queue(
            state,
            queue_address + PORPOISE_MESSAGE_RECEIVE_HEAD,
            queue->receive_head,
            queue->receive_tail,
            "guest message receive-wait queue has only one endpoint") ||
        !porpoise_message_validate_array(
            state,
            queue_address,
            queue->message_array,
            queue->capacity)) {
        return 0;
    }
    if (queue->used < 0 || queue->used > queue->capacity) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            queue_address + PORPOISE_MESSAGE_USED,
            "guest message queue used count is outside its capacity");
        return 0;
    }
    if (queue->capacity == 0) {
        if (queue->first != 0 || queue->used != 0) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_INVALID_STATE,
                queue_address + PORPOISE_MESSAGE_FIRST,
                "zero-capacity guest message queue has nonzero indices");
            return 0;
        }
    } else if (queue->first < 0 || queue->first >= queue->capacity) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            queue_address + PORPOISE_MESSAGE_FIRST,
            "guest message queue first index is outside its capacity");
        return 0;
    }
    return 1;
}

static int porpoise_message_slot_address(
    PorpoisePpcState *state,
    const PorpoiseGuestMessageQueue *queue,
    uint32_t index,
    uint32_t *slot_address)
{
    uint64_t address;

    if (slot_address == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            queue->message_array,
            "guest message slot destination is NULL");
        return 0;
    }
    address = (uint64_t)queue->message_array +
              (uint64_t)index * PORPOISE_GUEST_MESSAGE_SIZE;
    if (address > UINT32_MAX -
                      (PORPOISE_GUEST_MESSAGE_SIZE - UINT32_C(1))) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            queue->message_array,
            "guest message slot crosses the 32-bit address boundary");
        return 0;
    }
    *slot_address = (uint32_t)address;
    return 1;
}

static void porpoise_message_scheduler_fault(
    PorpoisePpcState *state,
    uint32_t queue_address,
    const char *message)
{
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        queue_address,
        message);
}

void porpoise_libporpoise_os_init_message_queue_adapter(PorpoisePpcState *state)
{
    uint32_t queue_address;
    uint32_t array_address;
    int32_t capacity;
    uint8_t old_queue[PORPOISE_GUEST_MESSAGE_QUEUE_SIZE];
    uint8_t new_queue[PORPOISE_GUEST_MESSAGE_QUEUE_SIZE];

    if (porpoise_message_state_should_stop(state)) {
        return;
    }
    queue_address = state->gpr[3];
    array_address = state->gpr[4];
    memcpy(&capacity, &state->gpr[5], sizeof(capacity));

    if (!porpoise_message_validate_span(
            state,
            queue_address,
            PORPOISE_GUEST_MESSAGE_QUEUE_SIZE,
            1,
            "OSInitMessageQueue received a NULL guest queue") ||
        !porpoise_message_validate_array(
            state,
            queue_address,
            array_address,
            capacity) ||
        !porpoise_message_preflight_write(
            state,
            queue_address,
            old_queue,
            sizeof(old_queue),
            1,
            "OSInitMessageQueue received a NULL guest queue")) {
        return;
    }

    memset(new_queue, 0, sizeof(new_queue));
    porpoise_message_write_be32(
        new_queue + PORPOISE_MESSAGE_ARRAY,
        array_address);
    porpoise_message_write_be32(
        new_queue + PORPOISE_MESSAGE_CAPACITY,
        state->gpr[5]);
    (void)porpoise_message_write_span(
        state,
        queue_address,
        new_queue,
        sizeof(new_queue));
}

void porpoise_libporpoise_os_send_message_adapter(PorpoisePpcState *state)
{
    PorpoiseGuestMessageQueue queue;
    uint32_t queue_address;
    uint32_t index;
    uint32_t slot_address;
    uint64_t unwrapped_index;
    uint8_t raw_queue[PORPOISE_GUEST_MESSAGE_QUEUE_SIZE];
    uint8_t old_slot[PORPOISE_GUEST_MESSAGE_SIZE];
    uint8_t old_used[PORPOISE_GUEST_MESSAGE_SIZE];
    uint8_t new_slot[PORPOISE_GUEST_MESSAGE_SIZE];
    uint8_t new_used[PORPOISE_GUEST_MESSAGE_SIZE];

    if (porpoise_message_state_should_stop(state)) {
        return;
    }
    queue_address = state->gpr[3];
    if (!porpoise_message_read_queue(
            state,
            queue_address,
            &queue,
            raw_queue) ||
        !porpoise_message_validate_queue_state(
            state,
            queue_address,
            &queue)) {
        return;
    }

    if (queue.used == queue.capacity) {
        if ((state->gpr[5] & UINT32_C(1)) == UINT32_C(0)) {
            state->gpr[3] = UINT32_C(0);
        } else {
            porpoise_message_scheduler_fault(
                state,
                queue_address,
                "OSSendMessage requires guest scheduling because the queue is full");
        }
        return;
    }
    if (queue.receive_head != 0U) {
        porpoise_message_scheduler_fault(
            state,
            queue_address + PORPOISE_MESSAGE_RECEIVE_HEAD,
            "OSSendMessage requires guest scheduling to wake a receiver");
        return;
    }

    unwrapped_index = (uint64_t)(uint32_t)queue.first +
                      (uint64_t)(uint32_t)queue.used;
    index = (uint32_t)(unwrapped_index % (uint32_t)queue.capacity);
    if (!porpoise_message_slot_address(
            state,
            &queue,
            index,
            &slot_address) ||
        !porpoise_message_preflight_write(
            state,
            slot_address,
            old_slot,
            sizeof(old_slot),
            1,
            "OSSendMessage selected an invalid guest message slot") ||
        !porpoise_message_preflight_write(
            state,
            queue_address + PORPOISE_MESSAGE_USED,
            old_used,
            sizeof(old_used),
            1,
            "OSSendMessage selected an invalid used-count word")) {
        return;
    }

    porpoise_message_write_be32(new_slot, state->gpr[4]);
    porpoise_message_write_be32(
        new_used,
        (uint32_t)(queue.used + 1));
    if (!porpoise_message_write_span(
            state,
            slot_address,
            new_slot,
            sizeof(new_slot))) {
        return;
    }
    if (!porpoise_message_write_span(
            state,
            queue_address + PORPOISE_MESSAGE_USED,
            new_used,
            sizeof(new_used))) {
        porpoise_message_best_effort_restore(
            state,
            slot_address,
            old_slot,
            sizeof(old_slot));
        return;
    }
    state->gpr[3] = UINT32_C(1);
}

void porpoise_libporpoise_os_receive_message_adapter(PorpoisePpcState *state)
{
    PorpoiseGuestMessageQueue queue;
    uint32_t queue_address;
    uint32_t output_address;
    uint32_t slot_address;
    uint32_t next_first;
    uint8_t raw_queue[PORPOISE_GUEST_MESSAGE_QUEUE_SIZE];
    uint8_t message[PORPOISE_GUEST_MESSAGE_SIZE];
    uint8_t old_output[PORPOISE_GUEST_MESSAGE_SIZE];
    uint8_t old_indices[PORPOISE_GUEST_THREAD_QUEUE_SIZE];
    uint8_t new_indices[PORPOISE_GUEST_THREAD_QUEUE_SIZE];
    int overlaps_queue;

    if (porpoise_message_state_should_stop(state)) {
        return;
    }
    queue_address = state->gpr[3];
    output_address = state->gpr[4];
    if (!porpoise_message_read_queue(
            state,
            queue_address,
            &queue,
            raw_queue) ||
        !porpoise_message_validate_queue_state(
            state,
            queue_address,
            &queue)) {
        return;
    }

    if (queue.used == 0) {
        if ((state->gpr[5] & UINT32_C(1)) == UINT32_C(0)) {
            state->gpr[3] = UINT32_C(0);
        } else {
            porpoise_message_scheduler_fault(
                state,
                queue_address + PORPOISE_MESSAGE_RECEIVE_HEAD,
                "OSReceiveMessage requires guest scheduling because the queue is empty");
        }
        return;
    }
    if (queue.send_head != 0U) {
        porpoise_message_scheduler_fault(
            state,
            queue_address + PORPOISE_MESSAGE_SEND_HEAD,
            "OSReceiveMessage requires guest scheduling to wake a sender");
        return;
    }
    if (!porpoise_message_slot_address(
            state,
            &queue,
            (uint32_t)queue.first,
            &slot_address) ||
        !porpoise_message_read_span(
            state,
            slot_address,
            message,
            sizeof(message),
            1,
            "OSReceiveMessage selected an invalid guest message slot") ||
        !porpoise_message_preflight_write(
            state,
            queue_address + PORPOISE_MESSAGE_FIRST,
            old_indices,
            sizeof(old_indices),
            1,
            "OSReceiveMessage selected invalid queue-index words")) {
        return;
    }

    if (output_address != 0U) {
        if (!porpoise_message_spans_alias(
                state,
                output_address,
                PORPOISE_GUEST_MESSAGE_SIZE,
                queue_address,
                PORPOISE_GUEST_MESSAGE_QUEUE_SIZE,
                &overlaps_queue)) {
            return;
        }
        if (overlaps_queue) {
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_INVALID_ARGUMENT,
                output_address,
                "OSReceiveMessage output overlaps its guest queue structure");
            return;
        }
        if (!porpoise_message_preflight_write(
                state,
                output_address,
                old_output,
                sizeof(old_output),
                1,
                "OSReceiveMessage received an invalid guest output pointer")) {
            return;
        }
    }

    next_first = ((uint32_t)queue.first + UINT32_C(1)) %
                 (uint32_t)queue.capacity;
    porpoise_message_write_be32(new_indices, next_first);
    porpoise_message_write_be32(
        new_indices + PORPOISE_GUEST_MESSAGE_SIZE,
        (uint32_t)(queue.used - 1));

    if (output_address != 0U) {
        if (!porpoise_message_write_span(
                state,
                output_address,
                message,
                sizeof(message))) {
            return;
        }
    }
    if (!porpoise_message_write_span(
            state,
            queue_address + PORPOISE_MESSAGE_FIRST,
            new_indices,
            sizeof(new_indices))) {
        if (output_address != 0U) {
            porpoise_message_best_effort_restore(
                state,
                output_address,
                old_output,
                sizeof(old_output));
        }
        return;
    }
    state->gpr[3] = UINT32_C(1);
}
