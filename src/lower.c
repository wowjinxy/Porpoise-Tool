#include "porpoise/lower.h"
#include "porpoise/system_lower.h"
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
    OP_DIVIDE_WORD,
    OP_MULTIPLY_HIGH,
    OP_CARRY_ARITHMETIC,
    OP_SUBFZE,
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
    OP_RLWNM,
    OP_ROTATE_ALIAS,
    OP_INTEGER_UNARY,
    OP_LOAD,
    OP_STORE,
    OP_INDEXED_LOAD,
    OP_INDEXED_STORE,
    OP_BYTE_REVERSE_STORE,
    OP_PSQ_DFORM,
    OP_PSQ_INDEXED,
    OP_LOAD_MULTIPLE,
    OP_STORE_MULTIPLE,
    OP_B,
    OP_BL,
    OP_BLR,
    OP_BLRL,
    OP_BCTR,
    OP_BCTRL,
    OP_CONDITIONAL_BRANCH,
    OP_CONDITIONAL_RETURN,
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
    OP_FLOAT_COMPARE,
    OP_FLOAT_SELECT,
    OP_FRSP,
    OP_FCTIWZ,
    OP_MFFS,
    OP_MTFSF,
    OP_MTFSB1,
    OP_FLOAT_FMA,
    OP_PAIRED_BINARY,
    OP_PAIRED_TERNARY,
    OP_PAIRED_SCALAR_MADD,
    OP_PAIRED_SCALAR_MULTIPLY,
    OP_PAIRED_SUM,
    OP_PAIRED_MERGE,
    OP_PAIRED_UNARY,
    OP_PAIRED_COMPARE,
    OP_PAIRED_SELECT,
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
    PSQ_STORE = 1,
    PSQ_UPDATE = 2
};

typedef enum RelocationKind {
    RELOCATION_NONE = 0,
    RELOCATION_LOW,
    RELOCATION_HIGH,
    RELOCATION_HIGH_ADJUSTED,
    RELOCATION_SDA21
} RelocationKind;

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
    LOGICAL_NOT,
    LOGICAL_OR_COMPLEMENT
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

enum {
    PAIRED_MADD = 1,
    PAIRED_MSUB,
    PAIRED_NMADD,
    PAIRED_NMSUB
};

enum {
    PAIRED_MERGE_00 = 1,
    PAIRED_MERGE_01,
    PAIRED_MERGE_10,
    PAIRED_MERGE_11
};

enum {
    PAIRED_MOVE = 1,
    PAIRED_NEGATE
};

enum {
    SCALAR_FMA_MADD = 0,
    SCALAR_FMA_MSUB,
    SCALAR_FMA_NMADD,
    SCALAR_FMA_NMSUB,
    SCALAR_FMA_SINGLE = 0x100
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
    {"divw", OP_DIVIDE_WORD, PORPOISE_LOWERED, true, 1},
    {"divw.", OP_DIVIDE_WORD, PORPOISE_LOWERED, true, 1},
    {"divwu", OP_DIVIDE_WORD, PORPOISE_LOWERED, true, 0},
    {"divwu.", OP_DIVIDE_WORD, PORPOISE_LOWERED, true, 0},
    {"mulhw", OP_MULTIPLY_HIGH, PORPOISE_LOWERED, true, 1},
    {"mulhwu", OP_MULTIPLY_HIGH, PORPOISE_LOWERED, true, 0},
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
    {"subfze", OP_SUBFZE, PORPOISE_LOWERED, true, 0},
    {"subfze.", OP_SUBFZE, PORPOISE_LOWERED, true, 0},
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
    {"orc", OP_LOGICAL_COMPLEMENT, PORPOISE_LOWERED, true, LOGICAL_OR_COMPLEMENT},
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
    {"rlwnm", OP_RLWNM, PORPOISE_LOWERED, true, 0},
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
    {"sthbrx", OP_BYTE_REVERSE_STORE, PORPOISE_LOWERED, true, 0},
    {"psq_l", OP_PSQ_DFORM, PORPOISE_APPROXIMATE, true, 0},
    {"psq_lu", OP_PSQ_DFORM, PORPOISE_APPROXIMATE, true, PSQ_UPDATE},
    {"psq_st", OP_PSQ_DFORM, PORPOISE_APPROXIMATE, true, PSQ_STORE},
    {"psq_stu", OP_PSQ_DFORM, PORPOISE_APPROXIMATE, true, PSQ_STORE | PSQ_UPDATE},
    {"psq_lx", OP_PSQ_INDEXED, PORPOISE_APPROXIMATE, true, 0},
    {"psq_lux", OP_PSQ_INDEXED, PORPOISE_APPROXIMATE, true, PSQ_UPDATE},
    {"psq_stx", OP_PSQ_INDEXED, PORPOISE_APPROXIMATE, true, PSQ_STORE},
    {"psq_stux", OP_PSQ_INDEXED, PORPOISE_APPROXIMATE, true, PSQ_STORE | PSQ_UPDATE},
    {"lmw", OP_LOAD_MULTIPLE, PORPOISE_LOWERED, true, 0},
    {"stmw", OP_STORE_MULTIPLE, PORPOISE_LOWERED, true, 0},
    {"b", OP_B, PORPOISE_LOWERED, false, 0},
    {"bl", OP_BL, PORPOISE_LOWERED, true, 0},
    {"blr", OP_BLR, PORPOISE_APPROXIMATE, false, 0},
    {"blrl", OP_BLRL, PORPOISE_LOWERED, true, 0},
    {"bctr", OP_BCTR, PORPOISE_LOWERED, false, 0},
    {"bctrl", OP_BCTRL, PORPOISE_LOWERED, true, 0},
    {"beq", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 2},
    {"beq+", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, true, 2},
    {"bne", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, true, -2},
    {"blt", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 0},
    {"bge", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 0x100},
    {"bgt", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 1},
    {"ble", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 0x101},
    {"ble+", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, true, 0x101},
    {"beqlr", OP_CONDITIONAL_RETURN, PORPOISE_APPROXIMATE, true, 2},
    {"bnelr", OP_CONDITIONAL_RETURN, PORPOISE_APPROXIMATE, true, -2},
    {"bltlr", OP_CONDITIONAL_RETURN, PORPOISE_APPROXIMATE, true, 0},
    {"bgelr", OP_CONDITIONAL_RETURN, PORPOISE_APPROXIMATE, true, 0x100},
    {"bgtlr", OP_CONDITIONAL_RETURN, PORPOISE_APPROXIMATE, true, 1},
    {"blelr", OP_CONDITIONAL_RETURN, PORPOISE_APPROXIMATE, true, 0x101},
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
    {"fadd", OP_FLOAT_BINARY, PORPOISE_APPROXIMATE, true, FLOAT_ADD},
    {"fadds", OP_FLOAT_BINARY, PORPOISE_APPROXIMATE, true, FLOAT_ADD},
    {"fsub", OP_FLOAT_BINARY, PORPOISE_APPROXIMATE, false, FLOAT_SUB},
    {"fsubs", OP_FLOAT_BINARY, PORPOISE_APPROXIMATE, false, FLOAT_SUB},
    {"fmul", OP_FLOAT_BINARY, PORPOISE_APPROXIMATE, false, FLOAT_MUL},
    {"fmuls", OP_FLOAT_BINARY, PORPOISE_APPROXIMATE, false, FLOAT_MUL},
    {"fdiv", OP_FLOAT_BINARY, PORPOISE_APPROXIMATE, false, FLOAT_DIV},
    {"fdivs", OP_FLOAT_BINARY, PORPOISE_APPROXIMATE, false, FLOAT_DIV},
    {"fmr", OP_FLOAT_UNARY, PORPOISE_LOWERED, false, FLOAT_MOVE},
    {"fmr.", OP_FLOAT_UNARY, PORPOISE_LOWERED, true, FLOAT_MOVE},
    {"fneg", OP_FLOAT_UNARY, PORPOISE_LOWERED, false, FLOAT_NEG},
    {"fneg.", OP_FLOAT_UNARY, PORPOISE_LOWERED, true, FLOAT_NEG},
    {"fabs", OP_FLOAT_UNARY, PORPOISE_LOWERED, false, FLOAT_ABS},
    {"fabs.", OP_FLOAT_UNARY, PORPOISE_LOWERED, true, FLOAT_ABS},
    {"fnabs", OP_FLOAT_UNARY, PORPOISE_LOWERED, false, FLOAT_NABS},
    {"fnabs.", OP_FLOAT_UNARY, PORPOISE_LOWERED, true, FLOAT_NABS},
    {"fcmpo", OP_FLOAT_COMPARE, PORPOISE_LOWERED, true, 1},
    {"fcmpu", OP_FLOAT_COMPARE, PORPOISE_LOWERED, true, 0},
    {"fsel", OP_FLOAT_SELECT, PORPOISE_LOWERED, true, 0},
    {"fsel.", OP_FLOAT_SELECT, PORPOISE_LOWERED, true, 0},
    {"frsp", OP_FRSP, PORPOISE_APPROXIMATE, true, 0},
    {"frsp.", OP_FRSP, PORPOISE_APPROXIMATE, true, 0},
    {"fctiwz", OP_FCTIWZ, PORPOISE_LOWERED, true, 0},
    {"fctiwz.", OP_FCTIWZ, PORPOISE_LOWERED, true, 0},
    {"mffs", OP_MFFS, PORPOISE_LOWERED, true, 0},
    {"mffs.", OP_MFFS, PORPOISE_LOWERED, true, 0},
    {"mtfsf", OP_MTFSF, PORPOISE_LOWERED, true, 0},
    {"mtfsf.", OP_MTFSF, PORPOISE_LOWERED, true, 0},
    {"mtfsb1", OP_MTFSB1, PORPOISE_LOWERED, true, 0},
    {"mtfsb1.", OP_MTFSB1, PORPOISE_LOWERED, true, 0},
    {"fmadd", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, true, SCALAR_FMA_MADD},
    {"fmadd.", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, true, SCALAR_FMA_MADD},
    {"fmadds", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, true, SCALAR_FMA_MADD | SCALAR_FMA_SINGLE},
    {"fmadds.", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, true, SCALAR_FMA_MADD | SCALAR_FMA_SINGLE},
    {"fmsub", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, true, SCALAR_FMA_MSUB},
    {"fmsub.", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, false, SCALAR_FMA_MSUB},
    {"fmsubs", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, true, SCALAR_FMA_MSUB | SCALAR_FMA_SINGLE},
    {"fmsubs.", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, false, SCALAR_FMA_MSUB | SCALAR_FMA_SINGLE},
    {"fnmadd", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, true, SCALAR_FMA_NMADD},
    {"fnmadd.", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, false, SCALAR_FMA_NMADD},
    {"fnmadds", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, true, SCALAR_FMA_NMADD | SCALAR_FMA_SINGLE},
    {"fnmadds.", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, false, SCALAR_FMA_NMADD | SCALAR_FMA_SINGLE},
    {"fnmsub", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, true, SCALAR_FMA_NMSUB},
    {"fnmsub.", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, false, SCALAR_FMA_NMSUB},
    {"fnmsubs", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, true, SCALAR_FMA_NMSUB | SCALAR_FMA_SINGLE},
    {"fnmsubs.", OP_FLOAT_FMA, PORPOISE_APPROXIMATE, false, SCALAR_FMA_NMSUB | SCALAR_FMA_SINGLE},
    {"ps_add", OP_PAIRED_BINARY, PORPOISE_APPROXIMATE, true, FLOAT_ADD},
    {"ps_sub", OP_PAIRED_BINARY, PORPOISE_APPROXIMATE, false, FLOAT_SUB},
    {"ps_mul", OP_PAIRED_BINARY, PORPOISE_APPROXIMATE, false, FLOAT_MUL},
    {"ps_div", OP_PAIRED_BINARY, PORPOISE_APPROXIMATE, true, FLOAT_DIV},
    {"ps_madd", OP_PAIRED_TERNARY, PORPOISE_APPROXIMATE, true, PAIRED_MADD},
    {"ps_msub", OP_PAIRED_TERNARY, PORPOISE_APPROXIMATE, true, PAIRED_MSUB},
    {"ps_nmadd", OP_PAIRED_TERNARY, PORPOISE_APPROXIMATE, true, PAIRED_NMADD},
    {"ps_nmsub", OP_PAIRED_TERNARY, PORPOISE_APPROXIMATE, true, PAIRED_NMSUB},
    {"ps_madds0", OP_PAIRED_SCALAR_MADD, PORPOISE_APPROXIMATE, true, 0},
    {"ps_madds1", OP_PAIRED_SCALAR_MADD, PORPOISE_APPROXIMATE, true, 1},
    {"ps_muls0", OP_PAIRED_SCALAR_MULTIPLY, PORPOISE_APPROXIMATE, true, 0},
    {"ps_muls1", OP_PAIRED_SCALAR_MULTIPLY, PORPOISE_APPROXIMATE, true, 1},
    {"ps_sum0", OP_PAIRED_SUM, PORPOISE_APPROXIMATE, true, 0},
    {"ps_sum1", OP_PAIRED_SUM, PORPOISE_APPROXIMATE, true, 1},
    {"ps_merge00", OP_PAIRED_MERGE, PORPOISE_LOWERED, true, PAIRED_MERGE_00},
    {"ps_merge01", OP_PAIRED_MERGE, PORPOISE_LOWERED, true, PAIRED_MERGE_01},
    {"ps_merge10", OP_PAIRED_MERGE, PORPOISE_LOWERED, true, PAIRED_MERGE_10},
    {"ps_merge11", OP_PAIRED_MERGE, PORPOISE_LOWERED, true, PAIRED_MERGE_11},
    {"ps_mr", OP_PAIRED_UNARY, PORPOISE_LOWERED, true, PAIRED_MOVE},
    {"ps_neg", OP_PAIRED_UNARY, PORPOISE_LOWERED, true, PAIRED_NEGATE},
    {"ps_cmpo0", OP_PAIRED_COMPARE, PORPOISE_LOWERED, true, 0},
    {"ps_sel", OP_PAIRED_SELECT, PORPOISE_LOWERED, true, 0},
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
        {
            bool in_quotes = false;
            while (*cursor != '\0') {
                if (in_quotes && *cursor == '\\' &&
                    (cursor[1] == '\\' || cursor[1] == '"')) {
                    cursor += 2;
                    continue;
                }
                if (*cursor == '"') {
                    in_quotes = !in_quotes;
                    cursor++;
                    continue;
                }
                if (!in_quotes && *cursor == ',') break;
                cursor++;
            }
            if (in_quotes) return false;
        }
        end = cursor;
        if (*cursor == ',') *cursor++ = '\0';
        while (end > start && isspace((unsigned char)end[-1])) *--end = '\0';
        if (*start == '\0') return false;
        list->values[list->count++] = start;
    }
    return true;
}

static bool normalize_branch_target(const char *text, char *target, size_t capacity) {
    size_t input_index;
    size_t output_index = 0U;
    size_t length;
    if (text == NULL || target == NULL || capacity == 0U) return false;
    length = strlen(text);
    if (length == 0U) return false;
    if (text[0] != '"') {
        if (strchr(text, '"') != NULL) return false;
        return porpoise_copy_string(target, capacity, text);
    }
    if (length < 2U || text[length - 1U] != '"') return false;
    for (input_index = 1U; input_index + 1U < length; input_index++) {
        char character = text[input_index];
        if (character == '"') return false;
        if (character == '\\' && input_index + 2U < length &&
            (text[input_index + 1U] == '\\' || text[input_index + 1U] == '"')) {
            character = text[++input_index];
        }
        if (output_index + 1U >= capacity) return false;
        target[output_index++] = character;
    }
    if (output_index == 0U) return false;
    target[output_index] = '\0';
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

static bool parse_gqr_register(const char *text, unsigned int *index) {
    return text != NULL && text[0] == 'q' &&
           parse_register(text + 1, 'r', index) && *index < 8U;
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

static bool parse_relocation(const char *text, RelocationKind *kind) {
    const char *at;
    const char *cursor;
    if (text == NULL || kind == NULL) return false;
    at = strrchr(text, '@');
    if (at == NULL || at == text) return false;
    if (strcmp(at, "@l") == 0) *kind = RELOCATION_LOW;
    else if (strcmp(at, "@h") == 0) *kind = RELOCATION_HIGH;
    else if (strcmp(at, "@ha") == 0) *kind = RELOCATION_HIGH_ADJUSTED;
    else if (strcmp(at, "@sda21") == 0) *kind = RELOCATION_SDA21;
    else return false;
    for (cursor = text; cursor < at; cursor++) {
        unsigned char character = (unsigned char)*cursor;
        if (iscntrl(character) || isspace(character) || *cursor == '(' ||
            *cursor == ')' || *cursor == ',') return false;
    }
    return true;
}

static int32_t decode_signed_immediate(uint32_t word) {
    uint32_t bits = word & UINT32_C(0xFFFF);
    return bits <= (uint32_t)INT16_MAX
        ? (int32_t)bits
        : (int32_t)bits - INT32_C(65536);
}

static int32_t decode_psq_displacement(uint32_t word) {
    uint32_t bits = word & UINT32_C(0xFFF);
    return bits <= UINT32_C(0x7FF)
        ? (int32_t)bits
        : (int32_t)bits - INT32_C(4096);
}

static bool parse_signed_or_relocated(
    const char *text,
    uint32_t word,
    unsigned int allowed_relocations,
    int32_t *value,
    RelocationKind *relocation) {
    RelocationKind parsed = RELOCATION_NONE;
    if (parse_signed(text, value)) {
        *relocation = RELOCATION_NONE;
        return true;
    }
    if (!parse_relocation(text, &parsed) ||
        (allowed_relocations & (1U << (unsigned int)parsed)) == 0U) return false;
    *value = decode_signed_immediate(word);
    *relocation = parsed;
    return true;
}

static bool parse_unsigned_or_relocated(
    const char *text,
    uint32_t word,
    unsigned int allowed_relocations,
    uint32_t *value) {
    RelocationKind relocation = RELOCATION_NONE;
    if (parse_unsigned(text, value)) return true;
    if (!parse_relocation(text, &relocation) ||
        (allowed_relocations & (1U << (unsigned int)relocation)) == 0U) return false;
    *value = word & UINT32_C(0xFFFF);
    return true;
}

static bool parse_memory_operand(
    const char *text,
    uint32_t word,
    int32_t *offset,
    unsigned int *base) {
    char copy[128];
    char *open;
    char *close;
    unsigned int encoded_base;
    RelocationKind relocation = RELOCATION_NONE;
    if (!porpoise_copy_string(copy, sizeof(copy), text)) return false;
    open = strchr(copy, '(');
    close = strrchr(copy, ')');
    if (open == NULL || close == NULL || close[1] != '\0' || open >= close) return false;
    *open = '\0';
    *close = '\0';
    if (copy[0] == '\0') {
        *offset = 0;
    } else if (!parse_signed_or_relocated(
                   copy,
                   word,
                   (1U << RELOCATION_LOW) | (1U << RELOCATION_SDA21),
                   offset,
                   &relocation)) {
        return false;
    }
    if (!parse_register(open + 1, 'r', base)) return false;
    encoded_base = (unsigned int)((word >> 16U) & 31U);
    if (relocation == RELOCATION_SDA21) {
        *base = encoded_base;
    } else if (relocation == RELOCATION_LOW && *base != encoded_base) {
        return false;
    }
    return true;
}

static bool parse_psq_memory_operand(
    const char *text,
    int32_t *offset,
    unsigned int *base) {
    char copy[128];
    char *open;
    char *close;

    if (!porpoise_copy_string(copy, sizeof(copy), text)) return false;
    open = strchr(copy, '(');
    close = strrchr(copy, ')');
    if (open == NULL || close == NULL || close[1] != '\0' || open >= close)
        return false;
    *open = '\0';
    *close = '\0';
    if (copy[0] == '\0') {
        *offset = 0;
    } else if (!parse_signed(copy, offset)) {
        return false;
    }
    return parse_register(open + 1, 'r', base);
}

static bool psq_dform_operands_match_word(
    uint32_t word,
    unsigned int expected_opcode,
    unsigned int fpr,
    unsigned int base,
    int32_t displacement,
    unsigned int w,
    unsigned int gqr) {
    return (word >> 26U) == expected_opcode &&
           ((word >> 21U) & 31U) == fpr &&
           ((word >> 16U) & 31U) == base &&
           decode_psq_displacement(word) == displacement &&
           ((word >> 15U) & 1U) == w &&
           ((word >> 12U) & 7U) == gqr;
}

static bool psq_indexed_operands_match_word(
    uint32_t word,
    unsigned int expected_xo,
    unsigned int fpr,
    unsigned int base,
    unsigned int index,
    unsigned int w,
    unsigned int gqr) {
    return (word >> 26U) == 4U &&
           ((word >> 21U) & 31U) == fpr &&
           ((word >> 16U) & 31U) == base &&
           ((word >> 11U) & 31U) == index &&
           ((word >> 10U) & 1U) == w &&
           ((word >> 7U) & 7U) == gqr &&
           (word & UINT32_C(0x7F)) == (uint32_t)(expected_xo << 1U);
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

static bool function_resolve_local_label(
    const PorpoiseFunction *function,
    const char *label_name,
    size_t *instruction_item_index) {
    size_t index;
    for (index = 0U; index < function->item_count; index++) {
        const PorpoiseAsmItem *item = &function->items[index];
        size_t target_index;
        if (item->kind != PORPOISE_ASM_LABEL ||
            strcmp(item->label, label_name) != 0) continue;
        for (target_index = index + 1U;
             target_index < function->item_count;
             target_index++) {
            if (function->items[target_index].kind == PORPOISE_ASM_INSTRUCTION) {
                if (instruction_item_index != NULL) *instruction_item_index = target_index;
                return true;
            }
        }
        return false;
    }
    return false;
}

static bool function_alias_address_seen_before(
    const PorpoiseFunction *function,
    size_t limit,
    uint32_t address) {
    size_t index;
    for (index = 0U; index < limit; index++) {
        if (function->aliases[index].address == address) return true;
    }
    return false;
}

static bool function_has_alias_address(
    const PorpoiseFunction *function,
    uint32_t address) {
    return function_alias_address_seen_before(function, function->alias_count, address);
}

static bool function_item_has_preceding_label(
    const PorpoiseFunction *function,
    size_t item_index) {
    return item_index != 0U &&
           function->items[item_index - 1U].kind == PORPOISE_ASM_LABEL;
}

static bool function_item_needs_entry_label(
    const PorpoiseFunction *function,
    size_t item_index) {
    size_t alias_index;
    const PorpoiseAsmItem *item = &function->items[item_index];
    if (item->kind != PORPOISE_ASM_INSTRUCTION) return false;
    if (function_item_has_preceding_label(function, item_index)) return true;
    for (alias_index = 0U; alias_index < function->alias_count; alias_index++) {
        if (function->aliases[alias_index].instruction_item_index == item_index)
            return true;
    }
    return false;
}

static bool emit_function_entry_dispatch(
    FILE *output,
    const PorpoiseFunction *function) {
    size_t alias_index;
    size_t item_index;
    bool opened = false;
    for (alias_index = 0U; alias_index < function->alias_count; alias_index++) {
        const PorpoiseAddressAlias *alias = &function->aliases[alias_index];
        if (function_alias_address_seen_before(function, alias_index, alias->address)) continue;
        if (!opened) {
            if (!file_printf(output, "    switch (state->pc) {\n")) return false;
            opened = true;
        }
        if (!file_printf(output,
            "        case UINT32_C(0x%08lX): goto porpoise_entry_item_%lu;\n",
            (unsigned long)alias->address,
            (unsigned long)alias->instruction_item_index)) return false;
    }
    for (item_index = 0U; item_index < function->item_count; item_index++) {
        const PorpoiseAsmItem *item = &function->items[item_index];
        if (item->kind != PORPOISE_ASM_INSTRUCTION ||
            !function_item_has_preceding_label(function, item_index) ||
            function_has_alias_address(function, item->address)) continue;
        if (!opened) {
            if (!file_printf(output, "    switch (state->pc) {\n")) return false;
            opened = true;
        }
        if (!file_printf(output,
            "        case UINT32_C(0x%08lX): goto porpoise_entry_item_%lu;\n",
            (unsigned long)item->address, (unsigned long)item_index)) return false;
    }
    if (!opened) return true;
    return file_printf(output, "        default: break;\n    }\n");
}

static bool emit_branch_target(
    FILE *output,
    const PorpoiseProgram *program,
    const PorpoiseFunction *function,
    const PorpoiseAbiManifest *abi,
    const char *target_text,
    bool link,
    PorpoiseDiagnostics *diagnostics,
    const PorpoiseSourceFile *source,
    const PorpoiseAsmItem *item) {
    char target[PORPOISE_NAME_CAPACITY];
    const PorpoiseFunction *callee;
    const PorpoiseAddressAlias *alias;
    const PorpoiseAbiFunction *imported;
    uint32_t target_address;
    size_t target_item_index;
    if (!normalize_branch_target(target_text, target, sizeof(target))) return false;
    if (function_resolve_local_label(function, target, &target_item_index)) {
        if (link) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, source->relative_path,
                                     item->source_line, item->address,
                                     "linked branches to local labels are not yet supported");
            return false;
        }
        return file_printf(output, "    goto porpoise_entry_item_%lu;\n",
                           (unsigned long)target_item_index);
    }
    if (porpoise_program_resolve_symbol(
            program, target, &callee, &alias, &target_address)) {
        if (!file_printf(output,
            "    if (!porpoise_call_address(state, UINT32_C(0x%08lX))) return;\n",
            (unsigned long)target_address)) return false;
        if (link) return file_printf(output, "    if (porpoise_state_should_stop(state)) return;\n");
        return file_printf(output, "    return;\n");
    }
    if (porpoise_program_resolve_unique_label(
            program, target, &callee, &target_address, &target_item_index)) {
        (void)target_item_index;
        if (!file_printf(output,
            "    if (!porpoise_call_address(state, UINT32_C(0x%08lX))) return;\n",
            (unsigned long)target_address)) return false;
        if (link) return file_printf(output, "    if (porpoise_state_should_stop(state)) return;\n");
        return file_printf(output, "    return;\n");
    }
    imported = porpoise_abi_find_import(abi, target);
    if (imported != NULL) {
        char c_name[PORPOISE_NAME_CAPACITY];
        porpoise_sanitize_identifier(imported->symbol, c_name, sizeof(c_name));
        if (!file_printf(output, "    porpoise_import_%s(state);\n", c_name)) return false;
        if (link)
            return file_printf(output, "    if (porpoise_state_should_stop(state)) return;\n");
        return file_printf(output, "    return;\n");
    }
    porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, source->relative_path,
                             item->source_line, item->address,
                             "branch target %s is neither a lifted symbol, unique label, nor declared ABI import",
                             target);
    return false;
}

static bool emit_conditional_target(
    FILE *output,
    const PorpoiseProgram *program,
    const PorpoiseFunction *function,
    const PorpoiseAbiManifest *abi,
    const char *target_text,
    const char *condition,
    PorpoiseDiagnostics *diagnostics,
    const PorpoiseSourceFile *source,
    const PorpoiseAsmItem *item) {
    char target[PORPOISE_NAME_CAPACITY];
    const PorpoiseFunction *callee;
    const PorpoiseAddressAlias *alias;
    const PorpoiseAbiFunction *imported;
    uint32_t target_address;
    size_t target_item_index;
    if (!normalize_branch_target(target_text, target, sizeof(target))) return false;
    if (function_resolve_local_label(function, target, &target_item_index)) {
        return file_printf(output, "    if (%s) goto porpoise_entry_item_%lu;\n",
                           condition, (unsigned long)target_item_index);
    }
    if (porpoise_program_resolve_symbol(
            program, target, &callee, &alias, &target_address)) {
        return file_printf(output,
            "    if (%s) { (void)porpoise_call_address(state, UINT32_C(0x%08lX)); return; }\n",
            condition, (unsigned long)target_address);
    }
    if (porpoise_program_resolve_unique_label(
            program, target, &callee, &target_address, &target_item_index)) {
        (void)target_item_index;
        return file_printf(output,
            "    if (%s) { (void)porpoise_call_address(state, UINT32_C(0x%08lX)); return; }\n",
            condition, (unsigned long)target_address);
    }
    imported = porpoise_abi_find_import(abi, target);
    if (imported != NULL) {
        char c_name[PORPOISE_NAME_CAPACITY];
        porpoise_sanitize_identifier(imported->symbol, c_name, sizeof(c_name));
        return file_printf(output, "    if (%s) { porpoise_import_%s(state); return; }\n",
                           condition, c_name);
    }
    porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, source->relative_path,
                             item->source_line, item->address,
                             "branch target %s is neither a local label, lifted symbol, unique label, nor ABI import",
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
    unsigned int rd, ra, rb, rc;
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
        case OP_LIS: {
            RelocationKind relocation;
            unsigned int allowed_relocations = spec->operation == OP_LIS
                ? (1U << RELOCATION_HIGH) | (1U << RELOCATION_HIGH_ADJUSTED)
                : (1U << RELOCATION_LOW) | (1U << RELOCATION_SDA21);
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
            if (relocation == RELOCATION_SDA21) {
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
            RelocationKind relocation;
            unsigned int allowed_relocations = spec->operation == OP_ADDIS
                ? (1U << RELOCATION_HIGH) | (1U << RELOCATION_HIGH_ADJUSTED)
                : (1U << RELOCATION_LOW) | (1U << RELOCATION_SDA21);
            if (operands.count != 3U || !parse_register(operands.values[0], 'r', &rd) ||
                !parse_register(operands.values[1], 'r', &ra) ||
                !parse_signed_or_relocated(operands.values[2], item->word,
                                           allowed_relocations, &immediate,
                                           &relocation) ||
                immediate < INT16_MIN || immediate > (int32_t)UINT16_MAX) return false;
            if (relocation == RELOCATION_SDA21) ra = (item->word >> 16U) & 31U;
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
                RelocationKind relocation = RELOCATION_NONE;
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
                            1U << RELOCATION_LOW, &immediate,
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
                    spec->operation == OP_ORI ? 1U << RELOCATION_LOW : 0U,
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
                if (kind == MEMORY_U8 && !file_printf(output, "porpoise_store_u8(state, ea, (uint8_t)state->gpr[%u]); ", rd)) return false;
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
                "if (!porpoise_psq_%s(state, %uU, ea, %uU, %uU, 1U)) return; ",
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
                "if (!porpoise_psq_%s(state, %uU, ea, %luU, %uU, 0U)) return; ",
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
        case OP_FLOAT_BINARY:
        case OP_PAIRED_BINARY: {
            const char *operator_text = spec->detail == FLOAT_ADD ? "+" : spec->detail == FLOAT_SUB ? "-" :
                                        spec->detail == FLOAT_MUL ? "*" : "/";
            if (operands.count != 3U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) || !parse_register(operands.values[2], 'f', &rb)) return false;
            if (spec->operation == OP_FLOAT_BINARY) {
                bool single = item->mnemonic[strlen(item->mnemonic) - 1U] == 's';
                if (single) {
                    return file_printf(output,
                        "    { double result = (double)(float)(porpoise_fpr_get_f64(state, %uU, 0U) %s porpoise_fpr_get_f64(state, %uU, 0U)); "
                        "porpoise_fpr_set_f64(state, %uU, 0U, result); porpoise_fpr_set_f64(state, %uU, 1U, result); }\n",
                        ra, operator_text, rb, rd, rd);
                }
                return file_printf(output,
                    "    porpoise_fpr_set_f64(state, %uU, 0U, porpoise_fpr_get_f64(state, %uU, 0U) %s porpoise_fpr_get_f64(state, %uU, 0U));\n",
                    rd, ra, operator_text, rb);
            }
            return file_printf(output,
                "    porpoise_fpr_set_f64(state, %uU, 0U, (double)(float)(porpoise_fpr_get_f64(state, %uU, 0U) %s porpoise_fpr_get_f64(state, %uU, 0U)));\n"
                "    porpoise_fpr_set_f64(state, %uU, 1U, (double)(float)(porpoise_fpr_get_f64(state, %uU, 1U) %s porpoise_fpr_get_f64(state, %uU, 1U)));\n",
                rd, ra, operator_text, rb, rd, ra, operator_text, rb);
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
            return file_printf(output,
                "    { double a0 = porpoise_fpr_get_f64(state, %uU, 0U); double a1 = porpoise_fpr_get_f64(state, %uU, 1U); "
                "double c0 = porpoise_fpr_get_f64(state, %uU, 0U); double c1 = porpoise_fpr_get_f64(state, %uU, 1U); "
                "double b0 = porpoise_fpr_get_f64(state, %uU, 0U); double b1 = porpoise_fpr_get_f64(state, %uU, 1U); "
                "double result0 = (double)(float)(a0 * c0 %s b0); double result1 = (double)(float)(a1 * c1 %s b1); "
                "%sporpoise_fpr_set_f64(state, %uU, 0U, result0); porpoise_fpr_set_f64(state, %uU, 1U, result1); }\n",
                ra, ra, rc, rc, rb, rb, operator_text, operator_text, negate_text, rd, rd);
        }
        case OP_PAIRED_SCALAR_MADD:
            if (operands.count != 4U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) ||
                !parse_register(operands.values[2], 'f', &rc) ||
                !parse_register(operands.values[3], 'f', &rb)) return false;
            return file_printf(output,
                "    { double a0 = porpoise_fpr_get_f64(state, %uU, 0U); double a1 = porpoise_fpr_get_f64(state, %uU, 1U); "
                "double scalar = porpoise_fpr_get_f64(state, %uU, %dU); "
                "double b0 = porpoise_fpr_get_f64(state, %uU, 0U); double b1 = porpoise_fpr_get_f64(state, %uU, 1U); "
                "double result0 = (double)(float)(a0 * scalar + b0); double result1 = (double)(float)(a1 * scalar + b1); "
                "porpoise_fpr_set_f64(state, %uU, 0U, result0); porpoise_fpr_set_f64(state, %uU, 1U, result1); }\n",
                ra, ra, rc, spec->detail, rb, rb, rd, rd);
        case OP_PAIRED_SCALAR_MULTIPLY:
            if (operands.count != 3U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) ||
                !parse_register(operands.values[2], 'f', &rc)) return false;
            return file_printf(output,
                "    { double a0 = porpoise_fpr_get_f64(state, %uU, 0U); double a1 = porpoise_fpr_get_f64(state, %uU, 1U); "
                "double scalar = porpoise_fpr_get_f64(state, %uU, %dU); "
                "porpoise_fpr_set_f64(state, %uU, 0U, (double)(float)(a0 * scalar)); "
                "porpoise_fpr_set_f64(state, %uU, 1U, (double)(float)(a1 * scalar)); }\n",
                ra, ra, rc, spec->detail, rd, rd);
        case OP_PAIRED_SUM:
            if (operands.count != 4U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) ||
                !parse_register(operands.values[2], 'f', &rc) ||
                !parse_register(operands.values[3], 'f', &rb)) return false;
            if (spec->detail == 0) {
                return file_printf(output,
                    "    { double sum = (double)(float)(porpoise_fpr_get_f64(state, %uU, 0U) + porpoise_fpr_get_f64(state, %uU, 1U)); "
                    "double passthrough = (double)(float)porpoise_fpr_get_f64(state, %uU, 1U); "
                    "porpoise_fpr_set_f64(state, %uU, 0U, sum); porpoise_fpr_set_f64(state, %uU, 1U, passthrough); }\n",
                    ra, rb, rc, rd, rd);
            }
            return file_printf(output,
                "    { double sum = (double)(float)(porpoise_fpr_get_f64(state, %uU, 0U) + porpoise_fpr_get_f64(state, %uU, 1U)); "
                "double passthrough = (double)(float)porpoise_fpr_get_f64(state, %uU, 0U); "
                "porpoise_fpr_set_f64(state, %uU, 0U, passthrough); porpoise_fpr_set_f64(state, %uU, 1U, sum); }\n",
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
        case OP_FCTIWZ:
            if (operands.count != 2U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra)) return false;
            return file_printf(output,
                "    if (!porpoise_fctiwz(state, %uU, %uU, %d)) return;\n",
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
            const char *operation_name = operation == SCALAR_FMA_MADD ? "MADD" :
                                         operation == SCALAR_FMA_MSUB ? "MSUB" :
                                         operation == SCALAR_FMA_NMADD ? "NMADD" : "NMSUB";
            const char *precision_name = (spec->detail & SCALAR_FMA_SINGLE) != 0
                ? "SINGLE" : "DOUBLE";
            if (operands.count != 4U || !parse_register(operands.values[0], 'f', &rd) ||
                !parse_register(operands.values[1], 'f', &ra) ||
                !parse_register(operands.values[2], 'f', &rc) ||
                !parse_register(operands.values[3], 'f', &rb)) return false;
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
                "    porpoise_fpr_set_f64(state, %uU, 0U, 1.0 / sqrt(porpoise_fpr_get_f64(state, %uU, 0U))); /* approximation */\n",
                rd, ra);
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
        !file_printf(output, "    if (porpoise_state_should_stop(state)) return;\n"))
        return PORPOISE_EXIT_IO;
    if (!emit_function_entry_dispatch(output, function)) return PORPOISE_EXIT_IO;
    for (index = 0U; index < function->item_count; index++) {
        const PorpoiseAsmItem *item = &function->items[index];
        if (item->kind == PORPOISE_ASM_LABEL) {
            continue;
        } else {
            const OpcodeSpec *spec = find_opcode(item->mnemonic);
            const char *detail = NULL;
            bool emitted;
            if (function_item_needs_entry_label(function, index) &&
                !file_printf(output, "porpoise_entry_item_%lu:\n    ;\n", (unsigned long)index))
                return PORPOISE_EXIT_IO;
            if (spec == NULL) {
                PorpoiseSystemInstruction system_instruction;
                PorpoiseSystemResolveResult system_result =
                    porpoise_system_resolve(
                        item->mnemonic,
                        item->operands,
                        item->word,
                        &system_instruction);

                if (system_result == PORPOISE_SYSTEM_NOT_RECOGNIZED) {
                    if (!porpoise_report_add(
                            report,
                            source->relative_path,
                            item->source_line,
                            item->address,
                            item->mnemonic,
                            PORPOISE_UNSUPPORTED,
                            false,
                            "opcode is not in the lowering registry")) {
                        return PORPOISE_EXIT_INTERNAL;
                    }
                    if (!porpoise_diagnostics_add(
                            diagnostics,
                            PORPOISE_SEVERITY_ERROR,
                            source->relative_path,
                            item->source_line,
                            item->address,
                            "unsupported instruction %s",
                            item->mnemonic)) {
                        return PORPOISE_EXIT_INTERNAL;
                    }
                    result = PORPOISE_EXIT_TRANSLATION;
                    continue;
                }
                if (system_result == PORPOISE_SYSTEM_INVALID) {
                    if (!porpoise_report_add(
                            report,
                            source->relative_path,
                            item->source_line,
                            item->address,
                            item->mnemonic,
                            PORPOISE_UNSUPPORTED,
                            false,
                            system_instruction.detail)) {
                        return PORPOISE_EXIT_INTERNAL;
                    }
                    if (!porpoise_diagnostics_add(
                            diagnostics,
                            PORPOISE_SEVERITY_ERROR,
                            source->relative_path,
                            item->source_line,
                            item->address,
                            "invalid operands or annotated word for %s: %s",
                            item->mnemonic,
                            item->operands)) {
                        return PORPOISE_EXIT_INTERNAL;
                    }
                    result = PORPOISE_EXIT_TRANSLATION;
                    continue;
                }
                if (!porpoise_report_add(
                        report,
                        source->relative_path,
                        item->source_line,
                        item->address,
                        item->mnemonic,
                        system_instruction.status,
                        system_instruction.semantic_test,
                        system_instruction.detail)) {
                    return PORPOISE_EXIT_INTERNAL;
                }
                if (system_instruction.status == PORPOISE_UNSUPPORTED) {
                    if (!porpoise_diagnostics_add(
                            diagnostics,
                            PORPOISE_SEVERITY_ERROR,
                            source->relative_path,
                            item->source_line,
                            item->address,
                            "unsupported instruction %s: %s",
                            item->mnemonic,
                            system_instruction.detail)) {
                        return PORPOISE_EXIT_INTERNAL;
                    }
                    result = PORPOISE_EXIT_TRANSLATION;
                    continue;
                }
                if (system_instruction.status == PORPOISE_APPROXIMATE) {
                    if (!porpoise_diagnostics_add(
                            diagnostics,
                            options->strict
                                ? PORPOISE_SEVERITY_ERROR
                                : PORPOISE_SEVERITY_WARNING,
                            source->relative_path,
                            item->source_line,
                            item->address,
                            "%s instruction uses approximate host semantics",
                            item->mnemonic)) {
                        return PORPOISE_EXIT_INTERNAL;
                    }
                    if (options->strict) {
                        result = PORPOISE_EXIT_TRANSLATION;
                        continue;
                    }
                }
                if (!file_printf(
                        output,
                        "    state->pc = UINT32_C(0x%08lX);\n",
                        (unsigned long)item->address)) {
                    return PORPOISE_EXIT_IO;
                }
                if (!porpoise_system_emit(
                        output,
                        &system_instruction,
                        item->address)) {
                    if (ferror(output) != 0) return PORPOISE_EXIT_IO;
                    if (!porpoise_diagnostics_add(
                            diagnostics,
                            PORPOISE_SEVERITY_ERROR,
                            source->relative_path,
                            item->source_line,
                            item->address,
                            "internal system-lowering failure for %s",
                            item->mnemonic)) {
                        return PORPOISE_EXIT_INTERNAL;
                    }
                    return PORPOISE_EXIT_INTERNAL;
                }
                continue;
            }
            if (spec->status == PORPOISE_HOST_NOOP) detail = "documented host-equivalent no-op";
            if (spec->status == PORPOISE_APPROXIMATE) {
                if (spec->operation == OP_BLR) {
                    detail = "C call-stack return does not dispatch an arbitrary guest LR target";
                } else if (spec->operation == OP_CONDITIONAL_RETURN) {
                    detail = "taken LR branch returns through the C call stack instead of dispatching the arbitrary guest LR target";
                } else if (spec->operation == OP_FRSP) {
                    detail = "runtime duplicates lane 0 into architecturally undefined destination lane 1 for deterministic compatibility";
                } else if (spec->operation == OP_FLOAT_FMA) {
                    detail = "finite arithmetic uses host C99 fma and does not reproduce all PPC rounding and exception semantics";
                } else if (spec->operation == OP_PSQ_DFORM ||
                           spec->operation == OP_PSQ_INDEXED) {
                    detail = "runtime models GQR quantization deterministically but does not reproduce all Gekko NI behavior or FPSCR and floating-point exception side effects";
                } else if (spec->operation == OP_FLOAT_BINARY) {
                    detail = "host arithmetic does not reproduce all PPC floating-point rounding, exception, and status semantics";
                } else if (spec->operation == OP_PAIRED_TERNARY ||
                           spec->operation == OP_PAIRED_SCALAR_MADD) {
                    detail = "host arithmetic does not reproduce PPC paired-single Force25, fused rounding, exception, and FPSCR semantics";
                } else if ((spec->operation == OP_PAIRED_BINARY && spec->detail == FLOAT_MUL) ||
                           spec->operation == OP_PAIRED_SCALAR_MULTIPLY) {
                    detail = "host arithmetic does not reproduce PPC paired-single Force25, rounding, exception, and FPSCR semantics";
                } else if (spec->operation == OP_PAIRED_BINARY ||
                           spec->operation == OP_PAIRED_SUM) {
                    detail = "host arithmetic does not reproduce PPC paired-single rounding, exception, and FPSCR semantics";
                } else {
                    detail = "host arithmetic does not reproduce hardware estimate semantics";
                }
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
