#include "porpoise_libporpoise_adapter.h"

/*
 * Keep these unstable libPorpoise host interfaces confined to this adapter.
 * Generated lifted sources and porpoise_lifted.c must not include them.
 */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include <dolphin/os/OSHostAddress.h>
#include <dolphin/os/OSTime.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* libPorpoise currently declares OSInit only through its broad os.h facade. */
extern void OSInit(void);

#define PORPOISE_PHYSICAL_EFB_START UINT32_C(0x08000000)
#define PORPOISE_PHYSICAL_MMIO_END UINT32_C(0x10000000)
#define PORPOISE_CACHED_EFB_START UINT32_C(0x88000000)
#define PORPOISE_CACHED_MMIO_END UINT64_C(0x90000000)
#define PORPOISE_EFFECTIVE_EFB_START UINT32_C(0xC8000000)
#define PORPOISE_EFFECTIVE_MMIO_END UINT64_C(0xD0000000)

typedef struct PorpoiseLibporpoiseContext {
    u32 *tokens;
    size_t token_count;
    size_t token_capacity;
} PorpoiseLibporpoiseContext;

static int porpoise_libporpoise_os_initialized;
static PorpoiseLibporpoiseContext *porpoise_libporpoise_active_context;

static int porpoise_libporpoise_owns_token(
    const PorpoiseLibporpoiseContext *context,
    u32 token);

static PorpoiseHostResult porpoise_libporpoise_record_token(
    PorpoiseLibporpoiseContext *context,
    u32 token)
{
    if (context == NULL || !__OSHostIsAddressToken(token)) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    /* Some host implementations may intern an already-live pointer token. */
    if (porpoise_libporpoise_owns_token(context, token)) {
        return PORPOISE_HOST_OK;
    }
    if (context->token_count >= OS_HOST_ADDRESS_TOKEN_SLOT_COUNT) {
        return PORPOISE_HOST_IO_ERROR;
    }
    if (context->token_count == context->token_capacity) {
        size_t new_capacity = context->token_capacity == 0U
                                  ? 16U
                                  : context->token_capacity * 2U;
        u32 *resized;
        if (new_capacity > OS_HOST_ADDRESS_TOKEN_SLOT_COUNT) {
            new_capacity = OS_HOST_ADDRESS_TOKEN_SLOT_COUNT;
        }
        resized = (u32 *)realloc(
            context->tokens,
            new_capacity * sizeof(*context->tokens));
        if (resized == NULL) {
            return PORPOISE_HOST_IO_ERROR;
        }
        context->tokens = resized;
        context->token_capacity = new_capacity;
    }
    context->tokens[context->token_count++] = token;
    return PORPOISE_HOST_OK;
}

static int porpoise_libporpoise_owns_token(
    const PorpoiseLibporpoiseContext *context,
    u32 token)
{
    size_t index;

    if (context == NULL) {
        return 0;
    }
    for (index = 0U; index < context->token_count; index++) {
        if (context->tokens[index] == token) {
            return 1;
        }
    }
    return 0;
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

    if (__OSHostIsAddressToken((u32)guest_address) &&
        !porpoise_libporpoise_owns_token(
            adapter_context,
            (u32)guest_address)) {
        return PORPOISE_HOST_INVALID_POINTER;
    }

    pointer = __OSHostDecodeAddress((u32)guest_address);
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
    u32 guest_address;

    if (adapter_context == NULL || guest_address_out == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    *guest_address_out = 0U;

    if (pointer == NULL) {
        return PORPOISE_HOST_OK;
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
        PorpoiseHostResult result = porpoise_libporpoise_record_token(
            adapter_context,
            guest_address);
        if (result != PORPOISE_HOST_OK) {
            __OSHostReleaseAddress(guest_address);
            return result;
        }
    }

    *guest_address_out = (uint32_t)guest_address;
    return PORPOISE_HOST_OK;
}

PorpoiseHostResult porpoise_libporpoise_adapter_init(
    PorpoiseHostAdapter *adapter)
{
    PorpoiseLibporpoiseContext *context;

    if (adapter == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (porpoise_libporpoise_active_context != NULL) {
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

    adapter->context = context;
    adapter->read_bytes = porpoise_libporpoise_read_bytes;
    adapter->write_bytes = porpoise_libporpoise_write_bytes;
    adapter->decode_pointer = porpoise_libporpoise_decode_pointer;
    adapter->encode_pointer = porpoise_libporpoise_encode_pointer;
    adapter->read_time_base = porpoise_libporpoise_read_time_base;
    porpoise_libporpoise_active_context = context;
    return PORPOISE_HOST_OK;
}

void porpoise_libporpoise_adapter_shutdown(
    PorpoiseHostAdapter *adapter)
{
    PorpoiseLibporpoiseContext *context;

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
    porpoise_libporpoise_active_context = NULL;
    if (context != NULL) {
        size_t index;
        for (index = 0U; index < context->token_count; index++) {
            __OSHostReleaseAddress(context->tokens[index]);
        }
        free(context->tokens);
        free(context);
    }
    memset(adapter, 0, sizeof(*adapter));
}
