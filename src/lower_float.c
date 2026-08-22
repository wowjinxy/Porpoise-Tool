#include "lower_internal.h"

#include <stdint.h>
#include <string.h>

static bool emit_runtime_approximation_trace(
    const PorpoiseLowerInstructionContext *context,
    const char *indent)
{
    return file_printf(
        context->output,
        "%sporpoise_trace_approximate(state, UINT32_C(0x%08lX), \"%s\");\n"
        "%sif (porpoise_state_has_fault(state)) return;\n",
        indent,
        (unsigned long)context->item->address,
        context->item->mnemonic,
        indent);
}

static const char *binary_operation_name(int operation)
{
    switch (operation) {
        case FLOAT_ADD:
            return "ADD";
        case FLOAT_SUB:
            return "SUBTRACT";
        case FLOAT_MUL:
            return "MULTIPLY";
        default:
            return "DIVIDE";
    }
}

static const char *fma_operation_name(int operation)
{
    switch (operation) {
        case SCALAR_FMA_MADD:
            return "MADD";
        case SCALAR_FMA_MSUB:
            return "MSUB";
        case SCALAR_FMA_NMADD:
            return "NMADD";
        default:
            return "NMSUB";
    }
}

static const char *paired_fma_operation_name(int operation)
{
    switch (operation) {
        case PAIRED_MADD:
            return "MADD";
        case PAIRED_MSUB:
            return "MSUB";
        case PAIRED_NMADD:
            return "NMADD";
        default:
            return "NMSUB";
    }
}

bool porpoise_lower_emit_float(
    const PorpoiseLowerInstructionContext *context) {
    FILE *output = context->output;
    const OpcodeSpec *spec = context->spec;
    const PorpoiseAsmItem *item = context->item;
    OperandList operands = context->operands;
    unsigned int rd, ra, rb, rc;
    uint32_t unsigned_value;
    bool record = context->record;

    switch (spec->operation) {
        case OP_FLOAT_BINARY:
        case OP_PAIRED_BINARY: {
            const char *operator_text = spec->detail == FLOAT_ADD ? "+" : spec->detail == FLOAT_SUB ? "-" :
                                        spec->detail == FLOAT_MUL ? "*" : "/";
            const char *operation_name = binary_operation_name(spec->detail);
            if (operands.count != 3U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) || !parse_register(operands.values[2], 'f', &rb)) return false;
            if (spec->operation == OP_FLOAT_BINARY) {
                bool single = item->mnemonic[strlen(item->mnemonic) - 1U] == 's';
                if (single) {
                    if (!file_printf(
                            output,
                            "    if (!porpoise_fp_binary_single_try_exact(state, %uU, %uU, %uU, PORPOISE_FP_BINARY_%s, %d)) {\n",
                            rd, ra, rb, operation_name, record ? 1 : 0) ||
                        !emit_runtime_approximation_trace(context, "        ") ||
                        !file_printf(
                            output,
                            "        { double result = (double)(float)(porpoise_fpr_get_f64(state, %uU, 0U) %s porpoise_fpr_get_f64(state, %uU, 0U)); "
                            "porpoise_fpr_set_f64(state, %uU, 0U, result); porpoise_fpr_set_f64(state, %uU, 1U, result); }\n"
                            "    }\n",
                            ra, operator_text, rb, rd, rd)) {
                        return false;
                    }
                    return true;
                }
                return emit_runtime_approximation_trace(context, "    ") &&
                    file_printf(output,
                    "    porpoise_fpr_set_f64(state, %uU, 0U, porpoise_fpr_get_f64(state, %uU, 0U) %s porpoise_fpr_get_f64(state, %uU, 0U));\n",
                    rd, ra, operator_text, rb);
            }
            if (!file_printf(
                    output,
                    "    if (!porpoise_ps_binary_try_exact(state, %uU, %uU, %uU, PORPOISE_FP_BINARY_%s, %d)) {\n",
                    rd, ra, rb, operation_name, record ? 1 : 0) ||
                !emit_runtime_approximation_trace(context, "        ") ||
                !file_printf(
                    output,
                    "        porpoise_fpr_set_f64(state, %uU, 0U, (double)(float)(porpoise_fpr_get_f64(state, %uU, 0U) %s porpoise_fpr_get_f64(state, %uU, 0U)));\n"
                    "        porpoise_fpr_set_f64(state, %uU, 1U, (double)(float)(porpoise_fpr_get_f64(state, %uU, 1U) %s porpoise_fpr_get_f64(state, %uU, 1U)));\n"
                    "    }\n",
                    rd, ra, operator_text, rb, rd, ra, operator_text, rb)) {
                return false;
            }
            return true;
        }
        case OP_PAIRED_TERNARY: {
            const char *operator_text =
                spec->detail == PAIRED_MSUB || spec->detail == PAIRED_NMSUB ? "-" : "+";
            const char *negate_text =
                spec->detail == PAIRED_NMADD || spec->detail == PAIRED_NMSUB
                    ? "if (!isnan(result0)) result0 = -result0; if (!isnan(result1)) result1 = -result1; "
                    : "";
            if (operands.count != 4U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) ||
                !parse_register(operands.values[2], 'f', &rc) ||
                !parse_register(operands.values[3], 'f', &rb)) return false;
            if (!file_printf(
                    output,
                    "    if (!porpoise_ps_fma_try_exact(state, %uU, %uU, %uU, %uU, PORPOISE_FP_FMA_%s, -1, %d)) {\n",
                    rd, ra, rc, rb, paired_fma_operation_name(spec->detail), record ? 1 : 0) ||
                !file_printf(
                    output,
                    "        if (porpoise_state_has_fault(state)) return;\n") ||
                !emit_runtime_approximation_trace(context, "        ") ||
                !file_printf(output,
                    "        { double a0 = porpoise_fpr_get_f64(state, %uU, 0U); double a1 = porpoise_fpr_get_f64(state, %uU, 1U); "
                    "double c0 = porpoise_fpr_get_f64(state, %uU, 0U); double c1 = porpoise_fpr_get_f64(state, %uU, 1U); "
                    "double b0 = porpoise_fpr_get_f64(state, %uU, 0U); double b1 = porpoise_fpr_get_f64(state, %uU, 1U); "
                    "double result0 = (double)(float)(a0 * c0 %s b0); double result1 = (double)(float)(a1 * c1 %s b1); "
                    "%sporpoise_fpr_set_f64(state, %uU, 0U, result0); porpoise_fpr_set_f64(state, %uU, 1U, result1); }\n"
                    "    }\n",
                    ra, ra, rc, rc, rb, rb, operator_text, operator_text, negate_text, rd, rd)) {
                return false;
            }
            return true;
        }
        case OP_PAIRED_SCALAR_MADD:
            if (operands.count != 4U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) ||
                !parse_register(operands.values[2], 'f', &rc) ||
                !parse_register(operands.values[3], 'f', &rb)) return false;
            return file_printf(
                output,
                "    if (!porpoise_ps_madds_scalar(state, %uU, %uU, %uU, %uU, %dU, %d)) return;\n",
                rd, ra, rc, rb, spec->detail, record ? 1 : 0);
        case OP_PAIRED_SCALAR_MULTIPLY:
            if (operands.count != 3U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) ||
                !parse_register(operands.values[2], 'f', &rc)) return false;
            return file_printf(
                output,
                "    if (!porpoise_ps_muls_scalar(state, %uU, %uU, %uU, %dU, %d)) return;\n",
                rd, ra, rc, spec->detail, record ? 1 : 0);
        case OP_PAIRED_SUM:
            if (operands.count != 4U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) ||
                !parse_register(operands.values[2], 'f', &rc) ||
                !parse_register(operands.values[3], 'f', &rb)) return false;
            if (!file_printf(
                    output,
                    "    if (!porpoise_ps_sum_try_exact(state, %uU, %uU, %uU, %uU, %dU, %d)) {\n",
                    rd, ra, rc, rb, spec->detail, record ? 1 : 0) ||
                !emit_runtime_approximation_trace(context, "        ")) {
                return false;
            }
            if (spec->detail == 0) {
                return file_printf(output,
                    "        { double sum = (double)(float)(porpoise_fpr_get_f64(state, %uU, 0U) + porpoise_fpr_get_f64(state, %uU, 1U)); "
                    "double passthrough = (double)(float)porpoise_fpr_get_f64(state, %uU, 1U); "
                    "porpoise_fpr_set_f64(state, %uU, 0U, sum); porpoise_fpr_set_f64(state, %uU, 1U, passthrough); }\n"
                    "    }\n",
                    ra, rb, rc, rd, rd);
            }
            return file_printf(output,
                "        { double sum = (double)(float)(porpoise_fpr_get_f64(state, %uU, 0U) + porpoise_fpr_get_f64(state, %uU, 1U)); "
                "double passthrough = (double)(float)porpoise_fpr_get_f64(state, %uU, 0U); "
                "porpoise_fpr_set_f64(state, %uU, 0U, passthrough); porpoise_fpr_set_f64(state, %uU, 1U, sum); }\n"
                "    }\n",
                ra, rb, rc, rd, rd);
        case OP_PAIRED_MERGE: {
            unsigned int left_lane =
                spec->detail == PAIRED_MERGE_10 || spec->detail == PAIRED_MERGE_11 ? 1U : 0U;
            unsigned int right_lane =
                spec->detail == PAIRED_MERGE_01 || spec->detail == PAIRED_MERGE_11 ? 1U : 0U;
            if (operands.count != 3U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) ||
                !parse_register(operands.values[2], 'f', &rb)) return false;
            return file_printf(output,
                "    { uint64_t result0 = porpoise_fpr_get_bits(state, %uU, %uU); "
                "uint64_t result1 = porpoise_fpr_get_bits(state, %uU, %uU); "
                "porpoise_fpr_set_bits(state, %uU, 0U, result0); porpoise_fpr_set_bits(state, %uU, 1U, result1); }\n",
                ra, left_lane, rb, right_lane, rd, rd);
        }
        case OP_PAIRED_UNARY:
            if (operands.count != 2U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra)) return false;
            return file_printf(output,
                "    { uint64_t lane0 = porpoise_fpr_get_bits(state, %uU, 0U)%s; "
                "uint64_t lane1 = porpoise_fpr_get_bits(state, %uU, 1U)%s; "
                "porpoise_fpr_set_bits(state, %uU, 0U, lane0); porpoise_fpr_set_bits(state, %uU, 1U, lane1); }\n",
                ra, spec->detail == PAIRED_NEGATE ? " ^ UINT64_C(0x8000000000000000)" : "",
                ra, spec->detail == PAIRED_NEGATE ? " ^ UINT64_C(0x8000000000000000)" : "",
                rd, rd);
        case OP_PAIRED_COMPARE: {
            unsigned int field;
            if (operands.count != 3U || strncmp(operands.values[0], "cr", 2U) != 0 ||
                !parse_unsigned(operands.values[0] + 2, &unsigned_value) || unsigned_value > 7U ||
                !parse_register(operands.values[1], 'f', &ra) ||
                !parse_register(operands.values[2], 'f', &rb)) return false;
            field = (unsigned int)unsigned_value;
            return file_printf(output,
                "    porpoise_fcmpo(state, %uU, porpoise_fpr_get_bits(state, %uU, 0U), porpoise_fpr_get_bits(state, %uU, 0U));\n"
                "    if (porpoise_state_has_fault(state)) return;\n",
                field, ra, rb);
        }
        case OP_PAIRED_SELECT:
            if (operands.count != 4U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) ||
                !parse_register(operands.values[2], 'f', &rc) ||
                !parse_register(operands.values[3], 'f', &rb)) return false;
            return file_printf(output,
                "    { uint64_t result0 = porpoise_fsel_bits(porpoise_fpr_get_bits(state, %uU, 0U), porpoise_fpr_get_bits(state, %uU, 0U), porpoise_fpr_get_bits(state, %uU, 0U)); "
                "uint64_t result1 = porpoise_fsel_bits(porpoise_fpr_get_bits(state, %uU, 1U), porpoise_fpr_get_bits(state, %uU, 1U), porpoise_fpr_get_bits(state, %uU, 1U)); "
                "porpoise_fpr_set_bits(state, %uU, 0U, result0); porpoise_fpr_set_bits(state, %uU, 1U, result1); }\n",
                ra, rc, rb, ra, rc, rb, rd, rd);
        case OP_FLOAT_UNARY:
            if (operands.count != 2U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra)) return false;
            if (spec->detail == FLOAT_MOVE) {
                if (!file_printf(output,
                    "    porpoise_fpr_set_bits(state, %uU, 0U, porpoise_fpr_get_bits(state, %uU, 0U));\n",
                    rd, ra)) return false;
            } else if (spec->detail == FLOAT_NEG) {
                if (!file_printf(output,
                    "    porpoise_fpr_set_bits(state, %uU, 0U, porpoise_fpr_get_bits(state, %uU, 0U) ^ UINT64_C(0x8000000000000000));\n",
                    rd, ra)) return false;
            } else if (spec->detail == FLOAT_ABS) {
                if (!file_printf(output,
                    "    porpoise_fpr_set_bits(state, %uU, 0U, porpoise_fpr_get_bits(state, %uU, 0U) & UINT64_C(0x7FFFFFFFFFFFFFFF));\n",
                    rd, ra)) return false;
            } else if (!file_printf(output,
                       "    porpoise_fpr_set_bits(state, %uU, 0U, porpoise_fpr_get_bits(state, %uU, 0U) | UINT64_C(0x8000000000000000));\n",
                       rd, ra)) {
                return false;
            }
            return !record || file_printf(output, "    porpoise_fpscr_update_cr1(state);\n");
        case OP_FLOAT_COMPARE: {
            unsigned int field;
            if (operands.count != 3U || strncmp(operands.values[0], "cr", 2U) != 0 ||
                !parse_unsigned(operands.values[0] + 2, &unsigned_value) ||
                unsigned_value > 7U ||
                !parse_register(operands.values[1], 'f', &ra) ||
                !parse_register(operands.values[2], 'f', &rb)) return false;
            field = (unsigned int)unsigned_value;
            return file_printf(
                output,
                "    porpoise_fcmp%c(state, %uU, porpoise_fpr_get_bits(state, %uU, 0U), porpoise_fpr_get_bits(state, %uU, 0U));\n"
                "    if (porpoise_state_has_fault(state)) return;\n",
                spec->detail != 0 ? 'o' : 'u', field, ra, rb);
        }
        case OP_FLOAT_SELECT:
            if (operands.count != 4U ||
                !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) ||
                !parse_register(operands.values[2], 'f', &rc) ||
                !parse_register(operands.values[3], 'f', &rb) ||
                !file_printf(
                    output,
                    "    porpoise_fpr_set_bits(state, %uU, 0U, porpoise_fsel_bits(porpoise_fpr_get_bits(state, %uU, 0U), porpoise_fpr_get_bits(state, %uU, 0U), porpoise_fpr_get_bits(state, %uU, 0U)));\n",
                    rd, ra, rc, rb)) return false;
            return !record || file_printf(output, "    porpoise_fpscr_update_cr1(state);\n");
        case OP_FRSP:
            if (operands.count != 2U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra)) return false;
            return file_printf(output,
                "    if (!porpoise_frsp(state, %uU, %uU, %d)) return;\n",
                rd, ra, record ? 1 : 0);
        case OP_FCTIW:
        case OP_FCTIWZ:
            if (operands.count != 2U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra)) return false;
            return file_printf(output,
                "    if (!porpoise_fctiw%s(state, %uU, %uU, %d)) return;\n",
                spec->operation == OP_FCTIWZ ? "z" : "",
                rd, ra, record ? 1 : 0);
        case OP_MFFS:
            if (operands.count != 1U || !parse_register(operands.values[0], 'f', &rd))
                return false;
            return file_printf(output,
                "    if (!porpoise_mffs(state, %uU, %d)) return;\n",
                rd, record ? 1 : 0);
        case OP_MTFSF:
            if (operands.count != 2U ||
                !parse_unsigned(operands.values[0], &unsigned_value) || unsigned_value > UINT8_MAX ||
                !parse_register(operands.values[1], 'f', &ra)) return false;
            return file_printf(output,
                "    if (!porpoise_mtfsf(state, %luU, %uU, %d)) return;\n",
                (unsigned long)unsigned_value, ra, record ? 1 : 0);
        case OP_MTFSB1:
            if (operands.count != 1U || !parse_cr_bit(operands.values[0], &rd))
                return false;
            return file_printf(output,
                "    if (!porpoise_mtfsb1(state, %uU, %d)) return;\n",
                rd, record ? 1 : 0);
        case OP_FLOAT_FMA: {
            int operation = spec->detail & 0xff;
            const char *operation_name = fma_operation_name(operation);
            const char *precision_name = (spec->detail & SCALAR_FMA_SINGLE) != 0
                ? "SINGLE" : "DOUBLE";
            if (operands.count != 4U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) ||
                !parse_register(operands.values[2], 'f', &rc) ||
                !parse_register(operands.values[3], 'f', &rb)) return false;
            if (!file_printf(
                    output,
                    "    if (!porpoise_fp_fma_execution_is_exact(state, %uU, %uU, %uU, PORPOISE_FP_FMA_%s, PORPOISE_FP_PRECISION_%s)) {\n",
                    ra, rc, rb, operation_name, precision_name) ||
                !emit_runtime_approximation_trace(context, "        ") ||
                !file_printf(output, "    }\n")) {
                return false;
            }
            return file_printf(output,
                "    if (!porpoise_fp_fma(state, %uU, %uU, %uU, %uU, "
                "PORPOISE_FP_FMA_%s, PORPOISE_FP_PRECISION_%s, %d)) return;\n",
                rd, ra, rc, rb, operation_name, precision_name, record ? 1 : 0);
        }
        case OP_RECIPROCAL_APPROX:
            if (operands.count != 2U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra)) return false;
            if (spec->detail == 0)
                return file_printf(output,
                    "    porpoise_fpr_set_f64(state, %uU, 0U, 1.0 / porpoise_fpr_get_f64(state, %uU, 0U)); /* approximation */\n",
                    rd, ra);
            return file_printf(output,
                "    if (!porpoise_frsqrte(state, %uU, %uU, %d)) return;\n",
                rd, ra, record ? 1 : 0);

        default:
            return false;
    }
}
