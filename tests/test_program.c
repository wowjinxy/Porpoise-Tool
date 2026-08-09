#include "porpoise/program.h"
#include "porpoise/analysis.h"

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

static bool make_fixture_path(
    char *path,
    size_t capacity,
    const char *source_root,
    const char *name) {
    int written = snprintf(path, capacity, "%s/tests/fixtures/program/%s",
                           source_root, name);
    return written >= 0 && (size_t)written < capacity;
}

static bool diagnostics_contain(
    const PorpoiseDiagnostics *diagnostics,
    const char *fragment) {
    size_t index;
    for (index = 0U; index < diagnostics->count; index++) {
        if (strstr(diagnostics->items[index].message, fragment) != NULL) {
            return true;
        }
    }
    return false;
}

static bool path_has_basename(const char *path, const char *basename) {
    const char *forward;
    const char *backward;
    const char *leaf;
    if (path == NULL || basename == NULL) return false;
    forward = strrchr(path, '/');
    backward = strrchr(path, '\\');
    leaf = forward;
    if (leaf == NULL || (backward != NULL && backward > leaf)) leaf = backward;
    return strcmp(leaf == NULL ? path : leaf + 1, basename) == 0;
}

static void test_valid_aliases(const char *source_root) {
    char path[PORPOISE_PATH_CAPACITY];
    PorpoiseProgram program;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseFunction *primary;
    const PorpoiseFunction *secondary;
    const PorpoiseFunction *owner = NULL;
    const PorpoiseSourceFile *source_file = NULL;
    const PorpoiseAddressAlias *alias;
    const PorpoiseAddressAlias *resolved_alias = NULL;
    uint32_t address = 0U;
    int result;

    CHECK(make_fixture_path(path, sizeof(path), source_root,
                            "symbol_aliases_valid.s"));
    porpoise_program_init(&program);
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_program_load(&program, path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    CHECK(program.file_count == 1U);
    CHECK(program.files[0].function_count == 2U);
    CHECK(program.symbol_index_count == 9U);
    CHECK(program.symbol_index_capacity == program.symbol_index_count);
    CHECK(program.label_index_count == 1U);
    CHECK(program.label_index_capacity == program.label_index_count);

    primary = porpoise_program_find_function(&program, "primary");
    secondary = porpoise_program_find_function(&program, "secondary");
    CHECK(primary != NULL);
    CHECK(secondary != NULL);
    if (primary != NULL) {
        CHECK(primary->alias_count == 4U);
        if (primary->alias_count == 4U) {
            CHECK(strcmp(primary->aliases[0].name, "entry.alias") == 0);
            CHECK(strcmp(primary->aliases[0].c_name, "entry_alias") == 0);
            CHECK(primary->aliases[0].is_global);
            CHECK(primary->aliases[0].source_line == 2U);
            CHECK(primary->aliases[0].address == UINT32_C(0x80001000));
            CHECK(primary->aliases[0].instruction_item_index == 0U);

            CHECK(strcmp(primary->aliases[1].name, "start_alias") == 0);
            CHECK(!primary->aliases[1].is_global);
            CHECK(primary->aliases[1].source_line == 4U);
            CHECK(primary->aliases[1].address == UINT32_C(0x80001000));
            CHECK(primary->aliases[1].instruction_item_index == 0U);

            CHECK(strcmp(primary->aliases[2].name, "resume_alias") == 0);
            CHECK(primary->aliases[2].is_global);
            CHECK(primary->aliases[2].source_line == 6U);
            CHECK(primary->aliases[2].address == UINT32_C(0x80001004));
            CHECK(primary->aliases[2].instruction_item_index == 2U);

            CHECK(strcmp(primary->aliases[3].name, "same-address-alias") == 0);
            CHECK(strcmp(primary->aliases[3].c_name, "same_address_alias") == 0);
            CHECK(!primary->aliases[3].is_global);
            CHECK(primary->aliases[3].source_line == 8U);
            CHECK(primary->aliases[3].address == UINT32_C(0x80001004));
            CHECK(primary->aliases[3].instruction_item_index == 2U);
        }
    }
    if (secondary != NULL) {
        CHECK(secondary->alias_count == 1U);
        if (secondary->alias_count == 1U) {
            CHECK(strcmp(secondary->aliases[0].name, "second_alias") == 0);
            CHECK(secondary->aliases[0].source_line == 12U);
            CHECK(secondary->aliases[0].address == UINT32_C(0x80002000));
            CHECK(secondary->aliases[0].instruction_item_index == 0U);
        }
    }

    CHECK(porpoise_program_count_aliases(&program) == 5U);
    alias = porpoise_program_alias_at(&program, 0U, &source_file, &owner);
    CHECK(alias != NULL && strcmp(alias->name, "entry.alias") == 0);
    CHECK(source_file == &program.files[0]);
    CHECK(owner == primary);
    alias = porpoise_program_alias_at(&program, 4U, &source_file, &owner);
    CHECK(alias != NULL && strcmp(alias->name, "second_alias") == 0);
    CHECK(owner == secondary);
    alias = porpoise_program_alias_at(&program, 5U, &source_file, &owner);
    CHECK(alias == NULL);
    CHECK(source_file == NULL);
    CHECK(owner == NULL);

    alias = porpoise_program_find_alias(&program, "entry_alias", &owner);
    CHECK(alias != NULL && strcmp(alias->name, "entry.alias") == 0);
    CHECK(owner == primary);
    alias = porpoise_program_find_alias(&program, "same-address-alias", &owner);
    CHECK(alias != NULL && alias->address == UINT32_C(0x80001004));
    CHECK(owner == primary);

    CHECK(porpoise_program_resolve_symbol(&program, "primary", &owner,
                                          &resolved_alias, &address));
    CHECK(owner == primary);
    CHECK(resolved_alias == NULL);
    CHECK(address == UINT32_C(0x80001000));
    CHECK(porpoise_program_resolve_symbol(&program, "same_address_alias", &owner,
                                          &resolved_alias, &address));
    CHECK(owner == primary);
    CHECK(resolved_alias != NULL &&
          strcmp(resolved_alias->name, "same-address-alias") == 0);
    CHECK(address == UINT32_C(0x80001004));
    CHECK(!porpoise_program_resolve_symbol(&program, "missing", &owner,
                                           &resolved_alias, &address));
    CHECK(owner == NULL);
    CHECK(resolved_alias == NULL);
    CHECK(address == 0U);

    program.files[0].functions[0].skipped = true;
    CHECK(porpoise_program_find_alias(&program, "entry.alias", &owner) == NULL);
    CHECK(owner == NULL);
    CHECK(porpoise_program_count_aliases(&program) == 5U);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_program_free(&program);
}

static void test_double_load_rejected(const char *source_root) {
    char path[PORPOISE_PATH_CAPACITY];
    PorpoiseProgram program;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseSourceFile *files_before;
    const PorpoiseProgramSymbolIndexEntry *symbols_before;
    const PorpoiseProgramLabelIndexEntry *labels_before;
    size_t file_count_before;
    size_t file_capacity_before;
    size_t symbol_count_before;
    size_t symbol_capacity_before;
    size_t label_count_before;
    size_t label_capacity_before;
    int result;

    CHECK(make_fixture_path(path, sizeof(path), source_root,
                            "symbol_aliases_valid.s"));
    porpoise_program_init(&program);
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_program_load(&program, path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    files_before = program.files;
    symbols_before = program.symbol_index;
    labels_before = program.label_index;
    file_count_before = program.file_count;
    file_capacity_before = program.file_capacity;
    symbol_count_before = program.symbol_index_count;
    symbol_capacity_before = program.symbol_index_capacity;
    label_count_before = program.label_index_count;
    label_capacity_before = program.label_index_capacity;

    result = porpoise_program_load(&program, path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_INTERNAL);
    CHECK(diagnostics_contain(&diagnostics,
                              "program already contains parsed input"));
    CHECK(program.files == files_before);
    CHECK(program.symbol_index == symbols_before);
    CHECK(program.label_index == labels_before);
    CHECK(program.file_count == file_count_before);
    CHECK(program.file_capacity == file_capacity_before);
    CHECK(program.symbol_index_count == symbol_count_before);
    CHECK(program.symbol_index_capacity == symbol_capacity_before);
    CHECK(program.label_index_count == label_count_before);
    CHECK(program.label_index_capacity == label_capacity_before);
    CHECK(porpoise_program_find_function(&program, "primary") != NULL);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_program_free(&program);
}

static void test_atomic_skip_list(const char *source_root) {
    char input_path[PORPOISE_PATH_CAPACITY];
    char invalid_path[PORPOISE_PATH_CAPACITY];
    char valid_path[PORPOISE_PATH_CAPACITY];
    PorpoiseProgram program;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseFunction *primary;
    const PorpoiseFunction *secondary;
    int result;

    CHECK(make_fixture_path(input_path, sizeof(input_path), source_root,
                            "symbol_aliases_valid.s"));
    CHECK(make_fixture_path(invalid_path, sizeof(invalid_path), source_root,
                            "skip_atomic_invalid.txt"));
    CHECK(make_fixture_path(valid_path, sizeof(valid_path), source_root,
                            "skip_valid.txt"));
    porpoise_program_init(&program);
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_program_load(&program, input_path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    primary = porpoise_program_find_function(&program, "primary");
    secondary = porpoise_program_find_function(&program, "secondary");
    CHECK(primary != NULL);
    CHECK(secondary != NULL);

    result = porpoise_program_apply_skip_list(
        &program, invalid_path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_USAGE);
    CHECK(diagnostics_contain(&diagnostics,
                              "missing_function is not present"));
    CHECK(primary != NULL && !primary->skipped);
    CHECK(secondary != NULL && !secondary->skipped);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_program_apply_skip_list(
        &program, valid_path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(primary != NULL && primary->skipped);
    CHECK(secondary != NULL && !secondary->skipped);
    CHECK(porpoise_program_find_function(&program, "primary") == NULL);
    result = porpoise_program_apply_skip_list(
        &program, valid_path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_program_free(&program);
}

static void test_invalid_program(
    const char *source_root,
    const char *fixture,
    const char *expected_diagnostic) {
    char path[PORPOISE_PATH_CAPACITY];
    PorpoiseProgram program;
    PorpoiseDiagnostics diagnostics;
    int result;

    CHECK(make_fixture_path(path, sizeof(path), source_root, fixture));
    porpoise_program_init(&program);
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_program_load(&program, path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_TRANSLATION);
    CHECK(porpoise_diagnostics_have_errors(&diagnostics));
    CHECK(diagnostics_contain(&diagnostics, expected_diagnostic));
    porpoise_diagnostics_free(&diagnostics);
    porpoise_program_free(&program);
}

static void test_unique_label_resolution(const char *source_root) {
    char path[PORPOISE_PATH_CAPACITY];
    PorpoiseProgram program;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseFunction *owner = NULL;
    uint32_t address = 0U;
    size_t instruction_item_index = SIZE_MAX;
    int result;

    CHECK(make_fixture_path(path, sizeof(path), source_root,
                            "labels_across_functions.s"));
    porpoise_program_init(&program);
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_program_load(&program, path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    CHECK(program.file_count == 1U);
    CHECK(program.files[0].function_count == 3U);

    CHECK(porpoise_program_resolve_unique_label(
        &program, "unique_target", &owner, &address, &instruction_item_index));
    CHECK(owner == &program.files[0].functions[0]);
    CHECK(address == UINT32_C(0x80001000));
    CHECK(instruction_item_index == 2U);
    CHECK(porpoise_program_resolve_unique_label(
        &program, "also_unique", &owner, &address, &instruction_item_index));
    CHECK(owner == &program.files[0].functions[0]);
    CHECK(address == UINT32_C(0x80001000));
    CHECK(instruction_item_index == 2U);

    CHECK(!porpoise_program_resolve_unique_label(
        &program, ".Lshared", &owner, &address, &instruction_item_index));
    CHECK(owner == NULL);
    CHECK(address == 0U);
    CHECK(instruction_item_index == SIZE_MAX);

    program.files[0].functions[1].skipped = true;
    CHECK(porpoise_program_resolve_unique_label(
        &program, ".Lshared", &owner, &address, &instruction_item_index));
    CHECK(owner == &program.files[0].functions[2]);
    CHECK(address == UINT32_C(0x80003000));
    CHECK(instruction_item_index == 1U);

    CHECK(!porpoise_program_resolve_unique_label(
        &program, "dangling_target", &owner, &address, &instruction_item_index));
    CHECK(owner == NULL);
    CHECK(address == 0U);
    CHECK(instruction_item_index == SIZE_MAX);

    program.files[0].functions[0].skipped = true;
    CHECK(!porpoise_program_resolve_unique_label(
        &program, "unique_target", &owner, &address, &instruction_item_index));
    CHECK(owner == NULL);
    CHECK(address == 0U);
    CHECK(instruction_item_index == SIZE_MAX);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_program_free(&program);
}

static void test_weak_function_scope(const char *source_root) {
    char path[PORPOISE_PATH_CAPACITY];
    PorpoiseProgram program;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseFunction *function;
    int result;

    CHECK(make_fixture_path(path, sizeof(path), source_root,
                            "weak_function.s"));
    porpoise_program_init(&program);
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_program_load(&program, path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    CHECK(program.file_count == 1U);
    CHECK(program.files[0].function_count == 1U);
    function = &program.files[0].functions[0];
    CHECK(strcmp(function->name, "weak_helper") == 0);
    CHECK(function->is_global);
    porpoise_diagnostics_free(&diagnostics);
    porpoise_program_free(&program);
}

static void test_multiline_comments(const char *source_root) {
    char path[PORPOISE_PATH_CAPACITY];
    PorpoiseProgram program;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseFunction *function;
    int result;

    CHECK(make_fixture_path(path, sizeof(path), source_root,
                            "multiline_comments.s"));
    porpoise_program_init(&program);
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_program_load(&program, path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    CHECK(program.file_count == 1U);
    CHECK(program.files[0].function_count == 1U);
    CHECK(program.files[0].data_word_count == 0U);
    function = &program.files[0].functions[0];
    CHECK(strcmp(function->name, "commented_function") == 0);
    CHECK(function->instruction_count == 2U);
    CHECK(function->start_address == UINT32_C(0x8000A000));
    porpoise_diagnostics_free(&diagnostics);
    porpoise_program_free(&program);
}

static void test_exact_duplicate_functions(const char *source_root) {
    char path[PORPOISE_PATH_CAPACITY];
    char skip_path[PORPOISE_PATH_CAPACITY];
    PorpoiseProgram program;
    PorpoiseDiagnostics diagnostics;
    PorpoiseAnalysis analysis;
    PorpoiseAbiManifest empty_abi = {0};
    PorpoiseAbiFunction export_function = {0};
    PorpoiseAbiManifest export_abi = {0};
    const PorpoiseFunction *function;
    const PorpoiseFunction *owner = NULL;
    const PorpoiseAddressAlias *alias = NULL;
    uint32_t address = 0U;
    int result;

    CHECK(make_fixture_path(path, sizeof(path), source_root,
                            "function_exact_duplicates"));
    CHECK(make_fixture_path(
        skip_path, sizeof(skip_path), source_root,
        "function_exact_duplicates/skip_alternate.txt"));
    porpoise_program_init(&program);
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_program_load(&program, path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    CHECK(program.file_count == 4U);
    CHECK(program.files[0].function_count == 1U);
    CHECK(program.files[1].function_count == 0U);
    CHECK(program.files[2].function_count == 0U);
    CHECK(program.files[3].function_count == 0U);
    CHECK(program.symbol_index_count == 5U);
    CHECK(program.label_index_count == 1U);

    function = porpoise_program_find_function(&program, "canonical_body");
    CHECK(function == &program.files[0].functions[0]);
    CHECK(function != NULL && function->is_global);
    CHECK(function != NULL && function->alias_count == 4U);
    CHECK(function != NULL && function->item_count == 6U);
    CHECK(function != NULL &&
          strcmp(function->items[1].operands, "r3, object_a@ha") == 0);
    CHECK(porpoise_program_count_named_function(
              &program, "canonical_body") == 1U);
    CHECK(porpoise_program_count_named_function(
              &program, "alternate_entry") == 1U);
    CHECK(porpoise_program_count_named_function(&program, "main") == 1U);
    CHECK(porpoise_program_find_function(
              &program, "alternate_entry") == function);
    CHECK(porpoise_program_find_function(&program, "main") == function);

    alias = porpoise_program_find_alias(
        &program, "alternate_entry", &owner);
    CHECK(alias != NULL);
    CHECK(owner == function);
    CHECK(alias != NULL && alias->is_global);
    CHECK(alias != NULL && alias->is_function_name);
    CHECK(alias != NULL && path_has_basename(alias->source_path, "b.s"));
    CHECK(alias != NULL && alias->address == UINT32_C(0x80004000));
    CHECK(alias != NULL && alias->instruction_item_index == 1U);

    alias = porpoise_program_find_alias(&program, "shared_alias", &owner);
    CHECK(alias != NULL && alias->is_global);
    CHECK(alias != NULL && !alias->is_function_name);
    CHECK(owner == function);
    alias = porpoise_program_find_alias(&program, "secondary_alias", &owner);
    CHECK(alias != NULL && alias->is_global);
    CHECK(owner == function);

    CHECK(porpoise_program_resolve_symbol(
        &program, "alternate_entry", &owner, &alias, &address));
    CHECK(owner == function);
    CHECK(alias != NULL && strcmp(alias->name, "alternate_entry") == 0);
    CHECK(address == UINT32_C(0x80004000));

    result = porpoise_analyze_program(
        &program, &empty_abi, "alternate_entry", &analysis, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(analysis.entry == function);
    result = porpoise_analyze_program(
        &program, &empty_abi, NULL, &analysis, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(analysis.entry == function);

    export_function.kind = PORPOISE_ABI_EXPORT;
    export_function.symbol = (char *)"alternate_entry";
    export_function.wrapper = (char *)"call_alternate_entry";
    export_abi.functions = &export_function;
    export_abi.function_count = 1U;
    result = porpoise_analyze_program(
        &program, &export_abi, NULL, &analysis, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(analysis.entry == function);

    result = porpoise_program_apply_skip_list(
        &program, skip_path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(function != NULL && function->skipped);
    CHECK(porpoise_program_find_function(
              &program, "alternate_entry") == NULL);
    CHECK(porpoise_program_find_function(&program, "canonical_body") == NULL);

    porpoise_diagnostics_free(&diagnostics);
    porpoise_program_free(&program);
}

static void test_coalesced_alias_diagnostic_provenance(
    const char *source_root) {
    char path[PORPOISE_PATH_CAPACITY];
    PorpoiseProgram program;
    PorpoiseDiagnostics diagnostics;
    size_t index;
    bool found = false;
    int result;

    CHECK(make_fixture_path(
        path, sizeof(path), source_root,
        "validation_coalesced_alias_provenance"));
    porpoise_program_init(&program);
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_program_load(&program, path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_TRANSLATION);
    for (index = 0U; index < diagnostics.count; index++) {
        const PorpoiseDiagnostic *diagnostic = &diagnostics.items[index];
        if (strstr(diagnostic->message,
                   "conflicts with function symbol") == NULL) {
            continue;
        }
        found = true;
        CHECK(path_has_basename(diagnostic->file, "b.s"));
        CHECK(diagnostic->line == 2U);
    }
    CHECK(found);
    porpoise_diagnostics_free(&diagnostics);
    porpoise_program_free(&program);
}

static void test_gap_duplicate_precedence(const char *source_root) {
    char path[PORPOISE_PATH_CAPACITY];
    PorpoiseProgram program;
    PorpoiseDiagnostics diagnostics;
    const PorpoiseFunction *after;
    const PorpoiseFunction *before;
    size_t file_index;
    size_t function_count = 0U;
    size_t data_word_count = 0U;
    int result;

    CHECK(make_fixture_path(
        path, sizeof(path), source_root, "gap_duplicate_precedence"));
    porpoise_program_init(&program);
    porpoise_diagnostics_init(&diagnostics);
    result = porpoise_program_load(&program, path, &diagnostics);
    CHECK(result == PORPOISE_EXIT_OK);
    CHECK(!porpoise_diagnostics_have_errors(&diagnostics));
    for (file_index = 0U; file_index < program.file_count; file_index++) {
        function_count += program.files[file_index].function_count;
        data_word_count += program.files[file_index].data_word_count;
    }
    CHECK(function_count == 2U);
    CHECK(data_word_count == 0U);
    after = porpoise_program_find_function(&program, "real_after_gap");
    before = porpoise_program_find_function(&program, "real_before_gap");
    CHECK(after != NULL && strcmp(after->name, "real_after_gap") == 0);
    CHECK(before != NULL && strcmp(before->name, "real_before_gap") == 0);
    CHECK(after != NULL && !after->data_region && !after->skipped);
    CHECK(before != NULL && !before->data_region && !before->skipped);
    CHECK(porpoise_program_find_function(
              &program, "gap_01_8000B000_text") == after);
    CHECK(porpoise_program_find_function(
              &program, "gap_01_8000B100_text") == before);
    porpoise_diagnostics_free(&diagnostics);
    porpoise_program_free(&program);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s SOURCE_ROOT\n", argv[0]);
        return 2;
    }
    test_valid_aliases(argv[1]);
    test_unique_label_resolution(argv[1]);
    test_weak_function_scope(argv[1]);
    test_multiline_comments(argv[1]);
    test_exact_duplicate_functions(argv[1]);
    test_coalesced_alias_diagnostic_provenance(argv[1]);
    test_gap_duplicate_precedence(argv[1]);
    test_double_load_rejected(argv[1]);
    test_atomic_skip_list(argv[1]);
    test_invalid_program(argv[1], "symbol_aliases_malformed.s",
                       "malformed .sym directive");
    test_invalid_program(argv[1], "symbol_aliases_duplicate.s",
                       "duplicate or colliding symbol alias");
    test_invalid_program(argv[1], "symbol_aliases_unresolved_inside.s",
                       "before .endfn");
    test_invalid_program(argv[1], "symbol_aliases_unresolved_outside.s",
                       "not followed by an annotated instruction");
    test_invalid_program(argv[1], "symbol_aliases_function_conflict.s",
                       "conflicts with function symbol");
    test_invalid_program(argv[1], "symbol_aliases_label_conflict.s",
                       "conflicts with label");
    test_invalid_program(argv[1],
                       "symbol_aliases_cross_function_label_conflict.s",
                       "conflicts with label");
    test_invalid_program(argv[1], "validation_data_overlap",
                         "annotated data overlaps");
    test_invalid_program(argv[1], "validation_function_overlap",
                          "function address range overlaps");
    test_invalid_program(argv[1], "validation_function_exact_word_mismatch",
                          "function address range overlaps");
    test_invalid_program(argv[1], "validation_function_exact_mnemonic_mismatch",
                          "function address range overlaps");
    test_invalid_program(argv[1], "validation_function_exact_operand_mismatch",
                          "function address range overlaps");
    test_invalid_program(argv[1], "validation_function_exact_label_mismatch",
                          "function address range overlaps");
    test_invalid_program(argv[1],
                           "validation_function_relocation_suffix_mismatch",
                           "function address range overlaps");
    test_invalid_program(argv[1],
                          "validation_function_branch_suffix_mismatch",
                          "function address range overlaps");
    test_invalid_program(argv[1],
                          "validation_function_instruction_address_mismatch",
                          "function address range overlaps");
    test_invalid_program(argv[1],
                          "validation_function_item_count_mismatch",
                          "function address range overlaps");
    test_invalid_program(argv[1], "validation_filename_collision",
                         "filenames collide");
    test_invalid_program(argv[1], "unterminated_block_comment.s",
                         "unterminated block comment");
    if (failures != 0U) {
        fprintf(stderr, "%u parser test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
