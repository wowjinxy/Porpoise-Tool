#include <dolphin.h>
#include <dolphin/ar.h>
#include <dolphin/dsp.h>
#include <dolphin/dvd.h>
#include <dolphin/pad.h>
#include <dolphin/vi.h>
#include <porpoise/stub.h>
#include <simulator/sim_gx_CommandProcessor.h>

#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    STUB_MEMORY_SIZE = 24 * 1024 * 1024,
    STUB_ARENA_OFFSET = 0x1000,
    STUB_DVD_MAX_OPEN_FILES = 64,
    STUB_DVD_FILE_ENTRY = 7,
    STUB_DVD_FILE_LENGTH = 70,
    STUB_GX_FIFO_MAX_CALLS = 64,
    STUB_GX_FIFO_MAX_BYTES = 512,
    STUB_DSP_MAX_EVENTS = 64,
    STUB_DISPATCH_MAX_ADDRESSES = 32
};

typedef struct StubDvdFileSlot {
    BOOL in_use;
    DVDFileInfo *file_info;
} StubDvdFileSlot;

#define STUB_ADDRESS_TOKEN_GENERATION_BITS \
    (28U - OS_HOST_ADDRESS_TOKEN_SLOT_BITS)
#define STUB_ADDRESS_TOKEN_GENERATION_MASK \
    ((UINT32_C(1) << STUB_ADDRESS_TOKEN_GENERATION_BITS) - UINT32_C(1))
#define STUB_ADDRESS_TOKEN (OS_HOST_ADDRESS_TOKEN_TAG | (1U << OS_HOST_ADDRESS_TOKEN_SLOT_BITS))

typedef struct StubAddressTokenSlot {
    const void *pointer;
    u32 token;
    u32 generation;
    BOOL in_use;
} StubAddressTokenSlot;

#if defined(__GNUC__)
static u8 stub_memory[STUB_MEMORY_SIZE] __attribute__((aligned(32)));
#else
static u8 stub_memory[STUB_MEMORY_SIZE];
#endif
static u8 native_pointer_bytes[OS_HOST_ADDRESS_TOKEN_SLOT_COUNT + 1U];
static unsigned int os_init_count;
static unsigned int pad_init_count;
static unsigned int demo_pad_init_count;
static unsigned int pad_read_count;
static PADStatus pad_read_status[PAD_MAX_CONTROLLERS];
static u32 pad_read_motor_mask;
static __thread BOOL interrupts_enabled = TRUE;
static pthread_mutex_t interrupt_execution_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t interrupt_observer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t interrupt_observer_condition = PTHREAD_COND_INITIALIZER;
static unsigned int interrupt_waiter_count;
static unsigned int interrupt_disable_count;
static unsigned int interrupt_restore_count;
static unsigned int interrupt_disable_transition_count;
static unsigned int interrupt_restore_transition_count;
static unsigned int dvd_init_count;
static unsigned int dvd_convert_call_count;
static unsigned int dvd_open_call_count;
static unsigned int dvd_fast_open_call_count;
static unsigned int dvd_read_call_count;
static unsigned int dvd_close_call_count;
static unsigned int dvd_cancel_call_count;
static unsigned int report_count;
static unsigned int token_encode_count;
static unsigned int token_release_count;
static unsigned int token_active_count;
static unsigned int token_next_slot;
static unsigned int token_decode_bias;
static u32 token_last_address = STUB_ADDRESS_TOKEN;
static StubAddressTokenSlot
    address_token_slots[OS_HOST_ADDRESS_TOKEN_SLOT_COUNT];
static unsigned int bootstrap_count;
static unsigned int runtime_prepare_count;
static u64 host_time_ticks;
static OSHostMemoryLayout memory_layout;
static BOOL memory_initialized;
static BOOL system_call_vector_mapped = TRUE;
static unsigned int decode_bias;
static void *arena_lo;
static void *arena_hi;
static unsigned int reject_arena_lo_countdown;
static unsigned int reject_arena_hi_countdown;
static char dvd_root[128] = "files";
static int dvd_fst_snapshot;
static StubDvdFileSlot dvd_files[STUB_DVD_MAX_OPEN_FILES];
static DVDFileInfo *dvd_last_open_file_info;
static DVDFileInfo *dvd_last_read_file_info;
static BOOL gx_fifo_accept = TRUE;
static unsigned int gx_fifo_call_count;
static unsigned int gx_fifo_queued_call_count;
static unsigned int gx_fifo_synchronous_call_count;
static unsigned int gx_fifo_call_sizes[STUB_GX_FIFO_MAX_CALLS];
static unsigned int gx_fifo_byte_count;
static u8 gx_fifo_bytes[STUB_GX_FIFO_MAX_BYTES];
static unsigned int gx_numeric_write_count;
static unsigned int vi_configure_count;
static unsigned int vi_init_count;
static GXRenderModeObj vi_last_render_mode;
static unsigned int vi_set_next_frame_buffer_call_count;
static u32 vi_next_frame_buffer_guest_address;
static u32 vi_pending_frame_buffer_guest_address;
static u32 vi_current_frame_buffer_guest_address;
static BOOL vi_set_next_frame_buffer_result = TRUE;
static unsigned int vi_wait_for_retrace_call_count;
static uint64_t vi_presentation_count;
static unsigned int vi_set_black_call_count;
static BOOL vi_black;
static unsigned int vi_flush_call_count;
static ARDMAResult ar_dma_result = AR_DMA_RESULT_SUCCESS;
static unsigned int ar_dma_call_count;
static u32 ar_dma_type;
static u32 ar_dma_main_memory;
static u32 ar_dma_aram;
static u32 ar_dma_length;
static DSPTaskInfo *dsp_first_task;
static DSPTaskInfo *dsp_last_task;
static DSPTaskInfo *dsp_current_task;
static BOOL dsp_dispatching;
static BOOL dsp_reject_next;
static u32 dsp_callback_mask =
    PORPOISE_STUB_DSP_CALLBACK_INIT |
    PORPOISE_STUB_DSP_CALLBACK_DONE;
static unsigned int dsp_add_task_call_count;
static unsigned int dsp_active_task_count;
static unsigned int dsp_event_count;
static u32 dsp_event_kind[STUB_DSP_MAX_EVENTS];
static u32 dsp_event_state[STUB_DSP_MAX_EVENTS];
static u32 dsp_event_flags[STUB_DSP_MAX_EVENTS];
static const DSPTaskInfo *dsp_last_submitted_task;
static const void *dsp_last_iram_memory;
static const void *dsp_last_dram_memory;
static u32 dsp_last_priority;
static u32 dsp_last_iram_length;
static u32 dsp_last_iram_address;
static u32 dsp_last_dram_length;
static u32 dsp_last_dram_address;
static u16 dsp_last_init_vector;
static u16 dsp_last_resume_vector;
static OSTime dsp_last_context_time;
static OSTime dsp_last_task_time;
static u32 dispatch_addresses[STUB_DISPATCH_MAX_ADDRESSES];
static size_t dispatch_address_count;

void SIM_GX_CommandProcessor_SendU8(u8 data) {
    (void)data;
    gx_numeric_write_count++;
}

void SIM_GX_CommandProcessor_SendU16(u16 data) {
    (void)data;
    gx_numeric_write_count++;
}

void SIM_GX_CommandProcessor_SendS16(s16 data) {
    (void)data;
    gx_numeric_write_count++;
}

void SIM_GX_CommandProcessor_SendU32(u32 data) {
    (void)data;
    gx_numeric_write_count++;
}

void SIM_GX_CommandProcessor_SendF32(f32 data) {
    (void)data;
    gx_numeric_write_count++;
}

static GXBool porpoise_stub_gx_fifo_record(
    const u8 *data,
    u32 size)
{
    if (size == 0U) {
        return GX_TRUE;
    }
    if (data == NULL || !gx_fifo_accept ||
        gx_fifo_call_count >= STUB_GX_FIFO_MAX_CALLS ||
        size > STUB_GX_FIFO_MAX_BYTES - gx_fifo_byte_count) {
        return GX_FALSE;
    }
    gx_fifo_call_sizes[gx_fifo_call_count] = (unsigned int)size;
    gx_fifo_call_count++;
    memcpy(&gx_fifo_bytes[gx_fifo_byte_count], data, (size_t)size);
    gx_fifo_byte_count += (unsigned int)size;
    return GX_TRUE;
}

GXBool SIM_GX_CommandProcessor_SendCanonicalBytes(
    const u8 *data,
    u32 size)
{
    gx_fifo_synchronous_call_count++;
    return porpoise_stub_gx_fifo_record(data, size);
}

GXBool SIM_GX_CommandProcessor_QueueCanonicalBytes(
    const u8 *data,
    u32 size)
{
    gx_fifo_queued_call_count++;
    return porpoise_stub_gx_fifo_record(data, size);
}

void VIConfigure(const GXRenderModeObj *mode) {
    if (mode != NULL) {
        vi_last_render_mode = *mode;
        vi_configure_count++;
    }
}

void VIInit(void) {
    vi_init_count++;
}

void VIWaitForRetrace(void) {
    vi_wait_for_retrace_call_count++;
    if (vi_pending_frame_buffer_guest_address != 0U) {
        vi_current_frame_buffer_guest_address =
            vi_pending_frame_buffer_guest_address;
        vi_pending_frame_buffer_guest_address = 0U;
    }
    if (vi_current_frame_buffer_guest_address != 0U) {
        vi_presentation_count++;
    }
}

void VISetBlack(BOOL black) {
    vi_set_black_call_count++;
    vi_black = black;
}

void VIFlush(void) {
    vi_flush_call_count++;
}

BOOL PADInit(void) {
    pad_init_count++;
    return TRUE;
}

u32 PADRead(PADStatus *status) {
    pad_read_count++;
    memcpy(
        status,
        pad_read_status,
        sizeof(pad_read_status));
    return pad_read_motor_mask;
}

void DEMOPadInit(void) {
    demo_pad_init_count++;
    (void)PADInit();
}

#ifndef PORPOISE_STUB_DISABLE_VI_NEXT_FRAMEBUFFER_GUEST_ADDRESS_CONTRACT
BOOL VIHostSetNextFrameBufferGuestAddress(u32 guest_address) {
    vi_set_next_frame_buffer_call_count++;
    vi_next_frame_buffer_guest_address = guest_address;
    if (vi_set_next_frame_buffer_result != FALSE) {
        vi_pending_frame_buffer_guest_address = guest_address;
    }
    return vi_set_next_frame_buffer_result;
}
#endif

ARDMAResult ARStartDMAEx(
    u32 type,
    u32 mainmem_addr,
    u32 aram_addr,
    u32 length) {
    ar_dma_call_count++;
    ar_dma_type = type;
    ar_dma_main_memory = mainmem_addr;
    ar_dma_aram = aram_addr;
    ar_dma_length = length;
    return ar_dma_result;
}

static BOOL stub_dsp_task_is_queued(const DSPTaskInfo *task)
{
    const DSPTaskInfo *current;

    for (current = dsp_first_task;
         current != NULL;
         current = current->next) {
        if (current == task) {
            return TRUE;
        }
    }
    return FALSE;
}

static void stub_dsp_insert_task(DSPTaskInfo *task)
{
    DSPTaskInfo *current;

    task->next = NULL;
    task->prev = NULL;
    if (dsp_first_task == NULL) {
        dsp_first_task = task;
        dsp_last_task = task;
        return;
    }
    current = dsp_first_task;
    while (current != NULL && task->priority >= current->priority) {
        current = current->next;
    }
    if (current == NULL) {
        task->prev = dsp_last_task;
        dsp_last_task->next = task;
        dsp_last_task = task;
        return;
    }
    task->next = current;
    task->prev = current->prev;
    current->prev = task;
    if (task->prev != NULL) {
        task->prev->next = task;
    } else {
        dsp_first_task = task;
    }
}

static void stub_dsp_remove_task(DSPTaskInfo *task)
{
    if (task->prev != NULL) {
        task->prev->next = task->next;
    } else {
        dsp_first_task = task->next;
    }
    if (task->next != NULL) {
        task->next->prev = task->prev;
    } else {
        dsp_last_task = task->prev;
    }
    task->next = NULL;
    task->prev = NULL;
    task->flags = DSP_TASK_FLAG_CLEARALL;
    task->state = DSP_TASK_STATE_DONE;
    if (dsp_active_task_count != 0U) {
        dsp_active_task_count--;
    }
}

static void stub_dsp_invoke_callback(
    DSPTaskInfo *task,
    u32 event_kind,
    DSPCallback callback)
{
    if ((dsp_callback_mask & event_kind) == 0U || callback == NULL) {
        return;
    }
    if (dsp_event_count < STUB_DSP_MAX_EVENTS) {
        dsp_event_kind[dsp_event_count] = event_kind;
        dsp_event_state[dsp_event_count] = (u32)task->state;
        dsp_event_flags[dsp_event_count] = (u32)task->flags;
        dsp_event_count++;
    }
    callback(task);
}

static void stub_dsp_dispatch_tasks(void)
{
    dsp_dispatching = TRUE;
    while (dsp_first_task != NULL) {
        DSPTaskInfo *task = dsp_first_task;
        dsp_current_task = task;
        task->state = DSP_TASK_STATE_RUN;
        stub_dsp_invoke_callback(
            task, PORPOISE_STUB_DSP_CALLBACK_INIT, task->init_cb);
        stub_dsp_invoke_callback(
            task, PORPOISE_STUB_DSP_CALLBACK_RESUME, task->res_cb);
        stub_dsp_invoke_callback(
            task, PORPOISE_STUB_DSP_CALLBACK_REQUEST, task->req_cb);
        stub_dsp_invoke_callback(
            task, PORPOISE_STUB_DSP_CALLBACK_DONE, task->done_cb);
        stub_dsp_remove_task(task);
    }
    dsp_current_task = NULL;
    dsp_dispatching = FALSE;
}

DSPTaskInfo *DSPAddTask(DSPTaskInfo *task)
{
    dsp_add_task_call_count++;
    if (task == NULL || dsp_reject_next) {
        dsp_reject_next = FALSE;
        return NULL;
    }

    dsp_last_submitted_task = task;
    dsp_last_iram_memory = task->iram_mmem_addr;
    dsp_last_dram_memory = task->dram_mmem_addr;
    dsp_last_priority = (u32)task->priority;
    dsp_last_iram_length = task->iram_length;
    dsp_last_iram_address = task->iram_addr;
    dsp_last_dram_length = task->dram_length;
    dsp_last_dram_address = task->dram_addr;
    dsp_last_init_vector = task->dsp_init_vector;
    dsp_last_resume_vector = task->dsp_resume_vector;
    dsp_last_context_time = task->t_context;
    dsp_last_task_time = task->t_task;

    if (stub_dsp_task_is_queued(task)) {
        return task;
    }
    task->state = DSP_TASK_STATE_INIT;
    task->flags = DSP_TASK_FLAG_ATTACHED;
    stub_dsp_insert_task(task);
    dsp_active_task_count++;
    if (!dsp_dispatching) {
        stub_dsp_dispatch_tasks();
    }
    return task;
}

int PorpoiseStubDispatchAvailable(uint32_t address)
{
    size_t index;

    for (index = 0U; index < dispatch_address_count; index++) {
        if (dispatch_addresses[index] == address) {
            return 1;
        }
    }
    return 0;
}

static BOOL pointer_is_in_memory(const void *pointer) {
    const uintptr_t value = (uintptr_t)pointer;
    const uintptr_t begin = (uintptr_t)&stub_memory[0];
    const uintptr_t end = begin + (uintptr_t)sizeof(stub_memory);

    return value >= begin && value < end;
}

const OSHostMemoryLayout *__OSHostMemoryInit(OSHostMemoryProfile profile) {
    memory_layout.profile = profile;
    memory_layout.cachedBase = &stub_memory[0];
    memory_layout.uncachedBase = &stub_memory[0];
    memory_layout.size = (u32)sizeof(stub_memory);
    memory_layout.consoleSize = (u32)sizeof(stub_memory);
    memory_layout.arenaLo = &stub_memory[STUB_ARENA_OFFSET];
    memory_layout.arenaHi = &stub_memory[sizeof(stub_memory)];
    memory_layout.consoleArenaHi = memory_layout.arenaHi;
    memory_initialized = TRUE;
    return &memory_layout;
}

const OSHostMemoryLayout *__OSHostMemoryGetLayout(void) {
    return memory_initialized ? &memory_layout : NULL;
}

BOOL __OSHostMemoryContainsAddress(const void *address) {
    return pointer_is_in_memory(address);
}

void *__OSHostMemoryResolveArenaHi(void *previous, void *requested) {
    (void)previous;
    if (pointer_is_in_memory(requested)) {
        return requested;
    }
    return memory_initialized ? memory_layout.arenaHi : NULL;
}

BOOL __OSHostIsAddressToken(u32 address) {
    return (address & OS_HOST_ADDRESS_TOKEN_MASK) == OS_HOST_ADDRESS_TOKEN_TAG;
}

BOOL __OSHostIsFileBackedImageAddress(const void *pointer) {
    (void)pointer;
    return FALSE;
}

static BOOL pointer_is_native(const void *pointer) {
    const uintptr_t value = (uintptr_t)pointer;
    const uintptr_t begin = (uintptr_t)&native_pointer_bytes[0];
    const uintptr_t end = begin + (uintptr_t)sizeof(native_pointer_bytes);

    return value >= begin && value < end;
}

u32 __OSHostEncodeAddress(const void *pointer) {
    const uintptr_t value = (uintptr_t)pointer;
    const uintptr_t begin = (uintptr_t)&stub_memory[0];
    unsigned int scanned;

    if (!pointer_is_in_memory(pointer)) {
        if (!pointer_is_native(pointer)) {
            return 0U;
        }
        token_encode_count++;
        if (token_active_count >= OS_HOST_ADDRESS_TOKEN_SLOT_COUNT) {
            return 0U;
        }
        for (scanned = 0U;
             scanned < OS_HOST_ADDRESS_TOKEN_SLOT_COUNT;
             scanned++) {
            unsigned int slot_index =
                (token_next_slot + scanned) %
                OS_HOST_ADDRESS_TOKEN_SLOT_COUNT;
            StubAddressTokenSlot *slot = &address_token_slots[slot_index];

            if (slot->in_use) {
                continue;
            }
            slot->generation =
                (slot->generation + 1U) &
                STUB_ADDRESS_TOKEN_GENERATION_MASK;
            if (slot->generation == 0U) {
                slot->generation = 1U;
            }
            slot->pointer = pointer;
            slot->token =
                OS_HOST_ADDRESS_TOKEN_TAG |
                (slot->generation << OS_HOST_ADDRESS_TOKEN_SLOT_BITS) |
                (u32)slot_index;
            slot->in_use = TRUE;
            token_active_count++;
            token_next_slot =
                (slot_index + 1U) % OS_HOST_ADDRESS_TOKEN_SLOT_COUNT;
            token_last_address = slot->token;
            return slot->token;
        }
        return 0U;
    }
    return 0x80000000U + (u32)(value - begin);
}

u32 __OSHostEncodePointerWord(const void *pointer) {
    return __OSHostEncodeAddress(pointer);
}

void *__OSHostDecodeAddress(u32 address) {
    u32 offset;

    if (__OSHostIsAddressToken(address)) {
        unsigned int slot_index =
            (unsigned int)(address &
                           (OS_HOST_ADDRESS_TOKEN_SLOT_COUNT - 1U));
        const StubAddressTokenSlot *slot =
            &address_token_slots[slot_index];

        if (!slot->in_use || slot->token != address) {
            return NULL;
        }
        return (void *)((uintptr_t)slot->pointer +
                        (uintptr_t)token_decode_bias);
    }

    if (address < (u32)sizeof(stub_memory)) {
        offset = address;
    } else if (address >= 0x80000000U &&
               address < 0x80000000U + (u32)sizeof(stub_memory)) {
        offset = address - 0x80000000U;
    } else if (address >= 0xC0000000U &&
               address < 0xC0000000U + (u32)sizeof(stub_memory)) {
        offset = address - 0xC0000000U;
    } else {
        return NULL;
    }

    if (!system_call_vector_mapped && offset >= 0xC00U &&
        offset < 0xC1CU) {
        return NULL;
    }

    if (decode_bias != 0U &&
        decode_bias >=
            (unsigned int)sizeof(stub_memory) - (unsigned int)offset) {
        return NULL;
    }
    return &stub_memory[offset] + decode_bias;
}

void __OSHostReleaseAddress(u32 token) {
    unsigned int slot_index;
    StubAddressTokenSlot *slot;

    if (!__OSHostIsAddressToken(token)) {
        return;
    }
    token_release_count++;
    slot_index = (unsigned int)(
        token & (OS_HOST_ADDRESS_TOKEN_SLOT_COUNT - 1U));
    slot = &address_token_slots[slot_index];
    if (slot->in_use && slot->token == token) {
        slot->pointer = NULL;
        slot->in_use = FALSE;
        token_active_count--;
        if (slot_index < token_next_slot || token_active_count == 0U) {
            token_next_slot = slot_index;
        }
    }
}

void OSInit(void) {
    ++os_init_count;
    if (!memory_initialized) {
        (void)__OSHostMemoryInit(OS_HOST_MEMORY_PROFILE_GAMECUBE);
    }
    arena_lo = memory_layout.arenaLo;
    arena_hi = memory_layout.arenaHi;
}

void OSReport(const char *format, ...) {
    va_list arguments;

    if (format == NULL) {
        return;
    }
    ++report_count;
    va_start(arguments, format);
    (void)vfprintf(stdout, format, arguments);
    va_end(arguments);
    (void)fflush(stdout);
}

static void stub_require_thread_success(int result)
{
    if (result != 0) {
        abort();
    }
}

static void stub_lock_interrupt_execution(void)
{
    stub_require_thread_success(pthread_mutex_lock(&interrupt_observer_mutex));
    interrupt_waiter_count++;
    stub_require_thread_success(
        pthread_cond_broadcast(&interrupt_observer_condition));
    stub_require_thread_success(
        pthread_mutex_unlock(&interrupt_observer_mutex));

    stub_require_thread_success(pthread_mutex_lock(&interrupt_execution_mutex));

    stub_require_thread_success(pthread_mutex_lock(&interrupt_observer_mutex));
    if (interrupt_waiter_count == 0U) {
        abort();
    }
    interrupt_waiter_count--;
    stub_require_thread_success(
        pthread_cond_broadcast(&interrupt_observer_condition));
    stub_require_thread_success(
        pthread_mutex_unlock(&interrupt_observer_mutex));
}

BOOL OSDisableInterrupts(void)
{
    BOOL previous = interrupts_enabled;

    if (previous) {
        stub_lock_interrupt_execution();
        interrupts_enabled = FALSE;
    }

    stub_require_thread_success(pthread_mutex_lock(&interrupt_observer_mutex));
    interrupt_disable_count++;
    if (previous) {
        interrupt_disable_transition_count++;
    }
    stub_require_thread_success(
        pthread_mutex_unlock(&interrupt_observer_mutex));
    return previous;
}

BOOL OSRestoreInterrupts(BOOL enabled)
{
    BOOL previous = interrupts_enabled;

    if (previous && !enabled) {
        stub_lock_interrupt_execution();
    }

    stub_require_thread_success(pthread_mutex_lock(&interrupt_observer_mutex));
    interrupt_restore_count++;
    if (previous != enabled) {
        interrupt_restore_transition_count++;
    }
    interrupts_enabled = enabled;
    stub_require_thread_success(
        pthread_mutex_unlock(&interrupt_observer_mutex));

    if (!previous && enabled) {
        stub_require_thread_success(
            pthread_mutex_unlock(&interrupt_execution_mutex));
    }
    return previous;
}

BOOL DVDSetRootDirectory(const char *path) {
    size_t length;

    if (path == NULL || path[0] == '\0') {
        return FALSE;
    }
    length = strlen(path);
    if (length >= sizeof(dvd_root)) {
        return FALSE;
    }
    memcpy(dvd_root, path, length + 1U);
    return TRUE;
}

void DVDInit(void) {
    ++dvd_init_count;
    dvd_fst_snapshot = 1;
}

void *DVDGetFSTLocation(void) {
    return dvd_fst_snapshot ? &dvd_fst_snapshot : NULL;
}

static StubDvdFileSlot *stub_dvd_find_file(DVDFileInfo *file_info) {
    size_t index;

    for (index = 0U; index < STUB_DVD_MAX_OPEN_FILES; index++) {
        if (dvd_files[index].in_use &&
            dvd_files[index].file_info == file_info) {
            return &dvd_files[index];
        }
    }
    return NULL;
}

static BOOL stub_dvd_open_entry(
    s32 entry_number,
    DVDFileInfo *file_info) {
    size_t index;

    if (entry_number != STUB_DVD_FILE_ENTRY || file_info == NULL) {
        return FALSE;
    }
    for (index = 0U; index < STUB_DVD_MAX_OPEN_FILES; index++) {
        if (!dvd_files[index].in_use) {
            memset(file_info, 0, sizeof(*file_info));
            file_info->startAddr = UINT32_C(0x00123000);
            file_info->length = STUB_DVD_FILE_LENGTH;
            file_info->cBlock.state = DVD_STATE_END;
            dvd_files[index].in_use = TRUE;
            dvd_files[index].file_info = file_info;
            dvd_last_open_file_info = file_info;
            return TRUE;
        }
    }
    return FALSE;
}

s32 DVDConvertPathToEntrynum(const char *path) {
    dvd_convert_call_count++;
    if (!dvd_fst_snapshot || path == NULL) {
        return -1;
    }
    if (strcmp(path, "/test.bin") == 0 ||
        strcmp(path, "test.bin") == 0) {
        return STUB_DVD_FILE_ENTRY;
    }
    return -1;
}

BOOL DVDOpen(const char *filename, DVDFileInfo *file_info) {
    s32 entry_number;

    dvd_open_call_count++;
    entry_number = DVDConvertPathToEntrynum(filename);
    return stub_dvd_open_entry(entry_number, file_info);
}

BOOL DVDFastOpen(s32 entry_number, DVDFileInfo *file_info) {
    dvd_fast_open_call_count++;
    return stub_dvd_open_entry(entry_number, file_info);
}

uint8_t PorpoiseStubDVDExpectedByte(uint32_t offset) {
    return (uint8_t)((offset * UINT32_C(37) + UINT32_C(11)) &
                     UINT32_C(0xFF));
}

s32 DVDReadPrio(
    DVDFileInfo *file_info,
    void *address,
    s32 length,
    s32 offset,
    s32 priority) {
    StubDvdFileSlot *slot;
    uint64_t end_offset;
    size_t file_bytes;
    size_t index;

    (void)priority;
    dvd_read_call_count++;
    dvd_last_read_file_info = file_info;
    slot = stub_dvd_find_file(file_info);
    if (slot == NULL || address == NULL || length < 0 || offset < 0 ||
        ((uintptr_t)address & UINT32_C(31)) != 0U) {
        return DVD_RESULT_FATAL_ERROR;
    }
    end_offset = (uint64_t)(uint32_t)offset +
                 (uint64_t)(uint32_t)length;
    if ((uint32_t)offset >= file_info->length ||
        end_offset >=
            (uint64_t)file_info->length + DVD_MIN_TRANSFER_SIZE) {
        return DVD_RESULT_FATAL_ERROR;
    }

    file_info->cBlock.state = DVD_STATE_BUSY;
    file_bytes = (size_t)(uint32_t)length;
    if (end_offset > file_info->length) {
        file_bytes =
            (size_t)(file_info->length - (uint32_t)offset);
    }
    for (index = 0U; index < file_bytes; index++) {
        ((uint8_t *)address)[index] = PorpoiseStubDVDExpectedByte(
            (uint32_t)offset + (uint32_t)index);
    }
    if (file_bytes < (size_t)(uint32_t)length) {
        memset(
            (uint8_t *)address + file_bytes,
            0,
            (size_t)(uint32_t)length - file_bytes);
    }
    file_info->cBlock.currTransferSize = (uint32_t)length;
    file_info->cBlock.transferredSize = (uint32_t)length;
    file_info->cBlock.state = DVD_STATE_END;
    return length;
}

BOOL DVDClose(DVDFileInfo *file_info) {
    StubDvdFileSlot *slot;

    dvd_close_call_count++;
    slot = stub_dvd_find_file(file_info);
    if (slot == NULL) {
        return FALSE;
    }
    memset(slot, 0, sizeof(*slot));
    file_info->cBlock.state = DVD_STATE_END;
    return TRUE;
}

s32 DVDGetCommandBlockStatus(const DVDCommandBlock *command_block) {
    if (command_block == NULL) {
        return DVD_STATE_FATAL_ERROR;
    }
    return command_block->state == DVD_STATE_COVER_CLOSED
               ? DVD_STATE_BUSY
               : command_block->state;
}

s32 DVDCancel(DVDCommandBlock *command_block) {
    dvd_cancel_call_count++;
    if (command_block == NULL) {
        return DVD_RESULT_FATAL_ERROR;
    }
    if (command_block->state != DVD_STATE_FATAL_ERROR &&
        command_block->state != DVD_STATE_END &&
        command_block->state != DVD_STATE_CANCELED) {
        command_block->state = DVD_STATE_CANCELED;
    }
    return 0;
}

void *OSGetArenaLo(void) {
    return arena_lo;
}

void *OSGetArenaHi(void) {
    return arena_hi;
}

void OSSetArenaLo(void *value) {
    if (reject_arena_lo_countdown != 0U) {
        reject_arena_lo_countdown--;
        if (reject_arena_lo_countdown == 0U) {
            return;
        }
    }
    arena_lo = value;
}

void OSSetArenaHi(void *value) {
    if (reject_arena_hi_countdown != 0U) {
        reject_arena_hi_countdown--;
        if (reject_arena_hi_countdown == 0U) {
            return;
        }
    }
    arena_hi = value;
}

void PorpoiseStubRejectNextArenaLo(void) {
    PorpoiseStubRejectArenaLoOnCall(1U);
}

void PorpoiseStubRejectNextArenaHi(void) {
    PorpoiseStubRejectArenaHiOnCall(1U);
}

void PorpoiseStubRejectArenaLoOnCall(unsigned int call_number) {
    reject_arena_lo_countdown = call_number;
}

void PorpoiseStubRejectArenaHiOnCall(unsigned int call_number) {
    reject_arena_hi_countdown = call_number;
}

OSTime OSGetTime(void) {
    host_time_ticks++;
    return (OSTime)host_time_ticks;
}

unsigned int PorpoiseStubOSInitCount(void) {
    return os_init_count;
}

unsigned int PorpoiseStubPADInitCount(void) {
    return pad_init_count;
}

unsigned int PorpoiseStubDEMOPadInitCount(void) {
    return demo_pad_init_count;
}

void PorpoiseStubPADReadReset(void) {
    memset(pad_read_status, 0, sizeof(pad_read_status));
    pad_read_status[0].button = UINT16_C(0x1234);
    pad_read_status[0].stickX = INT8_C(-128);
    pad_read_status[0].stickY = INT8_C(127);
    pad_read_status[0].substickX = INT8_C(-2);
    pad_read_status[0].substickY = INT8_C(2);
    pad_read_status[0].triggerLeft = UINT8_C(0x56);
    pad_read_status[0].triggerRight = UINT8_C(0x78);
    pad_read_status[0].analogA = UINT8_C(0x9A);
    pad_read_status[0].analogB = UINT8_C(0xBC);
    pad_read_status[0].err = INT8_C(-3);

    pad_read_status[1].button = UINT16_C(0xABCD);
    pad_read_status[1].stickX = INT8_C(-1);
    pad_read_status[1].stickY = INT8_C(1);
    pad_read_status[1].substickX = INT8_C(-64);
    pad_read_status[1].substickY = INT8_C(64);
    pad_read_status[1].triggerLeft = UINT8_C(0x10);
    pad_read_status[1].triggerRight = UINT8_C(0x20);
    pad_read_status[1].analogA = UINT8_C(0x30);
    pad_read_status[1].analogB = UINT8_C(0x40);

    pad_read_status[2].button = UINT16_C(0x1000);
    pad_read_status[2].err = INT8_C(-1);
    pad_read_status[3].err = INT8_C(-2);
    pad_read_motor_mask = UINT32_C(0xA0000000);
    pad_read_count = 0U;
}

unsigned int PorpoiseStubPADReadCount(void) {
    return pad_read_count;
}

void PorpoiseStubInterruptReset(void) {
    if (!interrupts_enabled) {
        abort();
    }
    stub_require_thread_success(pthread_mutex_lock(&interrupt_observer_mutex));
    if (interrupt_waiter_count != 0U) {
        abort();
    }
    interrupt_disable_count = 0U;
    interrupt_restore_count = 0U;
    interrupt_disable_transition_count = 0U;
    interrupt_restore_transition_count = 0U;
    stub_require_thread_success(
        pthread_mutex_unlock(&interrupt_observer_mutex));
}

int PorpoiseStubInterruptsEnabled(void) {
    return interrupts_enabled ? 1 : 0;
}

unsigned int PorpoiseStubInterruptDisableCount(void) {
    unsigned int result;

    stub_require_thread_success(pthread_mutex_lock(&interrupt_observer_mutex));
    result = interrupt_disable_count;
    stub_require_thread_success(
        pthread_mutex_unlock(&interrupt_observer_mutex));
    return result;
}

unsigned int PorpoiseStubInterruptRestoreCount(void) {
    unsigned int result;

    stub_require_thread_success(pthread_mutex_lock(&interrupt_observer_mutex));
    result = interrupt_restore_count;
    stub_require_thread_success(
        pthread_mutex_unlock(&interrupt_observer_mutex));
    return result;
}

unsigned int PorpoiseStubInterruptDisableTransitionCount(void) {
    unsigned int result;

    stub_require_thread_success(pthread_mutex_lock(&interrupt_observer_mutex));
    result = interrupt_disable_transition_count;
    stub_require_thread_success(
        pthread_mutex_unlock(&interrupt_observer_mutex));
    return result;
}

unsigned int PorpoiseStubInterruptRestoreTransitionCount(void) {
    unsigned int result;

    stub_require_thread_success(pthread_mutex_lock(&interrupt_observer_mutex));
    result = interrupt_restore_transition_count;
    stub_require_thread_success(
        pthread_mutex_unlock(&interrupt_observer_mutex));
    return result;
}

void PorpoiseStubWaitForInterruptWaiter(void)
{
    stub_require_thread_success(pthread_mutex_lock(&interrupt_observer_mutex));
    while (interrupt_waiter_count == 0U) {
        stub_require_thread_success(pthread_cond_wait(
            &interrupt_observer_condition,
            &interrupt_observer_mutex));
    }
    stub_require_thread_success(
        pthread_mutex_unlock(&interrupt_observer_mutex));
}

void PorpoiseStubSetSystemCallVectorMapped(int mapped) {
    system_call_vector_mapped = mapped ? TRUE : FALSE;
}

unsigned int PorpoiseStubDVDInitCount(void) {
    return dvd_init_count;
}

const char *PorpoiseStubDVDRoot(void) {
    return dvd_root;
}

unsigned int PorpoiseStubDVDConvertCallCount(void) {
    return dvd_convert_call_count;
}

unsigned int PorpoiseStubDVDOpenCallCount(void) {
    return dvd_open_call_count;
}

unsigned int PorpoiseStubDVDFastOpenCallCount(void) {
    return dvd_fast_open_call_count;
}

unsigned int PorpoiseStubDVDReadCallCount(void) {
    return dvd_read_call_count;
}

unsigned int PorpoiseStubDVDCloseCallCount(void) {
    return dvd_close_call_count;
}

unsigned int PorpoiseStubDVDCancelCallCount(void) {
    return dvd_cancel_call_count;
}

unsigned int PorpoiseStubDVDActiveFileCount(void) {
    size_t index;
    unsigned int count = 0U;

    for (index = 0U; index < STUB_DVD_MAX_OPEN_FILES; index++) {
        if (dvd_files[index].in_use) {
            count++;
        }
    }
    return count;
}

const void *PorpoiseStubDVDLastOpenFileInfo(void) {
    return dvd_last_open_file_info;
}

const void *PorpoiseStubDVDLastReadFileInfo(void) {
    return dvd_last_read_file_info;
}

void PorpoiseStubGXFifoReset(void) {
    gx_fifo_accept = TRUE;
    gx_fifo_call_count = 0U;
    gx_fifo_queued_call_count = 0U;
    gx_fifo_synchronous_call_count = 0U;
    memset(gx_fifo_call_sizes, 0, sizeof(gx_fifo_call_sizes));
    gx_fifo_byte_count = 0U;
    memset(gx_fifo_bytes, 0, sizeof(gx_fifo_bytes));
    gx_numeric_write_count = 0U;
}

void PorpoiseStubGXFifoSetAccept(int accept) {
    gx_fifo_accept = accept ? TRUE : FALSE;
}

unsigned int PorpoiseStubGXFifoCallCount(void) {
    return gx_fifo_call_count;
}

unsigned int PorpoiseStubGXFifoQueuedCallCount(void) {
    return gx_fifo_queued_call_count;
}

unsigned int PorpoiseStubGXFifoSynchronousCallCount(void) {
    return gx_fifo_synchronous_call_count;
}

unsigned int PorpoiseStubGXFifoCallSize(unsigned int index) {
    if (index >= gx_fifo_call_count ||
        index >= STUB_GX_FIFO_MAX_CALLS) {
        return 0U;
    }
    return gx_fifo_call_sizes[index];
}

unsigned int PorpoiseStubGXFifoByteCount(void) {
    return gx_fifo_byte_count;
}

uint8_t PorpoiseStubGXFifoByte(unsigned int index) {
    if (index >= gx_fifo_byte_count ||
        index >= STUB_GX_FIFO_MAX_BYTES) {
        return UINT8_C(0);
    }
    return gx_fifo_bytes[index];
}

unsigned int PorpoiseStubGXNumericWriteCount(void) {
    return gx_numeric_write_count;
}

void PorpoiseStubSetDecodeBias(unsigned int bias) {
    decode_bias = bias;
}

void PorpoiseStubVIReset(void) {
    vi_configure_count = 0U;
    memset(&vi_last_render_mode, 0, sizeof(vi_last_render_mode));
    vi_set_next_frame_buffer_call_count = 0U;
    vi_next_frame_buffer_guest_address = 0U;
    vi_pending_frame_buffer_guest_address = 0U;
    vi_current_frame_buffer_guest_address = 0U;
    vi_set_next_frame_buffer_result = TRUE;
    vi_wait_for_retrace_call_count = 0U;
    vi_presentation_count = UINT64_C(0);
    vi_set_black_call_count = 0U;
    vi_black = FALSE;
    vi_flush_call_count = 0U;
}

unsigned int PorpoiseStubVIInitCount(void) {
    return vi_init_count;
}

unsigned int PorpoiseStubVIConfigureCount(void) {
    return vi_configure_count;
}

const void *PorpoiseStubVILastRenderMode(void) {
    return &vi_last_render_mode;
}

void PorpoiseStubVISetNextFrameBufferResult(int result) {
    vi_set_next_frame_buffer_result = result != 0 ? TRUE : FALSE;
}

unsigned int PorpoiseStubVISetNextFrameBufferCallCount(void) {
    return vi_set_next_frame_buffer_call_count;
}

unsigned int PorpoiseStubVIWaitForRetraceCallCount(void) {
    return vi_wait_for_retrace_call_count;
}

unsigned int PorpoiseStubVISetBlackCallCount(void) {
    return vi_set_black_call_count;
}

uint32_t PorpoiseStubVIBlack(void) {
    return vi_black != FALSE ? UINT32_C(1) : UINT32_C(0);
}

unsigned int PorpoiseStubVIFlushCallCount(void) {
    return vi_flush_call_count;
}

uint64_t PorpoiseStubVIPresentationCount(void) {
    return vi_presentation_count;
}

uint32_t PorpoiseStubVICurrentFrameBufferGuestAddress(void) {
    return (uint32_t)vi_current_frame_buffer_guest_address;
}

uint32_t PorpoiseStubVINextFrameBufferGuestAddress(void) {
    return (uint32_t)vi_next_frame_buffer_guest_address;
}

uint32_t PorpoiseStubVIPendingFrameBufferGuestAddress(void) {
    return (uint32_t)vi_pending_frame_buffer_guest_address;
}

void PorpoiseStubARReset(void) {
    ar_dma_result = AR_DMA_RESULT_SUCCESS;
    ar_dma_call_count = 0U;
    ar_dma_type = 0U;
    ar_dma_main_memory = 0U;
    ar_dma_aram = 0U;
    ar_dma_length = 0U;
}

void PorpoiseStubARSetDMAResult(int result) {
    ar_dma_result = (ARDMAResult)result;
}

unsigned int PorpoiseStubARDMACallCount(void) {
    return ar_dma_call_count;
}

uint32_t PorpoiseStubARLastDMAType(void) {
    return (uint32_t)ar_dma_type;
}

uint32_t PorpoiseStubARLastDMAMainMemory(void) {
    return (uint32_t)ar_dma_main_memory;
}

uint32_t PorpoiseStubARLastDMAAram(void) {
    return (uint32_t)ar_dma_aram;
}

uint32_t PorpoiseStubARLastDMALength(void) {
    return (uint32_t)ar_dma_length;
}

void PorpoiseStubDSPReset(void) {
    dsp_first_task = NULL;
    dsp_last_task = NULL;
    dsp_current_task = NULL;
    dsp_dispatching = FALSE;
    dsp_reject_next = FALSE;
    dsp_callback_mask =
        PORPOISE_STUB_DSP_CALLBACK_INIT |
        PORPOISE_STUB_DSP_CALLBACK_DONE;
    dsp_add_task_call_count = 0U;
    dsp_active_task_count = 0U;
    dsp_event_count = 0U;
    memset(dsp_event_kind, 0, sizeof(dsp_event_kind));
    memset(dsp_event_state, 0, sizeof(dsp_event_state));
    memset(dsp_event_flags, 0, sizeof(dsp_event_flags));
    dsp_last_submitted_task = NULL;
    dsp_last_iram_memory = NULL;
    dsp_last_dram_memory = NULL;
    dsp_last_priority = 0U;
    dsp_last_iram_length = 0U;
    dsp_last_iram_address = 0U;
    dsp_last_dram_length = 0U;
    dsp_last_dram_address = 0U;
    dsp_last_init_vector = 0U;
    dsp_last_resume_vector = 0U;
    dsp_last_context_time = 0;
    dsp_last_task_time = 0;
}

void PorpoiseStubDSPSetCallbackMask(uint32_t mask) {
    dsp_callback_mask = (u32)mask;
}

void PorpoiseStubDSPRejectNext(int reject) {
    dsp_reject_next = reject ? TRUE : FALSE;
}

unsigned int PorpoiseStubDSPAddTaskCallCount(void) {
    return dsp_add_task_call_count;
}

unsigned int PorpoiseStubDSPActiveTaskCount(void) {
    return dsp_active_task_count;
}

unsigned int PorpoiseStubDSPEventCount(void) {
    return dsp_event_count;
}

uint32_t PorpoiseStubDSPEventKind(unsigned int index) {
    return index < dsp_event_count && index < STUB_DSP_MAX_EVENTS
               ? (uint32_t)dsp_event_kind[index]
               : 0U;
}

uint32_t PorpoiseStubDSPEventState(unsigned int index) {
    return index < dsp_event_count && index < STUB_DSP_MAX_EVENTS
               ? (uint32_t)dsp_event_state[index]
               : 0U;
}

uint32_t PorpoiseStubDSPEventFlags(unsigned int index) {
    return index < dsp_event_count && index < STUB_DSP_MAX_EVENTS
               ? (uint32_t)dsp_event_flags[index]
               : 0U;
}

const void *PorpoiseStubDSPLastTask(void) {
    return dsp_last_submitted_task;
}

const void *PorpoiseStubDSPLastIramMemory(void) {
    return dsp_last_iram_memory;
}

const void *PorpoiseStubDSPLastDramMemory(void) {
    return dsp_last_dram_memory;
}

uint32_t PorpoiseStubDSPLastPriority(void) {
    return (uint32_t)dsp_last_priority;
}

uint32_t PorpoiseStubDSPLastIramLength(void) {
    return (uint32_t)dsp_last_iram_length;
}

uint32_t PorpoiseStubDSPLastIramAddress(void) {
    return (uint32_t)dsp_last_iram_address;
}

uint32_t PorpoiseStubDSPLastDramLength(void) {
    return (uint32_t)dsp_last_dram_length;
}

uint32_t PorpoiseStubDSPLastDramAddress(void) {
    return (uint32_t)dsp_last_dram_address;
}

uint16_t PorpoiseStubDSPLastInitVector(void) {
    return (uint16_t)dsp_last_init_vector;
}

uint16_t PorpoiseStubDSPLastResumeVector(void) {
    return (uint16_t)dsp_last_resume_vector;
}

int64_t PorpoiseStubDSPLastContextTime(void) {
    return (int64_t)dsp_last_context_time;
}

int64_t PorpoiseStubDSPLastTaskTime(void) {
    return (int64_t)dsp_last_task_time;
}

void PorpoiseStubDispatchReset(void) {
    dispatch_address_count = 0U;
    memset(dispatch_addresses, 0, sizeof(dispatch_addresses));
}

int PorpoiseStubDispatchAddAddress(uint32_t address) {
    size_t index;

    if (address == 0U || (address & UINT32_C(3)) != 0U) {
        return 0;
    }
    for (index = 0U; index < dispatch_address_count; index++) {
        if (dispatch_addresses[index] == address) {
            return 1;
        }
    }
    if (dispatch_address_count >= STUB_DISPATCH_MAX_ADDRESSES) {
        return 0;
    }
    dispatch_addresses[dispatch_address_count++] = (u32)address;
    return 1;
}

void *PorpoiseStubNativePointer(void) {
    return &native_pointer_bytes[0];
}

void *PorpoiseStubNativePointerAt(unsigned int index) {
    if (index > OS_HOST_ADDRESS_TOKEN_SLOT_COUNT) {
        return NULL;
    }
    return &native_pointer_bytes[index];
}

uint32_t PorpoiseStubTokenAddress(void) {
    return token_last_address;
}

unsigned int PorpoiseStubTokenEncodeCount(void) {
    return token_encode_count;
}

unsigned int PorpoiseStubTokenReleaseCount(void) {
    return token_release_count;
}

unsigned int PorpoiseStubTokenActiveCount(void) {
    return token_active_count;
}

void PorpoiseStubSetTokenDecodeBias(unsigned int bias) {
    token_decode_bias = bias;
}

unsigned int PorpoiseStubBootstrapCount(void) {
    return bootstrap_count;
}

unsigned int PorpoiseStubRuntimePrepareCount(void) {
    return runtime_prepare_count;
}

int PorpoiseHostPrepareRuntimeV1(
    uint32_t entry_address,
    PorpoiseStubTitleRuntimeConfigV1 *config_out) {
    if (entry_address == 0U || config_out == NULL) {
        return 1;
    }
    config_out->flags = UINT32_C(0x00000001);
    config_out->dvd_root_directory = "stub-files";
    runtime_prepare_count++;
    return 0;
}

int PorpoiseStubTitleSentinelsValid(void) {
    static const u8 expected_stack[4] = {0x81U, 0x7FU, 0xF0U, 0x00U};
    static const u8 expected_toc[4] = {0x12U, 0x34U, 0x56U, 0x78U};
    static const u8 expected_sda[4] = {0x89U, 0xABU, 0xCDU, 0xEFU};
    static const u8 expected_startup_first[4] = {0x00U, 0x00U, 0x00U, 0x11U};
    static const u8 expected_startup_second[4] = {0x00U, 0x00U, 0x00U, 0x33U};
    const char *startup_mode = getenv("PORPOISE_STUB_ORDERED_STARTUP");

    if (memcmp(&stub_memory[0x17FEFF0], expected_stack, 4U) != 0 ||
        memcmp(&stub_memory[0x1000], expected_toc, 4U) != 0 ||
        memcmp(&stub_memory[0x2000], expected_sda, 4U) != 0) {
        return 0;
    }
    return startup_mode == NULL || strcmp(startup_mode, "1") != 0 ||
           (memcmp(&stub_memory[0x1004], expected_startup_first, 4U) == 0 &&
            memcmp(&stub_memory[0x1008], expected_startup_second, 4U) == 0);
}

int PorpoiseHostPrepareTitleEntryV3(
    uint32_t entry_address,
    PorpoiseStubTitleEntryStateV3 *state_out) {
    const char *startup_mode;
    size_t index;

    if (entry_address == 0U || state_out == NULL || !memory_initialized) {
        return 1;
    }
    for (index = 0U; index < 32U; index++) {
        state_out->gpr[index] = 0U;
    }
    state_out->gpr[1] = UINT32_C(0x817FF000);
    state_out->gpr[2] = UINT32_C(0x80001000);
    state_out->gpr[13] = UINT32_C(0x80002000);
    startup_mode = getenv("PORPOISE_STUB_ORDERED_STARTUP");
    if (startup_mode != NULL) {
        if (strcmp(startup_mode, "1") == 0) {
            state_out->startup_function_count = 2U;
            state_out->startup_functions[0].guest_address =
                UINT32_C(0x80002100);
            state_out->startup_functions[1].guest_address =
                UINT32_C(0x80002120);
        } else if (strcmp(startup_mode, "too-many") == 0) {
            state_out->startup_function_count = 9U;
        } else if (strcmp(startup_mode, "null") == 0) {
            state_out->startup_function_count = 1U;
        } else if (strcmp(startup_mode, "unknown-flags") == 0) {
            state_out->startup_function_count = 1U;
            state_out->startup_functions[0].guest_address =
                UINT32_C(0x80002100);
            state_out->startup_functions[0].flags = UINT32_C(0x80000000);
        } else {
            return 1;
        }
    }
    bootstrap_count++;
    return 0;
}

uint32_t PorpoiseStubAdd(uint32_t left, uint32_t right) {
    return left + right;
}

void *PorpoiseStubIdentity(void *pointer) {
    return pointer;
}

double PorpoiseStubFloatMix(float left, double right) {
    return (double)left + right;
}

void PorpoiseStubReportAdapter(struct PorpoisePpcState *state) {
    (void)state;
    ++report_count;
}

unsigned int PorpoiseStubReportCount(void) {
    return report_count;
}
