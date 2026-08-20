#include "porpoise_libporpoise_builtins_private.h"

#include <dolphin/card.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef char PorpoiseCardS32MustBeFourBytes[
    sizeof(s32) == sizeof(uint32_t) ? 1 : -1];

typedef struct PorpoiseCardGuestOutput {
    uint32_t guest_address;
    s32 value;
    uint8_t original_bytes[4];
    int present;
} PorpoiseCardGuestOutput;

static PorpoiseFault porpoise_card_fault_from_host_result(
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

static void porpoise_card_set_host_fault(
    PorpoisePpcState *state,
    PorpoiseHostResult result,
    uint32_t guest_address)
{
    porpoise_state_set_fault(
        state,
        porpoise_card_fault_from_host_result(result),
        guest_address,
        porpoise_host_result_string(result));
}

static uint32_t porpoise_card_read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24U) |
           ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) |
           (uint32_t)bytes[3];
}

static void porpoise_card_write_be32(
    uint8_t *bytes,
    uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
}

static int porpoise_card_preload_output(
    PorpoisePpcState *state,
    uint32_t guest_address,
    const char *description,
    PorpoiseCardGuestOutput *output)
{
    uint8_t bytes[4];
    uint32_t raw_value;
    PorpoiseHostResult result;

    memset(output, 0, sizeof(*output));
    output->guest_address = guest_address;
    if (guest_address == 0U) {
        return 1;
    }
    output->present = 1;
    if (guest_address > UINT32_MAX - 3U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            guest_address,
            "CARDProbeEx output crosses the 32-bit guest address boundary");
        return 0;
    }
    if ((guest_address & UINT32_C(3)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            guest_address,
            description);
        return 0;
    }
    if (state->host == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_NO_HOST_ADAPTER,
            guest_address,
            "CARDProbeEx guest outputs require a host adapter");
        return 0;
    }
    if (state->host->read_bytes == NULL ||
        state->host->write_bytes == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_MISSING_HOST_CALLBACK,
            guest_address,
            "CARDProbeEx guest outputs require host read and write callbacks");
        return 0;
    }
    result = state->host->read_bytes(
        state->host->context,
        guest_address,
        bytes,
        sizeof(bytes));
    if (result != PORPOISE_HOST_OK) {
        porpoise_card_set_host_fault(state, result, guest_address);
        return 0;
    }
    raw_value = porpoise_card_read_be32(bytes);
    memcpy(&output->value, &raw_value, sizeof(output->value));
    memcpy(output->original_bytes, bytes, sizeof(output->original_bytes));
    return 1;
}

static int porpoise_card_preflight_output(
    PorpoisePpcState *state,
    const PorpoiseCardGuestOutput *output)
{
    PorpoiseHostResult result;

    if (!output->present) {
        return 1;
    }
    /* PorpoiseHostAdapter has no pointer-free, nonmutating write probe. Write
     * the exact bytes just read back to ordinary guest RAM so an asymmetric
     * or read-only callback fails before native CARD/EXI execution. MMIO has
     * already been rejected by the preload read. */
    result = state->host->write_bytes(
        state->host->context,
        output->guest_address,
        output->original_bytes,
        sizeof(output->original_bytes));
    if (result != PORPOISE_HOST_OK) {
        porpoise_card_set_host_fault(
            state, result, output->guest_address);
        return 0;
    }
    return 1;
}

static int porpoise_card_store_output(
    PorpoisePpcState *state,
    const PorpoiseCardGuestOutput *output)
{
    uint8_t bytes[4];
    uint32_t raw_value;
    PorpoiseHostResult result;

    if (!output->present) {
        return 1;
    }
    memcpy(&raw_value, &output->value, sizeof(raw_value));
    porpoise_card_write_be32(bytes, raw_value);
    result = state->host->write_bytes(
        state->host->context,
        output->guest_address,
        bytes,
        sizeof(bytes));
    if (result != PORPOISE_HOST_OK) {
        porpoise_card_set_host_fault(
            state, result, output->guest_address);
        return 0;
    }
    return 1;
}

void porpoise_libporpoise_card_probe_ex_adapter(
    PorpoisePpcState *state)
{
    PorpoiseCardGuestOutput memory_size;
    PorpoiseCardGuestOutput sector_size;
    s32 channel;
    s32 result;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }

    /* Both complete output spans are read before the native boundary. A
     * failure therefore cannot enter CARD/EXI code or partially publish a
     * result. Preloading also preserves caller values when native
     * CARDProbeEx returns without writing them. */
    if (!porpoise_card_preload_output(
            state,
            state->gpr[4],
            "CARDProbeEx memSize output is not four-byte aligned",
            &memory_size) ||
        !porpoise_card_preload_output(
            state,
            state->gpr[5],
            "CARDProbeEx sectorSize output is not four-byte aligned",
            &sector_size)) {
        return;
    }
    if (!porpoise_card_preflight_output(state, &memory_size) ||
        !porpoise_card_preflight_output(state, &sector_size)) {
        return;
    }

    memcpy(&channel, &state->gpr[3], sizeof(channel));
    result = CARDProbeEx(
        channel,
        memory_size.present ? &memory_size.value : NULL,
        sector_size.present ? &sector_size.value : NULL);
    if (porpoise_state_has_fault(state)) {
        return;
    }
    if (!porpoise_card_store_output(state, &memory_size) ||
        !porpoise_card_store_output(state, &sector_size)) {
        return;
    }
    memcpy(&state->gpr[3], &result, sizeof(result));
}
