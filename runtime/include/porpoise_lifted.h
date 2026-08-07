#ifndef PORPOISE_LIFTED_H
#define PORPOISE_LIFTED_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORPOISE_FAULT_MESSAGE_CAPACITY 160U

typedef struct PorpoisePpcState PorpoisePpcState;
typedef struct PorpoiseHostAdapter PorpoiseHostAdapter;

/*
 * A PowerPC floating-point register can be viewed either as one scalar double
 * or as the two single-precision lanes used by paired-single instructions.
 * The bits member is useful when an instruction needs a representation-level
 * operation without violating C's strict-aliasing rules.
 */
typedef union PorpoiseFpr {
    double f64;
    float ps[2];
    uint64_t bits;
} PorpoiseFpr;

typedef enum PorpoiseHostResult {
    PORPOISE_HOST_OK = 0,
    PORPOISE_HOST_INVALID_ARGUMENT,
    PORPOISE_HOST_INVALID_POINTER,
    PORPOISE_HOST_UNMAPPED_ADDRESS,
    PORPOISE_HOST_UNSUPPORTED_MMIO,
    PORPOISE_HOST_ADDRESS_OVERFLOW,
    PORPOISE_HOST_IO_ERROR
} PorpoiseHostResult;

typedef enum PorpoiseFault {
    PORPOISE_FAULT_NONE = 0,
    PORPOISE_FAULT_INVALID_STATE,
    PORPOISE_FAULT_NO_HOST_ADAPTER,
    PORPOISE_FAULT_MISSING_HOST_CALLBACK,
    PORPOISE_FAULT_INVALID_ARGUMENT,
    PORPOISE_FAULT_INVALID_POINTER,
    PORPOISE_FAULT_UNMAPPED_ADDRESS,
    PORPOISE_FAULT_UNSUPPORTED_MMIO,
    PORPOISE_FAULT_ADDRESS_OVERFLOW,
    PORPOISE_FAULT_HOST_IO,
    PORPOISE_FAULT_UNSUPPORTED_OPERATION
} PorpoiseFault;

typedef enum PorpoiseExecutionStatus {
    PORPOISE_EXECUTION_READY = 0,
    PORPOISE_EXECUTION_RUNNING,
    PORPOISE_EXECUTION_RETURNED,
    PORPOISE_EXECUTION_FAULTED
} PorpoiseExecutionStatus;

typedef PorpoiseHostResult (*PorpoiseHostReadBytesFn)(
    void *context,
    uint32_t guest_address,
    void *destination,
    size_t size);

typedef PorpoiseHostResult (*PorpoiseHostWriteBytesFn)(
    void *context,
    uint32_t guest_address,
    const void *source,
    size_t size);

typedef PorpoiseHostResult (*PorpoiseHostDecodePointerFn)(
    void *context,
    uint32_t guest_address,
    void **pointer_out);

typedef PorpoiseHostResult (*PorpoiseHostEncodePointerFn)(
    void *context,
    const void *pointer,
    uint32_t *guest_address_out);

struct PorpoiseHostAdapter {
    void *context;
    PorpoiseHostReadBytesFn read_bytes;
    PorpoiseHostWriteBytesFn write_bytes;
    PorpoiseHostDecodePointerFn decode_pointer;
    PorpoiseHostEncodePointerFn encode_pointer;
};

struct PorpoisePpcState {
    uint32_t gpr[32];
    PorpoiseFpr fpr[32];

    uint32_t cr;
    uint32_t xer;
    uint32_t lr;
    uint32_t ctr;
    uint32_t pc;
    uint32_t gqr[8];

    PorpoiseExecutionStatus status;
    PorpoiseFault fault;
    uint32_t fault_address;
    char fault_message[PORPOISE_FAULT_MESSAGE_CAPACITY];

    PorpoiseHostAdapter *host;
};

/* Every lifted function uses registers in state for arguments and results. */
typedef void (*PorpoiseLiftedFunction)(PorpoisePpcState *state);

void porpoise_state_init(
    PorpoisePpcState *state,
    PorpoiseHostAdapter *host);
void porpoise_state_clear_fault(PorpoisePpcState *state);
void porpoise_state_set_fault(
    PorpoisePpcState *state,
    PorpoiseFault fault,
    uint32_t guest_address,
    const char *message);
int porpoise_state_has_fault(const PorpoisePpcState *state);
const char *porpoise_state_fault_message(const PorpoisePpcState *state);
const char *porpoise_fault_string(PorpoiseFault fault);
const char *porpoise_host_result_string(PorpoiseHostResult result);

/*
 * CR indices follow PowerPC notation: field 0 occupies bits 0..3 of the
 * architectural CR (the most-significant nibble in its uint32_t storage), and
 * CR bit 0 is the most-significant bit.
 */
uint8_t porpoise_cr_get_field(
    const PorpoisePpcState *state,
    unsigned int field_index);
void porpoise_cr_set_field(
    PorpoisePpcState *state,
    unsigned int field_index,
    uint8_t value);
int porpoise_cr_get_bit(
    const PorpoisePpcState *state,
    unsigned int bit_index);
void porpoise_cr_set_bit(
    PorpoisePpcState *state,
    unsigned int bit_index,
    int value);

/* Defined shift helpers avoid C's undefined shift-by-width behavior. */
uint32_t porpoise_shift_left32(uint32_t value, uint32_t shift_source);
uint32_t porpoise_shift_right32(uint32_t value, uint32_t shift_source);
uint32_t porpoise_sign_extend8(uint32_t value);
uint32_t porpoise_sign_extend16(uint32_t value);
uint32_t porpoise_count_leading_zeros32(uint32_t value);
uint32_t porpoise_add_with_carry32(
    PorpoisePpcState *state,
    uint32_t left,
    uint32_t right,
    uint32_t carry_in);
uint32_t porpoise_arithmetic_shift_right32(
    PorpoisePpcState *state,
    uint32_t value,
    unsigned int shift);
uint32_t porpoise_rotate_left32(uint32_t value, unsigned int shift);
uint32_t porpoise_mask32(unsigned int mask_begin, unsigned int mask_end);
void porpoise_set_cr0_result(PorpoisePpcState *state, uint32_t value);
void porpoise_compare_signed(
    PorpoisePpcState *state,
    unsigned int field_index,
    uint32_t left,
    uint32_t right);
void porpoise_compare_unsigned(
    PorpoisePpcState *state,
    unsigned int field_index,
    uint32_t left,
    uint32_t right);

uint8_t porpoise_load_u8(PorpoisePpcState *state, uint32_t guest_address);
uint16_t porpoise_load_u16(PorpoisePpcState *state, uint32_t guest_address);
uint32_t porpoise_load_u32(PorpoisePpcState *state, uint32_t guest_address);
uint64_t porpoise_load_u64(PorpoisePpcState *state, uint32_t guest_address);
float porpoise_load_f32(PorpoisePpcState *state, uint32_t guest_address);
double porpoise_load_f64(PorpoisePpcState *state, uint32_t guest_address);
int porpoise_load_multiple_words(
    PorpoisePpcState *state,
    uint32_t guest_address,
    unsigned int first_register);

void porpoise_store_u8(
    PorpoisePpcState *state,
    uint32_t guest_address,
    uint8_t value);
void porpoise_store_u16(
    PorpoisePpcState *state,
    uint32_t guest_address,
    uint16_t value);
void porpoise_store_u32(
    PorpoisePpcState *state,
    uint32_t guest_address,
    uint32_t value);
void porpoise_store_u64(
    PorpoisePpcState *state,
    uint32_t guest_address,
    uint64_t value);
void porpoise_store_f32(
    PorpoisePpcState *state,
    uint32_t guest_address,
    float value);
void porpoise_store_f64(
    PorpoisePpcState *state,
    uint32_t guest_address,
    double value);
int porpoise_store_multiple_words(
    PorpoisePpcState *state,
    uint32_t guest_address,
    unsigned int first_register);

void *porpoise_decode_pointer(
    PorpoisePpcState *state,
    uint32_t guest_address);
uint32_t porpoise_encode_pointer(
    PorpoisePpcState *state,
    const void *pointer);

#ifdef __cplusplus
}
#endif

#endif
