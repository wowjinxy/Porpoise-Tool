#include "porpoise/signature.h"

#include "porpoise/relocation.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef enum SignatureTargetKind {
    SIGNATURE_TARGET_NONE = 0,
    SIGNATURE_TARGET_INTERNAL_BRANCH,
    SIGNATURE_TARGET_EXTERNAL_BRANCH,
    SIGNATURE_TARGET_INTERNAL_REFERENCE,
    SIGNATURE_TARGET_EXTERNAL_REFERENCE
} SignatureTargetKind;

typedef struct SignatureInstruction {
    uint32_t offset;
    uint32_t significant_mask;
    uint32_t normalized_word;
    PorpoiseRelocationKind relocation_kind;
    SignatureTargetKind target_kind;
    uint32_t target_ordinal;
    uint32_t target_offset;
    int64_t addend;
} SignatureInstruction;

typedef struct SignatureTargetKey {
    SignatureTargetKind kind;
    bool by_address;
    uint32_t address;
    const char *text;
    size_t text_length;
} SignatureTargetKey;

typedef struct ParsedInstructionRelocation {
    bool found;
    bool malformed;
    bool multiple;
    PorpoiseRelocation relocation;
    const char *base;
    size_t base_length;
    int64_t addend;
} ParsedInstructionRelocation;

typedef struct SymbolResolution {
    const PorpoiseFunction *owner;
    uint32_t address;
} SymbolResolution;

static bool span_equal_string(
    const char *span,
    size_t length,
    const char *string) {
    return span != NULL && string != NULL && strlen(string) == length &&
           memcmp(span, string, length) == 0;
}

static void trim_span(const char **text, size_t *length) {
    while (*length != 0U && isspace((unsigned char)(*text)[0])) {
        (*text)++;
        (*length)--;
    }
    while (*length != 0U &&
           isspace((unsigned char)(*text)[*length - 1U])) {
        (*length)--;
    }
}

static bool parse_unsigned_span(
    const char *text,
    size_t length,
    uint64_t *value_out) {
    uint64_t value = UINT64_C(0);
    unsigned int base = 10U;
    size_t index = 0U;
    bool have_digit = false;

    if (text == NULL || value_out == NULL || length == 0U) return false;
    if (length > 2U && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X')) {
        base = 16U;
        index = 2U;
    }
    for (; index < length; index++) {
        unsigned int digit;
        unsigned char character = (unsigned char)text[index];
        if (character >= (unsigned char)'0' &&
            character <= (unsigned char)'9') {
            digit = (unsigned int)(character - (unsigned char)'0');
        } else if (base == 16U && character >= (unsigned char)'a' &&
                   character <= (unsigned char)'f') {
            digit = 10U + (unsigned int)(character - (unsigned char)'a');
        } else if (base == 16U && character >= (unsigned char)'A' &&
                   character <= (unsigned char)'F') {
            digit = 10U + (unsigned int)(character - (unsigned char)'A');
        } else {
            return false;
        }
        if (digit >= base ||
            value > (UINT64_MAX - (uint64_t)digit) / (uint64_t)base) {
            return false;
        }
        value = value * (uint64_t)base + (uint64_t)digit;
        have_digit = true;
    }
    if (!have_digit) return false;
    *value_out = value;
    return true;
}

static bool parse_signed_addend(
    const char *text,
    size_t length,
    size_t *base_length_out,
    int64_t *addend_out) {
    size_t index;

    if (text == NULL || base_length_out == NULL || addend_out == NULL)
        return false;
    for (index = length; index > 1U; index--) {
        size_t separator = index - 1U;
        uint64_t magnitude;
        if (text[separator] != '+' && text[separator] != '-') continue;
        if (!parse_unsigned_span(text + separator + 1U,
                                 length - separator - 1U,
                                 &magnitude)) {
            continue;
        }
        if (text[separator] == '+') {
            if (magnitude > (uint64_t)INT64_MAX) return false;
            *addend_out = (int64_t)magnitude;
        } else {
            if (magnitude > (uint64_t)INT64_MAX + UINT64_C(1)) return false;
            *addend_out = magnitude == (uint64_t)INT64_MAX + UINT64_C(1)
                ? INT64_MIN
                : -(int64_t)magnitude;
        }
        *base_length_out = separator;
        return true;
    }
    *base_length_out = length;
    *addend_out = INT64_C(0);
    return true;
}

static bool instruction_address_in_function(
    const PorpoiseFunction *function,
    uint32_t address,
    uint32_t *offset_out) {
    size_t index;
    if (function == NULL) return false;
    for (index = 0U; index < function->item_count; index++) {
        const PorpoiseAsmItem *item = &function->items[index];
        if (item->kind == PORPOISE_ASM_INSTRUCTION &&
            item->address == address) {
            if (offset_out != NULL)
                *offset_out = address - function->start_address;
            return true;
        }
    }
    return false;
}

static bool function_label_address(
    const PorpoiseFunction *function,
    const char *name,
    size_t name_length,
    uint32_t *address_out) {
    size_t index;

    if (function == NULL || name == NULL || address_out == NULL) return false;
    if (span_equal_string(name, name_length, function->name)) {
        *address_out = function->start_address;
        return true;
    }
    for (index = 0U; index < function->alias_count; index++) {
        if (span_equal_string(name, name_length,
                              function->aliases[index].name)) {
            *address_out = function->aliases[index].address;
            return true;
        }
    }
    for (index = 0U; index < function->item_count; index++) {
        const PorpoiseAsmItem *item = &function->items[index];
        size_t following;
        if (item->kind != PORPOISE_ASM_LABEL ||
            !span_equal_string(name, name_length, item->label)) {
            continue;
        }
        for (following = index + 1U;
             following < function->item_count;
             following++) {
            if (function->items[following].kind ==
                PORPOISE_ASM_INSTRUCTION) {
                *address_out = function->items[following].address;
                return true;
            }
        }
        return false;
    }
    return false;
}

static bool resolve_symbol_span(
    const PorpoiseProgram *program,
    const PorpoiseFunction *scope,
    const char *name,
    size_t name_length,
    SymbolResolution *resolution_out) {
    uint32_t address;
    size_t file_index;

    if (resolution_out == NULL) return false;
    memset(resolution_out, 0, sizeof(*resolution_out));
    if (function_label_address(scope, name, name_length, &address)) {
        resolution_out->owner = scope;
        resolution_out->address = address;
        return true;
    }
    if (program == NULL) return false;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U;
             function_index < file->function_count;
             function_index++) {
            const PorpoiseFunction *candidate =
                &file->functions[function_index];
            if (candidate == scope) continue;
            if (function_label_address(candidate, name, name_length,
                                       &address)) {
                resolution_out->owner = candidate;
                resolution_out->address = address;
                return true;
            }
        }
    }
    return false;
}

static bool operand_delimiter(char character) {
    return isspace((unsigned char)character) || character == ',' ||
           character == '(' || character == ')';
}

static ParsedInstructionRelocation parse_instruction_relocation(
    const PorpoiseAsmItem *item) {
    ParsedInstructionRelocation parsed;
    const char *operands;
    size_t length;
    size_t index = 0U;
    size_t operand_index = 0U;

    memset(&parsed, 0, sizeof(parsed));
    if (item == NULL || item->operands == NULL) return parsed;
    operands = item->operands;
    length = strlen(operands);

    while (index < length) {
        size_t start;
        size_t end;
        PorpoiseRelocation relocation;
        bool memory_offset;
        unsigned int allowed;

        if (operands[index] == ',') {
            operand_index++;
            index++;
            continue;
        }
        if (operand_delimiter(operands[index])) {
            index++;
            continue;
        }
        start = index;
        while (index < length && !operand_delimiter(operands[index]))
            index++;
        end = index;
        if (memchr(operands + start, '@', end - start) == NULL) continue;
        if (!porpoise_relocation_parse_span(
                operands + start, end - start, &relocation)) {
            parsed.malformed = true;
            continue;
        }
        memory_offset = end < length && operands[end] == '(';
        allowed = porpoise_relocation_allowed_mask(
            item->mnemonic, operand_index, memory_offset);
        if ((allowed & PORPOISE_RELOCATION_MASK(relocation.kind)) == 0U) {
            parsed.malformed = true;
            continue;
        }
        if (parsed.found) {
            parsed.multiple = true;
            continue;
        }
        parsed.found = true;
        parsed.relocation = relocation;
        parsed.base = relocation.expression;
        if (!parse_signed_addend(
                relocation.expression, relocation.expression_length,
                &parsed.base_length, &parsed.addend) ||
            parsed.base_length == 0U) {
            parsed.malformed = true;
        }
    }
    return parsed;
}

static bool ppc_branch_form(
    uint32_t word,
    uint32_t *variable_mask_out,
    int32_t *displacement_out,
    bool *absolute_out) {
    uint32_t opcode = word >> 26U;
    uint32_t field;

    if (opcode == 18U) {
        field = word & UINT32_C(0x03fffffc);
        *displacement_out = (int32_t)(field << 6U) >> 6U;
        *variable_mask_out = UINT32_C(0x03fffffc);
        *absolute_out = (word & UINT32_C(2)) != 0U;
        return true;
    }
    if (opcode == 16U) {
        field = word & UINT32_C(0x0000fffc);
        *displacement_out = (int32_t)(field << 16U) >> 16U;
        *variable_mask_out = UINT32_C(0x0000fffc);
        *absolute_out = (word & UINT32_C(2)) != 0U;
        return true;
    }
    return false;
}

static void branch_target_operand(
    const char *operands,
    const char **target_out,
    size_t *target_length_out) {
    const char *target;
    size_t length;
    size_t index;
    bool quoted = false;
    char quote = '\0';

    *target_out = NULL;
    *target_length_out = 0U;
    if (operands == NULL) return;
    target = operands;
    length = strlen(operands);
    for (index = 0U; index < length; index++) {
        char character = operands[index];
        if (quoted) {
            if (character == '\\' && index + 1U < length) {
                index++;
            } else if (character == quote) {
                quoted = false;
            }
        } else if (character == '\'' || character == '"') {
            quoted = true;
            quote = character;
        } else if (character == ',') {
            target = operands + index + 1U;
        }
    }
    length = strlen(target);
    trim_span(&target, &length);
    *target_out = target;
    *target_length_out = length;
}

static bool target_keys_equal(
    const SignatureTargetKey *left,
    const SignatureTargetKey *right) {
    if (left->kind != right->kind || left->by_address != right->by_address)
        return false;
    if (left->by_address) return left->address == right->address;
    return left->text_length == right->text_length &&
           memcmp(left->text, right->text, left->text_length) == 0;
}

static uint32_t target_ordinal(
    SignatureTargetKey *targets,
    size_t *target_count,
    size_t target_capacity,
    const SignatureTargetKey *key) {
    size_t index;
    for (index = 0U; index < *target_count; index++) {
        if (target_keys_equal(&targets[index], key))
            return (uint32_t)index + 1U;
    }
    if (*target_count >= target_capacity || *target_count >= UINT32_MAX)
        return 0U;
    targets[*target_count] = *key;
    (*target_count)++;
    return (uint32_t)(*target_count);
}

static bool boilerplate_word(uint32_t word) {
    uint32_t opcode = word >> 26U;
    uint32_t rt = (word >> 21U) & UINT32_C(31);
    uint32_t ra = (word >> 16U) & UINT32_C(31);

    if (word == UINT32_C(0x60000000) ||
        word == UINT32_C(0x4e800020) ||
        word == UINT32_C(0x4e800420) ||
        word == UINT32_C(0x4e800421) ||
        word == UINT32_C(0x7c0802a6) ||
        word == UINT32_C(0x7c0803a6)) {
        return true;
    }
    if (opcode == 37U && rt == 1U && ra == 1U) return true;
    if (opcode == 14U && rt == 1U && ra == 1U) return true;
    if (opcode == 36U && rt == 0U && ra == 1U) return true;
    if (opcode == 32U && rt == 0U && ra == 1U) return true;
    return false;
}

static void hash_u32(PorpoiseSha256Context *hash, uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
    porpoise_sha256_update(hash, bytes, sizeof(bytes));
}

static void hash_u64(PorpoiseSha256Context *hash, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0U; index < 8U; index++)
        bytes[7U - index] = (uint8_t)(value >> (index * 8U));
    porpoise_sha256_update(hash, bytes, sizeof(bytes));
}

static void hash_instructions(
    const SignatureInstruction *instructions,
    size_t instruction_count,
    PorpoiseFunctionSignature *signature) {
    static const uint8_t magic[8] = {
        'P', 'P', 'S', 'I', 'G', 0U, 0U, 1U
    };
    PorpoiseSha256Context hash;
    size_t index;

    porpoise_sha256_init(&hash);
    porpoise_sha256_update(&hash, magic, sizeof(magic));
    hash_u32(&hash, signature->algorithm_version);
    hash_u32(&hash, signature->function_size);
    hash_u32(&hash, signature->instruction_count);
    hash_u32(&hash, signature->issue_flags);
    for (index = 0U; index < instruction_count; index++) {
        const SignatureInstruction *instruction = &instructions[index];
        uint8_t kinds[4];
        hash_u32(&hash, instruction->offset);
        hash_u32(&hash, instruction->significant_mask);
        hash_u32(&hash, instruction->normalized_word);
        kinds[0] = (uint8_t)instruction->relocation_kind;
        kinds[1] = (uint8_t)instruction->target_kind;
        kinds[2] = 0U;
        kinds[3] = 0U;
        porpoise_sha256_update(&hash, kinds, sizeof(kinds));
        hash_u32(&hash, instruction->target_ordinal);
        hash_u32(&hash, instruction->target_offset);
        hash_u64(&hash, (uint64_t)instruction->addend);
    }
    porpoise_sha256_final(&hash, signature->digest);
    porpoise_sha256_hex(signature->digest, signature->digest_hex);
}

bool porpoise_signature_compute(
    const PorpoiseProgram *program,
    const PorpoiseFunction *function,
    PorpoiseFunctionSignature *signature_out) {
    SignatureInstruction *instructions = NULL;
    SignatureTargetKey *targets = NULL;
    size_t target_count = 0U;
    size_t actual_instruction_count = 0U;
    size_t record_index = 0U;
    size_t item_index;
    uint32_t previous_address = 0U;
    bool have_previous_address = false;

    if (function == NULL || signature_out == NULL) return false;
    memset(signature_out, 0, sizeof(*signature_out));
    signature_out->algorithm_version =
        PORPOISE_SIGNATURE_ALGORITHM_VERSION;
    signature_out->function_size = function->size;
    if (function->data_region)
        signature_out->issue_flags |= PORPOISE_SIGNATURE_ISSUE_DATA_REGION;

    for (item_index = 0U; item_index < function->item_count; item_index++) {
        if (function->items[item_index].kind == PORPOISE_ASM_INSTRUCTION)
            actual_instruction_count++;
    }
    if (actual_instruction_count > UINT32_MAX) return false;
    signature_out->instruction_count = (uint32_t)actual_instruction_count;
    if (actual_instruction_count == 0U)
        signature_out->issue_flags |= PORPOISE_SIGNATURE_ISSUE_EMPTY;
    if (actual_instruction_count != function->instruction_count)
        signature_out->issue_flags |=
            PORPOISE_SIGNATURE_ISSUE_COUNT_MISMATCH;
    if (function->size == 0U || (function->size & UINT32_C(3)) != 0U)
        signature_out->issue_flags |= PORPOISE_SIGNATURE_ISSUE_INVALID_SIZE;

    if (actual_instruction_count != 0U) {
        if (actual_instruction_count > SIZE_MAX / sizeof(*instructions) ||
            actual_instruction_count > SIZE_MAX / sizeof(*targets)) {
            return false;
        }
        instructions = (SignatureInstruction *)calloc(
            actual_instruction_count, sizeof(*instructions));
        targets = (SignatureTargetKey *)calloc(
            actual_instruction_count, sizeof(*targets));
        if (instructions == NULL || targets == NULL) {
            free(instructions);
            free(targets);
            return false;
        }
    }

    for (item_index = 0U; item_index < function->item_count; item_index++) {
        const PorpoiseAsmItem *item = &function->items[item_index];
        SignatureInstruction *record;
        ParsedInstructionRelocation parsed_relocation;
        uint32_t branch_variable_mask = UINT32_C(0);
        int32_t branch_displacement = 0;
        bool branch_absolute = false;

        if (item->kind != PORPOISE_ASM_INSTRUCTION) continue;
        record = &instructions[record_index++];
        record->significant_mask = UINT32_MAX;
        record->target_offset = UINT32_MAX;

        if (item->address < function->start_address ||
            (item->address & UINT32_C(3)) != 0U ||
            (have_previous_address && item->address <= previous_address)) {
            signature_out->issue_flags |=
                PORPOISE_SIGNATURE_ISSUE_INVALID_LAYOUT;
        }
        record->offset = item->address - function->start_address;
        if (record->offset > function->size ||
            function->size - record->offset < 4U) {
            signature_out->issue_flags |=
                PORPOISE_SIGNATURE_ISSUE_INVALID_LAYOUT;
        }
        if (!have_previous_address && record->offset != 0U)
            signature_out->issue_flags |=
                PORPOISE_SIGNATURE_ISSUE_INVALID_LAYOUT;
        previous_address = item->address;
        have_previous_address = true;

        parsed_relocation = parse_instruction_relocation(item);
        if (parsed_relocation.malformed)
            signature_out->issue_flags |=
                PORPOISE_SIGNATURE_ISSUE_MALFORMED_RELOCATION;
        if (parsed_relocation.multiple)
            signature_out->issue_flags |=
                PORPOISE_SIGNATURE_ISSUE_MULTIPLE_RELOCATIONS;
        if (parsed_relocation.found) {
            SymbolResolution resolution;
            SignatureTargetKey key;
            uint32_t internal_offset = 0U;
            uint32_t variable_mask =
                porpoise_relocation_variable_word_mask(
                    parsed_relocation.relocation.kind);

            signature_out->relocation_count++;
            record->relocation_kind = parsed_relocation.relocation.kind;
            record->addend = parsed_relocation.addend;
            record->significant_mask &= ~variable_mask;
            memset(&key, 0, sizeof(key));
            if (resolve_symbol_span(
                    program, function,
                    parsed_relocation.base,
                    parsed_relocation.base_length,
                    &resolution) && resolution.owner == function &&
                instruction_address_in_function(
                    function, resolution.address, &internal_offset)) {
                record->target_kind = SIGNATURE_TARGET_INTERNAL_REFERENCE;
                record->target_offset = internal_offset;
            } else {
                record->target_kind = SIGNATURE_TARGET_EXTERNAL_REFERENCE;
                key.kind = record->target_kind;
                if (resolution.owner != NULL) {
                    key.by_address = true;
                    key.address = resolution.address;
                } else {
                    key.text = parsed_relocation.base;
                    key.text_length = parsed_relocation.base_length;
                }
                record->target_ordinal = target_ordinal(
                    targets, &target_count, actual_instruction_count, &key);
                if (record->target_ordinal == 0U) {
                    free(instructions);
                    free(targets);
                    return false;
                }
            }
        }

        if (ppc_branch_form(item->word, &branch_variable_mask,
                            &branch_displacement, &branch_absolute)) {
            const char *target_text;
            size_t target_length;
            size_t target_base_length;
            int64_t target_addend;
            SymbolResolution resolution;
            uint32_t decoded_target = branch_absolute
                ? (uint32_t)branch_displacement
                : item->address + (uint32_t)branch_displacement;
            uint32_t internal_offset = 0U;
            bool have_symbol_resolution = false;
            bool internal = false;
            SignatureTargetKey key;
            uint64_t numeric_target;

            branch_target_operand(
                item->operands, &target_text, &target_length);
            target_base_length = target_length;
            target_addend = INT64_C(0);
            if (target_length != 0U) {
                if (!parse_signed_addend(
                        target_text, target_length,
                        &target_base_length, &target_addend) ||
                    target_base_length == 0U) {
                    signature_out->issue_flags |=
                        PORPOISE_SIGNATURE_ISSUE_INVALID_INTERNAL_BRANCH;
                    target_base_length = target_length;
                    target_addend = INT64_C(0);
                }
                have_symbol_resolution = resolve_symbol_span(
                    program, function, target_text, target_base_length,
                    &resolution);
                if (have_symbol_resolution && resolution.owner == function) {
                    internal = instruction_address_in_function(
                        function, resolution.address, &internal_offset);
                    if (!internal || resolution.address != decoded_target)
                        signature_out->issue_flags |=
                            PORPOISE_SIGNATURE_ISSUE_INVALID_INTERNAL_BRANCH;
                } else if (parse_unsigned_span(
                               target_text, target_base_length,
                               &numeric_target) &&
                           numeric_target <= UINT32_MAX) {
                    internal = instruction_address_in_function(
                        function, (uint32_t)numeric_target,
                        &internal_offset);
                }
            } else {
                internal = instruction_address_in_function(
                    function, decoded_target, &internal_offset);
            }

            record->addend = target_addend;
            if (internal) {
                record->target_kind = SIGNATURE_TARGET_INTERNAL_BRANCH;
                record->target_offset = internal_offset;
                if (branch_absolute) {
                    record->significant_mask &= ~branch_variable_mask;
                }
                signature_out->internal_branch_count++;
            } else {
                record->target_kind = SIGNATURE_TARGET_EXTERNAL_BRANCH;
                record->significant_mask &= ~branch_variable_mask;
                signature_out->external_branch_count++;
                memset(&key, 0, sizeof(key));
                key.kind = record->target_kind;
                if (have_symbol_resolution) {
                    key.by_address = true;
                    key.address = resolution.address;
                } else if (target_length != 0U) {
                    key.text = target_text;
                    key.text_length = target_base_length;
                } else {
                    key.by_address = true;
                    key.address = decoded_target;
                }
                record->target_ordinal = target_ordinal(
                    targets, &target_count, actual_instruction_count, &key);
                if (record->target_ordinal == 0U) {
                    free(instructions);
                    free(targets);
                    return false;
                }
            }
        }

        record->normalized_word = item->word & record->significant_mask;
        if (record->significant_mask == UINT32_MAX) {
            signature_out->fixed_instruction_count++;
            if (!boilerplate_word(record->normalized_word))
                signature_out->meaningful_fixed_instruction_count++;
        }
    }

    if (record_index != actual_instruction_count) {
        free(instructions);
        free(targets);
        return false;
    }
    signature_out->external_target_count = (uint32_t)target_count;
    hash_instructions(instructions, actual_instruction_count, signature_out);
    free(instructions);
    free(targets);
    return true;
}

bool porpoise_signature_equal(
    const PorpoiseFunctionSignature *left,
    const PorpoiseFunctionSignature *right) {
    if (left == NULL || right == NULL) return false;
    return left->algorithm_version == right->algorithm_version &&
           left->function_size == right->function_size &&
           left->instruction_count == right->instruction_count &&
           left->fixed_instruction_count == right->fixed_instruction_count &&
           left->meaningful_fixed_instruction_count ==
               right->meaningful_fixed_instruction_count &&
           left->relocation_count == right->relocation_count &&
           left->internal_branch_count == right->internal_branch_count &&
           left->external_branch_count == right->external_branch_count &&
           left->external_target_count == right->external_target_count &&
           left->issue_flags == right->issue_flags &&
           memcmp(left->digest, right->digest,
                  PORPOISE_SHA256_DIGEST_SIZE) == 0;
}

bool porpoise_signature_is_automatic_match_eligible(
    const PorpoiseFunctionSignature *signature) {
    return signature != NULL &&
           signature->algorithm_version ==
               PORPOISE_SIGNATURE_ALGORITHM_VERSION &&
           signature->issue_flags == PORPOISE_SIGNATURE_ISSUE_NONE &&
           signature->instruction_count >=
               PORPOISE_SIGNATURE_MIN_INSTRUCTION_COUNT &&
           signature->meaningful_fixed_instruction_count >=
               PORPOISE_SIGNATURE_MIN_MEANINGFUL_FIXED_COUNT;
}
