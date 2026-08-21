#ifndef PORPOISE_RAW_WORD_H
#define PORPOISE_RAW_WORD_H

#include "porpoise/report.h"

#include <stdio.h>

typedef enum PorpoiseRawWordResolveResult {
    PORPOISE_RAW_WORD_NOT_RECOGNIZED = 0,
    PORPOISE_RAW_WORD_RESOLVED,
    PORPOISE_RAW_WORD_UNSUPPORTED,
    PORPOISE_RAW_WORD_INVALID
} PorpoiseRawWordResolveResult;

typedef enum PorpoiseRawWordOperation {
    PORPOISE_RAW_WORD_ILLEGAL_ENCODING = 0,
    PORPOISE_RAW_WORD_LMW_OVERLAP
} PorpoiseRawWordOperation;

typedef struct PorpoiseRawWordInstruction {
    PorpoiseRawWordOperation operation;
    PorpoiseLoweringStatus status;
    bool semantic_test;
    const char *detail;
    uint32_t word;
    unsigned int destination_register;
    unsigned int base_register;
    int32_t displacement;
} PorpoiseRawWordInstruction;

/*
 * Resolve an annotated raw-word directive. An encoding explicitly identified
 * by the input metadata as invalid or illegal is modeled by an approximate
 * terminal illegal-instruction fault. The reserved lmw overlap form is
 * decoded separately and also carries an explicitly approximate status.
 * Every other raw word remains unsupported rather than being guessed to be
 * code or data.
 */
PorpoiseRawWordResolveResult porpoise_raw_word_resolve(
    const char *mnemonic,
    const char *operands,
    uint32_t word,
    PorpoiseRawWordInstruction *instruction_out);

bool porpoise_raw_word_emit(
    FILE *output,
    const PorpoiseRawWordInstruction *instruction,
    uint32_t instruction_address);

#endif
