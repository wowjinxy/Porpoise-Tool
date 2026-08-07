#include "porpoise/lower.h"
#include "porpoise/util.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef enum LoweringOperation {
    OP_NOP = 0,
    OP_LI,
    OP_LIS,
    OP_ADDI,
    OP_ADDIS,
    OP_SUB_IMMEDIATE,
    OP_ADD,
    OP_SUBF,
    OP_MULLI,
    OP_MULLW,
    OP_CARRY_ARITHMETIC,
    OP_AND,
    OP_OR,
    OP_XOR,
    OP_LOGICAL_COMPLEMENT,
    OP_ORI,
    OP_ORIS,
    OP_XORI,
    OP_XORIS,
    OP_ANDI,
    OP_ANDIS,
    OP_SLW,
    OP_SRW,
    OP_SRAW,
    OP_SRAWI,
    OP_RLWINM,
    OP_RLWIMI,
    OP_ROTATE_ALIAS,
    OP_INTEGER_UNARY,
    OP_LOAD,
    OP_STORE,
    OP_INDEXED_LOAD,
    OP_INDEXED_STORE,
    OP_LOAD_MULTIPLE,
    OP_STORE_MULTIPLE,
    OP_B,
    OP_BL,
    OP_BLR,
    OP_BCTR,
    OP_BCTRL,
    OP_CONDITIONAL_BRANCH,
    OP_BDNZ,
    OP_MFLR,
    OP_MTLR,
    OP_MFCTR,
    OP_MTCTR,
    OP_MFCR,
    OP_CR_LOGIC,
    OP_COMPARE,
    OP_FLOAT_BINARY,
    OP_FLOAT_UNARY,
    OP_PAIRED_BINARY,
    OP_HOST_NOOP,
    OP_RECIPROCAL_APPROX
} LoweringOperation;

typedef struct OpcodeSpec {
    const char *mnemonic;
    LoweringOperation operation;
    PorpoiseLoweringStatus status;
    bool semantic_test;
    int detail;
} OpcodeSpec;

enum {
    MEMORY_U8 = 1,
    MEMORY_U16,
    MEMORY_S16,
    MEMORY_U32,
    MEMORY_F32,
    MEMORY_F64
};

enum {
    CARRY_ADD_IMMEDIATE = 1,
    CARRY_SUBF_IMMEDIATE,
    CARRY_ADD,
    CARRY_ADD_EXTENDED,
    CARRY_ADD_ZERO_EXTENDED,
    CARRY_SUBF,
    CARRY_SUBF_EXTENDED
};

enum {
    LOGICAL_AND_COMPLEMENT = 1,
    LOGICAL_NOR,
    LOGICAL_NOT
};

enum {
    INTEGER_EXTEND_BYTE = 1,
    INTEGER_EXTEND_HALFWORD,
    INTEGER_COUNT_LEADING_ZEROS,
    INTEGER_NEGATE
};

enum {
    ROTATE_SHIFT_LEFT_IMMEDIATE = 1,
    ROTATE_SHIFT_RIGHT_IMMEDIATE,
    ROTATE_CLEAR_LEFT_IMMEDIATE,
    ROTATE_CLEAR_RIGHT_IMMEDIATE,
    ROTATE_CLEAR_LEFT_SHIFT_LEFT_IMMEDIATE,
    ROTATE_EXTRACT_LEFT_IMMEDIATE,
    ROTATE_EXTRACT_RIGHT_IMMEDIATE,
    ROTATE_LEFT_IMMEDIATE,
    ROTATE_RIGHT_IMMEDIATE
};

enum {
    CR_LOGICAL_OR = 1,
    CR_LOGICAL_CLEAR,
    CR_LOGICAL_SET
};

enum {
    FLOAT_ADD = 1,
    FLOAT_SUB,
    FLOAT_MUL,
    FLOAT_DIV,
    FLOAT_MOVE,
    FLOAT_NEG,
    FLOAT_ABS,
    FLOAT_NABS
};

/*
 * This table is deliberately conservative. An opcode is only marked lowered
 * when the generated C implements the behavior represented by the mnemonic.
 * Record/overflow variants are therefore not silently treated as aliases.
 */
static const OpcodeSpec OPCODES[] = {
    {"nop", OP_NOP, PORPOISE_LOWERED, false, 0},
    {"li", OP_LI, PORPOISE_LOWERED, true, 0},
    {"lis", OP_LIS, PORPOISE_LOWERED, true, 0},
    {"addi", OP_ADDI, PORPOISE_LOWERED, true, 0},
    {"addis", OP_ADDIS, PORPOISE_LOWERED, false, 0},
    {"subi", OP_SUB_IMMEDIATE, PORPOISE_LOWERED, true, 0},
    {"subis", OP_SUB_IMMEDIATE, PORPOISE_LOWERED, true, 1},
    {"add", OP_ADD, PORPOISE_LOWERED, true, 0},
    {"add.", OP_ADD, PORPOISE_LOWERED, false, 0},
    {"subf", OP_SUBF, PORPOISE_LOWERED, false, 0},
    {"subf.", OP_SUBF, PORPOISE_LOWERED, false, 0},
    {"mulli", OP_MULLI, PORPOISE_LOWERED, false, 0},
    {"mullw", OP_MULLW, PORPOISE_LOWERED, true, 0},
    {"mullw.", OP_MULLW, PORPOISE_LOWERED, false, 0},
    {"addic", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, false, CARRY_ADD_IMMEDIATE},
    {"addic.", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, true, CARRY_ADD_IMMEDIATE},
    {"subic", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, true, -CARRY_ADD_IMMEDIATE},
    {"subic.", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, true, -CARRY_ADD_IMMEDIATE},
    {"subfic", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, true, CARRY_SUBF_IMMEDIATE},
    {"addc", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, true, CARRY_ADD},
    {"addc.", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, false, CARRY_ADD},
    {"adde", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, true, CARRY_ADD_EXTENDED},
    {"adde.", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, false, CARRY_ADD_EXTENDED},
    {"addze", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, true, CARRY_ADD_ZERO_EXTENDED},
    {"addze.", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, false, CARRY_ADD_ZERO_EXTENDED},
    {"subfc", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, true, CARRY_SUBF},
    {"subfc.", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, false, CARRY_SUBF},
    {"subfe", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, true, CARRY_SUBF_EXTENDED},
    {"subfe.", OP_CARRY_ARITHMETIC, PORPOISE_LOWERED, false, CARRY_SUBF_EXTENDED},
    {"and", OP_AND, PORPOISE_LOWERED, false, 0},
    {"and.", OP_AND, PORPOISE_LOWERED, false, 0},
    {"or", OP_OR, PORPOISE_LOWERED, false, 0},
    {"or.", OP_OR, PORPOISE_LOWERED, false, 0},
    {"mr", OP_OR, PORPOISE_LOWERED, false, 1},
    {"mr.", OP_OR, PORPOISE_LOWERED, false, 1},
    {"xor", OP_XOR, PORPOISE_LOWERED, false, 0},
    {"xor.", OP_XOR, PORPOISE_LOWERED, false, 0},
    {"andc", OP_LOGICAL_COMPLEMENT, PORPOISE_LOWERED, true, LOGICAL_AND_COMPLEMENT},
    {"andc.", OP_LOGICAL_COMPLEMENT, PORPOISE_LOWERED, false, LOGICAL_AND_COMPLEMENT},
    {"nor", OP_LOGICAL_COMPLEMENT, PORPOISE_LOWERED, true, LOGICAL_NOR},
    {"nor.", OP_LOGICAL_COMPLEMENT, PORPOISE_LOWERED, false, LOGICAL_NOR},
    {"not", OP_LOGICAL_COMPLEMENT, PORPOISE_LOWERED, false, LOGICAL_NOT},
    {"not.", OP_LOGICAL_COMPLEMENT, PORPOISE_LOWERED, false, LOGICAL_NOT},
    {"ori", OP_ORI, PORPOISE_LOWERED, true, 0},
    {"oris", OP_ORIS, PORPOISE_LOWERED, false, 0},
    {"xori", OP_XORI, PORPOISE_LOWERED, false, 0},
    {"xoris", OP_XORIS, PORPOISE_LOWERED, false, 0},
    {"andi.", OP_ANDI, PORPOISE_LOWERED, false, 0},
    {"andis.", OP_ANDIS, PORPOISE_LOWERED, false, 0},
    {"slw", OP_SLW, PORPOISE_LOWERED, true, 0},
    {"srw", OP_SRW, PORPOISE_LOWERED, false, 0},
    {"sraw", OP_SRAW, PORPOISE_LOWERED, false, 0},
    {"sraw.", OP_SRAW, PORPOISE_LOWERED, true, 0},
    {"srawi", OP_SRAWI, PORPOISE_LOWERED, true, 0},
    {"srawi.", OP_SRAWI, PORPOISE_LOWERED, false, 0},
    {"rlwinm", OP_RLWINM, PORPOISE_LOWERED, true, 0},
    {"rlwinm.", OP_RLWINM, PORPOISE_LOWERED, true, 0},
    {"rlwimi", OP_RLWIMI, PORPOISE_LOWERED, false, 0},
    {"rlwimi.", OP_RLWIMI, PORPOISE_LOWERED, false, 0},
    {"slwi", OP_ROTATE_ALIAS, PORPOISE_LOWERED, true, ROTATE_SHIFT_LEFT_IMMEDIATE},
    {"slwi.", OP_ROTATE_ALIAS, PORPOISE_LOWERED, false, ROTATE_SHIFT_LEFT_IMMEDIATE},
    {"srwi", OP_ROTATE_ALIAS, PORPOISE_LOWERED, true, ROTATE_SHIFT_RIGHT_IMMEDIATE},
    {"srwi.", OP_ROTATE_ALIAS, PORPOISE_LOWERED, true, ROTATE_SHIFT_RIGHT_IMMEDIATE},
    {"clrlwi", OP_ROTATE_ALIAS, PORPOISE_LOWERED, true, ROTATE_CLEAR_LEFT_IMMEDIATE},
    {"clrlwi.", OP_ROTATE_ALIAS, PORPOISE_LOWERED, false, ROTATE_CLEAR_LEFT_IMMEDIATE},
    {"clrrwi", OP_ROTATE_ALIAS, PORPOISE_LOWERED, true, ROTATE_CLEAR_RIGHT_IMMEDIATE},
    {"clrrwi.", OP_ROTATE_ALIAS, PORPOISE_LOWERED, false, ROTATE_CLEAR_RIGHT_IMMEDIATE},
    {"clrlslwi", OP_ROTATE_ALIAS, PORPOISE_LOWERED, true, ROTATE_CLEAR_LEFT_SHIFT_LEFT_IMMEDIATE},
    {"clrlslwi.", OP_ROTATE_ALIAS, PORPOISE_LOWERED, false, ROTATE_CLEAR_LEFT_SHIFT_LEFT_IMMEDIATE},
    {"extlwi", OP_ROTATE_ALIAS, PORPOISE_LOWERED, true, ROTATE_EXTRACT_LEFT_IMMEDIATE},
    {"extlwi.", OP_ROTATE_ALIAS, PORPOISE_LOWERED, false, ROTATE_EXTRACT_LEFT_IMMEDIATE},
    {"extrwi", OP_ROTATE_ALIAS, PORPOISE_LOWERED, true, ROTATE_EXTRACT_RIGHT_IMMEDIATE},
    {"extrwi.", OP_ROTATE_ALIAS, PORPOISE_LOWERED, false, ROTATE_EXTRACT_RIGHT_IMMEDIATE},
    {"rotlwi", OP_ROTATE_ALIAS, PORPOISE_LOWERED, true, ROTATE_LEFT_IMMEDIATE},
    {"rotlwi.", OP_ROTATE_ALIAS, PORPOISE_LOWERED, false, ROTATE_LEFT_IMMEDIATE},
    {"rotrwi", OP_ROTATE_ALIAS, PORPOISE_LOWERED, true, ROTATE_RIGHT_IMMEDIATE},
    {"rotrwi.", OP_ROTATE_ALIAS, PORPOISE_LOWERED, false, ROTATE_RIGHT_IMMEDIATE},
    {"extsb", OP_INTEGER_UNARY, PORPOISE_LOWERED, true, INTEGER_EXTEND_BYTE},
    {"extsb.", OP_INTEGER_UNARY, PORPOISE_LOWERED, false, INTEGER_EXTEND_BYTE},
    {"extsh", OP_INTEGER_UNARY, PORPOISE_LOWERED, true, INTEGER_EXTEND_HALFWORD},
    {"extsh.", OP_INTEGER_UNARY, PORPOISE_LOWERED, false, INTEGER_EXTEND_HALFWORD},
    {"cntlzw", OP_INTEGER_UNARY, PORPOISE_LOWERED, true, INTEGER_COUNT_LEADING_ZEROS},
    {"cntlzw.", OP_INTEGER_UNARY, PORPOISE_LOWERED, false, INTEGER_COUNT_LEADING_ZEROS},
    {"neg", OP_INTEGER_UNARY, PORPOISE_LOWERED, true, INTEGER_NEGATE},
    {"neg.", OP_INTEGER_UNARY, PORPOISE_LOWERED, false, INTEGER_NEGATE},
    {"lbz", OP_LOAD, PORPOISE_LOWERED, false, MEMORY_U8},
    {"lbzu", OP_LOAD, PORPOISE_LOWERED, false, -MEMORY_U8},
    {"lhz", OP_LOAD, PORPOISE_LOWERED, false, MEMORY_U16},
    {"lhzu", OP_LOAD, PORPOISE_LOWERED, false, -MEMORY_U16},
    {"lha", OP_LOAD, PORPOISE_LOWERED, false, MEMORY_S16},
    {"lhau", OP_LOAD, PORPOISE_LOWERED, false, -MEMORY_S16},
    {"lwz", OP_LOAD, PORPOISE_LOWERED, true, MEMORY_U32},
    {"lwzu", OP_LOAD, PORPOISE_LOWERED, false, -MEMORY_U32},
    {"lfs", OP_LOAD, PORPOISE_LOWERED, false, MEMORY_F32},
    {"lfsu", OP_LOAD, PORPOISE_LOWERED, false, -MEMORY_F32},
    {"lfd", OP_LOAD, PORPOISE_LOWERED, false, MEMORY_F64},
    {"lfdu", OP_LOAD, PORPOISE_LOWERED, false, -MEMORY_F64},
    {"stb", OP_STORE, PORPOISE_LOWERED, false, MEMORY_U8},
    {"stbu", OP_STORE, PORPOISE_LOWERED, false, -MEMORY_U8},
    {"sth", OP_STORE, PORPOISE_LOWERED, false, MEMORY_U16},
    {"sthu", OP_STORE, PORPOISE_LOWERED, false, -MEMORY_U16},
    {"stw", OP_STORE, PORPOISE_LOWERED, true, MEMORY_U32},
    {"stwu", OP_STORE, PORPOISE_LOWERED, false, -MEMORY_U32},
    {"stfs", OP_STORE, PORPOISE_LOWERED, false, MEMORY_F32},
    {"stfsu", OP_STORE, PORPOISE_LOWERED, false, -MEMORY_F32},
    {"stfd", OP_STORE, PORPOISE_LOWERED, false, MEMORY_F64},
    {"stfdu", OP_STORE, PORPOISE_LOWERED, false, -MEMORY_F64},
    {"lbzx", OP_INDEXED_LOAD, PORPOISE_LOWERED, true, MEMORY_U8},
    {"lbzux", OP_INDEXED_LOAD, PORPOISE_LOWERED, false, -MEMORY_U8},
    {"lhzx", OP_INDEXED_LOAD, PORPOISE_LOWERED, true, MEMORY_U16},
    {"lhzux", OP_INDEXED_LOAD, PORPOISE_LOWERED, false, -MEMORY_U16},
    {"lhax", OP_INDEXED_LOAD, PORPOISE_LOWERED, false, MEMORY_S16},
    {"lhaux", OP_INDEXED_LOAD, PORPOISE_LOWERED, false, -MEMORY_S16},
    {"lwzx", OP_INDEXED_LOAD, PORPOISE_LOWERED, true, MEMORY_U32},
    {"lwzux", OP_INDEXED_LOAD, PORPOISE_LOWERED, true, -MEMORY_U32},
    {"lfsx", OP_INDEXED_LOAD, PORPOISE_LOWERED, true, MEMORY_F32},
    {"lfsux", OP_INDEXED_LOAD, PORPOISE_LOWERED, false, -MEMORY_F32},
    {"lfdx", OP_INDEXED_LOAD, PORPOISE_LOWERED, false, MEMORY_F64},
    {"lfdux", OP_INDEXED_LOAD, PORPOISE_LOWERED, false, -MEMORY_F64},
    {"stbx", OP_INDEXED_STORE, PORPOISE_LOWERED, true, MEMORY_U8},
    {"stbux", OP_INDEXED_STORE, PORPOISE_LOWERED, false, -MEMORY_U8},
    {"sthx", OP_INDEXED_STORE, PORPOISE_LOWERED, true, MEMORY_U16},
    {"sthux", OP_INDEXED_STORE, PORPOISE_LOWERED, false, -MEMORY_U16},
    {"stwx", OP_INDEXED_STORE, PORPOISE_LOWERED, true, MEMORY_U32},
    {"stwux", OP_INDEXED_STORE, PORPOISE_LOWERED, true, -MEMORY_U32},
    {"stfsx", OP_INDEXED_STORE, PORPOISE_LOWERED, true, MEMORY_F32},
    {"stfsux", OP_INDEXED_STORE, PORPOISE_LOWERED, false, -MEMORY_F32},
    {"stfdx", OP_INDEXED_STORE, PORPOISE_LOWERED, false, MEMORY_F64},
    {"stfdux", OP_INDEXED_STORE, PORPOISE_LOWERED, false, -MEMORY_F64},
    {"lmw", OP_LOAD_MULTIPLE, PORPOISE_LOWERED, true, 0},
    {"stmw", OP_STORE_MULTIPLE, PORPOISE_LOWERED, true, 0},
    {"b", OP_B, PORPOISE_LOWERED, false, 0},
    {"bl", OP_BL, PORPOISE_LOWERED, true, 0},
    {"blr", OP_BLR, PORPOISE_APPROXIMATE, false, 0},
    {"bctr", OP_BCTR, PORPOISE_LOWERED, false, 0},
    {"bctrl", OP_BCTRL, PORPOISE_LOWERED, true, 0},
    {"beq", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 2},
    {"bne", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, true, -2},
    {"blt", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 0},
    {"bge", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 0x100},
    {"bgt", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 1},
    {"ble", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 0x101},
    {"bdnz", OP_BDNZ, PORPOISE_LOWERED, false, 0},
    {"mflr", OP_MFLR, PORPOISE_LOWERED, false, 0},
    {"mtlr", OP_MTLR, PORPOISE_LOWERED, false, 0},
    {"mfctr", OP_MFCTR, PORPOISE_LOWERED, false, 0},
    {"mtctr", OP_MTCTR, PORPOISE_LOWERED, true, 0},
    {"mfcr", OP_MFCR, PORPOISE_LOWERED, false, 0},
    {"cror", OP_CR_LOGIC, PORPOISE_LOWERED, true, CR_LOGICAL_OR},
    {"crclr", OP_CR_LOGIC, PORPOISE_LOWERED, true, CR_LOGICAL_CLEAR},
    {"crset", OP_CR_LOGIC, PORPOISE_LOWERED, true, CR_LOGICAL_SET},
    {"cmpwi", OP_COMPARE, PORPOISE_LOWERED, true, 1},
    {"cmplwi", OP_COMPARE, PORPOISE_LOWERED, false, 2},
    {"cmpw", OP_COMPARE, PORPOISE_LOWERED, false, 3},
    {"cmplw", OP_COMPARE, PORPOISE_LOWERED, false, 4},
    {"fadd", OP_FLOAT_BINARY, PORPOISE_LOWERED, true, FLOAT_ADD},
    {"fadds", OP_FLOAT_BINARY, PORPOISE_LOWERED, false, FLOAT_ADD},
    {"fsub", OP_FLOAT_BINARY, PORPOISE_LOWERED, false, FLOAT_SUB},
    {"fsubs", OP_FLOAT_BINARY, PORPOISE_LOWERED, false, FLOAT_SUB},
    {"fmul", OP_FLOAT_BINARY, PORPOISE_LOWERED, false, FLOAT_MUL},
    {"fmuls", OP_FLOAT_BINARY, PORPOISE_LOWERED, false, FLOAT_MUL},
    {"fdiv", OP_FLOAT_BINARY, PORPOISE_LOWERED, false, FLOAT_DIV},
    {"fdivs", OP_FLOAT_BINARY, PORPOISE_LOWERED, false, FLOAT_DIV},
    {"fmr", OP_FLOAT_UNARY, PORPOISE_LOWERED, false, FLOAT_MOVE},
    {"fneg", OP_FLOAT_UNARY, PORPOISE_LOWERED, false, FLOAT_NEG},
    {"fabs", OP_FLOAT_UNARY, PORPOISE_LOWERED, false, FLOAT_ABS},
    {"fnabs", OP_FLOAT_UNARY, PORPOISE_LOWERED, false, FLOAT_NABS},
    {"ps_add", OP_PAIRED_BINARY, PORPOISE_LOWERED, true, FLOAT_ADD},
    {"ps_sub", OP_PAIRED_BINARY, PORPOISE_LOWERED, false, FLOAT_SUB},
    {"ps_mul", OP_PAIRED_BINARY, PORPOISE_LOWERED, false, FLOAT_MUL},
    {"sync", OP_HOST_NOOP, PORPOISE_HOST_NOOP, true, 0},
    {"isync", OP_HOST_NOOP, PORPOISE_HOST_NOOP, false, 0},
    {"eieio", OP_HOST_NOOP, PORPOISE_HOST_NOOP, false, 0},
    {"dcbf", OP_HOST_NOOP, PORPOISE_HOST_NOOP, false, 1},
    {"dcbst", OP_HOST_NOOP, PORPOISE_HOST_NOOP, false, 1},
    {"dcbt", OP_HOST_NOOP, PORPOISE_HOST_NOOP, false, 1},
    {"dcbtst", OP_HOST_NOOP, PORPOISE_HOST_NOOP, false, 1},
    {"icbi", OP_HOST_NOOP, PORPOISE_HOST_NOOP, false, 1},
    {"fres", OP_RECIPROCAL_APPROX, PORPOISE_APPROXIMATE, false, 0},
    {"frsqrte", OP_RECIPROCAL_APPROX, PORPOISE_APPROXIMATE, false, 1}
};

typedef struct OperandList {
    char storage[1024];
    char *values[8];
    size_t count;
} OperandList;

static const OpcodeSpec *find_opcode(const char *mnemonic) {
    size_t index;
    for (index = 0U; index < sizeof(OPCODES) / sizeof(OPCODES[0]); index++) {
        if (strcmp(OPCODES[index].mnemonic, mnemonic) == 0) {
            return &OPCODES[index];
        }
    }
    return NULL;
}

static bool split_operands(const char *text, OperandList *list) {
    char *cursor;
    if (!porpoise_copy_string(list->storage, sizeof(list->storage), text)) return false;
    list->count = 0U;
    cursor = list->storage;
    while (*cursor != '\0') {
        char *start;
        char *end;
        while (isspace((unsigned char)*cursor)) cursor++;
        if (*cursor == '\0') break;
        if (list->count == sizeof(list->values) / sizeof(list->values[0])) return false;
        start = cursor;
        while (*cursor != '\0' && *cursor != ',') cursor++;
        end = cursor;
        if (*cursor == ',') *cursor++ = '\0';
        while (end > start && isspace((unsigned char)end[-1])) *--end = '\0';
        if (*start == '\0') return false;
        list->values[list->count++] = start;
    }
    return true;
}

static bool parse_register(const char *text, char prefix, unsigned int *index) {
    char *end;
    unsigned long value;
    if (text == NULL || text[0] != prefix || !isdigit((unsigned char)text[1])) return false;
    errno = 0;
    value = strtoul(text + 1, &end, 10);
    if (errno != 0 || *end != '\0' || value > 31UL) return false;
    *index = (unsigned int)value;
    return true;
}

static bool parse_unsigned(const char *text, uint32_t *value) {
    char *end;
    unsigned long parsed;
    if (text == NULL || *text == '\0' || *text == '-') return false;
    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || *end != '\0') return false;
#if ULONG_MAX > UINT32_MAX
    if (parsed > UINT32_MAX) return false;
#endif
    *value = (uint32_t)parsed;
    return true;
}

static bool parse_signed(const char *text, int32_t *value) {
    char *end;
    long parsed;
    if (text == NULL || *text == '\0') return false;
    errno = 0;
    parsed = strtol(text, &end, 0);
    if (errno != 0 || *end != '\0') return false;
#if LONG_MIN < INT32_MIN || LONG_MAX > INT32_MAX
    if (parsed < INT32_MIN || parsed > INT32_MAX) return false;
#endif
    *value = (int32_t)parsed;
    return true;
}

static bool parse_memory_operand(const char *text, int32_t *offset, unsigned int *base) {
    char copy[128];
    char *open;
    char *close;
    if (!porpoise_copy_string(copy, sizeof(copy), text)) return false;
    open = strchr(copy, '(');
    close = strrchr(copy, ')');
    if (open == NULL || close == NULL || close[1] != '\0' || open >= close) return false;
    *open = '\0';
    *close = '\0';
    if (copy[0] == '\0') {
        *offset = 0;
    } else if (!parse_signed(copy, offset)) {
        return false;
    }
    return parse_register(open + 1, 'r', base);
}

static bool parse_cr_bit(const char *text, unsigned int *bit_index) {
    const char *suffix = text;
    uint32_t numeric;
    unsigned int field = 0U;
    unsigned int field_bit;

    if (parse_unsigned(text, &numeric)) {
        if (numeric > 31U) return false;
        *bit_index = (unsigned int)numeric;
        return true;
    }
    if (strncmp(text, "cr", 2U) == 0 && isdigit((unsigned char)text[2])) {
        char *end;
        unsigned long parsed_field;
        errno = 0;
        parsed_field = strtoul(text + 2, &end, 10);
        if (errno != 0 || parsed_field > 7UL) return false;
        field = (unsigned int)parsed_field;
        suffix = end;
    }
    if (strcmp(suffix, "lt") == 0) field_bit = 0U;
    else if (strcmp(suffix, "gt") == 0) field_bit = 1U;
    else if (strcmp(suffix, "eq") == 0) field_bit = 2U;
    else if (strcmp(suffix, "so") == 0 || strcmp(suffix, "un") == 0) field_bit = 3U;
    else return false;
    *bit_index = field * 4U + field_bit;
    return true;
}

static bool file_printf(FILE *output, const char *format, ...) {
    int result;
    va_list arguments;
    va_start(arguments, format);
    result = vfprintf(output, format, arguments);
    va_end(arguments);
    return result >= 0;
}

static bool opcode_records(const char *mnemonic) {
    size_t length = strlen(mnemonic);
    return length != 0U && mnemonic[length - 1U] == '.';
}

static bool emit_record_update(FILE *output, bool record, unsigned int destination) {
    return !record || file_printf(
        output,
        "    porpoise_set_cr0_result(state, state->gpr[%u]);\n",
        destination);
}

static bool function_has_label(const PorpoiseFunction *function, const char *name) {
    size_t index;
    for (index = 0U; index < function->item_count; index++) {
        if (function->items[index].kind == PORPOISE_ASM_LABEL &&
            strcmp(function->items[index].label, name) == 0) return true;
    }
    return false;
}

static bool emit_branch_target(
    FILE *output,
    const PorpoiseProgram *program,
    const PorpoiseFunction *function,
    const PorpoiseAbiManifest *abi,
    const char *target,
    bool link,
    PorpoiseDiagnostics *diagnostics,
    const PorpoiseSourceFile *source,
    const PorpoiseAsmItem *item) {
    char c_name[PORPOISE_NAME_CAPACITY];
    const PorpoiseFunction *callee;
    const PorpoiseAbiFunction *imported;
    if (function_has_label(function, target)) {
        if (link) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, source->relative_path,
                                     item->source_line, item->address,
                                     "linked branches to local labels are not yet supported");
            return false;
        }
        porpoise_sanitize_identifier(target, c_name, sizeof(c_name));
        return file_printf(output, "    goto porpoise_label_%s;\n", c_name);
    }
    callee = porpoise_program_find_function(program, target);
    if (callee != NULL) {
        if (!file_printf(output, "    porpoise_lifted_%s(state);\n", callee->c_name)) return false;
        if (link)
            return file_printf(output, "    if (porpoise_state_has_fault(state)) return;\n");
        return file_printf(output, "    return;\n");
    }
    imported = porpoise_abi_find_import(abi, target);
    if (imported != NULL) {
        porpoise_sanitize_identifier(imported->symbol, c_name, sizeof(c_name));
        if (!file_printf(output, "    porpoise_import_%s(state);\n", c_name)) return false;
        if (link)
            return file_printf(output, "    if (porpoise_state_has_fault(state)) return;\n");
        return file_printf(output, "    return;\n");
    }
    porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, source->relative_path,
                             item->source_line, item->address,
                             "branch target %s is neither a lifted function nor a declared ABI import",
                             target);
    return false;
}

static bool emit_conditional_target(
    FILE *output,
    const PorpoiseProgram *program,
    const PorpoiseFunction *function,
    const PorpoiseAbiManifest *abi,
    const char *target,
    const char *condition,
    PorpoiseDiagnostics *diagnostics,
    const PorpoiseSourceFile *source,
    const PorpoiseAsmItem *item) {
    char c_name[PORPOISE_NAME_CAPACITY];
    const PorpoiseFunction *callee;
    const PorpoiseAbiFunction *imported;
    if (function_has_label(function, target)) {
        porpoise_sanitize_identifier(target, c_name, sizeof(c_name));
        return file_printf(output, "    if (%s) goto porpoise_label_%s;\n", condition, c_name);
    }
    callee = porpoise_program_find_function(program, target);
    if (callee != NULL) {
        return file_printf(output, "    if (%s) { porpoise_lifted_%s(state); return; }\n",
                           condition, callee->c_name);
    }
    imported = porpoise_abi_find_import(abi, target);
    if (imported != NULL) {
        porpoise_sanitize_identifier(imported->symbol, c_name, sizeof(c_name));
        return file_printf(output, "    if (%s) { porpoise_import_%s(state); return; }\n",
                           condition, c_name);
    }
    porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, source->relative_path,
                             item->source_line, item->address,
                             "branch target %s is neither a local label, lifted function, nor ABI import",
                             target);
    return false;
}

static bool emit_instruction(
    FILE *output,
    const OpcodeSpec *spec,
    const PorpoiseProgram *program,
    const PorpoiseSourceFile *source,
    const PorpoiseFunction *function,
    const PorpoiseAsmItem *item,
    const PorpoiseAbiManifest *abi,
    PorpoiseDiagnostics *diagnostics) {
    OperandList operands;
    unsigned int rd, ra, rb;
    int32_t immediate;
    uint32_t unsigned_value;
    bool record = opcode_records(item->mnemonic);
    if (!split_operands(item->operands, &operands)) return false;
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
        case OP_LIS:
            if (operands.count != 2U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_signed(operands.values[1], &immediate) ||
                immediate < INT16_MIN || immediate > (int32_t)UINT16_MAX) return false;
            if (spec->operation == OP_LIS) {
                return file_printf(output,
                    "    state->gpr[%u] = ((uint32_t)UINT16_C(0x%04lX)) << 16U;\n",
                    rd, (unsigned long)(uint16_t)immediate);
            }
            return file_printf(output,
                "    state->gpr[%u] = (uint32_t)(int32_t)(int16_t)UINT16_C(0x%04lX);\n",
                rd, (unsigned long)(uint16_t)immediate);
        case OP_ADDI:
        case OP_ADDIS:
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_signed(operands.values[2], &immediate) ||
                immediate < INT16_MIN || immediate > (int32_t)UINT16_MAX) return false;
            if (ra == 0U) {
                if (spec->operation == OP_ADDIS)
                    return file_printf(output, "    state->gpr[%u] = ((uint32_t)UINT16_C(0x%04lX)) << 16U;\n",
                                       rd, (unsigned long)(uint16_t)immediate);
                return file_printf(output,
                    "    state->gpr[%u] = (uint32_t)(int32_t)(int16_t)UINT16_C(0x%04lX);\n",
                    rd, (unsigned long)(uint16_t)immediate);
            }
            if (spec->operation == OP_ADDIS)
                return file_printf(output,
                    "    state->gpr[%u] = state->gpr[%u] + (((uint32_t)UINT16_C(0x%04lX)) << 16U);\n",
                    rd, ra, (unsigned long)(uint16_t)immediate);
            return file_printf(output,
                "    state->gpr[%u] = state->gpr[%u] + (uint32_t)(int32_t)(int16_t)UINT16_C(0x%04lX);\n",
                rd, ra, (unsigned long)(uint16_t)immediate);
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
                    "    state->gpr[%u] = (uint32_t)(int32_t)(int16_t)UINT16_C(0x%04lX);\n",
                    rd, (unsigned long)encoded);
            }
            if (spec->detail != 0)
                return file_printf(output,
                    "    state->gpr[%u] = state->gpr[%u] + (((uint32_t)UINT16_C(0x%04lX)) << 16U);\n",
                    rd, ra, (unsigned long)encoded);
            return file_printf(output,
                "    state->gpr[%u] = state->gpr[%u] + (uint32_t)(int32_t)(int16_t)UINT16_C(0x%04lX);\n",
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
                "    state->gpr[%u] = state->gpr[%u] * (uint32_t)(int32_t)(int16_t)UINT16_C(0x%04lX);\n",
                rd, ra, (unsigned long)(uint16_t)immediate);
        case OP_MULLW:
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_register(operands.values[2], 'r', &rb) ||
                !file_printf(output,
                    "    state->gpr[%u] = state->gpr[%u] * state->gpr[%u];\n",
                    rd, ra, rb)) return false;
            return emit_record_update(output, record, rd);
        case OP_CARRY_ARITHMETIC: {
            int kind = spec->detail < 0 ? -spec->detail : spec->detail;
            if (kind == CARRY_ADD_IMMEDIATE || kind == CARRY_SUBF_IMMEDIATE) {
                uint16_t encoded;
                if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                    !parse_register(operands.values[1], 'r', &ra) ||
                    !parse_signed(operands.values[2], &immediate)) return false;
                if (spec->detail < 0) {
                    int64_t negated = -(int64_t)immediate;
                    if (negated < INT16_MIN || negated > INT16_MAX) return false;
                    encoded = (uint16_t)(int16_t)negated;
                } else {
                    if (immediate < INT16_MIN || immediate > (int32_t)UINT16_MAX) return false;
                    encoded = (uint16_t)immediate;
                }
                if (kind == CARRY_ADD_IMMEDIATE) {
                    if (!file_printf(output,
                        "    state->gpr[%u] = porpoise_add_with_carry32(state, state->gpr[%u], (uint32_t)(int32_t)(int16_t)UINT16_C(0x%04lX), 0U);\n",
                        rd, ra, (unsigned long)encoded)) return false;
                } else if (!file_printf(output,
                    "    state->gpr[%u] = porpoise_add_with_carry32(state, ~state->gpr[%u], (uint32_t)(int32_t)(int16_t)UINT16_C(0x%04lX), 1U);\n",
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
                !parse_unsigned(operands.values[2], &unsigned_value) || unsigned_value > UINT16_MAX) return false;
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
        case OP_LOAD:
        case OP_STORE: {
            int kind = spec->detail < 0 ? -spec->detail : spec->detail;
            bool update = spec->detail < 0;
            if (operands.count != 2U ||
                !parse_register(operands.values[0], kind >= MEMORY_F32 ? 'f' : 'r', &rd) ||
                !parse_memory_operand(operands.values[1], &immediate, &ra) || (update && ra == 0U) ||
                (update && spec->operation == OP_LOAD && kind < MEMORY_F32 && rd == ra)) return false;
            if (immediate < INT16_MIN || immediate > (int32_t)UINT16_MAX) return false;
            if (ra == 0U) {
                if (!file_printf(output,
                    "    { uint32_t ea = (uint32_t)(int32_t)(int16_t)UINT16_C(0x%04lX); ",
                    (unsigned long)(uint16_t)immediate)) return false;
            } else if (!file_printf(output,
                "    { uint32_t ea = state->gpr[%u] + (uint32_t)(int32_t)(int16_t)UINT16_C(0x%04lX); ",
                ra, (unsigned long)(uint16_t)immediate)) return false;
            if (spec->operation == OP_LOAD) {
                if (kind == MEMORY_U8 && !file_printf(output, "uint32_t value = porpoise_load_u8(state, ea); ")) return false;
                if ((kind == MEMORY_U16 || kind == MEMORY_S16) &&
                    !file_printf(output, "uint32_t value = porpoise_load_u16(state, ea); ")) return false;
                if (kind == MEMORY_U32 && !file_printf(output, "uint32_t value = porpoise_load_u32(state, ea); ")) return false;
                if (kind == MEMORY_F32 && !file_printf(output, "float value = porpoise_load_f32(state, ea); ")) return false;
                if (kind == MEMORY_F64 && !file_printf(output, "double value = porpoise_load_f64(state, ea); ")) return false;
            } else {
                if (kind == MEMORY_U8 && !file_printf(output, "porpoise_store_u8(state, ea, (uint8_t)state->gpr[%u]); ", rd)) return false;
                if ((kind == MEMORY_U16 || kind == MEMORY_S16) && !file_printf(output, "porpoise_store_u16(state, ea, (uint16_t)state->gpr[%u]); ", rd)) return false;
                if (kind == MEMORY_U32 && !file_printf(output, "porpoise_store_u32(state, ea, state->gpr[%u]); ", rd)) return false;
                if (kind == MEMORY_F32 && !file_printf(output, "porpoise_store_f32(state, ea, (float)state->fpr[%u].f64); ", rd)) return false;
                if (kind == MEMORY_F64 && !file_printf(output, "porpoise_store_f64(state, ea, state->fpr[%u].f64); ", rd)) return false;
            }
            if (!file_printf(output, "if (porpoise_state_has_fault(state)) return; ")) return false;
            if (spec->operation == OP_LOAD) {
                if ((kind == MEMORY_U8 || kind == MEMORY_U16 || kind == MEMORY_U32) &&
                    !file_printf(output, "state->gpr[%u] = value; ", rd)) return false;
                if (kind == MEMORY_S16 &&
                    !file_printf(output, "state->gpr[%u] = porpoise_sign_extend16(value); ", rd)) return false;
                if (kind == MEMORY_F32 &&
                    !file_printf(output, "state->fpr[%u].f64 = (double)value; ", rd)) return false;
                if (kind == MEMORY_F64 &&
                    !file_printf(output, "state->fpr[%u].f64 = value; ", rd)) return false;
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
                if (kind == MEMORY_F32 && !file_printf(output, "float value = porpoise_load_f32(state, ea); ")) return false;
                if (kind == MEMORY_F64 && !file_printf(output, "double value = porpoise_load_f64(state, ea); ")) return false;
            } else {
                if (kind == MEMORY_U8 && !file_printf(output, "porpoise_store_u8(state, ea, (uint8_t)state->gpr[%u]); ", rd)) return false;
                if (kind == MEMORY_U16 && !file_printf(output, "porpoise_store_u16(state, ea, (uint16_t)state->gpr[%u]); ", rd)) return false;
                if (kind == MEMORY_U32 && !file_printf(output, "porpoise_store_u32(state, ea, state->gpr[%u]); ", rd)) return false;
                if (kind == MEMORY_F32 && !file_printf(output, "porpoise_store_f32(state, ea, (float)state->fpr[%u].f64); ", rd)) return false;
                if (kind == MEMORY_F64 && !file_printf(output, "porpoise_store_f64(state, ea, state->fpr[%u].f64); ", rd)) return false;
            }
            if (!file_printf(output, "if (porpoise_state_has_fault(state)) return; ")) return false;
            if (spec->operation == OP_INDEXED_LOAD) {
                if ((kind == MEMORY_U8 || kind == MEMORY_U16 || kind == MEMORY_U32) &&
                    !file_printf(output, "state->gpr[%u] = value; ", rd)) return false;
                if (kind == MEMORY_S16 &&
                    !file_printf(output, "state->gpr[%u] = porpoise_sign_extend16(value); ", rd)) return false;
                if (kind == MEMORY_F32 &&
                    !file_printf(output, "state->fpr[%u].f64 = (double)value; ", rd)) return false;
                if (kind == MEMORY_F64 &&
                    !file_printf(output, "state->fpr[%u].f64 = value; ", rd)) return false;
            }
            if (update && !file_printf(output, "state->gpr[%u] = ea; ", ra)) return false;
            return file_printf(output, "}\n");
        }
        case OP_LOAD_MULTIPLE:
        case OP_STORE_MULTIPLE:
            if (operands.count != 2U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_memory_operand(operands.values[1], &immediate, &ra) ||
                immediate < INT16_MIN || immediate > (int32_t)UINT16_MAX ||
                (spec->operation == OP_LOAD_MULTIPLE &&
                 ((ra == 0U && rd == 0U) || (ra != 0U && ra >= rd)))) return false;
            if (ra == 0U) {
                if (!file_printf(output,
                    "    { uint32_t ea = (uint32_t)(int32_t)(int16_t)UINT16_C(0x%04lX); ",
                    (unsigned long)(uint16_t)immediate)) return false;
            } else if (!file_printf(output,
                "    { uint32_t ea = state->gpr[%u] + (uint32_t)(int32_t)(int16_t)UINT16_C(0x%04lX); ",
                ra, (unsigned long)(uint16_t)immediate)) return false;
            if (spec->operation == OP_LOAD_MULTIPLE) {
                if (!file_printf(output,
                    "if (!porpoise_load_multiple_words(state, ea, %uU)) return; }\n",
                    rd)) return false;
            } else if (!file_printf(output,
                "if (!porpoise_store_multiple_words(state, ea, %uU)) return; }\n",
                rd)) return false;
            return true;
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
        case OP_BCTR:
        case OP_BCTRL:
            if (operands.count != 0U) return false;
            if (spec->operation == OP_BCTRL && !file_printf(output, "    state->lr = UINT32_C(0x%08lX);\n",
                                                            (unsigned long)(item->address + 4U))) return false;
            if (!file_printf(output, "    if (!porpoise_call_address(state, state->ctr)) return;\n")) return false;
            if (!file_printf(output, "    if (porpoise_state_has_fault(state)) return;\n")) return false;
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
        case OP_BDNZ:
            if (operands.count != 1U) return false;
            if (!file_printf(output, "    state->ctr -= UINT32_C(1);\n")) return false;
            return emit_conditional_target(output, program, function, abi, operands.values[0],
                                           "state->ctr != UINT32_C(0)", diagnostics, source, item);
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
                        "    porpoise_compare_signed(state, %uU, state->gpr[%u], (uint32_t)(int32_t)(int16_t)UINT16_C(0x%04lX));\n",
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
        case OP_FLOAT_BINARY:
        case OP_PAIRED_BINARY: {
            const char *operator_text = spec->detail == FLOAT_ADD ? "+" : spec->detail == FLOAT_SUB ? "-" :
                                        spec->detail == FLOAT_MUL ? "*" : "/";
            if (operands.count != 3U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) || !parse_register(operands.values[2], 'f', &rb)) return false;
            if (spec->operation == OP_FLOAT_BINARY) {
                bool single = item->mnemonic[strlen(item->mnemonic) - 1U] == 's';
                return file_printf(output, "    state->fpr[%u].f64 = %s(state->fpr[%u].f64 %s state->fpr[%u].f64);\n",
                                   rd, single ? "(double)(float)" : "", ra, operator_text, rb);
            }
            return file_printf(output,
                "    state->fpr[%u].ps[0] = state->fpr[%u].ps[0] %s state->fpr[%u].ps[0];\n"
                "    state->fpr[%u].ps[1] = state->fpr[%u].ps[1] %s state->fpr[%u].ps[1];\n",
                rd, ra, operator_text, rb, rd, ra, operator_text, rb);
        }
        case OP_FLOAT_UNARY:
            if (operands.count != 2U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra)) return false;
            if (spec->detail == FLOAT_MOVE)
                return file_printf(output, "    state->fpr[%u].f64 = state->fpr[%u].f64;\n", rd, ra);
            if (spec->detail == FLOAT_NEG)
                return file_printf(output, "    state->fpr[%u].f64 = -state->fpr[%u].f64;\n", rd, ra);
            if (spec->detail == FLOAT_ABS)
                return file_printf(output, "    state->fpr[%u].f64 = fabs(state->fpr[%u].f64);\n", rd, ra);
            return file_printf(output, "    state->fpr[%u].f64 = -fabs(state->fpr[%u].f64);\n", rd, ra);
        case OP_RECIPROCAL_APPROX:
            if (operands.count != 2U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra)) return false;
            if (spec->detail == 0)
                return file_printf(output, "    state->fpr[%u].f64 = 1.0 / state->fpr[%u].f64; /* approximation */\n", rd, ra);
            return file_printf(output, "    state->fpr[%u].f64 = 1.0 / sqrt(state->fpr[%u].f64); /* approximation */\n", rd, ra);
    }
    return false;
}

int porpoise_lower_function(
    FILE *output,
    const PorpoiseProgram *program,
    const PorpoiseSourceFile *source,
    const PorpoiseFunction *function,
    const PorpoiseAbiManifest *abi,
    const PorpoiseLoweringOptions *options,
    PorpoiseReport *report,
    PorpoiseDiagnostics *diagnostics) {
    size_t index;
    int result = PORPOISE_EXIT_OK;
    if (!file_printf(output, "void porpoise_lifted_%s(PorpoisePpcState *state)\n{\n", function->c_name) ||
        !file_printf(output, "    if (state == NULL || porpoise_state_has_fault(state)) return;\n"))
        return PORPOISE_EXIT_IO;
    for (index = 0U; index < function->item_count; index++) {
        const PorpoiseAsmItem *item = &function->items[index];
        if (item->kind == PORPOISE_ASM_LABEL) {
            char label[PORPOISE_NAME_CAPACITY];
            porpoise_sanitize_identifier(item->label, label, sizeof(label));
            if (!file_printf(output, "porpoise_label_%s:\n    ;\n", label)) return PORPOISE_EXIT_IO;
        } else {
            const OpcodeSpec *spec = find_opcode(item->mnemonic);
            const char *detail = NULL;
            bool emitted;
            if (spec == NULL) {
                if (!porpoise_report_add(report, source->relative_path, item->source_line, item->address,
                                         item->mnemonic, PORPOISE_UNSUPPORTED, false,
                                         "opcode is not in the lowering registry")) return PORPOISE_EXIT_INTERNAL;
                if (!porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR,
                                              source->relative_path, item->source_line,
                                              item->address, "unsupported instruction %s",
                                              item->mnemonic)) return PORPOISE_EXIT_INTERNAL;
                result = PORPOISE_EXIT_TRANSLATION;
                continue;
            }
            if (spec->status == PORPOISE_HOST_NOOP) detail = "documented host-equivalent no-op";
            if (spec->status == PORPOISE_APPROXIMATE) {
                detail = spec->operation == OP_BLR
                    ? "C call-stack return does not dispatch an arbitrary guest LR target"
                    : "host arithmetic does not reproduce hardware estimate semantics";
            }
            if (!porpoise_report_add(report, source->relative_path, item->source_line, item->address,
                                     item->mnemonic, spec->status, spec->semantic_test, detail))
                return PORPOISE_EXIT_INTERNAL;
            if (spec->status == PORPOISE_APPROXIMATE) {
                if (!porpoise_diagnostics_add(
                        diagnostics,
                        options->strict ? PORPOISE_SEVERITY_ERROR : PORPOISE_SEVERITY_WARNING,
                        source->relative_path, item->source_line, item->address,
                        "%s instruction uses approximate host semantics", item->mnemonic))
                    return PORPOISE_EXIT_INTERNAL;
                if (options->strict) {
                    result = PORPOISE_EXIT_TRANSLATION;
                    continue;
                }
            }
            if (!file_printf(output, "    state->pc = UINT32_C(0x%08lX);\n",
                             (unsigned long)item->address)) return PORPOISE_EXIT_IO;
            {
                size_t diagnostic_count = diagnostics->count;
                emitted = emit_instruction(output, spec, program, source, function, item, abi, diagnostics);
                if (!emitted && diagnostics->count == diagnostic_count) {
                    if (!porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR,
                                                  source->relative_path, item->source_line,
                                                  item->address,
                                                  "invalid operands for %s: %s",
                                                  item->mnemonic, item->operands))
                        return PORPOISE_EXIT_INTERNAL;
                }
            }
            if (!emitted) {
                if (ferror(output) != 0) return PORPOISE_EXIT_IO;
                result = PORPOISE_EXIT_TRANSLATION;
            }
        }
    }
    if (!file_printf(output, "}\n")) return PORPOISE_EXIT_IO;
    return result;
}
