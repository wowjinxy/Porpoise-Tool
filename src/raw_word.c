#include "porpoise/raw_word.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static const char DETAIL_ILLEGAL_ENCODING[] =
    "input metadata identifies an illegal encoding that raises an illegal-instruction fault";
static const char DETAIL_LMW_OVERLAP[] =
    "reserved Gekko lmw overlap form uses a precomputed effective address";

static bool parse_raw_value(
    const char *text,
    uint32_t *value_out,
    const char **comment_out,
    size_t *comment_length_out)
{
    const char *cursor = text;
    char *end;
    unsigned long long value;

    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor == '\0' || *cursor == '-') return false;
    errno = 0;
    value = strtoull(cursor, &end, 0);
    if (errno != 0 || end == cursor || value > UINT32_MAX) return false;
    cursor = end;
    while (isspace((unsigned char)*cursor)) cursor++;
    *comment_out = NULL;
    *comment_length_out = 0U;
    if (*cursor != '\0') {
        const char *comment_end;

        if (cursor[0] != '/' || cursor[1] != '*') return false;
        comment_end = strstr(cursor + 2, "*/");
        if (comment_end == NULL) return false;
        *comment_out = cursor + 2;
        *comment_length_out = (size_t)(comment_end - (cursor + 2));
        cursor = comment_end + 2;
        while (isspace((unsigned char)*cursor)) cursor++;
        if (*cursor != '\0') return false;
    }
    *value_out = (uint32_t)value;
    return true;
}

static bool comment_is_illegal(const char *comment, size_t length)
{
    static const char INVALID[] = "invalid";
    static const char ILLEGAL[] = "illegal:";

    if (comment == NULL) return false;
    while (length != 0U && isspace((unsigned char)*comment)) {
        comment++;
        length--;
    }
    while (length != 0U && isspace((unsigned char)comment[length - 1U])) {
        length--;
    }
    return (length == sizeof(INVALID) - 1U &&
            memcmp(comment, INVALID, sizeof(INVALID) - 1U) == 0) ||
           (length >= sizeof(ILLEGAL) - 1U &&
            memcmp(comment, ILLEGAL, sizeof(ILLEGAL) - 1U) == 0);
}

static bool comment_is_illegal_lmw(const char *comment, size_t length)
{
    static const char PREFIX[] = "illegal:";
    static const char MNEMONIC[] = "lmw";

    if (comment == NULL) return false;
    while (length != 0U && isspace((unsigned char)*comment)) {
        comment++;
        length--;
    }
    if (length < sizeof(PREFIX) - 1U ||
        memcmp(comment, PREFIX, sizeof(PREFIX) - 1U) != 0) {
        return false;
    }
    comment += sizeof(PREFIX) - 1U;
    length -= sizeof(PREFIX) - 1U;
    while (length != 0U && isspace((unsigned char)*comment)) {
        comment++;
        length--;
    }
    return length >= sizeof(MNEMONIC) - 1U &&
           memcmp(comment, MNEMONIC, sizeof(MNEMONIC) - 1U) == 0 &&
           (length == sizeof(MNEMONIC) - 1U ||
            isspace((unsigned char)comment[sizeof(MNEMONIC) - 1U]));
}

static bool file_printf(FILE *output, const char *format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vfprintf(output, format, arguments);
    va_end(arguments);
    return result >= 0;
}

PorpoiseRawWordResolveResult porpoise_raw_word_resolve(
    const char *mnemonic,
    const char *operands,
    uint32_t word,
    PorpoiseRawWordInstruction *instruction)
{
    uint32_t operand_value;
    const char *comment;
    size_t comment_length;

    if (mnemonic == NULL || operands == NULL || instruction == NULL) {
        return PORPOISE_RAW_WORD_INVALID;
    }
    if (strcmp(mnemonic, ".4byte") != 0) {
        return PORPOISE_RAW_WORD_NOT_RECOGNIZED;
    }
    memset(instruction, 0, sizeof(*instruction));
    instruction->word = word;
    if (!parse_raw_value(
            operands,
            &operand_value,
            &comment,
            &comment_length) ||
        operand_value != word) {
        return PORPOISE_RAW_WORD_INVALID;
    }

    if ((word >> 26U) == UINT32_C(46)) {
        unsigned int destination_register =
            (unsigned int)((word >> 21U) & 31U);
        unsigned int base_register =
            (unsigned int)((word >> 16U) & 31U);

        if (base_register == 0U ||
            base_register < destination_register ||
            !comment_is_illegal_lmw(comment, comment_length)) {
            return PORPOISE_RAW_WORD_UNSUPPORTED;
        }
        instruction->operation = PORPOISE_RAW_WORD_LMW_OVERLAP;
        instruction->status = PORPOISE_APPROXIMATE;
        instruction->semantic_test = true;
        instruction->detail = DETAIL_LMW_OVERLAP;
        instruction->destination_register = destination_register;
        instruction->base_register = base_register;
        instruction->displacement = (int32_t)(int16_t)(word & UINT32_C(0xFFFF));
        return PORPOISE_RAW_WORD_RESOLVED;
    }
    if (!comment_is_illegal(comment, comment_length)) {
        return PORPOISE_RAW_WORD_UNSUPPORTED;
    }
    instruction->operation = PORPOISE_RAW_WORD_ILLEGAL_ENCODING;
    instruction->status = PORPOISE_APPROXIMATE;
    instruction->semantic_test = true;
    instruction->detail = DETAIL_ILLEGAL_ENCODING;
    return PORPOISE_RAW_WORD_RESOLVED;
}

bool porpoise_raw_word_emit(
    FILE *output,
    const PorpoiseRawWordInstruction *instruction,
    uint32_t instruction_address)
{
    if (output == NULL || instruction == NULL) return false;
    if (instruction->operation == PORPOISE_RAW_WORD_ILLEGAL_ENCODING) {
        return file_printf(
            output,
            "    (void)porpoise_illegal_instruction(state, UINT32_C(0x%08lX), \"annotated illegal instruction encoding reached\");\n    return;\n",
            (unsigned long)instruction_address);
    }
    if (instruction->operation == PORPOISE_RAW_WORD_LMW_OVERLAP) {
        uint32_t encoded_displacement = (uint32_t)instruction->displacement;

        if (instruction->base_register == 0U) {
            return file_printf(
                output,
                "    { uint32_t ea = UINT32_C(0x%08lX); if (!porpoise_load_multiple_words(state, ea, %uU)) return; }\n",
                (unsigned long)encoded_displacement,
                instruction->destination_register);
        }
        return file_printf(
            output,
            "    { uint32_t ea = state->gpr[%u] + UINT32_C(0x%08lX); if (!porpoise_load_multiple_words(state, ea, %uU)) return; }\n",
            instruction->base_register,
            (unsigned long)encoded_displacement,
            instruction->destination_register);
    }
    return false;
}
