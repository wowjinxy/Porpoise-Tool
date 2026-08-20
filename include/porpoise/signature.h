#ifndef PORPOISE_SIGNATURE_H
#define PORPOISE_SIGNATURE_H

#include "porpoise/program.h"
#include "porpoise/sha256.h"

#define PORPOISE_SIGNATURE_ALGORITHM_VERSION 1U
#define PORPOISE_SIGNATURE_MIN_INSTRUCTION_COUNT 8U
#define PORPOISE_SIGNATURE_MIN_MEANINGFUL_FIXED_COUNT 4U

typedef enum PorpoiseSignatureIssue {
    PORPOISE_SIGNATURE_ISSUE_NONE = 0U,
    PORPOISE_SIGNATURE_ISSUE_EMPTY = 1U << 0,
    PORPOISE_SIGNATURE_ISSUE_COUNT_MISMATCH = 1U << 1,
    PORPOISE_SIGNATURE_ISSUE_INVALID_SIZE = 1U << 2,
    PORPOISE_SIGNATURE_ISSUE_INVALID_LAYOUT = 1U << 3,
    PORPOISE_SIGNATURE_ISSUE_MALFORMED_RELOCATION = 1U << 4,
    PORPOISE_SIGNATURE_ISSUE_MULTIPLE_RELOCATIONS = 1U << 5,
    PORPOISE_SIGNATURE_ISSUE_INVALID_INTERNAL_BRANCH = 1U << 6,
    PORPOISE_SIGNATURE_ISSUE_DATA_REGION = 1U << 7
} PorpoiseSignatureIssue;

typedef struct PorpoiseFunctionSignature {
    uint32_t algorithm_version;
    uint32_t function_size;
    uint32_t instruction_count;
    uint32_t fixed_instruction_count;
    uint32_t meaningful_fixed_instruction_count;
    uint32_t relocation_count;
    uint32_t internal_branch_count;
    uint32_t external_branch_count;
    uint32_t external_target_count;
    uint32_t issue_flags;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    char digest_hex[PORPOISE_SHA256_HEX_SIZE];
} PorpoiseFunctionSignature;

/*
 * Compute a canonical, relocation-aware function identity.
 *
 * `program` may be NULL. Supplying it improves branch/alias resolution but
 * does not make source symbol spellings part of the digest. The digest keeps
 * the exact function size, instruction offsets, fixed PPC bits, relocation
 * kinds/addends, internal control flow, and repeated external-target topology.
 * Only validated linker-controlled fields and external branch displacements
 * are masked.
 *
 * A true return value means a digest was produced. Structural issues are
 * reported through `issue_flags`; use porpoise_signature_is_automatic_match_eligible
 * before allowing a signature to change translation behavior.
 */
bool porpoise_signature_compute(
    const PorpoiseProgram *program,
    const PorpoiseFunction *function,
    PorpoiseFunctionSignature *signature_out);

bool porpoise_signature_equal(
    const PorpoiseFunctionSignature *left,
    const PorpoiseFunctionSignature *right);

bool porpoise_signature_is_automatic_match_eligible(
    const PorpoiseFunctionSignature *signature);

#endif
