# libPorpoise host-thread carrier contract

Porpoise Tool must not cast a 32-bit guest `OSThread` or `OSContext` to the
native structures used by a PC host. The guest object is an exact big-endian
memory image; the host object contains native pointers, synchronization
objects, and implementation-specific fields. Even when the guest allocation
is large enough by accident, the layouts and byte order are incompatible.

A lifted guest thread instead needs an opaque host carrier. The carrier owns
one native thread and preserves the generated C call stack while a guest call
to `OSSuspendThread` is blocked. Porpoise Tool owns the corresponding
`PorpoisePpcState` and the guest-memory mirror. Neither side exposes or embeds
the other's object layout.

There are deliberately two identity domains. libPorpoise owns a stable,
registered **native** thread identity for the carrier. Porpoise Tool owns the
carrier-to-`uint32_t` **guest** `OSThread` address map. A guest address is not a
native pointer, is not passed in the carrier configuration, and is never
returned by libPorpoise's native `OSGetCurrentThread`. Conversely, a native
`OSThread *` never enters PPC state or guest memory.

## Required versioned surface

The evolving libPorpoise port should expose this surface from the dedicated
public header `<porpoise/host_thread_carrier.h>`, separate from the console SDK
`OSThread` structure. The names below define the contract Porpoise Tool
expects; they are not an invitation to expose SDL types.

```c
#define LIBPORPOISE_HOST_THREAD_CARRIER_API_VERSION 1U

typedef struct LibPorpoiseHostThreadCarrier
    LibPorpoiseHostThreadCarrier;

typedef void (*LibPorpoiseHostThreadCarrierEntryV1)(void *context);

typedef enum LibPorpoiseHostThreadCarrierResultV1 {
    LIBPORPOISE_HOST_THREAD_CARRIER_OK = 0,
    LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_ARGUMENT,
    LIBPORPOISE_HOST_THREAD_CARRIER_INVALID_STATE,
    LIBPORPOISE_HOST_THREAD_CARRIER_OUT_OF_MEMORY,
    LIBPORPOISE_HOST_THREAD_CARRIER_HOST_FAILURE,
    LIBPORPOISE_HOST_THREAD_CARRIER_TIMED_OUT
} LibPorpoiseHostThreadCarrierResultV1;

typedef struct LibPorpoiseHostThreadCarrierConfigV1 {
    uint32_t struct_size;
    LibPorpoiseHostThreadCarrierEntryV1 entry;
    void *entry_context;
    int32_t priority;
    const char *name;
} LibPorpoiseHostThreadCarrierConfigV1;

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierCreatePausedV1(
    const LibPorpoiseHostThreadCarrierConfigV1 *config,
    LibPorpoiseHostThreadCarrier **carrier_out);

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierResumeV1(
    LibPorpoiseHostThreadCarrier *carrier,
    int32_t *previous_suspend_count_out);

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierSuspendCurrentV1(
    LibPorpoiseHostThreadCarrier *carrier,
    int32_t *previous_suspend_count_out);

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierRequestStopV1(
    LibPorpoiseHostThreadCarrier *carrier);

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierJoinV1(
    LibPorpoiseHostThreadCarrier *carrier,
    uint32_t timeout_milliseconds);

LibPorpoiseHostThreadCarrierResultV1
LibPorpoiseHostThreadCarrierDestroyV1(
    LibPorpoiseHostThreadCarrier *carrier);
```

The final public spelling may change before both projects adopt version 1,
but all of the behavior below is required. Generated projects request
automatic detection with the Tool-private
`PORPOISE_AUTODETECT_LIBPORPOISE_HOST_THREAD_CARRIER_V1=1` gate. The runtime
uses `<porpoise/host_thread_carrier.h>` only when the compiler can find it and
enables the carrier path only when
`LIBPORPOISE_HOST_THREAD_CARRIER_API_VERSION == 1`. An absent, unversioned,
older, or newer header leaves the explicit unsupported-operation adapters in
place; Porpoise Tool does not guess compatibility from header presence.

Consumers should not normally define
`PORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1` themselves. Defining that
gate explicitly is a forced-contract build: the header must exist and publish
exactly version 1, and a mismatch is a compile-time error.

## Lifecycle guarantees

`CreatePausedV1` validates priority, allocates every mutex/condition/thread
resource, and starts a native trampoline parked before `entry`. Success
returns one unpublished, suspended carrier. Failure returns a null output and
leaves no active-list entry, native thread, mutex, condition, or other
resource behind. Guest stack addresses are never accepted as native stack
pointers.

Before the trampoline may invoke `entry`, it must be registered as a normal
libPorpoise native OS thread. For the entire callback lifetime, native
`OSGetCurrentThread()` must return that carrier's stable, non-null native
identity. The same identity must be observed after every park/resume and by
all nested native SDK calls. It participates in the process-wide scheduler
lock and priority handoff, native mutex ownership, GX/VI ownership or affinity
checks, and thread-exit observers exactly like any other libPorpoise-managed
thread. Launching `entry` on an unregistered raw pthread, Win32, or SDL thread
does not satisfy this contract.

`ResumeV1` has the console suspend-count result: it reports the previous count
and decrements only a positive count. The state change and wake are atomic. A
failure leaves the count and runnable state unchanged. When a carrier becomes
runnable at a strictly higher priority than the current emulated OS thread,
the call does not return until that carrier blocks, suspends, exits, or no
longer outranks the caller. This handoff applies both to first start and to a
later wake of the same native thread.

The returned previous count is a consistency witness for the carrier's native
park/run mirror, not the authoritative guest value. The Tool adapter compares
it with the staged guest previous count. A mismatch is an internal invariant
failure: the title must fault and all carriers must be stopped/joined rather
than allowing the two domains to keep scheduling.

`SuspendCurrentV1` is valid only when called by that carrier. Version 1 needs
the self-suspend transition from count zero. It increments the count, releases
libPorpoise's process-wide single-core scheduler lock while parked, and
reacquires the same recursive lock state before returning after a resume. It
must not return spuriously while the suspend count remains positive.

`RequestStopV1` is cooperative, idempotent, and wakes a parked carrier. The
Porpoise adapter first marks its PPC state terminal, so the resumed entry can
unwind without executing more guest instructions. It does not forcibly kill
arbitrary C code.

`JoinV1` waits at most the requested duration and never destroys a live
object. A timeout leaves the carrier valid for another stop/join attempt.
`DestroyV1` succeeds only after a completed join and releases every native
thread, mutex, condition, active-list, and diagnostic-name resource. No
fire-and-forget or detached carrier is permitted in version 1.

Returning from `entry`, including the cooperative unwind initiated by
`RequestStopV1`, must pass through libPorpoise's registered thread-exit path.
Exit observers and native ownership cleanup complete before `JoinV1` reports
success; only then may `DestroyV1` unregister and release the native identity.
No carrier callback may execute after a successful join.

All functions may be called while the caller already owns the recursive
single-core scheduler lock. Blocking functions must use the same wait hooks
as libPorpoise's native OS queues so alarms and higher-priority handoff remain
deterministic.

## Scheduler ownership

Porpoise Tool is the sole authority for guest scheduler state: guest thread
addresses, `OSThread`/`OSContext` bytes, active and run queues, lifecycle and
suspend-count fields, guest priorities, and selection of the next guest
thread. libPorpoise must neither read nor mutate those values and must not
choose a guest thread from a guest queue. The `priority` supplied in the
carrier configuration and the carrier's private park/run count are native
execution mirrors used only to enforce the requested single-CPU handoff; they
are not an independent guest scheduler or a second source of guest state.

libPorpoise is the sole authority for the carrier's native identity, native
thread resources, and process-wide recursive scheduler lock. Porpoise Tool
must not create a competing host lock, suspend a native thread directly, or
inspect libPorpoise's native thread representation. A guest transition is
initiated by the Tool adapter under the serialized boundary after validating
the complete guest state and preflighting every guest write. Before an
operation that can synchronously hand execution to another carrier, the
adapter stages the old bytes and commits the target object's guest lifecycle
and suspend-count fields that the resumed carrier must observe, then invokes
the carrier operation. As part of the actual handoff, the carrier trampoline
binds its Tool identity and switches the guest current-thread/current-context
words before its first lifted instruction; those changes cannot be deferred
until `ResumeV1` returns either. A `ResumeV1` failure has the
no-native-mutation/no-wake guarantee above, so the adapter restores the staged
guest bytes before returning. A rollback failure makes execution terminal and
stops/joins every affected carrier. This ordering is required because
`ResumeV1` may not return until the resumed carrier has itself blocked or
exited; a post-return guest commit would let that carrier execute against stale
scheduler state. The guest and native state domains must never continue with
divergent runnable state.

## Porpoise adapter responsibilities

The host carrier is not the guest scheduler. The Porpoise adapter must still:

- key one private carrier and one private `PorpoisePpcState` by the guest
  `uint32_t` thread address;
- bind the active carrier and its guest address in Tool-owned thread-local
  state before entering lifted code, retain that binding across nested guest
  dispatch, and remove it before the carrier can be destroyed;
- validate and marshal the exact 0x318-byte, big-endian guest `OSThread` and
  embedded 0x2C8-byte `OSContext` without native casts;
- load the initial PPC context once, save guest-visible context at suspension,
  and retain the generated C continuation while parked;
- switch the guest current-context/current-thread low-memory words as one
  serialized transaction and restore the previous owner before handing the
  host CPU back;
- keep CPU-global SPR, BAT, time-base, and decrementer state coherent across
  per-thread PPC states;
- bind generated exports to the carrier's thread-local PPC state for the
  carrier lifetime;
- propagate a carrier fault to the owning title state and stop/join every
  carrier before adapter memory is freed.

The protected guest import `OSGetCurrentThread` has no arguments and returns a
guest pointer in `r3`. Its Tool adapter must read the current Tool-owned
carrier binding and return that carrier's `uint32_t` guest address. It must
never call native `OSGetCurrentThread` and encode or truncate the returned
native pointer. A lifted implementation that reads the guest low-memory
current-thread word observes the same address because the adapter switches
that word in the same serialized transaction. If no guest identity is bound
-- including the bootstrap/main native thread until a canonical guest
main-thread mirror is explicitly established -- the import fails with an
unsupported-operation fault rather than returning null, stale state, or a
native address.

The bootstrap host thread is not implicitly the guest main thread. Native
`OSInit` creates only native runtime state; it does not populate the generated
guest low-memory current-thread/current-context words or a guest `OSThread`.
When a title supplies its lifted thread initializer, that initializer must run
before any user initializer or entry that can observe thread identity. After
it completes, the Tool adapter must validate the resulting nonzero guest
current-thread address and exact guest object before establishing a canonical
main-thread binding. Until then, the protected import remains unsupported; no
title address is guessed and no native identity is substituted.

The initial supported phase is intentionally narrow: first resume of a valid
ready thread with suspend count one, self-suspend, resume of that parked
thread, and normal/explicit exit without joiners or owned guest mutexes.
External suspension, cancellation, arbitrary context loading, nonempty join
queues, owned mutexes, detached threads, and nested suspend counts must fail
before mutation until their guest-side semantics are implemented and tested.
