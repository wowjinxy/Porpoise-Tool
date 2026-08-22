#include "porpoise/system_lower.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            (void)fprintf(                                                 \
                stderr,                                                    \
                "check failed at %s:%d: %s\n",                            \
                __FILE__,                                                  \
                __LINE__,                                                  \
                #condition);                                               \
            abort();                                                       \
        }                                                                  \
    } while (0)

static uint32_t encoded_spr_field(uint32_t spr)
{
    return ((spr & UINT32_C(0x1F)) << 5U) |
           ((spr >> 5U) & UINT32_C(0x1F));
}

static uint32_t spr_word(bool write, unsigned int gpr, uint32_t spr)
{
    return (UINT32_C(31) << 26U) |
           ((uint32_t)gpr << 21U) |
           (encoded_spr_field(spr) << 11U) |
           ((write ? UINT32_C(467) : UINT32_C(339)) << 1U);
}

static uint32_t x_word(
    uint32_t primary,
    unsigned int first,
    unsigned int second,
    unsigned int third,
    uint32_t xo)
{
    return (primary << 26U) |
           ((uint32_t)first << 21U) |
           ((uint32_t)second << 16U) |
           ((uint32_t)third << 11U) |
           (xo << 1U);
}

static uint32_t mtcrf_word(unsigned int source, uint32_t mask)
{
    return (UINT32_C(31) << 26U) |
           ((uint32_t)source << 21U) |
           ((mask & UINT32_C(0xFF)) << 12U) |
           (UINT32_C(144) << 1U);
}

static uint32_t mftb_word(unsigned int destination, uint32_t tbr)
{
    return (UINT32_C(31) << 26U) |
           ((uint32_t)destination << 21U) |
           (encoded_spr_field(tbr) << 11U) |
           (UINT32_C(371) << 1U);
}

static PorpoiseSystemInstruction resolve_ok(
    const char *mnemonic,
    const char *operands,
    uint32_t word)
{
    PorpoiseSystemInstruction instruction;

    CHECK(porpoise_system_resolve(
              mnemonic,
              operands,
              word,
              &instruction) == PORPOISE_SYSTEM_RESOLVED);
    CHECK(instruction.detail != NULL);
    return instruction;
}

static void emit_text(
    const PorpoiseSystemInstruction *instruction,
    uint32_t address,
    char *buffer,
    size_t capacity)
{
    FILE *output = tmpfile();
    long length;

    CHECK(output != NULL);
    CHECK(porpoise_system_emit(output, instruction, address));
    CHECK(fflush(output) == 0);
    CHECK(fseek(output, 0L, SEEK_END) == 0);
    length = ftell(output);
    CHECK(length >= 0);
    CHECK((size_t)length + 1U <= capacity);
    CHECK(fseek(output, 0L, SEEK_SET) == 0);
    CHECK(fread(buffer, 1U, (size_t)length, output) == (size_t)length);
    buffer[length] = '\0';
    CHECK(fclose(output) == 0);
}

static void check_before(
    const char *text,
    const char *earlier,
    const char *later)
{
    const char *earlier_position = strstr(text, earlier);
    const char *later_position = strstr(text, later);

    CHECK(earlier_position != NULL);
    CHECK(later_position != NULL);
    CHECK(earlier_position < later_position);
}

static void test_spr_half_swap_and_statuses(void)
{
    PorpoiseSystemInstruction instruction;
    uint32_t word = spr_word(false, 3U, 912U);

    CHECK(((word >> 11U) & UINT32_C(0x3FF)) == UINT32_C(540));
    instruction = resolve_ok("mfspr", "r3, GQR0", word);
    CHECK(instruction.spr_number == 912U);
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_GQR);
    CHECK(instruction.storage_index == 0U);
    CHECK(instruction.status == PORPOISE_LOWERED);
    CHECK(instruction.requires_supervisor);

    instruction = resolve_ok("mfspr", "r3, 912", word);
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_GQR);
    CHECK(porpoise_system_resolve(
              "mfspr",
              "r3, 540",
              word,
              &instruction) == PORPOISE_SYSTEM_INVALID);

    instruction = resolve_ok("mfspr", "r3, 540", spr_word(false, 3U, 540U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_DBAT_UPPER);
    CHECK(instruction.storage_index == 2U);
    CHECK(instruction.status == PORPOISE_LOWERED);

    instruction = resolve_ok("mfspr", "r3, 976", spr_word(false, 3U, 976U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR);
    CHECK(instruction.storage_index == 976U);
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    CHECK(instruction.semantic_test);

    instruction = resolve_ok("mfspr", "r3, 600", spr_word(false, 3U, 600U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_UNKNOWN);
    CHECK(instruction.status == PORPOISE_UNSUPPORTED);
    CHECK(!instruction.semantic_test);

    instruction = resolve_ok("mfspr", "r4, THRM1", spr_word(false, 4U, 1020U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_THERMAL);
    CHECK(instruction.storage_index == 0U);
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    CHECK(instruction.semantic_test);
    instruction = resolve_ok("mtspr", "THRM3, r5", spr_word(true, 5U, 1022U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_THERMAL);
    CHECK(instruction.storage_index == 2U);
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    CHECK(instruction.semantic_test);

    instruction = resolve_ok("mtspr", "MSSCR1, r6", spr_word(true, 6U, 1015U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR);
    CHECK(instruction.storage_index == 1015U);
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    CHECK(instruction.semantic_test);

    instruction = resolve_ok("mfspr", "r4, DSISR", spr_word(false, 4U, 18U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_DSISR);
    CHECK(instruction.status == PORPOISE_LOWERED);
    CHECK(!instruction.semantic_test);
    instruction = resolve_ok("mfspr", "r5, 19", spr_word(false, 5U, 19U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_DAR);

    instruction = resolve_ok("mfspr", "r6, DEC", spr_word(false, 6U, 22U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_DEC);
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    instruction = resolve_ok("mtspr", "SDR1, r7", spr_word(true, 7U, 25U));
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    instruction = resolve_ok("mfspr", "r8, HID0", spr_word(false, 8U, 1008U));
    CHECK(instruction.status == PORPOISE_LOWERED);
    instruction = resolve_ok("mtspr", "HID0, r8", spr_word(true, 8U, 1008U));
    CHECK(instruction.status == PORPOISE_APPROXIMATE);

    instruction = resolve_ok("mfspr", "r9, PVR", spr_word(false, 9U, 287U));
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    instruction = resolve_ok("mtspr", "PVR, r9", spr_word(true, 9U, 287U));
    CHECK(instruction.status == PORPOISE_HOST_NOOP);
    instruction = resolve_ok("mtspr", "HID1, r9", spr_word(true, 9U, 1009U));
    CHECK(instruction.status == PORPOISE_HOST_NOOP);

    instruction = resolve_ok("mfspr", "r10, UMMCR1", spr_word(false, 10U, 940U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_MMCR);
    CHECK(instruction.storage_index == 1U);
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    CHECK(!instruction.requires_supervisor);
    instruction = resolve_ok("mtspr", "USIA, r10", spr_word(true, 10U, 939U));
    CHECK(instruction.status == PORPOISE_UNSUPPORTED);
    instruction = resolve_ok("mtspr", "SIA, r10", spr_word(true, 10U, 955U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_SIA);
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    CHECK(instruction.requires_supervisor);
}

static void test_aliases_and_bats(void)
{
    PorpoiseSystemInstruction instruction;

    instruction = resolve_ok("mfdsisr", "r3", spr_word(false, 3U, 18U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_DSISR);
    instruction = resolve_ok("mtdar", "r4", spr_word(true, 4U, 19U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_DAR);
    instruction = resolve_ok("mfsrr0", "r5", spr_word(false, 5U, 26U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_SRR0);
    instruction = resolve_ok("mtsrr1", "r6", spr_word(true, 6U, 27U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_SRR1);
    instruction = resolve_ok("mfdec", "r7", spr_word(false, 7U, 22U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_DEC);
    instruction = resolve_ok("mtsdr1", "r8", spr_word(true, 8U, 25U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_SDR1);
    instruction = resolve_ok("mfear", "r9", spr_word(false, 9U, 282U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_EAR);

    instruction = resolve_ok("mfxer", "r10", spr_word(false, 10U, 1U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_XER);
    CHECK(!instruction.requires_supervisor);
    instruction = resolve_ok("mtxer", "r11", spr_word(true, 11U, 1U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_XER);

    instruction = resolve_ok("mfsprg", "r12, 3", spr_word(false, 12U, 275U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_SPRG);
    CHECK(instruction.storage_index == 3U);
    instruction = resolve_ok("mtsprg", "2, r13", spr_word(true, 13U, 274U));
    CHECK(instruction.storage_index == 2U);

    instruction = resolve_ok("mfspr", "r14, IBAT4U", spr_word(false, 14U, 560U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_IBAT_UPPER);
    CHECK(instruction.storage_index == 4U);
    instruction = resolve_ok("mtspr", "DBAT7L, r15", spr_word(true, 15U, 575U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_DBAT_LOWER);
    CHECK(instruction.storage_index == 7U);
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    instruction = resolve_ok("mfibat7l", "r16", spr_word(false, 16U, 567U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_IBAT_LOWER);
    CHECK(instruction.storage_index == 7U);
    instruction = resolve_ok("mtdbat4u", "r17", spr_word(true, 17U, 568U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_DBAT_UPPER);
    CHECK(instruction.storage_index == 4U);

    instruction = resolve_ok("mfibatu", "r12, 2", spr_word(false, 12U, 532U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_IBAT_UPPER);
    CHECK(instruction.storage_index == 2U);
    CHECK(instruction.destination_register == 12U);
    CHECK(instruction.status == PORPOISE_LOWERED);
    CHECK(instruction.semantic_test);
    CHECK(instruction.requires_supervisor);
    instruction = resolve_ok("mfibatl", "r13, 5", spr_word(false, 13U, 563U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_IBAT_LOWER);
    CHECK(instruction.storage_index == 5U);
    instruction = resolve_ok("mfdbatu", "r14, 3", spr_word(false, 14U, 542U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_DBAT_UPPER);
    CHECK(instruction.storage_index == 3U);
    instruction = resolve_ok("mfdbatl", "r15, 6", spr_word(false, 15U, 573U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_DBAT_LOWER);
    CHECK(instruction.storage_index == 6U);

    instruction = resolve_ok("mtibatu", "1, r16", spr_word(true, 16U, 530U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_IBAT_UPPER);
    CHECK(instruction.storage_index == 1U);
    CHECK(instruction.source_register == 16U);
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    CHECK(instruction.semantic_test);
    CHECK(instruction.requires_supervisor);
    instruction = resolve_ok("mtibatl", "4, r17", spr_word(true, 17U, 561U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_IBAT_LOWER);
    CHECK(instruction.storage_index == 4U);
    instruction = resolve_ok("mtdbatu", "2, r18", spr_word(true, 18U, 540U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_DBAT_UPPER);
    CHECK(instruction.storage_index == 2U);
    instruction = resolve_ok("mtdbatl", "7, r19", spr_word(true, 19U, 575U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_DBAT_LOWER);
    CHECK(instruction.storage_index == 7U);

    instruction = resolve_ok(
        "mfsr",
        "r18, 15",
        x_word(31U, 18U, 15U, 0U, 595U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_SEGMENT);
    CHECK(instruction.storage_index == 15U);
    instruction = resolve_ok(
        "mtsr",
        "7, r19",
        x_word(31U, 19U, 7U, 0U, 210U));
    CHECK(instruction.storage_index == 7U);
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
}

static void test_invalid_inputs(void)
{
    PorpoiseSystemInstruction instruction;
    uint32_t word = spr_word(false, 3U, 912U);

    CHECK(porpoise_system_resolve(
              "addi", "r3, r3, 1", 0U, &instruction) ==
          PORPOISE_SYSTEM_NOT_RECOGNIZED);
    CHECK(porpoise_system_resolve(
              "not_a_system_opcode", "r3,", 0U, &instruction) ==
          PORPOISE_SYSTEM_NOT_RECOGNIZED);
    CHECK(porpoise_system_resolve(
              "mfspr", "r32, GQR0", word, &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mfspr", "r3, GQR8", word, &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mfspr", "r3, 1024", word, &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mfspr", "r3, GQR0", spr_word(false, 4U, 912U), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mfspr", "r3, GQR0", spr_word(true, 3U, 912U), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mfspr", "r3, GQR0", word | UINT32_C(1), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mtdar", "r4", spr_word(true, 4U, 18U), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mfibatu", "r3, 8", spr_word(false, 3U, 528U), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mfibatu", "2, r3", spr_word(false, 3U, 532U), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mfibatu", "r3, 2", spr_word(false, 3U, 533U), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mtdbatl", "r4, 7", spr_word(true, 4U, 575U), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mtdbatl", "7, r4", spr_word(false, 4U, 575U), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mftb", "", mftb_word(3U, 268U), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mftb", "r3, 267", mftb_word(3U, 268U), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mftb", "r3, 270", mftb_word(3U, 268U), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mftb", "r3, 269", mftb_word(3U, 268U), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "mftbu", "r3, 269", mftb_word(3U, 269U), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(NULL, "", 0U, &instruction) ==
          PORPOISE_SYSTEM_INVALID);
    CHECK(porpoise_system_resolve(
              "sc", "", UINT32_C(0x44000003), &instruction) ==
          PORPOISE_SYSTEM_INVALID);
}

static void test_special_resolution_and_emission(void)
{
    PorpoiseSystemInstruction instruction;
    char text[4096];

    instruction = resolve_ok(
        "mfmsr", "r3", x_word(31U, 3U, 0U, 0U, 83U));
    CHECK(instruction.status == PORPOISE_LOWERED);
    emit_text(&instruction, UINT32_C(0x80001000), text, sizeof(text));
    check_before(text, "porpoise_require_supervisor", "state->gpr[3] = state->msr");

    instruction = resolve_ok(
        "mtmsr", "r4", x_word(31U, 4U, 0U, 0U, 146U));
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    emit_text(&instruction, UINT32_C(0x80001004), text, sizeof(text));
    CHECK(strstr(text, "porpoise_msr_transition_is_exact") != NULL);
    check_before(text, "porpoise_msr_transition_is_exact", "porpoise_write_msr");
    CHECK(strstr(text, "porpoise_write_msr") != NULL);

    instruction = resolve_ok("mtcrf", "0x81, r5", mtcrf_word(5U, 0x81U));
    CHECK(instruction.cr_mask == UINT32_C(0xF000000F));
    emit_text(&instruction, UINT32_C(0x80001008), text, sizeof(text));
    CHECK(strstr(text, "~UINT32_C(0xF000000F)") != NULL);
    CHECK(strstr(text, "state->gpr[5] & UINT32_C(0xF000000F)") != NULL);
    instruction = resolve_ok("mtcr", "r6", mtcrf_word(6U, 0xFFU));
    CHECK(instruction.cr_mask == UINT32_MAX);

    instruction = resolve_ok(
        "mcrxr", "cr3", x_word(31U, 12U, 0U, 0U, 512U));
    CHECK(instruction.operation == PORPOISE_SYSTEM_MCRXR);
    CHECK(instruction.storage_index == 3U);
    CHECK(instruction.status == PORPOISE_LOWERED);
    emit_text(&instruction, UINT32_C(0x8000100A), text, sizeof(text));
    CHECK(strstr(text, "porpoise_cr_set_field(state, 3U") != NULL);
    CHECK(strstr(text, "state->xer &= UINT32_C(0x0FFFFFFF)") != NULL);

    instruction = resolve_ok(
        "mfspr", "r20, 976", spr_word(false, 20U, 976U));
    emit_text(&instruction, UINT32_C(0x8000100B), text, sizeof(text));
    check_before(text, "porpoise_require_supervisor", "state->gpr[20] = state->opaque_spr[976]");
    instruction = resolve_ok(
        "mtspr", "THRM2, r21", spr_word(true, 21U, 1021U));
    emit_text(&instruction, UINT32_C(0x8000100C), text, sizeof(text));
    check_before(text, "porpoise_require_supervisor", "state->thermal_management[1] = state->gpr[21]");

    instruction = resolve_ok(
        "mftb", "r7", mftb_word(7U, 268U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_TIME_BASE_LOWER);
    emit_text(&instruction, UINT32_C(0x8000100C), text, sizeof(text));
    CHECK(strstr(text, "porpoise_time_base_read") != NULL);
    instruction = resolve_ok(
        "mftbu", "r8", mftb_word(8U, 269U));
    emit_text(&instruction, UINT32_C(0x80001010), text, sizeof(text));
    CHECK(strstr(text, ">> 32U") != NULL);
    instruction = resolve_ok(
        "mftb", "r27, 268", mftb_word(27U, 268U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_TIME_BASE_LOWER);
    CHECK(instruction.destination_register == 27U);
    instruction = resolve_ok(
        "mftb", "r28, 269", mftb_word(28U, 269U));
    CHECK(instruction.storage == PORPOISE_SYSTEM_STORAGE_TIME_BASE_UPPER);
    CHECK(instruction.destination_register == 28U);
    emit_text(&instruction, UINT32_C(0x80001014), text, sizeof(text));
    CHECK(strstr(text, ">> 32U") != NULL);
    instruction = resolve_ok("mttbl", "r9", spr_word(true, 9U, 284U));
    emit_text(&instruction, UINT32_C(0x80001018), text, sizeof(text));
    CHECK(strstr(text, "porpoise_time_base_write_lower") != NULL);
    instruction = resolve_ok("mttbu", "r10", spr_word(true, 10U, 285U));
    emit_text(&instruction, UINT32_C(0x8000101C), text, sizeof(text));
    CHECK(strstr(text, "porpoise_time_base_write_upper") != NULL);

    instruction = resolve_ok("mtspr", "SDR1, r11", spr_word(true, 11U, 25U));
    emit_text(&instruction, UINT32_C(0x8000101C), text, sizeof(text));
    check_before(text, "porpoise_require_supervisor", "state->sdr1 = state->gpr[11]");
    instruction = resolve_ok("mtspr", "PVR, r12", spr_word(true, 12U, 287U));
    emit_text(&instruction, UINT32_C(0x80001020), text, sizeof(text));
    check_before(text, "porpoise_require_supervisor", "Architectural host-equivalent no-op");
    CHECK(strstr(text, "state->pvr =") == NULL);
    instruction = resolve_ok("mtspr", "SIA, r24", spr_word(true, 24U, 955U));
    emit_text(&instruction, UINT32_C(0x80001040), text, sizeof(text));
    check_before(
        text,
        "porpoise_require_supervisor",
        "state->sia = state->gpr[24]");

    instruction = resolve_ok("mfibatu", "r12, 2", spr_word(false, 12U, 532U));
    emit_text(&instruction, UINT32_C(0x80001044), text, sizeof(text));
    check_before(
        text,
        "porpoise_require_supervisor",
        "state->gpr[12] = state->ibat_upper[2]");
    instruction = resolve_ok("mtdbatl", "7, r19", spr_word(true, 19U, 575U));
    emit_text(&instruction, UINT32_C(0x80001048), text, sizeof(text));
    check_before(
        text,
        "porpoise_require_supervisor",
        "state->dbat_lower[7] = state->gpr[19]");

    instruction = resolve_ok("mfdec", "r13", spr_word(false, 13U, 22U));
    emit_text(&instruction, UINT32_C(0x80001024), text, sizeof(text));
    CHECK(strstr(text, "porpoise_decrementer_read") != NULL);
    instruction = resolve_ok("mtdec", "r14", spr_word(true, 14U, 22U));
    emit_text(&instruction, UINT32_C(0x80001028), text, sizeof(text));
    CHECK(strstr(text, "porpoise_decrementer_write") != NULL);

    instruction = resolve_ok(
        "rfi", "", x_word(19U, 0U, 0U, 0U, 50U));
    emit_text(&instruction, UINT32_C(0x8000102C), text, sizeof(text));
    check_before(text, "porpoise_require_supervisor", "porpoise_dispatch_available");
    check_before(text, "porpoise_dispatch_available", "porpoise_write_msr");
    CHECK(strstr(text, "0x87C0FF73") != NULL);
    CHECK(strstr(text, "0x00040000") != NULL);
    CHECK(strstr(text, "porpoise_call_address") != NULL);

    instruction = resolve_ok(
        "dcbz", "r0, r15", x_word(31U, 0U, 0U, 15U, 1014U));
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    emit_text(&instruction, UINT32_C(0x80001030), text, sizeof(text));
    CHECK(strstr(text, "porpoise_cache_block_zero(state, state->gpr[15])") != NULL);
    instruction = resolve_ok(
        "dcbi", "r16, r17", x_word(31U, 0U, 16U, 17U, 470U));
    CHECK(instruction.status == PORPOISE_HOST_NOOP);
    emit_text(&instruction, UINT32_C(0x80001034), text, sizeof(text));
    CHECK(strstr(text, "porpoise_data_cache_block_invalidate") != NULL);

    instruction = resolve_ok(
        "twui",
        "r18, -1",
        (UINT32_C(3) << 26U) | (UINT32_C(31) << 21U) |
            (UINT32_C(18) << 16U) | UINT32_C(0xFFFF));
    CHECK(instruction.immediate == UINT32_MAX);
    emit_text(&instruction, UINT32_C(0x80001038), text, sizeof(text));
    CHECK(strstr(text, "PORPOISE_TRAP_ALWAYS") != NULL);
    CHECK(strstr(text, "0xFFFFFFFF") != NULL);

    instruction = resolve_ok("sc", "", (UINT32_C(17) << 26U) | 2U);
    emit_text(&instruction, UINT32_C(0x8000103C), text, sizeof(text));
    CHECK(strstr(text, "porpoise_system_call_event") != NULL);

    instruction = resolve_ok("dcbz_l", "r3, r4", 0U);
    CHECK(instruction.status == PORPOISE_UNSUPPORTED);
    CHECK(!instruction.semantic_test);
    emit_text(&instruction, UINT32_C(0x80001040), text, sizeof(text));
    CHECK(strstr(text, "porpoise_illegal_instruction") != NULL);
}

int main(void)
{
    test_spr_half_swap_and_statuses();
    test_aliases_and_bats();
    test_invalid_inputs();
    test_special_resolution_and_emission();
    return 0;
}
