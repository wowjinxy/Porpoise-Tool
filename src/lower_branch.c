#include "lower_internal.h"
#include "porpoise/util.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

bool porpoise_lower_emit_branch(
    const PorpoiseLowerInstructionContext *context) {
    FILE *output = context->output;
    const OpcodeSpec *spec = context->spec;
    const PorpoiseProgram *program = context->program;
    const PorpoiseSourceFile *source = context->source;
    const PorpoiseFunction *function = context->function;
    const PorpoiseAsmItem *item = context->item;
    const PorpoiseAbiManifest *abi = context->abi;
    PorpoiseDiagnostics *diagnostics = context->diagnostics;
    OperandList operands = context->operands;
    unsigned int rd, ra, rb;
    int32_t immediate;
    uint32_t unsigned_value;

    switch (spec->operation) {
        case OP_B:
        case OP_BL:
            if (operands.count != 1U) return false;
            if (spec->operation == OP_BL && !file_printf(output, "    state->lr = UINT32_C(0x%08lX);\n",
                                                         (unsigned long)(item->address + 4U))) return false;
            return emit_branch_target(output, program, function, abi, operands.values[0],
                                      spec->operation == OP_BL, diagnostics, source, item);
        case OP_BLR:
            if (operands.count != 0U) return false;
            return file_printf(output, "    return;\n");
        case OP_BLRL:
            if (operands.count != 0U) return false;
            return file_printf(output,
                "    { uint32_t target = state->lr; state->lr = UINT32_C(0x%08lX); "
                "if (!porpoise_call_address(state, target)) return; if (porpoise_state_should_stop(state)) return; }\n",
                (unsigned long)(item->address + 4U));
        case OP_BCTR:
        case OP_BCTRL:
            if (operands.count != 0U) return false;
            if (spec->operation == OP_BCTRL && !file_printf(output, "    state->lr = UINT32_C(0x%08lX);\n",
                                                            (unsigned long)(item->address + 4U))) return false;
            if (!file_printf(output, "    if (!porpoise_call_address(state, state->ctr)) return;\n")) return false;
            if (!file_printf(output, "    if (porpoise_state_should_stop(state)) return;\n")) return false;
            return spec->operation == OP_BCTRL || file_printf(output, "    return;\n");
        case OP_CONDITIONAL_BRANCH: {
            unsigned int field = 0U;
            const char *target;
            unsigned int bit;
            bool negate = spec->detail < 0 || (spec->detail & 0x100) != 0;
            char condition[128];
            if (operands.count == 2U) {
                if (strncmp(operands.values[0], "cr", 2U) != 0 ||
                    !parse_unsigned(operands.values[0] + 2, &unsigned_value) || unsigned_value > 7U) return false;
                field = (unsigned int)unsigned_value;
                target = operands.values[1];
            } else if (operands.count == 1U) {
                target = operands.values[0];
            } else return false;
            bit = (unsigned int)(spec->detail < 0 ? -spec->detail : (spec->detail & 0xff));
            if (!porpoise_format(condition, sizeof(condition), "%sporpoise_cr_get_bit(state, %uU)",
                                 negate ? "!" : "", field * 4U + bit)) return false;
            return emit_conditional_target(output, program, function, abi, target, condition,
                                           diagnostics, source, item);
        }
        case OP_CONDITIONAL_RETURN: {
            unsigned int field = 0U;
            unsigned int bit;
            bool negate = spec->detail < 0 || (spec->detail & 0x100) != 0;
            if (operands.count == 1U) {
                if (strncmp(operands.values[0], "cr", 2U) != 0 ||
                    !parse_unsigned(operands.values[0] + 2, &unsigned_value) || unsigned_value > 7U) return false;
                field = (unsigned int)unsigned_value;
            } else if (operands.count != 0U) return false;
            bit = (unsigned int)(spec->detail < 0 ? -spec->detail : (spec->detail & 0xff));
            return file_printf(output, "    if (%sporpoise_cr_get_bit(state, %uU)) return;\n",
                               negate ? "!" : "", field * 4U + bit);
        }
        case OP_BDNZ:
        case OP_BDZ:
            if (operands.count != 1U) return false;
            if (!file_printf(output, "    state->ctr -= UINT32_C(1);\n")) return false;
            return emit_conditional_target(output, program, function, abi, operands.values[0],
                                           spec->operation == OP_BDNZ
                                               ? "state->ctr != UINT32_C(0)"
                                               : "state->ctr == UINT32_C(0)",
                                           diagnostics, source, item);
        case OP_MFLR:
        case OP_MFCTR:
        case OP_MFCR:
            if (operands.count != 1U || !parse_register(operands.values[0], 'r', &rd)) return false;
            return file_printf(output, "    state->gpr[%u] = state->%s;\n", rd,
                               spec->operation == OP_MFLR ? "lr" : spec->operation == OP_MFCTR ? "ctr" : "cr");
        case OP_MTLR:
        case OP_MTCTR:
            if (operands.count != 1U || !parse_register(operands.values[0], 'r', &ra)) return false;
            return file_printf(output, "    state->%s = state->gpr[%u];\n",
                               spec->operation == OP_MTLR ? "lr" : "ctr", ra);
        case OP_CR_LOGIC: {
            unsigned int destination_bit, left_bit, right_bit;
            if (spec->detail == CR_LOGICAL_CLEAR || spec->detail == CR_LOGICAL_SET) {
                if (operands.count != 1U || !parse_cr_bit(operands.values[0], &destination_bit))
                    return false;
                return file_printf(output,
                    "    porpoise_cr_set_bit(state, %uU, %d);\n",
                    destination_bit, spec->detail == CR_LOGICAL_SET ? 1 : 0);
            }
            if (operands.count != 3U ||
                !parse_cr_bit(operands.values[0], &destination_bit) ||
                !parse_cr_bit(operands.values[1], &left_bit) ||
                !parse_cr_bit(operands.values[2], &right_bit)) return false;
            return file_printf(output,
                "    porpoise_cr_set_bit(state, %uU, porpoise_cr_get_bit(state, %uU) || porpoise_cr_get_bit(state, %uU));\n",
                destination_bit, left_bit, right_bit);
        }
        case OP_COMPARE: {
            unsigned int field = 0U;
            size_t base = 0U;
            if (operands.count == 3U) {
                if (strncmp(operands.values[0], "cr", 2U) != 0 ||
                    !parse_unsigned(operands.values[0] + 2, &unsigned_value) || unsigned_value > 7U) return false;
                field = (unsigned int)unsigned_value;
                base = 1U;
            } else if (operands.count != 2U) return false;
            if (!parse_register(operands.values[base], 'r', &ra)) return false;
            if (spec->detail <= 2) {
                if (spec->detail == 1) {
                    if (!parse_signed(operands.values[base + 1U], &immediate) ||
                        immediate < INT16_MIN || immediate > (int32_t)UINT16_MAX) return false;
                    return file_printf(output,
                        "    porpoise_compare_signed(state, %uU, state->gpr[%u], porpoise_sign_extend16(UINT32_C(0x%04lX)));\n",
                        field, ra, (unsigned long)(uint16_t)immediate);
                }
                if (!parse_unsigned(operands.values[base + 1U], &unsigned_value) || unsigned_value > UINT16_MAX) return false;
                return file_printf(output,
                    "    porpoise_compare_unsigned(state, %uU, state->gpr[%u], UINT32_C(0x%04lX));\n",
                    field, ra, (unsigned long)unsigned_value);
            }
            if (!parse_register(operands.values[base + 1U], 'r', &rb)) return false;
            return file_printf(output, "    porpoise_compare_%s(state, %uU, state->gpr[%u], state->gpr[%u]);\n",
                               spec->detail == 3 ? "signed" : "unsigned", field, ra, rb);
        }

        default:
            return false;
    }
}
