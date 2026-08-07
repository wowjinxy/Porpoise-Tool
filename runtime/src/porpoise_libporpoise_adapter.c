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
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <stdint.h>
#include <string.h>

/* libPorpoise currently declares OSInit only through its broad os.h facade. */
extern void OSInit(void);

#define PORPOISE_PHYSICAL_EFB_START UINT32_C(0x08000000)
#define PORPOISE_PHYSICAL_MMIO_END UINT32_C(0x10000000)
#define PORPOISE_EFFECTIVE_EFB_START UINT32_C(0xC8000000)
#define PORPOISE_EFFECTIVE_MMIO_END UINT64_C(0xD0000000)

static int porpoise_libporpoise_os_initialized;

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
               PORPOISE_EFFECTIVE_EFB_START,
               PORPOISE_EFFECTIVE_MMIO_END);
}

static PorpoiseHostResult porpoise_libporpoise_decode_span(
    uint32_t guest_address,
    size_t size,
    void **pointer_out)
{
    uint32_t last_address;
    void *first_pointer;

    if (pointer_out == NULL || size == 0U) {
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

    first_pointer = __OSHostDecodeAddress((u32)guest_address);
    if (first_pointer == NULL) {
        return PORPOISE_HOST_UNMAPPED_ADDRESS;
    }

    /*
     * Token addresses denote opaque native pointers rather than an arithmetic
     * guest-address range. The token itself is enough to validate the base;
     * callers remain responsible for the pointed object's size.
     */
    if (!__OSHostIsAddressToken((u32)guest_address) && size > 1U) {
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
    void *context,
    uint32_t guest_address,
    void *destination,
    size_t size)
{
    PorpoiseHostResult result;
    void *source;

    (void)context;
    if (destination == NULL && size != 0U) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (size == 0U) {
        return PORPOISE_HOST_OK;
    }

    source = NULL;
    result = porpoise_libporpoise_decode_span(
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
    void *context,
    uint32_t guest_address,
    const void *source,
    size_t size)
{
    PorpoiseHostResult result;
    void *destination;

    (void)context;
    if (source == NULL && size != 0U) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    if (size == 0U) {
        return PORPOISE_HOST_OK;
    }

    destination = NULL;
    result = porpoise_libporpoise_decode_span(
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
    void *context,
    uint32_t guest_address,
    void **pointer_out)
{
    void *pointer;

    (void)context;
    if (pointer_out == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    *pointer_out = NULL;

    if (guest_address == 0U) {
        return PORPOISE_HOST_OK;
    }
    if (porpoise_is_unsupported_mmio_span(guest_address, 1U)) {
        return PORPOISE_HOST_UNSUPPORTED_MMIO;
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
    u32 guest_address;

    (void)context;
    if (guest_address_out == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }
    *guest_address_out = 0U;

    if (pointer == NULL) {
        return PORPOISE_HOST_OK;
    }

    guest_address = __OSHostEncodePointerWord(pointer);
    if (guest_address == 0U) {
        return PORPOISE_HOST_INVALID_POINTER;
    }
    if (porpoise_is_unsupported_mmio_span((uint32_t)guest_address, 1U)) {
        return PORPOISE_HOST_UNSUPPORTED_MMIO;
    }

    *guest_address_out = (uint32_t)guest_address;
    return PORPOISE_HOST_OK;
}

PorpoiseHostResult porpoise_libporpoise_adapter_init(
    PorpoiseHostAdapter *adapter)
{
    if (adapter == NULL) {
        return PORPOISE_HOST_INVALID_ARGUMENT;
    }

    /* OSInit is internally idempotent; this guard keeps the adapter call once. */
    if (!porpoise_libporpoise_os_initialized) {
        OSInit();
        porpoise_libporpoise_os_initialized = 1;
    }

    adapter->context = NULL;
    adapter->read_bytes = porpoise_libporpoise_read_bytes;
    adapter->write_bytes = porpoise_libporpoise_write_bytes;
    adapter->decode_pointer = porpoise_libporpoise_decode_pointer;
    adapter->encode_pointer = porpoise_libporpoise_encode_pointer;
    return PORPOISE_HOST_OK;
}
