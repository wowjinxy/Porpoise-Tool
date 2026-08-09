#ifndef PORPOISE_SYSTEM_LOWER_H
#define PORPOISE_SYSTEM_LOWER_H

#include "porpoise/report.h"

#include <stdio.h>

typedef enum PorpoiseSystemResolveResult {
    PORPOISE_SYSTEM_NOT_RECOGNIZED = 0,
    PORPOISE_SYSTEM_RESOLVED,
    PORPOISE_SYSTEM_INVALID
} PorpoiseSystemResolveResult;

typedef enum PorpoiseSystemOperation {
    PORPOISE_SYSTEM_OPERATION_INVALID = 0,
    PORPOISE_SYSTEM_READ_STORAGE,
    PORPOISE_SYSTEM_WRITE_STORAGE,
    PORPOISE_SYSTEM_MTCRF,
    PORPOISE_SYSTEM_MCRXR,
    PORPOISE_SYSTEM_RFI,
    PORPOISE_SYSTEM_DCBZ,
    PORPOISE_SYSTEM_DCBI,
    PORPOISE_SYSTEM_TRAP_IMMEDIATE,
    PORPOISE_SYSTEM_CALL,
    PORPOISE_SYSTEM_UNSUPPORTED
} PorpoiseSystemOperation;

typedef enum PorpoiseSystemStorage {
    PORPOISE_SYSTEM_STORAGE_NONE = 0,
    PORPOISE_SYSTEM_STORAGE_UNKNOWN,
    PORPOISE_SYSTEM_STORAGE_MSR,
    PORPOISE_SYSTEM_STORAGE_XER,
    PORPOISE_SYSTEM_STORAGE_LR,
    PORPOISE_SYSTEM_STORAGE_CTR,
    PORPOISE_SYSTEM_STORAGE_GQR,
    PORPOISE_SYSTEM_STORAGE_SPRG,
    PORPOISE_SYSTEM_STORAGE_SRR0,
    PORPOISE_SYSTEM_STORAGE_SRR1,
    PORPOISE_SYSTEM_STORAGE_DAR,
    PORPOISE_SYSTEM_STORAGE_DSISR,
    PORPOISE_SYSTEM_STORAGE_DEC,
    PORPOISE_SYSTEM_STORAGE_SDR1,
    PORPOISE_SYSTEM_STORAGE_EAR,
    PORPOISE_SYSTEM_STORAGE_PVR,
    PORPOISE_SYSTEM_STORAGE_SEGMENT,
    PORPOISE_SYSTEM_STORAGE_IBAT_UPPER,
    PORPOISE_SYSTEM_STORAGE_IBAT_LOWER,
    PORPOISE_SYSTEM_STORAGE_DBAT_UPPER,
    PORPOISE_SYSTEM_STORAGE_DBAT_LOWER,
    PORPOISE_SYSTEM_STORAGE_TIME_BASE_LOWER,
    PORPOISE_SYSTEM_STORAGE_TIME_BASE_UPPER,
    PORPOISE_SYSTEM_STORAGE_HID0,
    PORPOISE_SYSTEM_STORAGE_HID1,
    PORPOISE_SYSTEM_STORAGE_HID2,
    PORPOISE_SYSTEM_STORAGE_HID4,
    PORPOISE_SYSTEM_STORAGE_L2CR,
    PORPOISE_SYSTEM_STORAGE_ICTC,
    PORPOISE_SYSTEM_STORAGE_WPAR,
    PORPOISE_SYSTEM_STORAGE_DMA_UPPER,
    PORPOISE_SYSTEM_STORAGE_DMA_LOWER,
    PORPOISE_SYSTEM_STORAGE_IABR,
    PORPOISE_SYSTEM_STORAGE_DABR,
    PORPOISE_SYSTEM_STORAGE_MMCR,
    PORPOISE_SYSTEM_STORAGE_PMC,
    PORPOISE_SYSTEM_STORAGE_SIA,
    PORPOISE_SYSTEM_STORAGE_SDA,
    PORPOISE_SYSTEM_STORAGE_USER_MMCR,
    PORPOISE_SYSTEM_STORAGE_USER_PMC,
    PORPOISE_SYSTEM_STORAGE_USER_SIA,
    PORPOISE_SYSTEM_STORAGE_USER_SDA,
    PORPOISE_SYSTEM_STORAGE_THERMAL,
    PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR
} PorpoiseSystemStorage;

typedef struct PorpoiseSystemInstruction {
    PorpoiseSystemOperation operation;
    PorpoiseSystemStorage storage;
    PorpoiseLoweringStatus status;
    bool semantic_test;
    const char *detail;

    uint32_t word;
    uint32_t spr_number;
    uint32_t immediate;
    uint32_t cr_mask;
    unsigned int storage_index;
    unsigned int destination_register;
    unsigned int source_register;
    unsigned int base_register;
    unsigned int index_register;
    bool requires_supervisor;
} PorpoiseSystemInstruction;

/*
 * Resolve a known system mnemonic and validate its textual operands against
 * the authoritative annotated instruction word. A recognized but unsupported
 * architectural operation returns PORPOISE_SYSTEM_RESOLVED with status
 * PORPOISE_UNSUPPORTED. Malformed operands or a word mismatch return
 * PORPOISE_SYSTEM_INVALID. All detail strings have static storage duration.
 */
PorpoiseSystemResolveResult porpoise_system_resolve(
    const char *mnemonic,
    const char *operands,
    uint32_t word,
    PorpoiseSystemInstruction *instruction_out);

/*
 * Emit one resolved operation into a lifted function body. False reports an
 * invalid argument or stream failure. Unsupported operations emit an explicit
 * illegal-instruction fault so they can never become silent no-ops.
 */
bool porpoise_system_emit(
    FILE *output,
    const PorpoiseSystemInstruction *instruction,
    uint32_t instruction_address);

#endif
