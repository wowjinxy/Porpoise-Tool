#ifndef PORPOISE_RELOCATION_H
#define PORPOISE_RELOCATION_H

#include "porpoise/common.h"

typedef enum PorpoiseRelocationKind {
    PORPOISE_RELOCATION_NONE = 0,
    PORPOISE_RELOCATION_LOW,
    PORPOISE_RELOCATION_HIGH,
    PORPOISE_RELOCATION_HIGH_ADJUSTED,
    PORPOISE_RELOCATION_SDA21
} PorpoiseRelocationKind;

#define PORPOISE_RELOCATION_MASK(kind) \
    (1U << (unsigned int)(kind))

typedef struct PorpoiseRelocation {
    PorpoiseRelocationKind kind;
    const char *expression;
    size_t expression_length;
    const char *suffix;
    size_t suffix_length;
} PorpoiseRelocation;

/*
 * Parse a relocation-bearing assembler expression such as `symbol+4@ha`.
 * The returned spans borrow storage from `text` and are not NUL-terminated.
 */
bool porpoise_relocation_parse_span(
    const char *text,
    size_t length,
    PorpoiseRelocation *relocation_out);

bool porpoise_relocation_parse(
    const char *text,
    PorpoiseRelocation *relocation_out);

/*
 * Return the relocation kinds accepted by the annotated PPC operand context.
 * Operand indices are zero-based. `memory_offset` identifies the displacement
 * token in a D-form memory operand such as `symbol@l(r3)`.
 */
unsigned int porpoise_relocation_allowed_mask(
    const char *mnemonic,
    size_t operand_index,
    bool memory_offset);

/* Return the linker-controlled bits for a validated relocation token. */
uint32_t porpoise_relocation_variable_word_mask(
    PorpoiseRelocationKind kind);

/*
 * Compare complete operand strings while allowing the expression preceding a
 * supported relocation suffix to differ. Delimiters and all other text must
 * remain byte-for-byte identical.
 */
bool porpoise_relocation_operands_equal(
    const char *mnemonic,
    const char *left,
    const char *right);

#endif
