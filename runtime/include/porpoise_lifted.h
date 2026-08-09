#ifndef PORPOISE_LIFTED_H
#define PORPOISE_LIFTED_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORPOISE_FAULT_MESSAGE_CAPACITY 160U

/*
 * FPSCR masks use PowerPC bit numbering, where architectural bit 0 is the
 * most-significant bit of the uint32_t value.
 */
#define PORPOISE_FPSCR_FX UINT32_C(0x80000000)
#define PORPOISE_FPSCR_FEX UINT32_C(0x40000000)
#define PORPOISE_FPSCR_VX UINT32_C(0x20000000)
#define PORPOISE_FPSCR_OX UINT32_C(0x10000000)
#define PORPOISE_FPSCR_UX UINT32_C(0x08000000)
#define PORPOISE_FPSCR_ZX UINT32_C(0x04000000)
#define PORPOISE_FPSCR_XX UINT32_C(0x02000000)
#define PORPOISE_FPSCR_VXSNAN UINT32_C(0x01000000)
#define PORPOISE_FPSCR_VXISI UINT32_C(0x00800000)
#define PORPOISE_FPSCR_VXIDI UINT32_C(0x00400000)
#define PORPOISE_FPSCR_VXZDZ UINT32_C(0x00200000)
#define PORPOISE_FPSCR_VXIMZ UINT32_C(0x00100000)
#define PORPOISE_FPSCR_VXVC UINT32_C(0x00080000)
#define PORPOISE_FPSCR_FR UINT32_C(0x00040000)
#define PORPOISE_FPSCR_FI UINT32_C(0x00020000)
#define PORPOISE_FPSCR_FPRF_MASK UINT32_C(0x0001F000)
#define PORPOISE_FPSCR_FPCC_MASK UINT32_C(0x0000F000)
#define PORPOISE_FPSCR_VXSOFT UINT32_C(0x00000400)
#define PORPOISE_FPSCR_VXSQRT UINT32_C(0x00000200)
#define PORPOISE_FPSCR_VXCVI UINT32_C(0x00000100)
#define PORPOISE_FPSCR_VE UINT32_C(0x00000080)
#define PORPOISE_FPSCR_OE UINT32_C(0x00000040)
#define PORPOISE_FPSCR_UE UINT32_C(0x00000020)
#define PORPOISE_FPSCR_ZE UINT32_C(0x00000010)
#define PORPOISE_FPSCR_XE UINT32_C(0x00000008)
#define PORPOISE_FPSCR_NI UINT32_C(0x00000004)
#define PORPOISE_FPSCR_RN_MASK UINT32_C(0x00000003)

#define PORPOISE_FPSCR_INVALID_CAUSE_MASK \
    (PORPOISE_FPSCR_VXSNAN | PORPOISE_FPSCR_VXISI | \
     PORPOISE_FPSCR_VXIDI | PORPOISE_FPSCR_VXZDZ | \
     PORPOISE_FPSCR_VXIMZ | PORPOISE_FPSCR_VXVC | \
     PORPOISE_FPSCR_VXSOFT | PORPOISE_FPSCR_VXSQRT | \
     PORPOISE_FPSCR_VXCVI)
#define PORPOISE_FPSCR_EXCEPTION_CAUSE_MASK \
    (PORPOISE_FPSCR_OX | PORPOISE_FPSCR_UX | PORPOISE_FPSCR_ZX | \
     PORPOISE_FPSCR_XX | PORPOISE_FPSCR_INVALID_CAUSE_MASK)

#define PORPOISE_FPCC_LESS 0x08U
#define PORPOISE_FPCC_GREATER 0x04U
#define PORPOISE_FPCC_EQUAL 0x02U
#define PORPOISE_FPCC_UNORDERED 0x01U

/* Machine-state and trap masks use their architectural bit positions. */
#define PORPOISE_HID2_LSQE UINT32_C(0x80000000)
#define PORPOISE_HID2_PSE UINT32_C(0x20000000)
#define PORPOISE_MSR_PR UINT32_C(0x00004000)
#define PORPOISE_MSR_FP UINT32_C(0x00002000)
#define PORPOISE_TRAP_SIGNED_LESS 0x10U
#define PORPOISE_TRAP_SIGNED_GREATER 0x08U
#define PORPOISE_TRAP_EQUAL 0x04U
#define PORPOISE_TRAP_UNSIGNED_LESS 0x02U
#define PORPOISE_TRAP_UNSIGNED_GREATER 0x01U
#define PORPOISE_TRAP_ALWAYS 0x1FU

typedef struct PorpoisePpcState PorpoisePpcState;
typedef struct PorpoiseHostAdapter PorpoiseHostAdapter;

/*
 * Gekko floating-point registers preserve two independently addressable
 * 64-bit lane representations. Raw bits are authoritative so scalar loads,
 * paired-single operations, NaNs and integer-conversion results cannot alias
 * through a host-endian C union.
 */
typedef struct PorpoiseFpr {
    uint64_t lane_bits[2];
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
    PORPOISE_FAULT_UNSUPPORTED_OPERATION,
    PORPOISE_FAULT_FLOATING_POINT_EXCEPTION,
    PORPOISE_FAULT_FLOATING_POINT_UNAVAILABLE,
    PORPOISE_FAULT_PRIVILEGED_OPERATION,
    PORPOISE_FAULT_ILLEGAL_INSTRUCTION
} PorpoiseFault;

typedef enum PorpoiseExecutionStatus {
    PORPOISE_EXECUTION_READY = 0,
    PORPOISE_EXECUTION_RUNNING,
    PORPOISE_EXECUTION_RETURNED,
    PORPOISE_EXECUTION_FAULTED
} PorpoiseExecutionStatus;

/* The four scalar fused multiply-add operations share one runtime entry. */
typedef enum PorpoiseFpFmaOperation {
    PORPOISE_FP_FMA_MADD = 0,
    PORPOISE_FP_FMA_MSUB,
    PORPOISE_FP_FMA_NMADD,
    PORPOISE_FP_FMA_NMSUB
} PorpoiseFpFmaOperation;

typedef enum PorpoiseFpPrecision {
    PORPOISE_FP_PRECISION_DOUBLE = 0,
    PORPOISE_FP_PRECISION_SINGLE
} PorpoiseFpPrecision;

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

typedef PorpoiseHostResult (*PorpoiseHostReadTimeBaseFn)(
    void *context,
    uint64_t *ticks_out);

typedef PorpoiseHostResult (*PorpoiseHostTrapFn)(
    void *context,
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t trap_options,
    uint32_t left,
    uint32_t right);

typedef PorpoiseHostResult (*PorpoiseHostSystemCallFn)(
    void *context,
    PorpoisePpcState *state,
    uint32_t instruction_address);

struct PorpoiseHostAdapter {
    void *context;
    PorpoiseHostReadBytesFn read_bytes;
    PorpoiseHostWriteBytesFn write_bytes;
    PorpoiseHostDecodePointerFn decode_pointer;
    PorpoiseHostEncodePointerFn encode_pointer;
    PorpoiseHostReadTimeBaseFn read_time_base;
    PorpoiseHostTrapFn trap;
    PorpoiseHostSystemCallFn system_call;
};

struct PorpoisePpcState {
    uint32_t gpr[32];
    PorpoiseFpr fpr[32];

    uint32_t cr;
    uint32_t fpscr;
    uint32_t xer;
    uint32_t lr;
    uint32_t ctr;
    uint32_t pc;
    uint32_t gqr[8];

    uint32_t msr;
    uint32_t sprg[4];
    uint32_t srr0;
    uint32_t srr1;
    uint32_t dar;
    uint32_t dsisr;
    uint32_t sdr1;
    uint32_t ear;
    uint32_t pvr;
    uint32_t segment_register[16];
    uint32_t ibat_upper[8];
    uint32_t ibat_lower[8];
    uint32_t dbat_upper[8];
    uint32_t dbat_lower[8];
    uint32_t hid0;
    uint32_t hid1;
    uint32_t hid2;
    uint32_t hid4;
    uint32_t l2cr;
    uint32_t ictc;
    uint32_t wpar;
    uint32_t dma_upper;
    uint32_t dma_lower;
    uint32_t iabr;
    uint32_t dabr;
    uint32_t mmcr[2];
    uint32_t pmc[4];
    uint32_t sia;
    uint32_t sda;

    uint64_t time_base_bias;
    uint32_t decrementer_value;
    uint64_t decrementer_anchor;
    int decrementer_valid;

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
/* Obtain host-owned entry registers, then enable title FP/paired-single modes. */
int porpoise_state_prepare_title_entry(PorpoisePpcState *state);
void porpoise_state_clear_fault(PorpoisePpcState *state);
void porpoise_state_set_fault(
    PorpoisePpcState *state,
    PorpoiseFault fault,
    uint32_t guest_address,
    const char *message);
int porpoise_state_has_fault(const PorpoisePpcState *state);
int porpoise_state_should_stop(const PorpoisePpcState *state);
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

/* Recompute non-sticky FPSCR summary bits after direct FPSCR manipulation. */
void porpoise_fpscr_recompute_summaries(PorpoisePpcState *state);
/* Raise sticky exception causes and update FX, VX and FEX. */
void porpoise_fpscr_raise_exceptions(
    PorpoisePpcState *state,
    uint32_t exception_causes);
/* Record-form floating-point instructions copy FPSCR[FX:FEX:VX:OX] to CR1. */
void porpoise_fpscr_update_cr1(PorpoisePpcState *state);

/*
 * Compare raw IEEE-754 binary64 encodings. Both helpers update FPSCR[FPCC]
 * and the requested CR field without evaluating a host signaling NaN.
 */
void porpoise_fcmpo(
    PorpoisePpcState *state,
    unsigned int field_index,
    uint64_t left_bits,
    uint64_t right_bits);
void porpoise_fcmpu(
    PorpoisePpcState *state,
    unsigned int field_index,
    uint64_t left_bits,
    uint64_t right_bits);

/*
 * fsel selects nonnegative_bits when condition is greater than or equal to
 * either signed zero. A NaN condition selects negative_bits. The selected
 * encoding is copied unchanged, including NaN sign and payload bits.
 */
uint64_t porpoise_fsel_bits(
    uint64_t condition_bits,
    uint64_t nonnegative_bits,
    uint64_t negative_bits);

/*
 * Scalar floating-point helpers consume and produce raw lane encodings. They
 * return zero when validation or an enabled invalid-operation exception has
 * faulted the state, allowing generated functions to stop immediately.
 *
 * frsp uses integer-only PowerPC rounding for all FPSCR[RN] modes. Gekko
 * leaves the second destination lane undefined; this runtime deliberately
 * duplicates lane zero into lane one as a deterministic compatibility policy.
 */
int porpoise_frsp(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int source_register,
    int record);
int porpoise_fctiwz(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int source_register,
    int record);
int porpoise_mffs(
    PorpoisePpcState *state,
    unsigned int destination_register,
    int record);
int porpoise_mtfsf(
    PorpoisePpcState *state,
    unsigned int field_mask,
    unsigned int source_register,
    int record);
int porpoise_mtfsb1(
    PorpoisePpcState *state,
    unsigned int bit_index,
    int record);

/*
 * NaN priority, invalid causes, suppression, FPSCR summaries, record forms,
 * and Gekko lane placement are handled from raw bits. The finite arithmetic
 * path intentionally uses the host C99 fma operation followed by deterministic
 * raw rounding. It is therefore an approximation rather than a claim of a
 * complete PowerPC software floating-point implementation.
 */
int porpoise_fp_fma(
    PorpoisePpcState *state,
    unsigned int destination_register,
    unsigned int multiplicand_register,
    unsigned int multiplier_register,
    unsigned int addend_register,
    PorpoiseFpFmaOperation operation,
    PorpoiseFpPrecision precision,
    int record);

/*
 * Exact PowerPC Appendix D.6 widening of a memory binary32 encoding into the
 * binary64 encoding held in an FPR. This is integer-only and preserves signed
 * zero, infinities, and representable QNaN/SNaN sign and payload bits.
 */
uint64_t porpoise_binary32_to_binary64_bits(uint32_t binary32_bits);

/*
 * PowerPC Appendix D.7 single-store conversion. Architectural software must
 * supply a value already representable as binary32; in that domain this is an
 * exact inverse of porpoise_binary32_to_binary64_bits. The architecture leaves
 * other operands undefined. This helper deliberately applies the manual's
 * bit-selection/denormalization model where specified and a stable raw-bit
 * fallback below that range, instead of inventing an IEEE rounding mode. It
 * never changes FPSCR.
 */
uint32_t porpoise_binary64_to_binary32_bits(uint64_t binary64_bits);

uint64_t porpoise_fpr_get_bits(
    const PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index);
void porpoise_fpr_set_bits(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index,
    uint64_t bits);
double porpoise_fpr_get_f64(
    const PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index);
void porpoise_fpr_set_f64(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index,
    double value);

/* Raw guest-endian FPR memory transfers for generated lfs/stfs/lfd/stfd code. */
int porpoise_fpr_load_binary32(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index,
    uint32_t guest_address);
int porpoise_fpr_store_binary32(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index,
    uint32_t guest_address);
int porpoise_fpr_load_binary64(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index,
    uint32_t guest_address);
int porpoise_fpr_store_binary64(
    PorpoisePpcState *state,
    unsigned int register_index,
    unsigned int lane_index,
    uint32_t guest_address);

/*
 * Paired-single quantized transfers decode their type and signed six-bit
 * scale from state->gqr[gqr_index]. requires_lsqe must be one for the D-form
 * instructions and zero for indexed forms. The mode preflight gives disabled
 * HID2 facilities priority over MSR[FP], matching Gekko exception priority.
 * A transfer performs exactly one host memory callback for the complete
 * scalar or pair, so a rejected read cannot partially update the destination
 * FPR.
 *
 * Type zero transfers preserve the raw binary32 encoding. Integer stores use
 * deterministic round-toward-zero and saturation, including defined NaN and
 * infinity handling, without out-of-range C conversions. Values that are not
 * valid paired-single representations are handled from their authoritative
 * binary64 lane encoding; lowering must therefore report PSQ conservatively
 * where hardware exception, NI, or non-representable-input behavior matters.
 */
int porpoise_psq_load(
    PorpoisePpcState *state,
    unsigned int destination_register,
    uint32_t guest_address,
    unsigned int w,
    unsigned int gqr_index,
    unsigned int requires_lsqe);
int porpoise_psq_store(
    PorpoisePpcState *state,
    unsigned int source_register,
    uint32_t guest_address,
    unsigned int w,
    unsigned int gqr_index,
    unsigned int requires_lsqe);

/*
 * Focused system-instruction helpers keep privilege checks and host events in
 * one place. All helpers stop immediately when state already carries a fault.
 * Time-base writes preserve the other half and adjust a guest-visible bias;
 * decrementer elapsed time is anchored to raw host ticks and is therefore not
 * affected by time-base writes.
 */
int porpoise_require_supervisor(
    PorpoisePpcState *state,
    uint32_t instruction_address);
int porpoise_write_msr(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t value);
int porpoise_time_base_read(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint64_t *ticks_out);
int porpoise_time_base_write_lower(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t value);
int porpoise_time_base_write_upper(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t value);
int porpoise_decrementer_read(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t *value_out);
int porpoise_decrementer_write(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t value);
int porpoise_cache_block_zero(
    PorpoisePpcState *state,
    uint32_t effective_address);
int porpoise_data_cache_block_invalidate(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t effective_address);
int porpoise_trap_event(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    uint32_t trap_options,
    uint32_t left,
    uint32_t right);
int porpoise_system_call_event(
    PorpoisePpcState *state,
    uint32_t instruction_address);
int porpoise_illegal_instruction(
    PorpoisePpcState *state,
    uint32_t instruction_address,
    const char *message);

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
