#include "porpoise/lower.h"
#include "lower_internal.h"
#include "porpoise/raw_word.h"
#include "porpoise/relocation.h"
#include "porpoise/system_lower.h"
#include "porpoise/util.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

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
    {"eqv", OP_LOGICAL_COMPLEMENT, PORPOISE_LOWERED, true, LOGICAL_EQUIVALENT},
    {"eqv.", OP_LOGICAL_COMPLEMENT, PORPOISE_LOWERED, false, LOGICAL_EQUIVALENT},
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
    {"rotlw", OP_ROTLW, PORPOISE_LOWERED, true, 0},
    {"rotlw.", OP_ROTLW, PORPOISE_LOWERED, false, 0},
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
    {"stfiwx", OP_INDEXED_STORE, PORPOISE_LOWERED, true, MEMORY_FPR_U32},
    {"lhbrx", OP_BYTE_REVERSE_LOAD, PORPOISE_LOWERED, true, 0},
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
    {"bla", OP_BL, PORPOISE_LOWERED, true, 0},
    {"blr", OP_BLR, PORPOISE_LOWERED, false, 0},
    {"blrl", OP_BLRL, PORPOISE_LOWERED, true, 0},
    {"bctr", OP_BCTR, PORPOISE_LOWERED, false, 0},
    {"bctrl", OP_BCTRL, PORPOISE_LOWERED, true, 0},
    {"beq", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 2},
    {"beq+", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, true, 2},
    {"bne", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, true, -2},
    {"bne+", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, true, -2},
    {"blt", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 0},
    {"bge", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 0x100},
    {"bgt", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 1},
    {"ble", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, false, 0x101},
    {"ble+", OP_CONDITIONAL_BRANCH, PORPOISE_LOWERED, true, 0x101},
    {"beqlr", OP_CONDITIONAL_RETURN, PORPOISE_LOWERED, true, 2},
    {"bnelr", OP_CONDITIONAL_RETURN, PORPOISE_LOWERED, true, -2},
    {"bltlr", OP_CONDITIONAL_RETURN, PORPOISE_LOWERED, true, 0},
    {"bgelr", OP_CONDITIONAL_RETURN, PORPOISE_LOWERED, true, 0x100},
    {"bgtlr", OP_CONDITIONAL_RETURN, PORPOISE_LOWERED, true, 1},
    {"blelr", OP_CONDITIONAL_RETURN, PORPOISE_LOWERED, true, 0x101},
    {"bdnz", OP_BDNZ, PORPOISE_LOWERED, false, 0},
    {"bdz", OP_BDZ, PORPOISE_LOWERED, true, 0},
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
    {"fctiw", OP_FCTIW, PORPOISE_LOWERED, true, 0},
    {"fctiw.", OP_FCTIW, PORPOISE_LOWERED, true, 0},
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
    {"ps_madds0", OP_PAIRED_SCALAR_MADD, PORPOISE_LOWERED, true, 0},
    {"ps_madds1", OP_PAIRED_SCALAR_MADD, PORPOISE_LOWERED, true, 1},
    {"ps_muls0", OP_PAIRED_SCALAR_MULTIPLY, PORPOISE_LOWERED, true, 0},
    {"ps_muls1", OP_PAIRED_SCALAR_MULTIPLY, PORPOISE_LOWERED, true, 1},
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
    {"frsqrte", OP_RECIPROCAL_APPROX, PORPOISE_LOWERED, true, 1},
    {"frsqrte.", OP_RECIPROCAL_APPROX, PORPOISE_LOWERED, true, 1}
};

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

bool parse_register(const char *text, char prefix, unsigned int *index) {
    char *end;
    unsigned long value;
    if (text == NULL || text[0] != prefix || !isdigit((unsigned char)text[1])) return false;
    errno = 0;
    value = strtoul(text + 1, &end, 10);
    if (errno != 0 || *end != '\0' || value > 31UL) return false;
    *index = (unsigned int)value;
    return true;
}

bool parse_gqr_register(const char *text, unsigned int *index) {
    return text != NULL && text[0] == 'q' &&
           parse_register(text + 1, 'r', index) && *index < 8U;
}

bool parse_unsigned(const char *text, uint32_t *value) {
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

bool parse_signed(const char *text, int32_t *value) {
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

bool parse_signed_or_relocated(
    const char *text,
    uint32_t word,
    unsigned int allowed_relocations,
    int32_t *value,
    PorpoiseRelocationKind *relocation) {
    PorpoiseRelocation parsed;
    if (parse_signed(text, value)) {
        *relocation = PORPOISE_RELOCATION_NONE;
        return true;
    }
    if (!porpoise_relocation_parse(text, &parsed) ||
        (allowed_relocations & PORPOISE_RELOCATION_MASK(parsed.kind)) == 0U)
        return false;
    *value = decode_signed_immediate(word);
    *relocation = parsed.kind;
    return true;
}

bool parse_unsigned_or_relocated(
    const char *text,
    uint32_t word,
    unsigned int allowed_relocations,
    uint32_t *value) {
    PorpoiseRelocation relocation;
    if (parse_unsigned(text, value)) return true;
    if (!porpoise_relocation_parse(text, &relocation) ||
        (allowed_relocations & PORPOISE_RELOCATION_MASK(relocation.kind)) == 0U)
        return false;
    *value = word & UINT32_C(0xFFFF);
    return true;
}

bool parse_memory_operand(
    const char *text,
    uint32_t word,
    int32_t *offset,
    unsigned int *base) {
    char copy[128];
    char *open;
    char *close;
    unsigned int encoded_base;
    PorpoiseRelocationKind relocation = PORPOISE_RELOCATION_NONE;
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
                   PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_LOW) |
                       PORPOISE_RELOCATION_MASK(PORPOISE_RELOCATION_SDA21),
                   offset,
                   &relocation)) {
        return false;
    }
    if (!parse_register(open + 1, 'r', base)) return false;
    encoded_base = (unsigned int)((word >> 16U) & 31U);
    if (relocation == PORPOISE_RELOCATION_SDA21) {
        *base = encoded_base;
    } else if (relocation == PORPOISE_RELOCATION_LOW &&
               *base != encoded_base) {
        return false;
    }
    return true;
}

bool parse_psq_memory_operand(
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

bool psq_dform_operands_match_word(
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

bool psq_indexed_operands_match_word(
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

bool parse_cr_bit(const char *text, unsigned int *bit_index) {
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

bool file_printf(FILE *output, const char *format, ...) {
    int result;
    va_list arguments;
    va_start(arguments, format);
    result = vfprintf(output, format, arguments);
    va_end(arguments);
    return result >= 0;
}

static bool file_write_c_string_literal(FILE *output, const char *value) {
    const unsigned char *cursor =
        (const unsigned char *)(value != NULL ? value : "");

    if (fputc('"', output) == EOF) return false;
    while (*cursor != '\0') {
        if (*cursor == '"' || *cursor == '\\') {
            if (fputc('\\', output) == EOF ||
                fputc((int)*cursor, output) == EOF) return false;
        } else if (*cursor == '\n') {
            if (fputs("\\n", output) == EOF) return false;
        } else if (*cursor == '\r') {
            if (fputs("\\r", output) == EOF) return false;
        } else if (*cursor == '\t') {
            if (fputs("\\t", output) == EOF) return false;
        } else if (*cursor >= 0x20U && *cursor < 0x7FU) {
            if (fputc((int)*cursor, output) == EOF) return false;
        } else if (fprintf(output, "\\%03o", (unsigned int)*cursor) < 0) {
            return false;
        }
        cursor++;
    }
    return fputc('"', output) != EOF;
}

static bool emit_direct_import_call(
    FILE *output,
    const PorpoiseAbiFunction *imported,
    uint32_t call_site,
    const char *indent) {
    char c_name[PORPOISE_NAME_CAPACITY];

    porpoise_sanitize_identifier(imported->symbol, c_name, sizeof(c_name));
    if (!file_printf(
            output,
            "%sporpoise_trace_call_enter(state, UINT32_C(0x%08lX), "
            "\"imported\", ",
            indent,
            (unsigned long)call_site) ||
        !file_write_c_string_literal(output, imported->symbol) ||
        !file_printf(
            output,
            ");\n%sporpoise_import_%s(state);\n"
            "%sporpoise_trace_call_exit(state, UINT32_C(0x%08lX), "
            "\"imported\", ",
            indent,
            c_name,
            indent,
            (unsigned long)call_site) ||
        !file_write_c_string_literal(output, imported->symbol)) {
        return false;
    }
    return file_printf(output, ");\n");
}

static bool opcode_records(const char *mnemonic) {
    size_t length = strlen(mnemonic);
    return length != 0U && mnemonic[length - 1U] == '.';
}

bool emit_record_update(FILE *output, bool record, unsigned int destination) {
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

static bool function_item_is_address_taken(
    const PorpoiseFunction *function,
    size_t item_index) {
    size_t entry_index;

    for (entry_index = 0U;
         entry_index < function->address_taken_entry_count;
         entry_index++) {
        if (function->address_taken_entries[
                entry_index].instruction_item_index == item_index) {
            return true;
        }
    }
    return false;
}

static bool function_item_needs_entry_label(
    const PorpoiseFunction *function,
    size_t item_index) {
    size_t alias_index;
    const PorpoiseAsmItem *item = &function->items[item_index];
    if (item->kind != PORPOISE_ASM_INSTRUCTION) return false;
    if (function_item_has_preceding_label(function, item_index) ||
        function_item_is_address_taken(function, item_index)) return true;
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
            !function_item_needs_entry_label(function, item_index) ||
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

bool emit_branch_target(
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
    if (parse_unsigned(target, &target_address)) {
        if ((target_address & UINT32_C(3)) != 0U) return false;
        if (!file_printf(output,
            "    if (!%s(state, UINT32_C(0x%08lX))) return;\n",
            link ? "porpoise_call_address" : "porpoise_branch_address",
            (unsigned long)target_address)) return false;
        if (link)
            return file_printf(output, "    if (porpoise_state_should_stop(state)) return;\n");
        return file_printf(output, "    return;\n");
    }
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
    if (porpoise_program_resolve_symbol_scoped(
            program, source, function, function->section, target,
            &callee, &alias, &target_address)) {
        if (!file_printf(output,
            "    if (!%s(state, UINT32_C(0x%08lX))) return;\n",
            link ? "porpoise_call_address" : "porpoise_branch_address",
            (unsigned long)target_address)) return false;
        if (link) return file_printf(output, "    if (porpoise_state_should_stop(state)) return;\n");
        return file_printf(output, "    return;\n");
    }
    if (porpoise_program_resolve_unique_label(
            program, target, &callee, &target_address, &target_item_index)) {
        (void)target_item_index;
        if (!file_printf(output,
            "    if (!%s(state, UINT32_C(0x%08lX))) return;\n",
            link ? "porpoise_call_address" : "porpoise_branch_address",
            (unsigned long)target_address)) return false;
        if (link) return file_printf(output, "    if (porpoise_state_should_stop(state)) return;\n");
        return file_printf(output, "    return;\n");
    }
    imported = porpoise_abi_find_import(abi, target);
    if (imported != NULL) {
        if (!emit_direct_import_call(
                output, imported, item->address, "    ")) return false;
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

bool emit_conditional_target(
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
    if (parse_unsigned(target, &target_address)) {
        if ((target_address & UINT32_C(3)) != 0U) return false;
        return file_printf(output,
            "    if (%s) { (void)porpoise_branch_address(state, UINT32_C(0x%08lX)); return; }\n",
            condition, (unsigned long)target_address);
    }
    if (function_resolve_local_label(function, target, &target_item_index)) {
        return file_printf(output, "    if (%s) goto porpoise_entry_item_%lu;\n",
                           condition, (unsigned long)target_item_index);
    }
    if (porpoise_program_resolve_symbol_scoped(
            program, source, function, function->section, target,
            &callee, &alias, &target_address)) {
        return file_printf(output,
            "    if (%s) { (void)porpoise_branch_address(state, UINT32_C(0x%08lX)); return; }\n",
            condition, (unsigned long)target_address);
    }
    if (porpoise_program_resolve_unique_label(
            program, target, &callee, &target_address, &target_item_index)) {
        (void)target_item_index;
        return file_printf(output,
            "    if (%s) { (void)porpoise_branch_address(state, UINT32_C(0x%08lX)); return; }\n",
            condition, (unsigned long)target_address);
    }
    imported = porpoise_abi_find_import(abi, target);
    if (imported != NULL) {
        if (!file_printf(output, "    if (%s) {\n", condition) ||
            !emit_direct_import_call(
                output, imported, item->address, "        ")) return false;
        return file_printf(output, "        return;\n    }\n");
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
    PorpoiseLowerInstructionContext context;

    context.output = output;
    context.spec = spec;
    context.program = program;
    context.source = source;
    context.function = function;
    context.item = item;
    context.abi = abi;
    context.diagnostics = diagnostics;
    context.record = opcode_records(item->mnemonic);
    if (!split_operands(item->operands, &context.operands)) return false;

    if ((spec->operation >= OP_NOP &&
         spec->operation <= OP_INTEGER_UNARY) ||
        spec->operation == OP_HOST_NOOP) {
        return porpoise_lower_emit_integer(&context);
    }
    if (spec->operation >= OP_LOAD &&
        spec->operation <= OP_STORE_MULTIPLE) {
        return porpoise_lower_emit_memory(&context);
    }
    if (spec->operation >= OP_B &&
        spec->operation <= OP_COMPARE) {
        return porpoise_lower_emit_branch(&context);
    }
    if ((spec->operation >= OP_FLOAT_BINARY &&
         spec->operation <= OP_PAIRED_SELECT) ||
        spec->operation == OP_RECIPROCAL_APPROX) {
        return porpoise_lower_emit_float(&context);
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
                PorpoiseRawWordInstruction raw_instruction;
                PorpoiseRawWordResolveResult raw_result =
                    porpoise_raw_word_resolve(
                        item->mnemonic,
                        item->operands,
                        item->word,
                        &raw_instruction);

                if (raw_result == PORPOISE_RAW_WORD_INVALID) {
                    if (!porpoise_report_add(
                            report,
                            source->relative_path,
                            item->source_line,
                            item->address,
                            item->mnemonic,
                            PORPOISE_UNSUPPORTED,
                            false,
                            "raw directive operand does not match the annotated word")) {
                        return PORPOISE_EXIT_INTERNAL;
                    }
                    if (!porpoise_diagnostics_add(
                            diagnostics,
                            PORPOISE_SEVERITY_ERROR,
                            source->relative_path,
                            item->source_line,
                            item->address,
                            "invalid raw-word directive %s: %s",
                            item->mnemonic,
                            item->operands)) {
                        return PORPOISE_EXIT_INTERNAL;
                    }
                    result = PORPOISE_EXIT_TRANSLATION;
                    continue;
                }
                if (raw_result == PORPOISE_RAW_WORD_UNSUPPORTED) {
                    if (!porpoise_report_add(
                            report,
                            source->relative_path,
                            item->source_line,
                            item->address,
                            item->mnemonic,
                            PORPOISE_UNSUPPORTED,
                            false,
                            "raw word is not a characterized supported encoding")) {
                        return PORPOISE_EXIT_INTERNAL;
                    }
                    if (!porpoise_diagnostics_add(
                            diagnostics,
                            PORPOISE_SEVERITY_ERROR,
                            source->relative_path,
                            item->source_line,
                            item->address,
                            "unsupported raw-word directive %s: %s",
                            item->mnemonic,
                            item->operands)) {
                        return PORPOISE_EXIT_INTERNAL;
                    }
                    result = PORPOISE_EXIT_TRANSLATION;
                    continue;
                }
                if (raw_result == PORPOISE_RAW_WORD_RESOLVED) {
                    if (!porpoise_report_add(
                            report,
                            source->relative_path,
                            item->source_line,
                            item->address,
                            item->mnemonic,
                            raw_instruction.status,
                            raw_instruction.semantic_test,
                            raw_instruction.detail)) {
                        return PORPOISE_EXIT_INTERNAL;
                    }
                    if (raw_instruction.status == PORPOISE_APPROXIMATE) {
                        if (!porpoise_diagnostics_add(
                                diagnostics,
                                options->strict
                                    ? PORPOISE_SEVERITY_ERROR
                                    : PORPOISE_SEVERITY_WARNING,
                                source->relative_path,
                                item->source_line,
                                item->address,
                                "%s directive uses approximate host semantics",
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
                    if (raw_instruction.status == PORPOISE_APPROXIMATE &&
                        !file_printf(
                            output,
                            "    porpoise_trace_approximate(state, "
                            "UINT32_C(0x%08lX), \"%s\");\n"
                            "    if (porpoise_state_has_fault(state)) return;\n",
                            (unsigned long)item->address,
                            item->mnemonic)) {
                        return PORPOISE_EXIT_IO;
                    }
                    if (!porpoise_raw_word_emit(
                            output,
                            &raw_instruction,
                            item->address)) {
                        if (ferror(output) != 0) return PORPOISE_EXIT_IO;
                        if (!porpoise_diagnostics_add(
                                diagnostics,
                                PORPOISE_SEVERITY_ERROR,
                                source->relative_path,
                                item->source_line,
                                item->address,
                                "internal raw-word lowering failure for %s",
                                item->mnemonic)) {
                            return PORPOISE_EXIT_INTERNAL;
                        }
                        return PORPOISE_EXIT_INTERNAL;
                    }
                    continue;
                }

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
                if (system_instruction.status == PORPOISE_APPROXIMATE &&
                    !(system_instruction.operation ==
                          PORPOISE_SYSTEM_WRITE_STORAGE &&
                      system_instruction.storage ==
                          PORPOISE_SYSTEM_STORAGE_MSR) &&
                    !file_printf(
                        output,
                        "    porpoise_trace_approximate(state, "
                        "UINT32_C(0x%08lX), \"%s\");\n"
                        "    if (porpoise_state_has_fault(state)) return;\n",
                        (unsigned long)item->address,
                        item->mnemonic)) {
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
                if (spec->operation == OP_FRSP) {
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
            if (spec->status == PORPOISE_APPROXIMATE &&
                spec->operation != OP_PSQ_DFORM &&
                spec->operation != OP_PSQ_INDEXED &&
                spec->operation != OP_FLOAT_BINARY &&
                spec->operation != OP_FLOAT_FMA &&
                spec->operation != OP_PAIRED_BINARY &&
                spec->operation != OP_PAIRED_TERNARY &&
                spec->operation != OP_PAIRED_SCALAR_MADD &&
                spec->operation != OP_PAIRED_SCALAR_MULTIPLY &&
                spec->operation != OP_PAIRED_SUM &&
                !file_printf(
                    output,
                    "    porpoise_trace_approximate(state, "
                    "UINT32_C(0x%08lX), \"%s\");\n"
                    "    if (porpoise_state_has_fault(state)) return;\n",
                    (unsigned long)item->address,
                    item->mnemonic)) return PORPOISE_EXIT_IO;
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
