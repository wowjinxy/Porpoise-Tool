#ifndef PORPOISE_TITLE_HOST_H
#define PORPOISE_TITLE_HOST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORPOISE_TITLE_HOST_GPR_COUNT 32U
#define PORPOISE_TITLE_HOST_INITIAL_WORD_CAPACITY 16U
#define PORPOISE_TITLE_HOST_STARTUP_FUNCTION_CAPACITY 8U
#define PORPOISE_TITLE_RUNTIME_INITIALIZE_DVD UINT32_C(0x00000001)
#define PORPOISE_TITLE_RUNTIME_KNOWN_FLAGS \
    PORPOISE_TITLE_RUNTIME_INITIALIZE_DVD
#define PORPOISE_TITLE_STARTUP_ESTABLISH_GUEST_MAIN_THREAD_AFTER \
    UINT32_C(0x00000001)
#define PORPOISE_TITLE_STARTUP_KNOWN_FLAGS \
    PORPOISE_TITLE_STARTUP_ESTABLISH_GUEST_MAIN_THREAD_AFTER

typedef enum PorpoiseTitleHostResultV3 {
    PORPOISE_TITLE_HOST_OK = 0,
    PORPOISE_TITLE_HOST_UNAVAILABLE = 1,
    PORPOISE_TITLE_HOST_INVALID_STATE = 2
} PorpoiseTitleHostResultV3;

typedef struct PorpoiseTitleInitialWordV3 {
    uint32_t guest_address;
    uint32_t value;
} PorpoiseTitleInitialWordV3;

typedef struct PorpoiseTitleStartupFunctionV3 {
    uint32_t guest_address;
    uint32_t flags;
} PorpoiseTitleStartupFunctionV3;

typedef struct PorpoiseTitleRuntimeConfigV1 {
    uint32_t flags;
    const char *dvd_root_directory;
} PorpoiseTitleRuntimeConfigV1;

typedef struct PorpoiseTitleEntryStateV3 {
    uint32_t gpr[PORPOISE_TITLE_HOST_GPR_COUNT];
    uint32_t arena_lo;
    uint32_t arena_hi;
    uint32_t startup_function_count;
    PorpoiseTitleStartupFunctionV3 startup_functions[
        PORPOISE_TITLE_HOST_STARTUP_FUNCTION_CAPACITY];
    uint32_t initial_word_count;
    PorpoiseTitleInitialWordV3
        initial_words[PORPOISE_TITLE_HOST_INITIAL_WORD_CAPACITY];
} PorpoiseTitleEntryStateV3;

/*
 * Supply host-runtime configuration before libPorpoise initializes. The
 * caller zeroes config_out first. Set PORPOISE_TITLE_RUNTIME_INITIALIZE_DVD
 * to request a native DVD/FST bootstrap. dvd_root_directory may then name an
 * explicit host directory, or remain NULL to retain libPorpoise's default.
 * The pointed-to string must remain valid through the immediately following
 * adapter initialization, which consumes it synchronously.
 */
int PorpoiseHostPrepareRuntimeV1(
    uint32_t entry_address,
    PorpoiseTitleRuntimeConfigV1 *config_out);

/*
 * Supply title-specific state after libPorpoise has initialized its console
 * memory and generated title data has been initialized. The caller zeroes
 * state_out first. The GPR array is the register image at the direct entry;
 * r1 must be aligned and r2/r13 must be the title's TOC/SDA bases.
 *
 * arena_lo/arena_hi are an optional pair of exclusive guest arena bounds; set
 * both to zero to retain the host defaults. startup_functions is an ordered
 * list of lifted guest-only initializers run before the direct entry. It may,
 * for example, establish the guest OS thread/low-memory mirror omitted by
 * native OSInit and then invoke the title's __init_user. Mark that mirror
 * initializer with PORPOISE_TITLE_STARTUP_ESTABLISH_GUEST_MAIN_THREAD_AFTER;
 * the adapter then validates and binds the resulting guest current-thread
 * state before a later initializer can observe it. Each listed function
 * starts from the supplied direct-entry GPR image; its memory side effects are
 * retained, while its register clobbers are discarded before the next listed
 * function or the direct entry. Unknown flags and null addresses are invalid.
 * initial_words describes bounded
 * linker/startup writes not present in the generated data. All addresses
 * remain 32-bit guest values. Porpoise Tool never runs the console __start.
 */
int PorpoiseHostPrepareTitleEntryV3(
    uint32_t entry_address,
    PorpoiseTitleEntryStateV3 *state_out);

#ifdef __cplusplus
}
#endif

#endif
