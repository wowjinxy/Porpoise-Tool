#include "porpoise/relocation.h"

#include <stdio.h>
#include <string.h>

static unsigned int failures = 0U;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                     \
                    __FILE__, __LINE__, #condition);                            \
            failures++;                                                        \
        }                                                                       \
    } while (0)

static void check_parse(
    const char *text,
    PorpoiseRelocationKind expected_kind,
    const char *expected_expression,
    const char *expected_suffix) {
    PorpoiseRelocation relocation;

    CHECK(porpoise_relocation_parse(text, &relocation));
    CHECK(relocation.kind == expected_kind);
    CHECK(relocation.expression_length == strlen(expected_expression));
    CHECK(memcmp(relocation.expression, expected_expression,
                 relocation.expression_length) == 0);
    CHECK(relocation.suffix_length == strlen(expected_suffix));
    CHECK(memcmp(relocation.suffix, expected_suffix,
                 relocation.suffix_length) == 0);
}

static void test_parsing(void) {
    PorpoiseRelocation relocation;
    const char embedded[] = {'o', 'b', 'j', '@', 'h', 'a', 'x'};

    check_parse("object@l", PORPOISE_RELOCATION_LOW, "object", "@l");
    check_parse("object@h", PORPOISE_RELOCATION_HIGH, "object", "@h");
    check_parse("object+4@ha", PORPOISE_RELOCATION_HIGH_ADJUSTED,
                "object+4", "@ha");
    check_parse("_SDA_BASE_@sda21", PORPOISE_RELOCATION_SDA21,
                "_SDA_BASE_", "@sda21");
    CHECK(porpoise_relocation_parse_span(
        embedded, 6U, &relocation));
    CHECK(relocation.kind == PORPOISE_RELOCATION_HIGH_ADJUSTED);
    CHECK(relocation.expression_length == 3U);

    CHECK(!porpoise_relocation_parse(NULL, &relocation));
    CHECK(relocation.kind == PORPOISE_RELOCATION_NONE);
    CHECK(!porpoise_relocation_parse("object", &relocation));
    CHECK(!porpoise_relocation_parse("@l", &relocation));
    CHECK(!porpoise_relocation_parse("object@higher", &relocation));
    CHECK(!porpoise_relocation_parse("object name@l", &relocation));
    CHECK(!porpoise_relocation_parse("object(r3)@l", &relocation));
    CHECK(!porpoise_relocation_parse("object,later@l", &relocation));
    CHECK(!porpoise_relocation_parse("object@l", NULL));
}

static void test_context_masks(void) {
    unsigned int low =
        PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_LOW);
    unsigned int high =
        PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_HIGH);
    unsigned int high_adjusted =
        PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_HIGH_ADJUSTED);
    unsigned int sda21 =
        PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_SDA21);

    CHECK(porpoise_relocation_allowed_mask("li", 1U, false) ==
          (low | sda21));
    CHECK(porpoise_relocation_allowed_mask("lis", 1U, false) ==
          (high | high_adjusted));
    CHECK(porpoise_relocation_allowed_mask("addi", 2U, false) ==
          (low | sda21));
    CHECK(porpoise_relocation_allowed_mask("addis", 2U, false) ==
          (high | high_adjusted));
    CHECK(porpoise_relocation_allowed_mask("addic.", 2U, false) == low);
    CHECK(porpoise_relocation_allowed_mask("ori", 2U, false) == low);
    CHECK(porpoise_relocation_allowed_mask("lwz", 1U, true) ==
          (low | sda21));
    CHECK(porpoise_relocation_allowed_mask("stfd", 1U, true) ==
          (low | sda21));

    CHECK(porpoise_relocation_allowed_mask("li", 0U, false) == 0U);
    CHECK(porpoise_relocation_allowed_mask("ori", 2U, true) == 0U);
    CHECK(porpoise_relocation_allowed_mask("lwzx", 1U, true) == 0U);
    CHECK(porpoise_relocation_allowed_mask(NULL, 1U, false) == 0U);

    CHECK(porpoise_relocation_variable_word_mask(
              PORPOISE_RELOCATION_LOW) == UINT32_C(0x0000FFFF));
    CHECK(porpoise_relocation_variable_word_mask(
              PORPOISE_RELOCATION_HIGH) == UINT32_C(0x0000FFFF));
    CHECK(porpoise_relocation_variable_word_mask(
              PORPOISE_RELOCATION_HIGH_ADJUSTED) ==
          UINT32_C(0x0000FFFF));
    CHECK(porpoise_relocation_variable_word_mask(
              PORPOISE_RELOCATION_SDA21) == UINT32_C(0x001FFFFF));
    CHECK(porpoise_relocation_variable_word_mask(
              PORPOISE_RELOCATION_NONE) == UINT32_C(0));
}

static void test_operand_equivalence(void) {
    CHECK(porpoise_relocation_operands_equal(
        "lis", "r3, object_a@ha", "r3, object_b@ha"));
    CHECK(porpoise_relocation_operands_equal(
        "addi", "r3, r3, object_a@l", "r3, r3, object_b@l"));
    CHECK(porpoise_relocation_operands_equal(
        "lwz", "r3, object_a@l(r4)", "r3, object_b@l(r4)"));
    CHECK(porpoise_relocation_operands_equal(
        "lwz", "r3, object_a@sda21(r13)",
        "r3, object_b@sda21(r13)"));
    CHECK(porpoise_relocation_operands_equal(
        "b", "object_a", "object_a"));
    CHECK(porpoise_relocation_operands_equal("li", NULL, NULL));

    CHECK(!porpoise_relocation_operands_equal(
        "lis", "r3, object_a@ha", "r3, object_b@h"));
    CHECK(!porpoise_relocation_operands_equal(
        "li", "r3, object_a@ha", "r3, object_b@ha"));
    CHECK(!porpoise_relocation_operands_equal(
        "lwzx", "r3, object_a@l(r4)", "r3, object_b@l(r4)"));
    CHECK(!porpoise_relocation_operands_equal(
        "lwz", "r3, object_a@l(r4)", "r3, object_b@l(r5)"));
    CHECK(!porpoise_relocation_operands_equal(
        "lis", "r3, object_a@ha", "r3,  object_b@ha"));
    CHECK(!porpoise_relocation_operands_equal("li", NULL, "r3, 0"));
}

int main(void) {
    test_parsing();
    test_context_masks();
    test_operand_equivalence();

    if (failures != 0U) {
        fprintf(stderr, "%u relocation test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
