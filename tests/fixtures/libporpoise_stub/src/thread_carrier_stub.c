#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <porpoise/host_thread_carrier.h>
#include <porpoise/thread_carrier_stub.h>

#include <dolphin/os/OSInterrupt.h>

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct LibPorpoiseHostThreadCarrier {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t thread;
    LibPorpoiseHostThreadCarrierEntryV1 entry;
    void *entry_context;
    int32_t suspend_count;
    int ready;
    int running;
    int stop_requested;
    int finished;
    int joined;
};

static pthread_mutex_t observer_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned int create_count;
static unsigned int resume_count;
static unsigned int suspend_count;
static unsigned int stop_count;
static unsigned int join_count;
static unsigned int destroy_count;

static int stub_thread_success(int result)
{
    return result == 0;
}

static void stub_observe(unsigned int *counter)
{
    if (stub_thread_success(pthread_mutex_lock(&observer_mutex))) {
        (*counter)++;
        (void)pthread_mutex_unlock(&observer_mutex);
    }
}

static unsigned int stub_observer_value(unsigned int *counter)
{
    unsigned int value = 0U;
    if (stub_thread_success(pthread_mutex_lock(&observer_mutex))) {
        value = *counter;
        (void)pthread_mutex_unlock(&observer_mutex);
    }
    return value;
}

/* Carrier calls can arrive with libPorpoise's recursive scheduler boundary
 * already held. Preserve that state while releasing the single-core lock for
 * a blocking carrier wait. */
static BOOL stub_release_scheduler(void)
{
    BOOL caller_was_enabled = OSDisableInterrupts();
    (void)OSRestoreInterrupts(TRUE);
    return caller_was_enabled;
}

static void stub_restore_scheduler(BOOL caller_was_enabled)
{
    (void)OSDisableInterrupts();
    if (caller_was_enabled) {
        (void)OSRestoreInterrupts(TRUE);
    }
}

static void *stub_carrier_entry(void *argument)
{
    LibPorpoiseHostThreadCarrier *carrier =
        (LibPorpoiseHostThreadCarrier *)argument;
    int run_entry;

    if (!stub_thread_success(pthread_mutex_lock(&carrier->mutex))) {
        return NULL;
    }
    carrier->ready = 1;
    (void)pthread_cond_broadcast(&carrier->condition);
    while (carrier->suspend_count > 0 && !carrier->stop_requested) {
        if (!stub_thread_success(
                pthread_cond_wait(&carrier->condition, &carrier->mutex))) {
            carrier->stop_requested = 1;
            break;
        }
    }
    run_entry = !carrier->stop_requested;
    (void)pthread_mutex_unlock(&carrier->mutex);

    if (run_entry) {
        BOOL entry_interrupts_were_enabled = OSDisableInterrupts();
        carrier->entry(carrier->entry_context);
        (void)OSRestoreInterrupts(entry_interrupts_were_enabled);
    }

    if (stub_thread_success(pthread_mutex_lock(&carrier->mutex))) {
        carrier->running = 0;
        carrier->finished = 1;
        (void)pthread_cond_broadcast(&carrier->condition);
        (void)pthread_mutex_unlock(&carrier->mutex);
    }
    return NULL;
}

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierCreatePausedV1(
    const LibPorpoiseHostThreadCarrierConfigV1 *config,
    LibPorpoiseHostThreadCarrier **carrier_out)
{
    LibPorpoiseHostThreadCarrier *carrier;

    if (carrier_out != NULL) {
        *carrier_out = NULL;
    }
    if (config == NULL || carrier_out == NULL ||
        config->struct_size != sizeof(*config) || config->entry == NULL ||
        config->priority < 0 || config->priority > 31) {
        return LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_ARGUMENT;
    }
    carrier = (LibPorpoiseHostThreadCarrier *)calloc(1U, sizeof(*carrier));
    if (carrier == NULL) {
        return LIBPORPOISE_HOST_THREAD_CARRIER_OUT_OF_MEMORY;
    }
    carrier->entry = config->entry;
    carrier->entry_context = config->entry_context;
    carrier->suspend_count = 1;
    if (!stub_thread_success(pthread_mutex_init(&carrier->mutex, NULL))) {
        free(carrier);
        return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
    }
    if (!stub_thread_success(pthread_cond_init(&carrier->condition, NULL))) {
        (void)pthread_mutex_destroy(&carrier->mutex);
        free(carrier);
        return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
    }
    if (!stub_thread_success(pthread_mutex_lock(&carrier->mutex))) {
        (void)pthread_cond_destroy(&carrier->condition);
        (void)pthread_mutex_destroy(&carrier->mutex);
        free(carrier);
        return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
    }
    if (!stub_thread_success(
            pthread_create(&carrier->thread, NULL, stub_carrier_entry, carrier))) {
        (void)pthread_mutex_unlock(&carrier->mutex);
        (void)pthread_cond_destroy(&carrier->condition);
        (void)pthread_mutex_destroy(&carrier->mutex);
        free(carrier);
        return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
    }
    while (!carrier->ready) {
        if (!stub_thread_success(
                pthread_cond_wait(&carrier->condition, &carrier->mutex))) {
            carrier->stop_requested = 1;
            carrier->suspend_count = 0;
            (void)pthread_cond_broadcast(&carrier->condition);
            (void)pthread_mutex_unlock(&carrier->mutex);
            (void)pthread_join(carrier->thread, NULL);
            (void)pthread_cond_destroy(&carrier->condition);
            (void)pthread_mutex_destroy(&carrier->mutex);
            free(carrier);
            return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
        }
    }
    (void)pthread_mutex_unlock(&carrier->mutex);
    *carrier_out = carrier;
    stub_observe(&create_count);
    return LIBPORPOISE_HOST_THREAD_CARRIER_OK;
}

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierResumeV1(
    LibPorpoiseHostThreadCarrier *carrier,
    int32_t *previous_suspend_count_out)
{
    BOOL caller_was_enabled;

    if (carrier == NULL || previous_suspend_count_out == NULL) {
        return LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_ARGUMENT;
    }
    if (!stub_thread_success(pthread_mutex_lock(&carrier->mutex))) {
        return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
    }
    if (carrier->joined || carrier->finished || carrier->stop_requested ||
        carrier->suspend_count <= 0) {
        (void)pthread_mutex_unlock(&carrier->mutex);
        return LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_STATE;
    }
    *previous_suspend_count_out = carrier->suspend_count;
    carrier->suspend_count--;
    carrier->running = 1;
    (void)pthread_cond_broadcast(&carrier->condition);
    (void)pthread_mutex_unlock(&carrier->mutex);

    caller_was_enabled = stub_release_scheduler();
    if (!stub_thread_success(pthread_mutex_lock(&carrier->mutex))) {
        stub_restore_scheduler(caller_was_enabled);
        return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
    }
    while (carrier->running && !carrier->finished &&
           carrier->suspend_count == 0) {
        if (!stub_thread_success(
                pthread_cond_wait(&carrier->condition, &carrier->mutex))) {
            (void)pthread_mutex_unlock(&carrier->mutex);
            stub_restore_scheduler(caller_was_enabled);
            return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
        }
    }
    (void)pthread_mutex_unlock(&carrier->mutex);
    stub_restore_scheduler(caller_was_enabled);
    stub_observe(&resume_count);
    return LIBPORPOISE_HOST_THREAD_CARRIER_OK;
}

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierSuspendCurrentV1(
    LibPorpoiseHostThreadCarrier *carrier,
    int32_t *previous_suspend_count_out)
{
    BOOL caller_was_enabled;

    if (carrier == NULL || previous_suspend_count_out == NULL) {
        return LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_ARGUMENT;
    }
    if (!pthread_equal(pthread_self(), carrier->thread) ||
        !stub_thread_success(pthread_mutex_lock(&carrier->mutex))) {
        return LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_STATE;
    }
    if (!carrier->running || carrier->suspend_count != 0 ||
        carrier->finished || carrier->stop_requested) {
        (void)pthread_mutex_unlock(&carrier->mutex);
        return LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_STATE;
    }
    *previous_suspend_count_out = 0;
    carrier->suspend_count = 1;
    carrier->running = 0;
    (void)pthread_cond_broadcast(&carrier->condition);
    (void)pthread_mutex_unlock(&carrier->mutex);

    caller_was_enabled = stub_release_scheduler();
    if (!stub_thread_success(pthread_mutex_lock(&carrier->mutex))) {
        stub_restore_scheduler(caller_was_enabled);
        return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
    }
    while (carrier->suspend_count > 0 && !carrier->stop_requested) {
        if (!stub_thread_success(
                pthread_cond_wait(&carrier->condition, &carrier->mutex))) {
            (void)pthread_mutex_unlock(&carrier->mutex);
            stub_restore_scheduler(caller_was_enabled);
            return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
        }
    }
    (void)pthread_mutex_unlock(&carrier->mutex);
    stub_restore_scheduler(caller_was_enabled);
    stub_observe(&suspend_count);
    return LIBPORPOISE_HOST_THREAD_CARRIER_OK;
}

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierRequestStopV1(
    LibPorpoiseHostThreadCarrier *carrier)
{
    if (carrier == NULL) {
        return LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_ARGUMENT;
    }
    if (!stub_thread_success(pthread_mutex_lock(&carrier->mutex))) {
        return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
    }
    carrier->stop_requested = 1;
    carrier->suspend_count = 0;
    carrier->running = 0;
    (void)pthread_cond_broadcast(&carrier->condition);
    (void)pthread_mutex_unlock(&carrier->mutex);
    stub_observe(&stop_count);
    return LIBPORPOISE_HOST_THREAD_CARRIER_OK;
}

static int stub_deadline_after(
    struct timespec *deadline,
    uint32_t timeout_milliseconds)
{
    if (clock_gettime(CLOCK_REALTIME, deadline) != 0) {
        return 0;
    }
    deadline->tv_sec += (time_t)(timeout_milliseconds / 1000U);
    deadline->tv_nsec +=
        (long)(timeout_milliseconds % 1000U) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
    return 1;
}

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierJoinV1(
    LibPorpoiseHostThreadCarrier *carrier,
    uint32_t timeout_milliseconds)
{
    BOOL caller_was_enabled;
    int wait_result = 0;

    if (carrier == NULL) {
        return LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_ARGUMENT;
    }
    if (carrier->joined) {
        return LIBPORPOISE_HOST_THREAD_CARRIER_OK;
    }
    caller_was_enabled = stub_release_scheduler();
    if (!stub_thread_success(pthread_mutex_lock(&carrier->mutex))) {
        stub_restore_scheduler(caller_was_enabled);
        return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
    }
    if (timeout_milliseconds == UINT32_MAX) {
        while (!carrier->finished && wait_result == 0) {
            wait_result = pthread_cond_wait(
                &carrier->condition, &carrier->mutex);
        }
    } else {
        struct timespec deadline;
        if (!stub_deadline_after(&deadline, timeout_milliseconds)) {
            (void)pthread_mutex_unlock(&carrier->mutex);
            stub_restore_scheduler(caller_was_enabled);
            return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
        }
        while (!carrier->finished && wait_result == 0) {
            wait_result = pthread_cond_timedwait(
                &carrier->condition, &carrier->mutex, &deadline);
        }
    }
    if (!carrier->finished) {
        (void)pthread_mutex_unlock(&carrier->mutex);
        stub_restore_scheduler(caller_was_enabled);
        return wait_result == ETIMEDOUT
                   ? LIBPORPOISE_HOST_THREAD_CARRIER_TIMED_OUT
                   : LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
    }
    (void)pthread_mutex_unlock(&carrier->mutex);
    if (!stub_thread_success(pthread_join(carrier->thread, NULL))) {
        stub_restore_scheduler(caller_was_enabled);
        return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
    }
    carrier->joined = 1;
    stub_restore_scheduler(caller_was_enabled);
    stub_observe(&join_count);
    return LIBPORPOISE_HOST_THREAD_CARRIER_OK;
}

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierDestroyV1(
    LibPorpoiseHostThreadCarrier *carrier)
{
    if (carrier == NULL) {
        return LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_ARGUMENT;
    }
    if (!carrier->joined || !carrier->finished) {
        return LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_STATE;
    }
    if (!stub_thread_success(pthread_cond_destroy(&carrier->condition)) ||
        !stub_thread_success(pthread_mutex_destroy(&carrier->mutex))) {
        return LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE;
    }
    free(carrier);
    stub_observe(&destroy_count);
    return LIBPORPOISE_HOST_THREAD_CARRIER_OK;
}

void PorpoiseThreadCarrierStubResetObservers(void)
{
    if (stub_thread_success(pthread_mutex_lock(&observer_mutex))) {
        create_count = 0U;
        resume_count = 0U;
        suspend_count = 0U;
        stop_count = 0U;
        join_count = 0U;
        destroy_count = 0U;
        (void)pthread_mutex_unlock(&observer_mutex);
    }
}

unsigned int PorpoiseThreadCarrierStubCreateCount(void)
{
    return stub_observer_value(&create_count);
}

unsigned int PorpoiseThreadCarrierStubResumeCount(void)
{
    return stub_observer_value(&resume_count);
}

unsigned int PorpoiseThreadCarrierStubSuspendCount(void)
{
    return stub_observer_value(&suspend_count);
}

unsigned int PorpoiseThreadCarrierStubStopCount(void)
{
    return stub_observer_value(&stop_count);
}

unsigned int PorpoiseThreadCarrierStubJoinCount(void)
{
    return stub_observer_value(&join_count);
}

unsigned int PorpoiseThreadCarrierStubDestroyCount(void)
{
    return stub_observer_value(&destroy_count);
}
