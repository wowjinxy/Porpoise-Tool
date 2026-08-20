#include "porpoise_libporpoise_builtins_private.h"
#include "porpoise_libporpoise_private.h"

#include <dolphin/ar.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORPOISE_AR_ENTRY_SIZE 4U
#define PORPOISE_AR_ALLOC_ALIGNMENT 32U
#define PORPOISE_AR_PREFLIGHT_CHUNK_SIZE 256U

typedef char PorpoiseArU32MustBeFourBytes[
    sizeof(u32) == PORPOISE_AR_ENTRY_SIZE ? 1 : -1];

typedef enum PorpoiseArLifecycle {
    PORPOISE_AR_UNINITIALIZED = 0,
    PORPOISE_AR_ACTIVE,
    PORPOISE_AR_POISONED
} PorpoiseArLifecycle;

typedef struct PorpoiseArBoundary {
    PorpoiseArLifecycle lifecycle;
    PorpoiseHostAdapter *owner_adapter;
    void *owner_context;
    PorpoiseHostReadBytesFn owner_read_bytes;
    PorpoiseHostWriteBytesFn owner_write_bytes;
    uint32_t guest_table;
    uint32_t capacity;
    uint32_t allocated;
    uint32_t committed_entries;
    uint32_t stack_pointer;
    uint32_t base_address;
    uint32_t aram_size;
    u32 *native_shadow;
    uint32_t *expected_shadow;
} PorpoiseArBoundary;

static PorpoiseArBoundary porpoise_ar_boundary;

static PorpoiseFault porpoise_ar_fault_from_host_result(
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

static void porpoise_ar_set_host_fault(
    PorpoisePpcState *state,
    PorpoiseHostResult result,
    uint32_t guest_address)
{
    porpoise_state_set_fault(
        state,
        porpoise_ar_fault_from_host_result(result),
        guest_address,
        porpoise_host_result_string(result));
}

static void porpoise_ar_set_fault(
    PorpoisePpcState *state,
    PorpoiseFault fault,
    uint32_t address,
    const char *message)
{
    porpoise_state_set_fault(state, fault, address, message);
}

static void porpoise_ar_poison(
    PorpoisePpcState *state,
    uint32_t address,
    const char *message)
{
    porpoise_ar_boundary.lifecycle = PORPOISE_AR_POISONED;
    porpoise_ar_set_fault(
        state, PORPOISE_FAULT_INVALID_STATE, address, message);
}

static uint32_t porpoise_ar_read_be32(const uint8_t bytes[4])
{
    return ((uint32_t)bytes[0] << 24U) |
           ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) |
           (uint32_t)bytes[3];
}

static void porpoise_ar_write_be32(
    uint8_t bytes[4],
    uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
}

static void porpoise_ar_release_storage(void)
{
    free(porpoise_ar_boundary.native_shadow);
    free(porpoise_ar_boundary.expected_shadow);
    memset(&porpoise_ar_boundary, 0, sizeof(porpoise_ar_boundary));
}

static int porpoise_ar_validate_host(PorpoisePpcState *state)
{
    if (state->host == NULL) {
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_NO_HOST_ADAPTER,
            state->pc,
            "AR allocator boundary requires a host adapter");
        return 0;
    }
    if (state->host->read_bytes == NULL ||
        state->host->write_bytes == NULL) {
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            state->pc,
            "AR allocator boundary requires host read and write callbacks");
        return 0;
    }
    return 1;
}

static int porpoise_ar_require_owner(
    PorpoisePpcState *state,
    int allow_poisoned)
{
    if (!porpoise_ar_validate_host(state)) {
        return 0;
    }
    if (porpoise_ar_boundary.lifecycle == PORPOISE_AR_UNINITIALIZED) {
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "AR allocator boundary is not initialized");
        return 0;
    }
    if (porpoise_ar_boundary.owner_adapter != state->host ||
        porpoise_ar_boundary.owner_context != state->host->context) {
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "AR allocator boundary belongs to another host adapter");
        return 0;
    }
    if (state->host->read_bytes !=
            porpoise_ar_boundary.owner_read_bytes ||
        state->host->write_bytes !=
            porpoise_ar_boundary.owner_write_bytes) {
        porpoise_ar_poison(
            state,
            state->pc,
            "AR allocator host callbacks diverged from their initialized owner");
        return 0;
    }
    if (!allow_poisoned &&
        porpoise_ar_boundary.lifecycle == PORPOISE_AR_POISONED) {
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "AR allocator boundary is poisoned; ARReset is required");
        return 0;
    }
    return 1;
}

static int porpoise_ar_read(
    PorpoisePpcState *state,
    uint32_t guest_address,
    void *destination,
    size_t size)
{
    PorpoiseHostResult result = state->host->read_bytes(
        state->host->context, guest_address, destination, size);
    if (result != PORPOISE_HOST_OK) {
        porpoise_ar_set_host_fault(state, result, guest_address);
        return 0;
    }
    return 1;
}

static int porpoise_ar_write(
    PorpoisePpcState *state,
    uint32_t guest_address,
    const void *source,
    size_t size)
{
    PorpoiseHostResult result = state->host->write_bytes(
        state->host->context, guest_address, source, size);
    if (result != PORPOISE_HOST_OK) {
        porpoise_ar_set_host_fault(state, result, guest_address);
        return 0;
    }
    return 1;
}

static int porpoise_ar_validate_table_range(
    PorpoisePpcState *state,
    uint32_t guest_table,
    uint32_t capacity,
    size_t *size_out)
{
    uint64_t table_size;

    *size_out = 0U;
    if (capacity == 0U) {
        if (guest_table != 0U &&
            (guest_table & (PORPOISE_AR_ENTRY_SIZE - 1U)) != 0U) {
            porpoise_ar_set_fault(
                state,
                PORPOISE_FAULT_INVALID_ARGUMENT,
                guest_table,
                "zero-capacity AR block table is not four-byte aligned");
            return 0;
        }
        return 1;
    }
    if (guest_table == 0U) {
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_table,
            "AR block table is NULL");
        return 0;
    }
    if ((guest_table & (PORPOISE_AR_ENTRY_SIZE - 1U)) != 0U) {
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_table,
            "AR block table is not four-byte aligned");
        return 0;
    }

    table_size = (uint64_t)capacity *
                 (uint64_t)PORPOISE_AR_ENTRY_SIZE;
    if (table_size > (uint64_t)SIZE_MAX ||
        table_size - UINT64_C(1) >
            (uint64_t)(UINT32_MAX - guest_table)) {
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            guest_table,
            "AR block table crosses the 32-bit guest address boundary");
        return 0;
    }
    *size_out = (size_t)table_size;
    return 1;
}

static int porpoise_ar_preflight_table(
    PorpoisePpcState *state,
    uint32_t guest_table,
    size_t table_size)
{
    uint8_t bytes[PORPOISE_AR_PREFLIGHT_CHUNK_SIZE];
    size_t offset = 0U;

    while (offset < table_size) {
        size_t chunk_size = table_size - offset;
        uint32_t address;

        if (chunk_size > sizeof(bytes)) {
            chunk_size = sizeof(bytes);
        }
        address = guest_table + (uint32_t)offset;
        if (!porpoise_ar_read(
                state, address, bytes, chunk_size) ||
            !porpoise_ar_write(
                state, address, bytes, chunk_size)) {
            return 0;
        }
        offset += chunk_size;
    }
    return 1;
}

static int porpoise_ar_preflight_word(
    PorpoisePpcState *state,
    uint32_t guest_address,
    uint8_t original[4],
    const char *alignment_message)
{
    if (guest_address == 0U) {
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_INVALID_POINTER,
            guest_address,
            "AR output pointer is NULL");
        return 0;
    }
    if ((guest_address & UINT32_C(3)) != 0U) {
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_address,
            alignment_message);
        return 0;
    }
    if (guest_address > UINT32_MAX - UINT32_C(3)) {
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            guest_address,
            "AR output crosses the 32-bit guest address boundary");
        return 0;
    }
    return porpoise_ar_read(state, guest_address, original, 4U) &&
           porpoise_ar_write(state, guest_address, original, 4U);
}

static int porpoise_ar_restore_word(
    PorpoisePpcState *state,
    uint32_t guest_address,
    const uint8_t original[4])
{
    uint8_t observed[4];
    PorpoiseHostResult result;

    result = state->host->write_bytes(
        state->host->context, guest_address, original, 4U);
    if (result != PORPOISE_HOST_OK) {
        return 0;
    }
    result = state->host->read_bytes(
        state->host->context, guest_address, observed, sizeof(observed));
    return result == PORPOISE_HOST_OK &&
           memcmp(observed, original, sizeof(observed)) == 0;
}

static int porpoise_ar_commit_word(
    PorpoisePpcState *state,
    uint32_t guest_address,
    const uint8_t original[4],
    uint32_t value)
{
    uint8_t encoded[4];
    uint8_t observed[4];
    PorpoiseHostResult result;

    porpoise_ar_write_be32(encoded, value);
    result = state->host->write_bytes(
        state->host->context, guest_address, encoded, sizeof(encoded));
    if (result == PORPOISE_HOST_OK) {
        result = state->host->read_bytes(
            state->host->context,
            guest_address,
            observed,
            sizeof(observed));
        if (result == PORPOISE_HOST_OK &&
            memcmp(observed, encoded, sizeof(observed)) == 0) {
            return 1;
        }
    }

    if (result == PORPOISE_HOST_OK) {
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_HOST_IO,
            guest_address,
            "host reported an incomplete AR guest-word commit");
    } else {
        porpoise_ar_set_host_fault(state, result, guest_address);
    }
    if (!porpoise_ar_restore_word(state, guest_address, original)) {
        porpoise_ar_boundary.lifecycle = PORPOISE_AR_POISONED;
        (void)fprintf(
            stderr,
            "Porpoise: could not roll back a failed AR guest-word commit at 0x%08lX\n",
            (unsigned long)guest_address);
    }
    return 0;
}

static int porpoise_ar_verify_guest_table(PorpoisePpcState *state)
{
    uint32_t index;
    uint8_t bytes[4];

    for (index = 0U;
         index < porpoise_ar_boundary.committed_entries;
         index++) {
        uint32_t address = porpoise_ar_boundary.guest_table +
                           index * PORPOISE_AR_ENTRY_SIZE;
        if (!porpoise_ar_read(state, address, bytes, sizeof(bytes))) {
            return 0;
        }
        if (porpoise_ar_read_be32(bytes) !=
            porpoise_ar_boundary.expected_shadow[index]) {
            porpoise_ar_poison(
                state,
                address,
                "guest AR block table diverged from the allocator mirror");
            return 0;
        }
    }
    return 1;
}

static int porpoise_ar_verify_native(PorpoisePpcState *state)
{
    uint32_t index;

    if (!ARCheckInit()) {
        porpoise_ar_poison(
            state,
            state->pc,
            "native libPorpoise AR allocator was reset outside its owner");
        return 0;
    }
    if ((uint32_t)ARGetBaseAddress() !=
            porpoise_ar_boundary.base_address ||
        (uint32_t)ARGetSize() != porpoise_ar_boundary.aram_size) {
        porpoise_ar_poison(
            state,
            state->pc,
            "native libPorpoise AR geometry diverged from its owner");
        return 0;
    }
    for (index = 0U; index < porpoise_ar_boundary.capacity; index++) {
        if ((uint32_t)porpoise_ar_boundary.native_shadow[index] !=
            porpoise_ar_boundary.expected_shadow[index]) {
            porpoise_ar_poison(
                state,
                state->pc,
                "native libPorpoise AR block table diverged from its owner");
            return 0;
        }
    }
    return 1;
}

static int porpoise_ar_verify_coherent(PorpoisePpcState *state)
{
    return porpoise_ar_verify_native(state) &&
           porpoise_ar_verify_guest_table(state);
}

void porpoise_libporpoise_ar_init_adapter(PorpoisePpcState *state)
{
    uint32_t guest_table;
    uint32_t capacity;
    size_t table_size;
    size_t allocation_size;
    u32 *native_shadow;
    uint32_t *expected_shadow;
    uint32_t native_base;
    uint32_t native_size;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    guest_table = state->gpr[3];
    capacity = state->gpr[4];
    state->gpr[3] = 0U;

    if (!porpoise_ar_validate_host(state) ||
        !porpoise_ar_validate_table_range(
            state, guest_table, capacity, &table_size)) {
        return;
    }

    if (porpoise_ar_boundary.lifecycle != PORPOISE_AR_UNINITIALIZED) {
        if (!porpoise_ar_require_owner(state, 0)) {
            return;
        }
        if (guest_table != porpoise_ar_boundary.guest_table ||
            capacity != porpoise_ar_boundary.capacity) {
            porpoise_ar_set_fault(
                state,
                PORPOISE_FAULT_INVALID_STATE,
                guest_table,
                "ARInit cannot replace the active guest block table");
            return;
        }
        if (!porpoise_ar_preflight_table(
                state, guest_table, table_size) ||
            !porpoise_ar_verify_coherent(state)) {
            return;
        }
        state->gpr[3] = porpoise_ar_boundary.base_address;
        return;
    }

    if (ARCheckInit()) {
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "native libPorpoise AR allocator is already initialized without an adapter owner");
        return;
    }
    if (!porpoise_ar_preflight_table(state, guest_table, table_size)) {
        return;
    }

    allocation_size = (size_t)capacity * sizeof(uint32_t);
    native_shadow = NULL;
    expected_shadow = NULL;
    if (capacity != 0U) {
        native_shadow = (u32 *)calloc(1U, allocation_size);
        expected_shadow = (uint32_t *)calloc(1U, allocation_size);
        if (native_shadow == NULL || expected_shadow == NULL) {
            free(native_shadow);
            free(expected_shadow);
            porpoise_ar_set_fault(
                state,
                PORPOISE_FAULT_HOST_IO,
                guest_table,
                "cannot allocate the native AR block-table shadows");
            return;
        }
    }

    porpoise_ar_boundary.lifecycle = PORPOISE_AR_POISONED;
    porpoise_ar_boundary.owner_adapter = state->host;
    porpoise_ar_boundary.owner_context = state->host->context;
    porpoise_ar_boundary.owner_read_bytes = state->host->read_bytes;
    porpoise_ar_boundary.owner_write_bytes = state->host->write_bytes;
    porpoise_ar_boundary.guest_table = guest_table;
    porpoise_ar_boundary.capacity = capacity;
    porpoise_ar_boundary.native_shadow = native_shadow;
    porpoise_ar_boundary.expected_shadow = expected_shadow;

    native_base = (uint32_t)ARInit(native_shadow, (u32)capacity);
    native_size = (uint32_t)ARGetSize();
    if (!ARCheckInit() ||
        native_base != (uint32_t)ARGetBaseAddress() ||
        (native_base & (PORPOISE_AR_ALLOC_ALIGNMENT - 1U)) != 0U ||
        native_size < native_base) {
        porpoise_ar_poison(
            state,
            state->pc,
            "native libPorpoise ARInit returned an invalid allocator geometry");
        return;
    }

    porpoise_ar_boundary.base_address = native_base;
    porpoise_ar_boundary.stack_pointer = native_base;
    porpoise_ar_boundary.aram_size = native_size;
    porpoise_ar_boundary.lifecycle = PORPOISE_AR_ACTIVE;
    if (!porpoise_ar_verify_native(state)) {
        return;
    }
    state->gpr[3] = native_base;
}

void porpoise_libporpoise_ar_alloc_adapter(PorpoisePpcState *state)
{
    uint32_t length;
    uint32_t index;
    uint32_t guest_entry;
    uint32_t expected_address;
    uint32_t native_address;
    uint8_t original[4];

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    length = state->gpr[3];
    state->gpr[3] = 0U;
    if (!porpoise_ar_require_owner(state, 0) ||
        !porpoise_ar_verify_coherent(state)) {
        return;
    }
    if ((length & (PORPOISE_AR_ALLOC_ALIGNMENT - 1U)) != 0U) {
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            length,
            "ARAlloc length is not 32-byte aligned");
        return;
    }
    if (porpoise_ar_boundary.allocated ==
            porpoise_ar_boundary.capacity ||
        length > porpoise_ar_boundary.aram_size -
                     porpoise_ar_boundary.stack_pointer) {
        return;
    }

    index = porpoise_ar_boundary.allocated;
    guest_entry = porpoise_ar_boundary.guest_table +
                  index * PORPOISE_AR_ENTRY_SIZE;
    if (!porpoise_ar_preflight_word(
            state,
            guest_entry,
            original,
            "AR block-table entry is not four-byte aligned")) {
        return;
    }

    expected_address = porpoise_ar_boundary.stack_pointer;
    if (!porpoise_ar_commit_word(
            state, guest_entry, original, length)) {
        return;
    }
    native_address = (uint32_t)ARAlloc((u32)length);
    if (native_address != expected_address ||
        (uint32_t)porpoise_ar_boundary.native_shadow[index] != length ||
        !ARCheckInit()) {
        (void)porpoise_ar_restore_word(
            state, guest_entry, original);
        porpoise_ar_poison(
            state,
            expected_address,
            "native libPorpoise ARAlloc diverged from the guest allocator mirror");
        return;
    }

    porpoise_ar_boundary.expected_shadow[index] = length;
    porpoise_ar_boundary.stack_pointer += length;
    porpoise_ar_boundary.allocated++;
    if (porpoise_ar_boundary.committed_entries <
        porpoise_ar_boundary.allocated) {
        porpoise_ar_boundary.committed_entries =
            porpoise_ar_boundary.allocated;
    }
    state->gpr[3] = native_address;
}

void porpoise_libporpoise_ar_free_adapter(PorpoisePpcState *state)
{
    uint32_t guest_length_out;
    uint32_t expected_length;
    uint32_t expected_address;
    uint32_t native_address;
    u32 native_length;
    uint8_t original[4];

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    guest_length_out = state->gpr[3];
    state->gpr[3] = 0U;
    if (!porpoise_ar_require_owner(state, 0) ||
        !porpoise_ar_verify_coherent(state)) {
        return;
    }
    if (guest_length_out != 0U &&
        !porpoise_ar_preflight_word(
            state,
            guest_length_out,
            original,
            "ARFree length output is not four-byte aligned")) {
        return;
    }

    if (porpoise_ar_boundary.allocated == 0U) {
        expected_length = 0U;
        expected_address = 0U;
    } else {
        expected_length = porpoise_ar_boundary.expected_shadow[
            porpoise_ar_boundary.allocated - 1U];
        expected_address = porpoise_ar_boundary.stack_pointer -
                           expected_length;
    }
    if ((uint64_t)guest_length_out >=
            (uint64_t)porpoise_ar_boundary.guest_table &&
        (uint64_t)guest_length_out <
            (uint64_t)porpoise_ar_boundary.guest_table +
                (uint64_t)porpoise_ar_boundary.capacity *
                    (uint64_t)PORPOISE_AR_ENTRY_SIZE) {
        uint32_t table_index =
            (guest_length_out - porpoise_ar_boundary.guest_table) /
            PORPOISE_AR_ENTRY_SIZE;

        if (table_index < porpoise_ar_boundary.committed_entries &&
            porpoise_ar_boundary.expected_shadow[table_index] !=
                expected_length) {
            porpoise_ar_set_fault(
                state,
                PORPOISE_FAULT_INVALID_ARGUMENT,
                guest_length_out,
                "ARFree output would corrupt a committed block-table entry");
            return;
        }
    }
    if (guest_length_out != 0U &&
        !porpoise_ar_commit_word(
            state, guest_length_out, original, expected_length)) {
        return;
    }
    native_length = UINT32_MAX;
    native_address = (uint32_t)ARFree(&native_length);
    if (native_address != expected_address ||
        (uint32_t)native_length != expected_length ||
        !ARCheckInit()) {
        if (guest_length_out != 0U) {
            (void)porpoise_ar_restore_word(
                state, guest_length_out, original);
        }
        porpoise_ar_poison(
            state,
            expected_address,
            "native libPorpoise ARFree diverged from the guest allocator mirror");
        return;
    }

    if (porpoise_ar_boundary.allocated != 0U) {
        porpoise_ar_boundary.allocated--;
        porpoise_ar_boundary.stack_pointer = expected_address;
    }
    state->gpr[3] = native_address;
}

void porpoise_libporpoise_ar_reset_adapter(PorpoisePpcState *state)
{
    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (porpoise_ar_boundary.lifecycle == PORPOISE_AR_UNINITIALIZED) {
        if (ARCheckInit()) {
            porpoise_ar_set_fault(
                state,
                PORPOISE_FAULT_INVALID_STATE,
                state->pc,
                "native libPorpoise AR allocator has no adapter owner");
        }
        return;
    }
    if (!porpoise_ar_validate_host(state) ||
        porpoise_ar_boundary.owner_adapter != state->host ||
        porpoise_ar_boundary.owner_context != state->host->context) {
        if (!porpoise_state_has_fault(state)) {
            porpoise_ar_set_fault(
                state,
                PORPOISE_FAULT_INVALID_STATE,
                state->pc,
                "ARReset may only be called by the allocator owner");
        }
        return;
    }

    ARReset();
    if (ARCheckInit()) {
        porpoise_ar_boundary.lifecycle = PORPOISE_AR_POISONED;
        porpoise_ar_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "native libPorpoise ARReset did not clear allocator state");
        return;
    }
    porpoise_ar_release_storage();
}

void porpoise_libporpoise_ar_get_size_adapter(PorpoisePpcState *state)
{
    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    state->gpr[3] = 0U;
    if (porpoise_ar_boundary.lifecycle == PORPOISE_AR_UNINITIALIZED) {
        if (ARCheckInit()) {
            porpoise_ar_set_fault(
                state,
                PORPOISE_FAULT_INVALID_STATE,
                state->pc,
                "native libPorpoise AR allocator has no adapter owner");
            return;
        }
        /* ARGetSize is a scalar SDK query. Before first initialization it
         * reports the native zero-initialized size; after ARReset it retains
         * the last detected geometry, matching the SDK global. */
        state->gpr[3] = (uint32_t)ARGetSize();
        return;
    }
    if (!porpoise_ar_require_owner(state, 0) ||
        !porpoise_ar_verify_coherent(state)) {
        return;
    }
    state->gpr[3] = porpoise_ar_boundary.aram_size;
}

void porpoise_libporpoise_ar_shutdown(
    const PorpoiseHostAdapter *adapter)
{
    if (adapter == NULL ||
        porpoise_ar_boundary.lifecycle == PORPOISE_AR_UNINITIALIZED ||
        porpoise_ar_boundary.owner_adapter != adapter ||
        porpoise_ar_boundary.owner_context != adapter->context) {
        return;
    }

    ARReset();
    if (ARCheckInit()) {
        (void)fprintf(
            stderr,
            "Porpoise: native AR allocator remained active during adapter shutdown; retaining its shadow storage\n");
        porpoise_ar_boundary.lifecycle = PORPOISE_AR_POISONED;
        porpoise_ar_boundary.owner_adapter = NULL;
        porpoise_ar_boundary.owner_context = NULL;
        return;
    }
    porpoise_ar_release_storage();
}
