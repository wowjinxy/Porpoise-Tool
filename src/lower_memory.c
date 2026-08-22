#include "lower_internal.h"

#include <limits.h>
#include <stdint.h>

bool porpoise_lower_emit_memory(
    const PorpoiseLowerInstructionContext *context) {
    FILE *output = context->output;
    const OpcodeSpec *spec = context->spec;
    const PorpoiseAsmItem *item = context->item;
    OperandList operands = context->operands;
    unsigned int rd, ra, rb, rc;
    int32_t immediate;
    uint32_t unsigned_value;

    switch (spec->operation) {
        case OP_LOAD:
        case OP_STORE: {
            int kind = spec->detail < 0 ? -spec->detail : spec->detail;
            bool update = spec->detail < 0;
            if (operands.count != 2U ||
                !parse_register(operands.values[0], kind >= MEMORY_F32 ? 'f' : 'r', &rd) ||
                !parse_memory_operand(operands.values[1], item->word, &immediate, &ra) ||
                (update && ra == 0U) ||
                (update && spec->operation == OP_LOAD && kind < MEMORY_F32 && rd == ra)) return false;
            if (immediate < INT16_MIN || immediate > (int32_t)UINT16_MAX) return false;
            if (ra == 0U) {
                if (!file_printf(output,
                    "    { uint32_t ea = porpoise_sign_extend16(UINT32_C(0x%04lX)); ",
                    (unsigned long)(uint16_t)immediate)) return false;
            } else if (!file_printf(output,
                "    { uint32_t ea = state->gpr[%u] + porpoise_sign_extend16(UINT32_C(0x%04lX)); ",
                ra, (unsigned long)(uint16_t)immediate)) return false;
            if (spec->operation == OP_LOAD) {
                if (kind == MEMORY_U8 && !file_printf(output, "uint32_t value = porpoise_load_u8(state, ea); ")) return false;
                if ((kind == MEMORY_U16 || kind == MEMORY_S16) &&
                    !file_printf(output, "uint32_t value = porpoise_load_u16(state, ea); ")) return false;
                if (kind == MEMORY_U32 && !file_printf(output, "uint32_t value = porpoise_load_u32(state, ea); ")) return false;
                if (kind == MEMORY_F32 && !file_printf(output,
                    "if (!porpoise_fpr_load_binary32(state, %uU, 0U, ea)) return; "
                    "porpoise_fpr_set_bits(state, %uU, 1U, porpoise_fpr_get_bits(state, %uU, 0U)); ",
                    rd, rd, rd)) return false;
                if (kind == MEMORY_F64 && !file_printf(output,
                    "if (!porpoise_fpr_load_binary64(state, %uU, 0U, ea)) return; ", rd)) return false;
            } else {
                if (kind == MEMORY_U8 && !update &&
                    immediate == INT16_MIN) {
                    if (!file_printf(
                            output,
                            "if (ea == UINT32_C(0xCC008000)) porpoise_store_gx_fifo_u8(state, (uint8_t)state->gpr[%u]); else porpoise_store_u8(state, ea, (uint8_t)state->gpr[%u]); ",
                            rd,
                            rd)) return false;
                } else if (kind == MEMORY_U8 && !file_printf(output, "porpoise_store_u8(state, ea, (uint8_t)state->gpr[%u]); ", rd)) return false;
                if ((kind == MEMORY_U16 || kind == MEMORY_S16) && !file_printf(output, "porpoise_store_u16(state, ea, (uint16_t)state->gpr[%u]); ", rd)) return false;
                if (kind == MEMORY_U32 && !file_printf(output, "porpoise_store_u32(state, ea, state->gpr[%u]); ", rd)) return false;
                if (kind == MEMORY_F32 && !file_printf(output,
                    "if (!porpoise_fpr_store_binary32(state, %uU, 0U, ea)) return; ", rd)) return false;
                if (kind == MEMORY_F64 && !file_printf(output,
                    "if (!porpoise_fpr_store_binary64(state, %uU, 0U, ea)) return; ", rd)) return false;
            }
            if (!file_printf(output, "if (porpoise_state_has_fault(state)) return; ")) return false;
            if (spec->operation == OP_LOAD) {
                if ((kind == MEMORY_U8 || kind == MEMORY_U16 || kind == MEMORY_U32) &&
                    !file_printf(output, "state->gpr[%u] = value; ", rd)) return false;
                if (kind == MEMORY_S16 &&
                    !file_printf(output, "state->gpr[%u] = porpoise_sign_extend16(value); ", rd)) return false;
            }
            if (update && !file_printf(output, "state->gpr[%u] = ea; ", ra)) return false;
            return file_printf(output, "}\n");
        }
        case OP_INDEXED_LOAD:
        case OP_INDEXED_STORE: {
            int kind = spec->detail < 0 ? -spec->detail : spec->detail;
            bool update = spec->detail < 0;
            if (operands.count != 3U ||
                !parse_register(operands.values[0], kind >= MEMORY_F32 ? 'f' : 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_register(operands.values[2], 'r', &rb) ||
                (update && ra == 0U) ||
                (update && spec->operation == OP_INDEXED_LOAD && kind < MEMORY_F32 && rd == ra))
                return false;
            if (ra == 0U) {
                if (!file_printf(output, "    { uint32_t ea = state->gpr[%u]; ", rb)) return false;
            } else if (!file_printf(output,
                "    { uint32_t ea = state->gpr[%u] + state->gpr[%u]; ", ra, rb)) return false;
            if (spec->operation == OP_INDEXED_LOAD) {
                if (kind == MEMORY_U8 && !file_printf(output, "uint32_t value = porpoise_load_u8(state, ea); ")) return false;
                if ((kind == MEMORY_U16 || kind == MEMORY_S16) &&
                    !file_printf(output, "uint32_t value = porpoise_load_u16(state, ea); ")) return false;
                if (kind == MEMORY_U32 && !file_printf(output, "uint32_t value = porpoise_load_u32(state, ea); ")) return false;
                if (kind == MEMORY_F32 && !file_printf(output,
                    "if (!porpoise_fpr_load_binary32(state, %uU, 0U, ea)) return; "
                    "porpoise_fpr_set_bits(state, %uU, 1U, porpoise_fpr_get_bits(state, %uU, 0U)); ",
                    rd, rd, rd)) return false;
                if (kind == MEMORY_F64 && !file_printf(output,
                    "if (!porpoise_fpr_load_binary64(state, %uU, 0U, ea)) return; ", rd)) return false;
            } else {
                if (kind == MEMORY_U8 && !file_printf(output, "porpoise_store_u8(state, ea, (uint8_t)state->gpr[%u]); ", rd)) return false;
                if (kind == MEMORY_U16 && !file_printf(output, "porpoise_store_u16(state, ea, (uint16_t)state->gpr[%u]); ", rd)) return false;
                if (kind == MEMORY_U32 && !file_printf(output, "porpoise_store_u32(state, ea, state->gpr[%u]); ", rd)) return false;
                if (kind == MEMORY_F32 && !file_printf(output,
                    "if (!porpoise_fpr_store_binary32(state, %uU, 0U, ea)) return; ", rd)) return false;
                if (kind == MEMORY_F64 && !file_printf(output,
                    "if (!porpoise_fpr_store_binary64(state, %uU, 0U, ea)) return; ", rd)) return false;
                if (kind == MEMORY_FPR_U32 && !file_printf(output,
                    "porpoise_store_u32(state, ea, (uint32_t)porpoise_fpr_get_bits(state, %uU, 0U)); ", rd)) return false;
            }
            if (!file_printf(output, "if (porpoise_state_has_fault(state)) return; ")) return false;
            if (spec->operation == OP_INDEXED_LOAD) {
                if ((kind == MEMORY_U8 || kind == MEMORY_U16 || kind == MEMORY_U32) &&
                    !file_printf(output, "state->gpr[%u] = value; ", rd)) return false;
                if (kind == MEMORY_S16 &&
                    !file_printf(output, "state->gpr[%u] = porpoise_sign_extend16(value); ", rd)) return false;
            }
            if (update && !file_printf(output, "state->gpr[%u] = ea; ", ra)) return false;
            return file_printf(output, "}\n");
        }
        case OP_BYTE_REVERSE_LOAD:
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_register(operands.values[2], 'r', &rb)) return false;
            if (ra == 0U) {
                if (!file_printf(output, "    { uint32_t ea = state->gpr[%u]; ", rb)) return false;
            } else if (!file_printf(output,
                "    { uint32_t ea = state->gpr[%u] + state->gpr[%u]; ", ra, rb)) return false;
            return file_printf(output,
                "uint32_t value = porpoise_load_u16(state, ea); if (porpoise_state_has_fault(state)) return; state->gpr[%u] = ((value & UINT32_C(0xFF)) << 8U) | ((value >> 8U) & UINT32_C(0xFF)); }\n",
                rd);
        case OP_BYTE_REVERSE_STORE:
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_register(operands.values[2], 'r', &rb)) return false;
            if (ra == 0U) {
                if (!file_printf(output, "    { uint32_t ea = state->gpr[%u]; ", rb)) return false;
            } else if (!file_printf(output,
                "    { uint32_t ea = state->gpr[%u] + state->gpr[%u]; ", ra, rb)) return false;
            return file_printf(output,
                "uint32_t value = state->gpr[%u]; porpoise_store_u16(state, ea, (uint16_t)(((value & UINT32_C(0xFF)) << 8U) | ((value >> 8U) & UINT32_C(0xFF)))); if (porpoise_state_has_fault(state)) return; }\n",
                rd);
        case OP_PSQ_DFORM: {
            bool store = (spec->detail & PSQ_STORE) != 0;
            bool update = (spec->detail & PSQ_UPDATE) != 0;
            unsigned int expected_opcode = 56U + (store ? 4U : 0U) + (update ? 1U : 0U);

            if (operands.count != 4U ||
                !parse_register(operands.values[0], 'f', &rd) ||
                !parse_psq_memory_operand(operands.values[1], &immediate, &ra) ||
                immediate < -2048 || immediate > 2047 ||
                !parse_unsigned(operands.values[2], &unsigned_value) || unsigned_value > 1U ||
                !parse_gqr_register(operands.values[3], &rc) ||
                (update && ra == 0U)) {
                return false;
            }
            rb = (unsigned int)unsigned_value;
            if (!psq_dform_operands_match_word(
                    item->word, expected_opcode, rd, ra, immediate, rb, rc)) {
                return false;
            }
            if (ra == 0U) {
                if (!file_printf(output,
                    "    { uint32_t ea = UINT32_C(0x%08lX); ",
                    (unsigned long)(uint32_t)immediate)) return false;
            } else if (!file_printf(output,
                "    { uint32_t ea = state->gpr[%u] + UINT32_C(0x%08lX); ",
                ra, (unsigned long)(uint32_t)immediate)) return false;
            if (!file_printf(output,
                "if (!porpoise_psq_transfer_is_exact(state, %uU, %uU, %uU, %d)) "
                "{ porpoise_trace_approximate(state, UINT32_C(0x%08lX), \"%s\"); "
                "if (porpoise_state_has_fault(state)) return; } "
                "if (!porpoise_psq_%s(state, %uU, ea, %uU, %uU, 1U)) return; ",
                rd, rb, rc, store ? 1 : 0,
                (unsigned long)item->address, item->mnemonic,
                store ? "store" : "load", rd, rb, rc)) return false;
            if (update && !file_printf(output, "state->gpr[%u] = ea; ", ra)) return false;
            return file_printf(output, "}\n");
        }
        case OP_PSQ_INDEXED: {
            bool store = (spec->detail & PSQ_STORE) != 0;
            bool update = (spec->detail & PSQ_UPDATE) != 0;
            unsigned int expected_xo = (store ? 7U : 6U) +
                                       (update ? 32U : 0U);

            if (operands.count != 5U ||
                !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_register(operands.values[2], 'r', &rb) ||
                !parse_unsigned(operands.values[3], &unsigned_value) || unsigned_value > 1U ||
                !parse_gqr_register(operands.values[4], &rc) ||
                (update && ra == 0U)) {
                return false;
            }
            if (!psq_indexed_operands_match_word(
                    item->word, expected_xo, rd, ra, rb,
                    (unsigned int)unsigned_value, rc)) {
                return false;
            }
            if (ra == 0U) {
                if (!file_printf(output,
                    "    { uint32_t ea = state->gpr[%u]; ", rb)) return false;
            } else if (!file_printf(output,
                "    { uint32_t ea = state->gpr[%u] + state->gpr[%u]; ",
                ra, rb)) return false;
            if (!file_printf(output,
                "if (!porpoise_psq_transfer_is_exact(state, %uU, %luU, %uU, %d)) "
                "{ porpoise_trace_approximate(state, UINT32_C(0x%08lX), \"%s\"); "
                "if (porpoise_state_has_fault(state)) return; } "
                "if (!porpoise_psq_%s(state, %uU, ea, %luU, %uU, 0U)) return; ",
                rd, (unsigned long)unsigned_value, rc, store ? 1 : 0,
                (unsigned long)item->address, item->mnemonic,
                store ? "store" : "load", rd,
                (unsigned long)unsigned_value, rc)) return false;
            if (update && !file_printf(output, "state->gpr[%u] = ea; ", ra)) return false;
            return file_printf(output, "}\n");
        }
        case OP_LOAD_MULTIPLE:
        case OP_STORE_MULTIPLE:
            if (operands.count != 2U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_memory_operand(operands.values[1], item->word, &immediate, &ra) ||
                immediate < INT16_MIN || immediate > (int32_t)UINT16_MAX ||
                (spec->operation == OP_LOAD_MULTIPLE &&
                 ((ra == 0U && rd == 0U) || (ra != 0U && ra >= rd)))) return false;
            if (ra == 0U) {
                if (!file_printf(output,
                    "    { uint32_t ea = porpoise_sign_extend16(UINT32_C(0x%04lX)); ",
                    (unsigned long)(uint16_t)immediate)) return false;
            } else if (!file_printf(output,
                "    { uint32_t ea = state->gpr[%u] + porpoise_sign_extend16(UINT32_C(0x%04lX)); ",
                ra, (unsigned long)(uint16_t)immediate)) return false;
            if (spec->operation == OP_LOAD_MULTIPLE) {
                if (!file_printf(output,
                    "if (!porpoise_load_multiple_words(state, ea, %uU)) return; }\n",
                    rd)) return false;
            } else if (!file_printf(output,
                "if (!porpoise_store_multiple_words(state, ea, %uU)) return; }\n",
                rd)) return false;
            return true;

        default:
            return false;
    }
}
