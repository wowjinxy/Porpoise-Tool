#include "lower_internal.h"

#include <limits.h>
#include <stdint.h>

bool porpoise_lower_emit_integer(
    const PorpoiseLowerInstructionContext *context) {
    FILE *output = context->output;
    const OpcodeSpec *spec = context->spec;
    const PorpoiseAsmItem *item = context->item;
    OperandList operands = context->operands;
    unsigned int rd, ra, rb;
    int32_t immediate;
    uint32_t unsigned_value;
    bool record = context->record;

    switch (spec->operation) {
        case OP_NOP:
            if (operands.count != 0U) return false;
            return file_printf(output, "    /* %s: architectural no-op. */\n", item->mnemonic);
        case OP_HOST_NOOP:
            if (spec->detail == 0) {
                if (operands.count != 0U) return false;
            } else if (operands.count != 2U ||
                       !parse_register(operands.values[0], 'r', &ra) ||
                       !parse_register(operands.values[1], 'r', &rb)) {
                return false;
            }
            return file_printf(output, "    /* %s: host-equivalent no state change. */\n", item->mnemonic);
        case OP_LI:
        case OP_LIS: {
            PorpoiseRelocationKind relocation;
            unsigned int allowed_relocations = spec->operation == OP_LIS
                ? PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_HIGH) |
                      PORPOISE_RELOCATION_MASK(
                          PORPOISE_RELOCATION_HIGH_ADJUSTED)
                : PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_LOW) |
                      PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_SDA21);
            if (operands.count != 2U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_signed_or_relocated(operands.values[1], item->word,
                                           allowed_relocations, &immediate,
                                           &relocation) ||
                immediate < INT16_MIN || immediate > (int32_t)UINT16_MAX) return false;
            if (spec->operation == OP_LIS) {
                return file_printf(output,
                    "    state->gpr[%u] = ((uint32_t)UINT16_C(0x%04lX)) << 16U;\n",
                    rd, (unsigned long)(uint16_t)immediate);
            }
            if (relocation == PORPOISE_RELOCATION_SDA21) {
                ra = (item->word >> 16U) & 31U;
                if (ra != 0U) {
                    return file_printf(output,
                        "    state->gpr[%u] = state->gpr[%u] + porpoise_sign_extend16(UINT32_C(0x%04lX));\n",
                        rd, ra, (unsigned long)(uint16_t)immediate);
                }
            }
            return file_printf(output,
                "    state->gpr[%u] = porpoise_sign_extend16(UINT32_C(0x%04lX));\n",
                rd, (unsigned long)(uint16_t)immediate);
        }
        case OP_ADDI:
        case OP_ADDIS: {
            PorpoiseRelocationKind relocation;
            unsigned int allowed_relocations = spec->operation == OP_ADDIS
                ? PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_HIGH) |
                      PORPOISE_RELOCATION_MASK(
                          PORPOISE_RELOCATION_HIGH_ADJUSTED)
                : PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_LOW) |
                      PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_SDA21);
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_signed_or_relocated(operands.values[2], item->word,
                                           allowed_relocations, &immediate,
                                           &relocation) ||
                immediate < INT16_MIN || immediate > (int32_t)UINT16_MAX) return false;
            if (relocation == PORPOISE_RELOCATION_SDA21)
                ra = (item->word >> 16U) & 31U;
            if (ra == 0U) {
                if (spec->operation == OP_ADDIS)
                    return file_printf(output, "    state->gpr[%u] = ((uint32_t)UINT16_C(0x%04lX)) << 16U;\n",
                                       rd, (unsigned long)(uint16_t)immediate);
                return file_printf(output,
                    "    state->gpr[%u] = porpoise_sign_extend16(UINT32_C(0x%04lX));\n",
                    rd, (unsigned long)(uint16_t)immediate);
            }
            if (spec->operation == OP_ADDIS)
                return file_printf(output,
                    "    state->gpr[%u] = state->gpr[%u] + (((uint32_t)UINT16_C(0x%04lX)) << 16U);\n",
                    rd, ra, (unsigned long)(uint16_t)immediate);
            return file_printf(output,
                "    state->gpr[%u] = state->gpr[%u] + porpoise_sign_extend16(UINT32_C(0x%04lX));\n",
                rd, ra, (unsigned long)(uint16_t)immediate);
        }
        case OP_SUB_IMMEDIATE: {
            int64_t negated;
            uint16_t encoded;
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_signed(operands.values[2], &immediate)) return false;
            negated = -(int64_t)immediate;
            if (negated < INT16_MIN || negated > INT16_MAX) return false;
            encoded = (uint16_t)(int16_t)negated;
            if (ra == 0U) {
                if (spec->detail != 0)
                    return file_printf(output,
                        "    state->gpr[%u] = ((uint32_t)UINT16_C(0x%04lX)) << 16U;\n",
                        rd, (unsigned long)encoded);
                return file_printf(output,
                    "    state->gpr[%u] = porpoise_sign_extend16(UINT32_C(0x%04lX));\n",
                    rd, (unsigned long)encoded);
            }
            if (spec->detail != 0)
                return file_printf(output,
                    "    state->gpr[%u] = state->gpr[%u] + (((uint32_t)UINT16_C(0x%04lX)) << 16U);\n",
                    rd, ra, (unsigned long)encoded);
            return file_printf(output,
                "    state->gpr[%u] = state->gpr[%u] + porpoise_sign_extend16(UINT32_C(0x%04lX));\n",
                rd, ra, (unsigned long)encoded);
        }
        case OP_ADD:
        case OP_SUBF:
        case OP_AND:
        case OP_OR:
        case OP_XOR:
            if (spec->detail == 1) {
                if (operands.count != 2U || !parse_register(operands.values[0], 'r', &rd) ||
                    !parse_register(operands.values[1], 'r', &ra)) return false;
                rb = ra;
            } else if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                       !parse_register(operands.values[1], 'r', &ra) ||
                       !parse_register(operands.values[2], 'r', &rb)) return false;
            if (spec->operation == OP_ADD &&
                !file_printf(output, "    state->gpr[%u] = state->gpr[%u] + state->gpr[%u];\n", rd, ra, rb)) return false;
            if (spec->operation == OP_SUBF &&
                !file_printf(output, "    state->gpr[%u] = state->gpr[%u] - state->gpr[%u];\n", rd, rb, ra)) return false;
            if (spec->operation == OP_AND &&
                !file_printf(output, "    state->gpr[%u] = state->gpr[%u] & state->gpr[%u];\n", rd, ra, rb)) return false;
            if (spec->operation == OP_OR &&
                !file_printf(output, "    state->gpr[%u] = state->gpr[%u] | state->gpr[%u];\n", rd, ra, rb)) return false;
            if (spec->operation == OP_XOR &&
                !file_printf(output, "    state->gpr[%u] = state->gpr[%u] ^ state->gpr[%u];\n", rd, ra, rb)) return false;
            return emit_record_update(output, record, rd);
        case OP_MULLI:
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_signed(operands.values[2], &immediate) ||
                immediate < INT16_MIN || immediate > (int32_t)UINT16_MAX) return false;
            return file_printf(output,
                "    state->gpr[%u] = state->gpr[%u] * porpoise_sign_extend16(UINT32_C(0x%04lX));\n",
                rd, ra, (unsigned long)(uint16_t)immediate);
        case OP_MULLW:
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_register(operands.values[2], 'r', &rb) ||
                !file_printf(output,
                    "    state->gpr[%u] = state->gpr[%u] * state->gpr[%u];\n",
                    rd, ra, rb)) return false;
            return emit_record_update(output, record, rd);
        case OP_DIVIDE_WORD:
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_register(operands.values[2], 'r', &rb)) return false;
            if (spec->detail != 0) {
                if (!file_printf(output,
                    "    { uint32_t dividend = state->gpr[%u]; uint32_t divisor = state->gpr[%u]; "
                    "int64_t signed_dividend = (dividend & UINT32_C(0x80000000)) != 0U ? (int64_t)dividend - INT64_C(4294967296) : (int64_t)dividend; "
                    "int64_t signed_divisor = (divisor & UINT32_C(0x80000000)) != 0U ? (int64_t)divisor - INT64_C(4294967296) : (int64_t)divisor; "
                    "if (divisor == 0U || (dividend == UINT32_C(0x80000000) && divisor == UINT32_MAX)) "
                    "state->gpr[%u] = signed_dividend < 0 ? UINT32_MAX : 0U; "
                    "else state->gpr[%u] = (uint32_t)(signed_dividend / signed_divisor); }\n",
                    ra, rb, rd, rd)) return false;
            } else if (!file_printf(output,
                "    { uint32_t divisor = state->gpr[%u]; state->gpr[%u] = divisor == 0U ? 0U : state->gpr[%u] / divisor; }\n",
                rb, rd, ra)) return false;
            return emit_record_update(output, record, rd);
        case OP_MULTIPLY_HIGH:
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_register(operands.values[2], 'r', &rb)) return false;
            if (spec->detail != 0) {
                if (!file_printf(output,
                    "    { uint32_t left = state->gpr[%u]; uint32_t right = state->gpr[%u]; "
                    "uint32_t high = (uint32_t)(((uint64_t)left * (uint64_t)right) >> 32U); "
                    "if ((left & UINT32_C(0x80000000)) != 0U) high -= right; "
                    "if ((right & UINT32_C(0x80000000)) != 0U) high -= left; state->gpr[%u] = high; }\n",
                    ra, rb, rd)) return false;
            } else if (!file_printf(output,
                "    state->gpr[%u] = (uint32_t)(((uint64_t)state->gpr[%u] * (uint64_t)state->gpr[%u]) >> 32U);\n",
                rd, ra, rb)) return false;
            return emit_record_update(output, record, rd);
        case OP_CARRY_ARITHMETIC: {
            int kind = spec->detail < 0 ? -spec->detail : spec->detail;
            if (kind == CARRY_ADD_IMMEDIATE || kind == CARRY_SUBF_IMMEDIATE) {
                uint16_t encoded;
                PorpoiseRelocationKind relocation =
                    PORPOISE_RELOCATION_NONE;
                if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                    !parse_register(operands.values[1], 'r', &ra)) return false;
                if (spec->detail < 0) {
                    int64_t negated;
                    if (!parse_signed(operands.values[2], &immediate)) return false;
                    negated = -(int64_t)immediate;
                    if (negated < INT16_MIN || negated > INT16_MAX) return false;
                    encoded = (uint16_t)(int16_t)negated;
                } else {
                    if (!parse_signed_or_relocated(
                            operands.values[2], item->word,
                            PORPOISE_RELOCATION_MASK(
                                PORPOISE_RELOCATION_LOW),
                            &immediate,
                            &relocation)) return false;
                    if (immediate < INT16_MIN || immediate > (int32_t)UINT16_MAX) return false;
                    encoded = (uint16_t)immediate;
                }
                if (kind == CARRY_ADD_IMMEDIATE) {
                    if (!file_printf(output,
                        "    state->gpr[%u] = porpoise_add_with_carry32(state, state->gpr[%u], porpoise_sign_extend16(UINT32_C(0x%04lX)), 0U);\n",
                        rd, ra, (unsigned long)encoded)) return false;
                } else if (!file_printf(output,
                    "    state->gpr[%u] = porpoise_add_with_carry32(state, ~state->gpr[%u], porpoise_sign_extend16(UINT32_C(0x%04lX)), 1U);\n",
                    rd, ra, (unsigned long)encoded)) return false;
            } else if (kind == CARRY_ADD_ZERO_EXTENDED) {
                if (operands.count != 2U || !parse_register(operands.values[0], 'r', &rd) ||
                    !parse_register(operands.values[1], 'r', &ra) ||
                    !file_printf(output,
                        "    state->gpr[%u] = porpoise_add_with_carry32(state, state->gpr[%u], 0U, (state->xer >> 29U) & 1U);\n",
                        rd, ra)) return false;
            } else {
                const char *left_prefix = kind == CARRY_SUBF || kind == CARRY_SUBF_EXTENDED ? "~" : "";
                const char *carry = kind == CARRY_ADD_EXTENDED || kind == CARRY_SUBF_EXTENDED
                    ? "(state->xer >> 29U) & 1U"
                    : kind == CARRY_SUBF ? "1U" : "0U";
                if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                    !parse_register(operands.values[1], 'r', &ra) ||
                    !parse_register(operands.values[2], 'r', &rb) ||
                    !file_printf(output,
                        "    state->gpr[%u] = porpoise_add_with_carry32(state, %sstate->gpr[%u], state->gpr[%u], %s);\n",
                        rd, left_prefix, ra, rb, carry)) return false;
            }
            return emit_record_update(output, record, rd);
        }
        case OP_SUBFZE:
            if (operands.count != 2U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !file_printf(output,
                    "    state->gpr[%u] = porpoise_add_with_carry32(state, ~state->gpr[%u], 0U, (state->xer >> 29U) & 1U);\n",
                    rd, ra)) return false;
            return emit_record_update(output, record, rd);
        case OP_LOGICAL_COMPLEMENT:
            if (spec->detail == LOGICAL_NOT) {
                if (operands.count != 2U || !parse_register(operands.values[0], 'r', &rd) ||
                    !parse_register(operands.values[1], 'r', &ra) ||
                    !file_printf(output, "    state->gpr[%u] = ~state->gpr[%u];\n", rd, ra)) return false;
            } else {
                if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                    !parse_register(operands.values[1], 'r', &ra) ||
                    !parse_register(operands.values[2], 'r', &rb)) return false;
                if (spec->detail == LOGICAL_AND_COMPLEMENT &&
                    !file_printf(output, "    state->gpr[%u] = state->gpr[%u] & ~state->gpr[%u];\n", rd, ra, rb)) return false;
                if (spec->detail == LOGICAL_OR_COMPLEMENT &&
                    !file_printf(output, "    state->gpr[%u] = state->gpr[%u] | ~state->gpr[%u];\n", rd, ra, rb)) return false;
                if (spec->detail == LOGICAL_EQUIVALENT &&
                    !file_printf(output, "    state->gpr[%u] = ~(state->gpr[%u] ^ state->gpr[%u]);\n", rd, ra, rb)) return false;
                if (spec->detail == LOGICAL_NOR &&
                    !file_printf(output, "    state->gpr[%u] = ~(state->gpr[%u] | state->gpr[%u]);\n", rd, ra, rb)) return false;
            }
            return emit_record_update(output, record, rd);
        case OP_ORI:
        case OP_ORIS:
        case OP_XORI:
        case OP_XORIS:
        case OP_ANDI:
        case OP_ANDIS: {
            const char *operator_text = spec->operation == OP_ORI || spec->operation == OP_ORIS ? "|" :
                                        spec->operation == OP_XORI || spec->operation == OP_XORIS ? "^" : "&";
            bool shifted = spec->operation == OP_ORIS || spec->operation == OP_XORIS || spec->operation == OP_ANDIS;
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_unsigned_or_relocated(
                    operands.values[2], item->word,
                    spec->operation == OP_ORI
                        ? PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_LOW)
                        : 0U,
                    &unsigned_value) || unsigned_value > UINT16_MAX) return false;
            if (!file_printf(output, "    state->gpr[%u] = state->gpr[%u] %s (UINT32_C(%lu)%s);\n",
                             rd, ra, operator_text, (unsigned long)unsigned_value, shifted ? " << 16" : "")) return false;
            if (spec->operation == OP_ANDI || spec->operation == OP_ANDIS)
                return file_printf(output, "    porpoise_set_cr0_result(state, state->gpr[%u]);\n", rd);
            return true;
        }
        case OP_SLW:
        case OP_SRW:
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) || !parse_register(operands.values[2], 'r', &rb)) return false;
            return file_printf(output, "    state->gpr[%u] = porpoise_%s32(state->gpr[%u], state->gpr[%u]);\n",
                               rd, spec->operation == OP_SLW ? "shift_left" : "shift_right", ra, rb);
        case OP_SRAW:
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_register(operands.values[2], 'r', &rb) ||
                !file_printf(output,
                    "    state->gpr[%u] = porpoise_arithmetic_shift_right32(state, state->gpr[%u], state->gpr[%u]);\n",
                    rd, ra, rb)) return false;
            return emit_record_update(output, record, rd);
        case OP_SRAWI:
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) || !parse_unsigned(operands.values[2], &unsigned_value) ||
                unsigned_value > 31U) return false;
            if (!file_printf(output, "    state->gpr[%u] = porpoise_arithmetic_shift_right32(state, state->gpr[%u], %luU);\n",
                             rd, ra, (unsigned long)unsigned_value)) return false;
            return emit_record_update(output, record, rd);
        case OP_RLWINM:
            if (operands.count != 5U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) || !parse_unsigned(operands.values[2], &unsigned_value) ||
                unsigned_value > 31U) return false;
            {
                uint32_t mb, me;
                if (!parse_unsigned(operands.values[3], &mb) || !parse_unsigned(operands.values[4], &me) || mb > 31U || me > 31U) return false;
                if (!file_printf(output, "    state->gpr[%u] = porpoise_rotate_left32(state->gpr[%u], %luU) & porpoise_mask32(%luU, %luU);\n",
                                 rd, ra, (unsigned long)unsigned_value, (unsigned long)mb, (unsigned long)me)) return false;
                return emit_record_update(output, record, rd);
            }
        case OP_RLWIMI:
            if (operands.count != 5U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) || !parse_unsigned(operands.values[2], &unsigned_value) ||
                unsigned_value > 31U) return false;
            {
                uint32_t mb, me;
                if (!parse_unsigned(operands.values[3], &mb) || !parse_unsigned(operands.values[4], &me) || mb > 31U || me > 31U) return false;
                if (!file_printf(output,
                    "    { uint32_t mask = porpoise_mask32(%luU, %luU); state->gpr[%u] = (state->gpr[%u] & ~mask) | (porpoise_rotate_left32(state->gpr[%u], %luU) & mask); }\n",
                    (unsigned long)mb, (unsigned long)me, rd, rd, ra, (unsigned long)unsigned_value)) return false;
                return emit_record_update(output, record, rd);
            }
        case OP_RLWNM:
            if (operands.count != 5U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_register(operands.values[2], 'r', &rb)) return false;
            {
                uint32_t mb, me;
                if (!parse_unsigned(operands.values[3], &mb) ||
                    !parse_unsigned(operands.values[4], &me) || mb > 31U || me > 31U) return false;
                if (!file_printf(output,
                    "    state->gpr[%u] = porpoise_rotate_left32(state->gpr[%u], state->gpr[%u] & 31U) & porpoise_mask32(%luU, %luU);\n",
                    rd, ra, rb, (unsigned long)mb, (unsigned long)me)) return false;
                return emit_record_update(output, record, rd);
            }
        case OP_ROTLW:
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_register(operands.values[2], 'r', &rb) ||
                !file_printf(output,
                    "    state->gpr[%u] = porpoise_rotate_left32(state->gpr[%u], state->gpr[%u] & 31U);\n",
                    rd, ra, rb)) return false;
            return emit_record_update(output, record, rd);
        case OP_ROTATE_ALIAS: {
            uint32_t first, second = 0U;
            uint32_t shift, mask_begin, mask_end;
            bool four_operands =
                spec->detail == ROTATE_CLEAR_LEFT_SHIFT_LEFT_IMMEDIATE ||
                spec->detail == ROTATE_EXTRACT_LEFT_IMMEDIATE ||
                spec->detail == ROTATE_EXTRACT_RIGHT_IMMEDIATE;
            if (operands.count != (four_operands ? 4U : 3U) ||
                !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_unsigned(operands.values[2], &first) || first > 32U ||
                (four_operands && (!parse_unsigned(operands.values[3], &second) || second > 31U)))
                return false;
            switch (spec->detail) {
                case ROTATE_SHIFT_LEFT_IMMEDIATE:
                    if (first >= 32U) return false;
                    shift = first; mask_begin = 0U; mask_end = 31U - first;
                    break;
                case ROTATE_SHIFT_RIGHT_IMMEDIATE:
                    if (first >= 32U) return false;
                    shift = (32U - first) & 31U; mask_begin = first; mask_end = 31U;
                    break;
                case ROTATE_CLEAR_LEFT_IMMEDIATE:
                    if (first >= 32U) return false;
                    shift = 0U; mask_begin = first; mask_end = 31U;
                    break;
                case ROTATE_CLEAR_RIGHT_IMMEDIATE:
                    if (first >= 32U) return false;
                    shift = 0U; mask_begin = 0U; mask_end = 31U - first;
                    break;
                case ROTATE_CLEAR_LEFT_SHIFT_LEFT_IMMEDIATE:
                    if (first >= 32U || second > first) return false;
                    shift = second; mask_begin = first - second; mask_end = 31U - second;
                    break;
                case ROTATE_EXTRACT_LEFT_IMMEDIATE:
                    if (first == 0U) return false;
                    shift = second; mask_begin = 0U; mask_end = first - 1U;
                    break;
                case ROTATE_EXTRACT_RIGHT_IMMEDIATE:
                    if (first == 0U || first + second > 32U) return false;
                    shift = (first + second) & 31U; mask_begin = (32U - first) & 31U; mask_end = 31U;
                    break;
                case ROTATE_LEFT_IMMEDIATE:
                    if (first >= 32U) return false;
                    shift = first; mask_begin = 0U; mask_end = 31U;
                    break;
                case ROTATE_RIGHT_IMMEDIATE:
                    if (first >= 32U) return false;
                    shift = (32U - first) & 31U; mask_begin = 0U; mask_end = 31U;
                    break;
                default:
                    return false;
            }
            if (!file_printf(output,
                "    state->gpr[%u] = porpoise_rotate_left32(state->gpr[%u], %luU) & porpoise_mask32(%luU, %luU);\n",
                rd, ra, (unsigned long)shift, (unsigned long)mask_begin,
                (unsigned long)mask_end)) return false;
            return emit_record_update(output, record, rd);
        }
        case OP_INTEGER_UNARY:
            if (operands.count != 2U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra)) return false;
            if (spec->detail == INTEGER_EXTEND_BYTE &&
                !file_printf(output,
                    "    state->gpr[%u] = porpoise_sign_extend8(state->gpr[%u]);\n",
                    rd, ra)) return false;
            if (spec->detail == INTEGER_EXTEND_HALFWORD &&
                !file_printf(output,
                    "    state->gpr[%u] = porpoise_sign_extend16(state->gpr[%u]);\n",
                    rd, ra)) return false;
            if (spec->detail == INTEGER_COUNT_LEADING_ZEROS &&
                !file_printf(output,
                    "    state->gpr[%u] = porpoise_count_leading_zeros32(state->gpr[%u]);\n",
                    rd, ra)) return false;
            if (spec->detail == INTEGER_NEGATE &&
                !file_printf(output, "    state->gpr[%u] = 0U - state->gpr[%u];\n", rd, ra)) return false;
            return emit_record_update(output, record, rd);

        default:
            return false;
    }
}
