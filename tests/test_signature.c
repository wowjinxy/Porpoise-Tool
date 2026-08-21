#include "porpoise/signature.h"

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

typedef struct SignatureFixture {
    PorpoiseFunction function;
    PorpoiseAsmItem items[9];
} SignatureFixture;

static void set_instruction(
    PorpoiseAsmItem *item,
    uint32_t address,
    uint32_t word,
    char *mnemonic,
    char *operands) {
    memset(item, 0, sizeof(*item));
    item->kind = PORPOISE_ASM_INSTRUCTION;
    item->address = address;
    item->word = word;
    item->mnemonic = mnemonic;
    item->operands = operands;
}

static void init_fixture(
    SignatureFixture *fixture,
    char *name,
    uint32_t base,
    uint32_t high_word,
    uint32_t low_word,
    char *high_operands,
    char *low_operands,
    char *local_label,
    char *external_target,
    uint32_t external_branch_word) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->function.name = name;
    fixture->function.start_address = base;
    fixture->function.size = 32U;
    fixture->function.items = fixture->items;
    fixture->function.item_count = 9U;
    fixture->function.item_capacity = 9U;
    fixture->function.instruction_count = 8U;

    set_instruction(&fixture->items[0], base, high_word,
                    "lis", high_operands);
    set_instruction(&fixture->items[1], base + 4U, low_word,
                    "addi", low_operands);
    set_instruction(&fixture->items[2], base + 8U,
                    UINT32_C(0x2c030007), "cmpwi", "r3, 7");
    set_instruction(&fixture->items[3], base + 12U,
                    UINT32_C(0x41820008), "beq", local_label);
    set_instruction(&fixture->items[4], base + 16U,
                    UINT32_C(0x38830001), "addi", "r4, r3, 1");
    fixture->items[5].kind = PORPOISE_ASM_LABEL;
    fixture->items[5].label = local_label;
    set_instruction(&fixture->items[6], base + 20U,
                    external_branch_word, "bl", external_target);
    set_instruction(&fixture->items[7], base + 24U,
                    UINT32_C(0x7c632278), "xor", "r3, r3, r4");
    set_instruction(&fixture->items[8], base + 28U,
                    UINT32_C(0x4e800020), "blr", "");
}

static void init_matching_fixtures(
    SignatureFixture *left,
    SignatureFixture *right) {
    init_fixture(left, "known_sdk", UINT32_C(0x80001000),
                 UINT32_C(0x3c608001), UINT32_C(0x38631234),
                 "r3, object_a@ha", "r3, r3, object_a@l",
                 ".Ldone_a", "sdk_call_a", UINT32_C(0x48001001));
    init_fixture(right, "candidate", UINT32_C(0x80422000),
                 UINT32_C(0x3c60abcd), UINT32_C(0x3863cdef),
                 "r3, lbl_80430000@ha", "r3, r3, lbl_80430000@l",
                 ".Ldone_b", "fn_80440000", UINT32_C(0x4bffe001));
}

static void test_relocation_and_address_normalization(void) {
    SignatureFixture left;
    SignatureFixture right;
    PorpoiseFunctionSignature left_signature;
    PorpoiseFunctionSignature right_signature;

    init_matching_fixtures(&left, &right);
    CHECK(porpoise_signature_compute(NULL, &left.function,
                                     &left_signature));
    CHECK(porpoise_signature_compute(NULL, &right.function,
                                     &right_signature));
    CHECK(left_signature.issue_flags == PORPOISE_SIGNATURE_ISSUE_NONE);
    CHECK(left_signature.function_size == 32U);
    CHECK(left_signature.instruction_count == 8U);
    CHECK(left_signature.relocation_count == 2U);
    CHECK(left_signature.internal_branch_count == 1U);
    CHECK(left_signature.external_branch_count == 1U);
    CHECK(left_signature.external_target_count == 2U);
    CHECK(left_signature.fixed_instruction_count == 5U);
    CHECK(left_signature.meaningful_fixed_instruction_count == 4U);
    CHECK(porpoise_signature_is_automatic_match_eligible(
        &left_signature));
    CHECK(porpoise_signature_equal(&left_signature, &right_signature));
    CHECK(strcmp(left_signature.digest_hex,
                 right_signature.digest_hex) == 0);
}

static void test_constants_and_size_remain_exact(void) {
    SignatureFixture baseline;
    SignatureFixture changed;
    PorpoiseFunctionSignature baseline_signature;
    PorpoiseFunctionSignature changed_signature;

    init_matching_fixtures(&baseline, &changed);
    changed.items[2].word = UINT32_C(0x2c030008);
    CHECK(porpoise_signature_compute(NULL, &baseline.function,
                                     &baseline_signature));
    CHECK(porpoise_signature_compute(NULL, &changed.function,
                                     &changed_signature));
    CHECK(!porpoise_signature_equal(&baseline_signature,
                                    &changed_signature));

    init_matching_fixtures(&baseline, &changed);
    changed.function.size = 36U;
    CHECK(porpoise_signature_compute(NULL, &baseline.function,
                                     &baseline_signature));
    CHECK(porpoise_signature_compute(NULL, &changed.function,
                                     &changed_signature));
    CHECK(!porpoise_signature_equal(&baseline_signature,
                                    &changed_signature));
}

static void test_internal_control_flow_remains_exact(void) {
    SignatureFixture baseline;
    SignatureFixture changed;
    PorpoiseAsmItem temporary;
    PorpoiseFunctionSignature baseline_signature;
    PorpoiseFunctionSignature changed_signature;

    init_matching_fixtures(&baseline, &changed);
    temporary = changed.items[5];
    changed.items[5] = changed.items[6];
    changed.items[6] = temporary;
    changed.items[3].word = UINT32_C(0x4182000c);

    CHECK(porpoise_signature_compute(NULL, &baseline.function,
                                     &baseline_signature));
    CHECK(porpoise_signature_compute(NULL, &changed.function,
                                     &changed_signature));
    CHECK(changed_signature.issue_flags == PORPOISE_SIGNATURE_ISSUE_NONE);
    CHECK(!porpoise_signature_equal(&baseline_signature,
                                    &changed_signature));
}

static void test_external_target_topology_remains_exact(void) {
    SignatureFixture repeated;
    SignatureFixture distinct;
    PorpoiseFunctionSignature repeated_signature;
    PorpoiseFunctionSignature distinct_signature;

    init_matching_fixtures(&repeated, &distinct);
    set_instruction(&repeated.items[7],
                    repeated.function.start_address + 24U,
                    UINT32_C(0x48002001), "bl", "sdk_call_a");
    set_instruction(&distinct.items[7],
                    distinct.function.start_address + 24U,
                    UINT32_C(0x4bffc001), "bl", "another_sdk_call");

    CHECK(porpoise_signature_compute(NULL, &repeated.function,
                                     &repeated_signature));
    CHECK(porpoise_signature_compute(NULL, &distinct.function,
                                     &distinct_signature));
    CHECK(repeated_signature.external_target_count == 2U);
    CHECK(distinct_signature.external_target_count == 3U);
    CHECK(!porpoise_signature_equal(&repeated_signature,
                                    &distinct_signature));
}

static void test_absolute_internal_branch_rebases(void) {
    SignatureFixture left;
    SignatureFixture right;
    PorpoiseFunctionSignature left_signature;
    PorpoiseFunctionSignature right_signature;

    init_fixture(&left, "absolute_left", UINT32_C(0x00001000),
                 UINT32_C(0x3c608001), UINT32_C(0x38631234),
                 "r3, object_a@ha", "r3, r3, object_a@l",
                 ".Ldone_a", "sdk_call_a", UINT32_C(0x48001001));
    init_fixture(&right, "absolute_right", UINT32_C(0x00002000),
                 UINT32_C(0x3c60abcd), UINT32_C(0x3863cdef),
                 "r3, object_b@ha", "r3, r3, object_b@l",
                 ".Ldone_b", "sdk_call_b", UINT32_C(0x4bffe001));
    left.items[3].word = UINT32_C(0x41821016);
    right.items[3].word = UINT32_C(0x41822016);

    CHECK(porpoise_signature_compute(NULL, &left.function,
                                     &left_signature));
    CHECK(porpoise_signature_compute(NULL, &right.function,
                                     &right_signature));
    CHECK(left_signature.issue_flags == PORPOISE_SIGNATURE_ISSUE_NONE);
    CHECK(right_signature.issue_flags == PORPOISE_SIGNATURE_ISSUE_NONE);
    CHECK(porpoise_signature_equal(&left_signature, &right_signature));
}

static void test_report_only_signatures(void) {
    SignatureFixture malformed;
    SignatureFixture tiny;
    PorpoiseFunctionSignature signature;

    init_matching_fixtures(&malformed, &tiny);
    malformed.items[1].operands = "r3, r3, object@ha";
    CHECK(porpoise_signature_compute(NULL, &malformed.function, &signature));
    CHECK((signature.issue_flags &
           PORPOISE_SIGNATURE_ISSUE_MALFORMED_RELOCATION) != 0U);
    CHECK(!porpoise_signature_is_automatic_match_eligible(&signature));

    tiny.function.item_count = 2U;
    tiny.function.instruction_count = 2U;
    tiny.function.size = 8U;
    CHECK(porpoise_signature_compute(NULL, &tiny.function, &signature));
    CHECK(signature.issue_flags == PORPOISE_SIGNATURE_ISSUE_NONE);
    CHECK(!porpoise_signature_is_automatic_match_eligible(&signature));
}

static void test_invalid_internal_branch_is_report_only(void) {
    SignatureFixture fixture;
    SignatureFixture unused;
    PorpoiseFunctionSignature signature;

    init_matching_fixtures(&fixture, &unused);
    fixture.items[3].word = UINT32_C(0x4182000c);
    CHECK(porpoise_signature_compute(NULL, &fixture.function, &signature));
    CHECK((signature.issue_flags &
           PORPOISE_SIGNATURE_ISSUE_INVALID_INTERNAL_BRANCH) != 0U);
    CHECK(!porpoise_signature_is_automatic_match_eligible(&signature));
}

int main(void) {
    test_relocation_and_address_normalization();
    test_constants_and_size_remain_exact();
    test_internal_control_flow_remains_exact();
    test_external_target_topology_remains_exact();
    test_absolute_internal_branch_rebases();
    test_report_only_signatures();
    test_invalid_internal_branch_is_report_only();

    if (failures != 0U) {
        fprintf(stderr, "%u signature test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
