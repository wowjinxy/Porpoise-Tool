#ifndef PORPOISE_LOWER_INTERNAL_H
#define PORPOISE_LOWER_INTERNAL_H

#include "porpoise/lower.h"
#include "porpoise/relocation.h"

#include <stdint.h>
#include <stdio.h>

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
    OP_ROTLW,
    OP_ROTATE_ALIAS,
    OP_INTEGER_UNARY,
    OP_LOAD,
    OP_STORE,
    OP_INDEXED_LOAD,
    OP_INDEXED_STORE,
    OP_BYTE_REVERSE_LOAD,
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
    OP_BDZ,
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
    OP_FCTIW,
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
    MEMORY_F64,
    MEMORY_FPR_U32
};

enum {
    PSQ_STORE = 1,
    PSQ_UPDATE = 2
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
    LOGICAL_EQUIVALENT,
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

typedef struct OperandList {
    char storage[1024];
    char *values[8];
    size_t count;
} OperandList;

typedef struct PorpoiseLowerInstructionContext {
    FILE *output;
    const OpcodeSpec *spec;
    const PorpoiseProgram *program;
    const PorpoiseSourceFile *source;
    const PorpoiseFunction *function;
    const PorpoiseAsmItem *item;
    const PorpoiseAbiManifest *abi;
    PorpoiseDiagnostics *diagnostics;
    OperandList operands;
    bool record;
} PorpoiseLowerInstructionContext;

bool porpoise_lower_file_printf(FILE *output, const char *format, ...);
bool porpoise_lower_parse_register(
    const char *text,
    char prefix,
    unsigned int *index);
bool porpoise_lower_parse_unsigned(const char *text, uint32_t *value);
bool porpoise_lower_parse_signed(const char *text, int32_t *value);
bool porpoise_lower_parse_signed_or_relocated(
    const char *text,
    uint32_t word,
    unsigned int allowed_relocations,
    int32_t *value,
    PorpoiseRelocationKind *relocation);
bool porpoise_lower_parse_unsigned_or_relocated(
    const char *text,
    uint32_t word,
    unsigned int allowed_relocations,
    uint32_t *value);
bool porpoise_lower_parse_gqr_register(
    const char *text,
    unsigned int *index);
bool porpoise_lower_parse_memory_operand(
    const char *text,
    uint32_t word,
    int32_t *offset,
    unsigned int *base);
bool porpoise_lower_parse_psq_memory_operand(
    const char *text,
    int32_t *offset,
    unsigned int *base);
bool porpoise_lower_psq_dform_operands_match_word(
    uint32_t word,
    unsigned int expected_opcode,
    unsigned int fpr,
    unsigned int base,
    int32_t displacement,
    unsigned int w,
    unsigned int gqr);
bool porpoise_lower_psq_indexed_operands_match_word(
    uint32_t word,
    unsigned int expected_xo,
    unsigned int fpr,
    unsigned int base,
    unsigned int index,
    unsigned int w,
    unsigned int gqr);
bool porpoise_lower_parse_cr_bit(const char *text, unsigned int *bit_index);
bool porpoise_lower_emit_record_update(
    FILE *output,
    bool record,
    unsigned int destination);
bool porpoise_lower_emit_branch_target(
    FILE *output,
    const PorpoiseProgram *program,
    const PorpoiseFunction *function,
    const PorpoiseAbiManifest *abi,
    const char *target_text,
    bool link,
    PorpoiseDiagnostics *diagnostics,
    const PorpoiseSourceFile *source,
    const PorpoiseAsmItem *item);
bool porpoise_lower_emit_conditional_target(
    FILE *output,
    const PorpoiseProgram *program,
    const PorpoiseFunction *function,
    const PorpoiseAbiManifest *abi,
    const char *target_text,
    const char *condition,
    PorpoiseDiagnostics *diagnostics,
    const PorpoiseSourceFile *source,
    const PorpoiseAsmItem *item);

bool porpoise_lower_emit_integer(
    const PorpoiseLowerInstructionContext *context);
bool porpoise_lower_emit_memory(
    const PorpoiseLowerInstructionContext *context);
bool porpoise_lower_emit_branch(
    const PorpoiseLowerInstructionContext *context);
bool porpoise_lower_emit_float(
    const PorpoiseLowerInstructionContext *context);

/* Keep family implementations readable while retaining namespaced linkage. */
#define file_printf porpoise_lower_file_printf
#define parse_register porpoise_lower_parse_register
#define parse_unsigned porpoise_lower_parse_unsigned
#define parse_signed porpoise_lower_parse_signed
#define parse_signed_or_relocated porpoise_lower_parse_signed_or_relocated
#define parse_unsigned_or_relocated \
    porpoise_lower_parse_unsigned_or_relocated
#define parse_gqr_register porpoise_lower_parse_gqr_register
#define parse_memory_operand porpoise_lower_parse_memory_operand
#define parse_psq_memory_operand porpoise_lower_parse_psq_memory_operand
#define psq_dform_operands_match_word \
    porpoise_lower_psq_dform_operands_match_word
#define psq_indexed_operands_match_word \
    porpoise_lower_psq_indexed_operands_match_word
#define parse_cr_bit porpoise_lower_parse_cr_bit
#define emit_record_update porpoise_lower_emit_record_update
#define emit_branch_target porpoise_lower_emit_branch_target
#define emit_conditional_target porpoise_lower_emit_conditional_target

#endif
