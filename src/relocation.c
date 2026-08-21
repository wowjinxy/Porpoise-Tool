#include "porpoise/relocation.h"

#include <ctype.h>
#include <string.h>

static bool relocation_kind_from_suffix(
    const char *suffix,
    size_t length,
    PorpoiseRelocationKind *kind_out) {
    if (length == 2U && memcmp(suffix, "@l", 2U) == 0) {
        *kind_out = PORPOISE_RELOCATION_LOW;
        return true;
    }
    if (length == 2U && memcmp(suffix, "@h", 2U) == 0) {
        *kind_out = PORPOISE_RELOCATION_HIGH;
        return true;
    }
    if (length == 3U && memcmp(suffix, "@ha", 3U) == 0) {
        *kind_out = PORPOISE_RELOCATION_HIGH_ADJUSTED;
        return true;
    }
    if (length == 6U && memcmp(suffix, "@sda21", 6U) == 0) {
        *kind_out = PORPOISE_RELOCATION_SDA21;
        return true;
    }
    return false;
}

bool porpoise_relocation_parse_span(
    const char *text,
    size_t length,
    PorpoiseRelocation *relocation_out) {
    size_t index;
    size_t suffix_offset = SIZE_MAX;
    PorpoiseRelocationKind kind = PORPOISE_RELOCATION_NONE;

    if (relocation_out != NULL) {
        memset(relocation_out, 0, sizeof(*relocation_out));
        relocation_out->kind = PORPOISE_RELOCATION_NONE;
    }
    if (text == NULL || relocation_out == NULL || length == 0U) return false;

    for (index = 0U; index < length; index++) {
        if (text[index] == '@') suffix_offset = index;
    }
    if (suffix_offset == SIZE_MAX || suffix_offset == 0U ||
        !relocation_kind_from_suffix(
            text + suffix_offset, length - suffix_offset, &kind)) {
        return false;
    }
    for (index = 0U; index < suffix_offset; index++) {
        unsigned char character = (unsigned char)text[index];
        if (iscntrl(character) || isspace(character) ||
            text[index] == '(' || text[index] == ')' || text[index] == ',') {
            return false;
        }
    }

    relocation_out->kind = kind;
    relocation_out->expression = text;
    relocation_out->expression_length = suffix_offset;
    relocation_out->suffix = text + suffix_offset;
    relocation_out->suffix_length = length - suffix_offset;
    return true;
}

bool porpoise_relocation_parse(
    const char *text,
    PorpoiseRelocation *relocation_out) {
    if (text == NULL) {
        if (relocation_out != NULL) {
            memset(relocation_out, 0, sizeof(*relocation_out));
            relocation_out->kind = PORPOISE_RELOCATION_NONE;
        }
        return false;
    }
    return porpoise_relocation_parse_span(
        text, strlen(text), relocation_out);
}

static bool memory_relocation_mnemonic(const char *mnemonic) {
    static const char *const mnemonics[] = {
        "lbz", "lbzu", "lhz", "lhzu", "lha", "lhau",
        "lwz", "lwzu", "lfs", "lfsu", "lfd", "lfdu",
        "stb", "stbu", "sth", "sthu", "stw", "stwu",
        "stfs", "stfsu", "stfd", "stfdu", "lmw", "stmw"
    };
    size_t index;

    if (mnemonic == NULL) return false;
    for (index = 0U;
         index < sizeof(mnemonics) / sizeof(mnemonics[0]);
         index++) {
        if (strcmp(mnemonic, mnemonics[index]) == 0) return true;
    }
    return false;
}

unsigned int porpoise_relocation_allowed_mask(
    const char *mnemonic,
    size_t operand_index,
    bool memory_offset) {
    if (mnemonic == NULL) return 0U;
    if (memory_offset) {
        if (operand_index == 1U && memory_relocation_mnemonic(mnemonic)) {
            return PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_LOW) |
                   PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_SDA21);
        }
        return 0U;
    }
    if (strcmp(mnemonic, "li") == 0 && operand_index == 1U) {
        return PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_LOW) |
               PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_SDA21);
    }
    if (strcmp(mnemonic, "lis") == 0 && operand_index == 1U) {
        return PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_HIGH) |
               PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_HIGH_ADJUSTED);
    }
    if (strcmp(mnemonic, "addi") == 0 && operand_index == 2U) {
        return PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_LOW) |
               PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_SDA21);
    }
    if (strcmp(mnemonic, "addis") == 0 && operand_index == 2U) {
        return PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_HIGH) |
               PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_HIGH_ADJUSTED);
    }
    if ((strcmp(mnemonic, "addic") == 0 ||
         strcmp(mnemonic, "addic.") == 0 ||
         strcmp(mnemonic, "ori") == 0) && operand_index == 2U) {
        return PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_LOW);
    }
    return 0U;
}

uint32_t porpoise_relocation_variable_word_mask(
    PorpoiseRelocationKind kind) {
    switch (kind) {
        case PORPOISE_RELOCATION_LOW:
        case PORPOISE_RELOCATION_HIGH:
        case PORPOISE_RELOCATION_HIGH_ADJUSTED:
            return UINT32_C(0x0000FFFF);
        case PORPOISE_RELOCATION_SDA21:
            return UINT32_C(0x001FFFFF);
        case PORPOISE_RELOCATION_NONE:
        default:
            return UINT32_C(0);
    }
}

static bool operand_token_delimiter(char character) {
    return isspace((unsigned char)character) || character == ',' ||
           character == '(' || character == ')';
}

static bool relocated_operand_tokens_equal(
    const char *mnemonic,
    size_t operand_index,
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length,
    bool memory_offset) {
    PorpoiseRelocation left_relocation;
    PorpoiseRelocation right_relocation;
    unsigned int allowed;

    if (left_length == right_length &&
        memcmp(left, right, left_length) == 0) {
        return true;
    }
    if (!porpoise_relocation_parse_span(
            left, left_length, &left_relocation) ||
        !porpoise_relocation_parse_span(
            right, right_length, &right_relocation) ||
        left_relocation.kind != right_relocation.kind) {
        return false;
    }
    allowed = porpoise_relocation_allowed_mask(
        mnemonic, operand_index, memory_offset);
    return (allowed & PORPOISE_RELOCATION_MASK(left_relocation.kind)) != 0U;
}

bool porpoise_relocation_operands_equal(
    const char *mnemonic,
    const char *left,
    const char *right) {
    size_t operand_index = 0U;

    if (left == NULL || right == NULL) return left == right;
    if (strcmp(left, right) == 0) return true;
    if (mnemonic == NULL) return false;

    while (*left != '\0' && *right != '\0') {
        bool left_delimiter = operand_token_delimiter(*left);
        bool right_delimiter = operand_token_delimiter(*right);
        const char *left_end;
        const char *right_end;

        if (left_delimiter || right_delimiter) {
            if (!left_delimiter || !right_delimiter || *left != *right)
                return false;
            if (*left == ',') operand_index++;
            left++;
            right++;
            continue;
        }
        left_end = left;
        while (*left_end != '\0' && !operand_token_delimiter(*left_end))
            left_end++;
        right_end = right;
        while (*right_end != '\0' && !operand_token_delimiter(*right_end))
            right_end++;
        if (!relocated_operand_tokens_equal(
                mnemonic, operand_index,
                left, (size_t)(left_end - left),
                right, (size_t)(right_end - right),
                *left_end == '(' && *right_end == '(')) {
            return false;
        }
        left = left_end;
        right = right_end;
    }
    return *left == *right;
}
