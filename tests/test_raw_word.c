#include "porpoise/raw_word.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n",         \
                          __FILE__, __LINE__, #condition);                  \
            abort();                                                       \
        }                                                                  \
    } while (0)

static void emitted_text(
    const PorpoiseRawWordInstruction *instruction,
    uint32_t address,
    char *buffer,
    size_t capacity)
{
    FILE *output = tmpfile();
    long length;

    CHECK(output != NULL);
    CHECK(porpoise_raw_word_emit(output, instruction, address));
    CHECK(fflush(output) == 0);
    CHECK(fseek(output, 0L, SEEK_END) == 0);
    length = ftell(output);
    CHECK(length >= 0 && (size_t)length + 1U <= capacity);
    CHECK(fseek(output, 0L, SEEK_SET) == 0);
    CHECK(fread(buffer, 1U, (size_t)length, output) == (size_t)length);
    buffer[length] = '\0';
    CHECK(fclose(output) == 0);
}

int main(void)
{
    PorpoiseRawWordInstruction instruction;
    char text[1024];

    CHECK(porpoise_raw_word_resolve(
              ".4byte", "0x00000000 /* invalid */", UINT32_C(0),
              &instruction) == PORPOISE_RAW_WORD_RESOLVED);
    CHECK(instruction.operation == PORPOISE_RAW_WORD_ILLEGAL_ENCODING);
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    CHECK(instruction.semantic_test);
    emitted_text(&instruction, UINT32_C(0x80001000), text, sizeof(text));
    CHECK(strstr(text, "porpoise_illegal_instruction") != NULL);
    CHECK(strstr(text, "return;") != NULL);

    CHECK(porpoise_raw_word_resolve(
              ".4byte", "0xB8030000 /* illegal: lmw r0, 0(r3) */",
              UINT32_C(0xB8030000), &instruction) ==
          PORPOISE_RAW_WORD_RESOLVED);
    CHECK(instruction.operation == PORPOISE_RAW_WORD_LMW_OVERLAP);
    CHECK(instruction.destination_register == 0U);
    CHECK(instruction.base_register == 3U);
    CHECK(instruction.displacement == 0);
    CHECK(instruction.status == PORPOISE_APPROXIMATE);
    CHECK(instruction.semantic_test);
    emitted_text(&instruction, UINT32_C(0x80001004), text, sizeof(text));
    CHECK(strstr(text, "uint32_t ea = state->gpr[3]") != NULL);
    CHECK(strstr(text, "porpoise_load_multiple_words(state, ea, 0U)") != NULL);

    CHECK(porpoise_raw_word_resolve(
              ".4byte", "0xFFFFFFFF", UINT32_C(0), &instruction) ==
          PORPOISE_RAW_WORD_INVALID);
    CHECK(porpoise_raw_word_resolve(
              ".4byte", "0xFFFFFFFF", UINT32_MAX, &instruction) ==
          PORPOISE_RAW_WORD_UNSUPPORTED);
    CHECK(porpoise_raw_word_resolve(
              ".4byte", "0xB8030000", UINT32_C(0xB8030000), &instruction) ==
          PORPOISE_RAW_WORD_UNSUPPORTED);
    CHECK(porpoise_raw_word_resolve(
              ".4byte", "0xB8830000 /* illegal: lmw r4, 0x0(r3) */",
              UINT32_C(0xB8830000), &instruction) ==
          PORPOISE_RAW_WORD_UNSUPPORTED);
    CHECK(porpoise_raw_word_resolve(
              ".4byte", "-1", UINT32_MAX, &instruction) ==
          PORPOISE_RAW_WORD_INVALID);
    CHECK(porpoise_raw_word_resolve(
              ".long", "0", UINT32_C(0), &instruction) ==
          PORPOISE_RAW_WORD_NOT_RECOGNIZED);
    return 0;
}
