# Architecture

Porpoise Tool is split into a build-time frontend and a generated runtime shim.
The frontend translates annotated title assembly, including managed or
prepared DTK inputs; the shim lets that lifted code run inside a
consumer-supplied `libPorpoise` host. It can emit a library-only target for
prepared module code, but it does not package/load a native REL. The tool does
not implement a second SDK, console memory system, or application host.

## Ownership boundary

| Porpoise Tool owns | `libPorpoise` owns |
| --- | --- |
| CLI and strict config validation | SDK and platform APIs |
| annotated assembly parsing and IR | console-memory allocation and mapping |
| conservative opcode lowering | host process startup and event loop |
| PPC register/control state | graphics, audio, input, filesystems, and threads |
| endian-safe guest memory helpers | native-pointer/guest-address policy |
| typed ABI import/export bridges | implementations called by those bridges |
| Meson project and measured report generation | the host `main` that calls `DolphinMain` |

`libPorpoise` is intentionally a runtime dependency of generated projects, not of the Porpoise Tool executable. It remains unstable and consumer-supplied. The generated project requests `dependency('libPorpoise', fallback: ['libPorpoise','libporpoise_dep'])`; it creates no wrap, checkout, or version pin. Meson derives a system-include dependency with `partial_dependency(compile_args: false, ...)`, preserving include/link/source requirements without importing broad flags such as `-w`. Generated targets retain their warning policy and receive only Porpoise's explicit Windows or Linux consumer ABI defines.

## Frontend pipeline

The CLI, in-process API, and optional desktop workbench share one staged
recovery pipeline. Classic CLI mode is a compatibility wrapper over these same
objects.

1. **Load/import** validates annotated assembly directly, validates an existing
   DTK-prepared tree, or imports a trusted local ELF into a fresh managed DTK
   stage. Source files are read-only.
2. **Session** recursively parses sorted `.s`/`.S` inputs, resolves linked data
   and file/section-scoped local symbols, validates additive ABI manifests,
   skip lists, optional map sources, and exact SDK catalogs, then exposes an
   immutable `PorpoiseSession`.
3. **Plan** builds relocation-aware signatures and combines input state, map
   provenance, SDK policy, bindings, and stable manual overrides into an
   immutable `PorpoiseTranslationPlan`. Every function has an explicit lift,
   import, omit, or data action and a blocking reason when incoherent.
4. **Validate** checks plan/session coherence, entry and export disposition,
   import contracts, stable locators, data coverage/overlap, and read-only data
   annotations before lowering begins.
5. **Generate** lowers only planned functions and writes runtime support,
   lifted sources, data, registries, ABI bridges, Meson metadata, and a
   schema-v3 measured report into a sibling stage.
6. **Publish** installs one stage or an entire multi-target batch. Existing
   outputs are backed up under a rollback journal and restored as a unit if any
   publication step fails.

Sessions outlive their borrowing plans. A plan digest binds the selected target
and decisions to the immutable inputs, so generation rejects a mismatched plan.
GUI replanning can therefore reuse a loaded session without reparsing.
Operation callbacks expose load, import, symbols, signatures, plan, validate,
generate, and publish progress plus cooperative cancellation.

Parsing, unsupported lowering, and strict-mode approximation failures use exit
code `3`; I/O and publication failures use `4`. A failed or cancelled stage is
removed and never merged into the requested destination. See
[Recovery workbench architecture](RECOVERY_WORKBENCH.md) for project, map,
catalog, and transactional details.

The implementation modules follow those responsibilities: options and strict
project/config parsing, managed DTK import, program/IR, symbol and SDK catalogs,
immutable sessions/plans, ABI validation, opcode lowering, report/artifact
generation, transactional publication, and shared filesystem/diagnostic
utilities.

## Lifted execution model

Every translated function has one signature:

```c
void porpoise_lifted_symbol(PorpoisePpcState *state);
```

Arguments, return values, and control state live in `PorpoisePpcState`. There are no incompatible indirect casts or generated “accept every possible C argument” signatures.

The state contains:

- 32 `uint32_t` GPRs;
- 32 FPRs with two authoritative raw 64-bit lane encodings, accessed through explicit binary32/binary64 conversion helpers;
- CR, FPSCR, XER, LR, CTR, PC, MSR, eight GQR values, and named supervisor/system registers;
- state-owned time-base bias and decrementer anchors, while the monotonic raw clock remains host-owned;
- an explicit ready/running/returned/faulted execution status;
- a sticky fault code, guest fault address, and message;
- a pointer to the active `PorpoiseHostAdapter`.

State initialization zeroes the register file and attaches the host adapter. It deliberately leaves startup registers, MSR, and HID2 neutral. After host and generated title-data initialization, generated `DolphinMain` asks the separate, versioned `porpoise-title-host` provider for `PorpoiseTitleEntryStateV3`. `porpoise_state_prepare_title_entry` then requires aligned guest `r1` (stack) plus nonzero `r2` (TOC/SDA2) and `r13` (SDA) before enabling MSR[EE], MSR[FP], HID2[PSE], and HID2[LSQE]. The direct lifted entry begins after native `OSInit`, so external interrupts start enabled; all guest interrupt disable/enable/restore operations must remain in the lifted MSR model rather than being split across native scheduler state. The state may also supply linked arena bounds, bounded startup writes, and an ordered list of guest-only lifted initializers. This list establishes guest mirrors that native `OSInit` cannot populate before title constructors observe them. Missing or invalid bootstrap state fails explicitly; Porpoise Tool does not guess it or execute a lifted `__start`. The first fault is retained, and generated code also stops when a callback marks execution returned or faulted.

### Title-host startup contract

The executable form has two consumer-supplied title-host hooks with deliberately different lifetimes:

```c
int PorpoiseHostPrepareRuntimeV1(
    uint32_t entry_address,
    PorpoiseTitleRuntimeConfigV1 *config_out);

int PorpoiseHostPrepareTitleEntryV3(
    uint32_t entry_address,
    PorpoiseTitleEntryStateV3 *state_out);
```

`DolphinMain` zeroes each output structure and uses this order:

1. Call `PorpoiseHostPrepareRuntimeV1` before `OSInit`, native DVD startup, guest-memory writes, or lifted code.
2. Validate that only known runtime flags are present. An explicit `dvd_root_directory` is valid only with `PORPOISE_TITLE_RUNTIME_INITIALIZE_DVD`.
3. Pass that configuration to `porpoise_libporpoise_adapter_init_for_title`. The adapter applies an explicit DVD root before `OSInit`, initializes `libPorpoise` once, and calls native `DVDInit` after `OSInit` when requested.
4. Attach PPC state and initialize generated title data through the host adapter.
5. Call `PorpoiseHostPrepareTitleEntryV3`, validate its counts and every nonzero startup address before mutation, apply its optional arena bounds and startup words, validate the direct-entry register state, and bind it for generated exports.
6. Run each ordered guest-only lifted initializer. When `PORPOISE_TITLE_STARTUP_ESTABLISH_GUEST_MAIN_THREAD_AFTER` is set, validate and bind the resulting guest main-thread/low-memory mirror before any later initializer can observe `OSGetCurrentThread`. Restore the direct-entry GPR image after every initializer while retaining its guest-memory effects, then call the selected lifted entry.

The runtime hook does not authorize discovery of title files. Any DVD root is a title-specific host path owned and supplied by the consumer; Porpoise Tool neither copies extracted content into generated output nor derives a root from the tool, input, or executable location. It does not rebase this runtime value like a config-file path; relative-root interpretation belongs to `libPorpoise` and the host process working directory. `NULL` retains the consumer's `libPorpoise` default. A non-null root string must remain valid through the subsequent adapter-initialization call.

Native DVD/FST bootstrap is one-shot process state. Once an FST has been built, a later configured adapter may request DVD only with the same explicit/default root. The generated runtime does not attempt to rebuild an FST after changing directories or paths; an integration that selected the wrong root must restart the process. This constraint is explicit because silently changing a root after `DVDInit` would split path policy from the existing native FST.

Direct calls to another translated function pass the same state pointer. Function starts, address aliases, and labeled instruction entry points share a generated `uint32_t` address switch whose targets all have the same lifted signature. Indirect branches and modeled interrupt return use that registry. An address absent from it produces an unsupported-operation fault instead of calling through a cast.

The registry is emitted as a small deterministic router plus high-16-bit address shards. This keeps very large titles from forcing the C compiler to optimize one enormous switch translation unit. A skipped function that has an exact ABI-import binding appears in the appropriate shard as the corresponding `void import(PorpoisePpcState *)` bridge; a plain skipped function has no dispatch entry. The dispatcher tracks lifted frames separately from imported calls. After a lifted frame returns with MSR[EE] set, it offers the host adapter a deferred-event safe point. A guest callback posted by an import is not eligible until its submitting lifted frame has returned; callback delivery is non-reentrant and uses the same address dispatcher and PPC-state calling convention.

The raw address dispatcher, its target types, and every per-input lifted declaration are generated beneath `src/` and remain private to the static-library build. Consumers bind the generated dispatcher only through `porpoise_generated_bind(PorpoiseHostAdapter *)`, declared by the public `porpoise_generated.h`. The facade performs the one-time/idempotent binding to `porpoise_libporpoise_run_guest`; neither generated `DolphinMain` nor a dependency consumer receives a declaration that can bypass that serialized boundary.

### Paired-single arithmetic boundary

Lifted `ps_muls0`, `ps_muls1`, `ps_madds0`, and `ps_madds1` execute through
the Tool-owned PPC runtime rather than host `float` expressions or SDK
matrix replacements. `runtime/src/porpoise_ppc_fp.c` is the host-FP-independent
arithmetic core: it accepts raw binary32 encodings, forms the complete exact
multiply or fused multiply-add with integer arithmetic, and rounds once at the
Gekko paired-single destination boundary. It models all four FPSCR rounding
modes, NaN selection and invalid causes, infinities, signed zero, subnormal
operands/results, NI flushing, and enabled overflow/underflow exponent
adjustment. No C floating-point environment, contraction setting, or extended
precision can alter this layer.

`porpoise_lifted.c` is the architectural wrapper. It selects the scalar lane,
evaluates both destination lanes before committing either one, applies FPSCR
cause/status and ps0 FPRF rules, honors record-form CR1 updates, suppresses the
destination for enabled invalid operations, and stops explicitly when an
enabled exception requires the not-yet-modeled guest program-exception vector.
The wrapper's defined input domain is a valid paired-single value produced as
two widened binary32 lanes. Gekko declares mixing paired-single and arbitrary
double-precision register contents a programming error; the runtime reports
that boundary instead of silently evaluating it with host arithmetic.

SDK routines such as a lifted `PSMTXConcat` stay untouched and exercise these
same instruction primitives. A future exact import for such a routine must use
the same primitives so optimization cannot introduce a different arithmetic
contract.

## Guest memory and pointer model

Guest addresses remain `uint32_t` throughout generated code. The lifted runtime never adds a fake 256 MiB allocation or converts an address with a native pointer cast.

`PorpoiseHostAdapter` provides four memory/pointer operations, three optional system operations, and two generated-code callback hooks:

```c
read_bytes(context, guest_address, destination, size)
write_bytes(context, guest_address, source, size)
decode_pointer(context, guest_address, pointer_out)
encode_pointer(context, pointer, guest_address_out)
read_time_base(context, ticks_out)
trap(context, state, instruction_address, trap_options, left, right)
system_call(context, state, instruction_address)
call_guest(state, guest_function_address)
poll_events(context, state)
```

Integer and floating loads/stores assemble or disassemble big-endian byte arrays explicitly. This avoids host-endian assumptions and unaligned native dereferences. Access spans are checked for 32-bit overflow before they reach the host.

Assembly data objects, fixups, explicit zero-fill ranges, and contribution bytes are emitted as deterministic C chunks plus a `porpoise_initialize_data` routine that uses these same store helpers. Host memory must already exist; generated code never embeds a linked executable or allocates a competing console-memory buffer.

The current `libPorpoise` adapter is the only generated source that includes unstable host-address interfaces. It:

- accepts the validated title runtime configuration before any host initialization;
- applies an explicit DVD root before `OSInit()`, calls `OSInit()` once, and optionally calls native `DVDInit()` immediately afterward;
- rejects an empty DVD root, a root without the DVD-bootstrap request, or a later one-shot DVD request whose explicit/default root differs from the first;
- verifies that a requested native DVD bootstrap produced an FST location;
- uses `__OSHostDecodeAddress`, `__OSHostEncodeAddress`, the address-token query, and matching release operation supplied by `libPorpoise`;
- obtains monotonic target ticks through `OSGetTime` for lifted time-base and decrementer instructions;
- treats host-address tokens as opaque handles that pointer conversion may use but guest memory reads, writes, and handle arithmetic may not;
- checks that multi-byte non-token mappings are contiguous;
- keeps configured guest arena bounds as private `uint32_t` state, verifies
  native libPorpoise bounds before each operation, and commits guest/native
  setter or allocator changes together without encoding native return values;
  the configured root is immutable for one adapter lifetime and shutdown
  restores and verifies the native bounds that preceded title configuration;
- mirrors synchronous guest `DVDFileInfo` objects in native storage and copies
  `GXRenderModeObj` fields into native endian/layout for `VIConfigure`;
- accepts `AIInit` only with a null guest callback-stack pointer, passes native
  `NULL` without guest pointer conversion, and faults non-null stacks before
  entering the host AI control approximation; this does not provide audio
  output;
- owns the complete native AR allocator family behind one exact adapter
  context: it validates and write-preflights the aligned guest block table,
  passes a separate host-endian shadow to native `ARInit`, mirrors allocation
  lengths to big-endian guest words, verifies LIFO alloc/free results and
  `ARGetSize`, and resets native AR before releasing the shadow; ownership is
  exclusive because the current native API has no nonmutating allocator
  position/generation snapshot, and a bypass is rolled back and poisoned when
  the next mutating adapter call exposes it;
- reserves native process-global GX initialization before `GXInit`, validates
  its complete aligned guest FIFO span, and exposes the native `GXFifoObj` only
  through an owned opaque token; an incomplete native transition poisons the
  lifecycle, and shutdown never pretends native GX can be reset;
- holds guest draw-done callbacks as generated-dispatch addresses, installs one
  native trampoline transactionally, snapshots each signaled guest target into
  a bounded queue, and delivers it later with cloned PPC state when MSR[EE]
  permits non-reentrant host-event dispatch;
- copies enabled GX sample/filter arrays and fixed-size color/light objects
  from complete guest spans, explicitly converting the big-endian 0x40-byte
  `GXLightObj` into native layout rather than casting guest storage;
- mirrors texture-copy destination geometry, calculates its preliminary tiled
  span, and checks only the exact aligned ordinary-RAM origin for display
  copies; native
  `GXCopyDisp`/`GXCopyTex` are entered only under their separate versioned
  guest-address contracts, which independently derive and validate the actual
  span from native GX state and materialize canonical guest-memory bytes;
- bridges high-priority `ARQPostRequest` through synchronous native
  `ARStartDMAEx`, preserves the exact 32-byte big-endian guest request layout,
  and queues non-null guest callbacks until both the submitting lifted frame
  has returned and MSR[EE] permits delivery;
- marshals `CARDProbeEx` through preloaded and idempotently write-preflighted
  nullable guest output spans so native CARD/EXI code never receives a guest
  pointer and cannot run after an invalid, read-only, overflowing, unmapped,
  or MMIO output address;
- routes exact-base 1-, 2-, 4-, and 8-byte writes to the GX FIFO only when
  `libPorpoise` advertises
  `SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION >= 1`, passing each
  already-big-endian guest byte sequence once to
  `SIM_GX_CommandProcessor_SendCanonicalBytes`;
- rejects null, unmapped, overflowing, and currently unsupported MMIO/EFB spans with explicit host results;
- returns those failures as sticky PPC execution faults.

The GX FIFO contract is intentionally byte-oriented. Native numeric
`SIM_GX_CommandProcessor_SendU32`/`SendF32` calls cannot preserve every guest
store: packed colors and mixed-width vertex streams depend on the exact Gekko
byte sequence. A `libPorpoise` without the versioned canonical-byte API, a FIFO
read, an offset write, or another access width remains unsupported MMIO. The
FIFO memory callback itself never initializes or replaces the native command
processor; the separate protected `GXInit` import owns that one-way native
transition.

### GX process and copy boundary

Native GX state is process-global. The adapter lifecycle is therefore
`uninitialized -> poisoned -> active`, with no transition back to
uninitialized. `GXInit` marks the process poisoned before calling native code;
only a non-null native FIFO that encodes as a newly owned, non-memory token can
commit the active state. Every subsequent GX data/value/frame-buffer adapter
requires both active state and the exact owning adapter context. This prevents
a later title session from inheriting native GX state whose mirror and callback
ownership belonged to a destroyed context.

`GXSetDrawDoneCallback` validates generated dispatch availability before
mutating native state. It compares native and mirrored previous callbacks and
rolls the native setter back on divergence. The trampoline never calls lifted
code directly: it captures the guest address current at signal time in a
64-entry queue. Overflow becomes a sticky host-I/O fault at the next event
poll. Delivery shares guest memory but uses a cloned register/control state,
does not invent callback arguments, respects MSR[EE], and cannot recursively
drain host events.

The Tool performs only validation it can establish from canonical state. Its
mirrored texture span is the exact rounded SDK tile count for the accepted
integer, depth, copy, color-index, and CMPR formats. For display copies it
checks a one-byte mapped origin plus 32-byte guest alignment, because real
output lines derive from copy-source height and y-scale and native state may
also change through raw FIFO writes. Address tokens are handles rather than
RAM, and unsupported MMIO is never redirected into console memory.

After preliminary validation, `GXCopyDisp` requires
`LIBPORPOISE_GX_COPY_DISP_GUEST_ADDRESS_API_VERSION >= 1` and
`GXBool GXHostCopyDispGuestAddress(u32, GXBool)`; `GXCopyTex` uses the analogous
`LIBPORPOISE_GX_COPY_TEX_GUEST_ADDRESS_API_VERSION` and
`GXHostCopyTexGuestAddress`. The original guest address is forwarded exactly.
The endpoint must consume one immutable native-state snapshot, independently
derive and validate the complete actual RAM span, synchronously materialize the
canonical XFB or tiled texture bytes, and perform a requested clear exactly once
only after success. Display copies update guest XFB memory; VI selects and
presents that memory rather than an unrelated host backbuffer. A false endpoint
result produces a sticky `PORPOISE_FAULT_HOST_IO` at that address and promises
that no copy, clear, or presentation side effect occurred. Absence of the
matching guest-address contract produces `PORPOISE_FAULT_UNSUPPORTED_OPERATION`
before native GX. A nominal `void *` SDK signature is deliberately not accepted
as a fallback because it cannot prove exact guest-address preservation or
memory materialization.

VI selection is a separate exact-address transaction. The protected
`VISetNextFrameBuffer` adapter requires
`LIBPORPOISE_VI_SET_NEXT_FRAME_BUFFER_GUEST_ADDRESS_API_VERSION >= 1` and
`BOOL VIHostSetNextFrameBufferGuestAddress(u32)`. It forwards the exact aligned
guest origin without decoding it to an arbitrary host pointer. The native
endpoint updates pending selection only and validates the complete XFB against
the final VI mode at latch or presentation time. This supports both
`Copy(B) -> Select(B)` and `Select(B) -> Copy(B)` and prevents a later scratch
copy from replacing the selected XFB.

These API version macros and declarations are producer-side promises. The
Tool's opt-in compatibility checker verifies only compile-interface
consumption; it does not link or execute `libPorpoise` and cannot establish
full-span validation, XFB materialization, VI latch/presentation, or clear
semantics.

The ARQ bridge intentionally supports only the high-priority path currently
provided synchronously by `libPorpoise`. Low-priority queue scheduling remains
unsupported. Completion callbacks run with a cloned register/control state,
receive the original guest `ARQRequest` address in `r3`, share guest memory,
and cannot recursively drain newly posted completions. A null callback is
stored as the generated address of the input's `__ARQCallbackHack`; generation
does not hard-code a title address, and the adapter fails closed when that
symbol is unavailable.

### Guest OS object containment

Guest SDK structures remain authoritative 32-bit, big-endian data. They are
never cast to native `libPorpoise` structures, even when a decoded guest address
happens to be a valid host pointer. The dedicated adapters currently enforce
these boundaries:

- `OSThreadQueue` is read as one exact eight-byte span. Waking an empty queue is
  a no-op; sleeping or waking a populated queue fails until a guest scheduler
  can preserve the blocked PPC continuation.
- `OSMessageQueue` is read and written as its exact 0x20-byte guest layout.
  Initialization and ready/nonblocking circular-buffer operations are
  supported. Any operation that would sleep or wake a guest thread fails before
  changing the queue.
- `OSThread` lifecycle operations validate one exact 0x318-byte snapshot.
  Resume and suspend currently fail with `PORPOISE_FAULT_UNSUPPORTED_OPERATION`;
  exit treats `r3` as an opaque guest return value and does not dereference it.
- The protected `OSGetCurrentThread` import has no arguments and returns the
  Tool-owned `uint32_t` address of the calling carrier's guest `OSThread` in
  `r3`. It never exposes libPorpoise's native `OSThread *`; until the caller
  has an explicit carrier identity (or a canonical main-thread mirror), the
  adapter fails closed.

A real thread bridge must retain the generated C call stack while a guest
thread is suspended. The intended carrier therefore runs a private
`PorpoisePpcState`, enters lifted code through the generated address dispatcher,
and blocks inside the self-suspend adapter. All lifted carriers must share one
serialized host execution boundary so they retain the console's single-CPU
semantics. Guest current-thread/current-context low-memory words must switch
transactionally, while CPU-global SPR, BAT, time-base, and decrementer state
must not diverge between per-thread PPC states.

Thread identity has two non-interchangeable owners. Porpoise Tool owns the
mapping from an opaque carrier to a guest `uint32_t` thread address, the guest
thread/context bytes and queues, and every guest scheduling decision.
libPorpoise owns the carrier's registered native identity, native resources,
and process-wide scheduler lock. Carrier priority and park/run state are
native mirrors used to perform a Tool-requested handoff, never a second guest
scheduler. Native code executed by a carrier must observe that registered
identity through native `OSGetCurrentThread` and through mutex, GX/VI
ownership, priority handoff, and exit-observer paths. Guest code observes only
the Tool-owned address through its carrier binding and guest low-memory word.
Because a resume may hand off synchronously before its API call returns, the
Tool adapter must preflight and commit the guest transition that the resumed
carrier will observe before requesting the native resume. A guaranteed
no-native-mutation failure rolls those staged guest bytes back; rollback
failure is terminal. Committing only after a successful resume is invalid.

Native `OSInit` does not create the guest main-thread mirror. A title-provided
lifted thread initializer must therefore precede every user initializer or
entry that may observe guest thread identity. Its resulting low-memory
current-thread address and exact guest `OSThread` must be validated before the
startup PPC state receives a canonical main-thread binding. Until that ordered
bootstrap has completed, the protected `OSGetCurrentThread` import fails
closed; the runtime does not guess a title-specific object address.

The current generated boundary implements that serialization by holding the
consumer's process-wide interrupt/scheduler lock across each complete outer
guest dispatch. Same-thread nested guest calls observe interrupts already
disabled and therefore do not reacquire the lock; the matching outer restore
releases it. The in-repo contract stub tests both nesting and deterministic
contention between two host threads.

The consumer's evolving `libPorpoise` host-thread contract must make carrier
ownership explicit. It must create a paused carrier transactionally, report a
failed start without publishing it, hand off to a newly runnable
higher-priority carrier on both first start and later wake, release the
single-CPU lock while the current carrier is blocked, join and destroy every
host resource, run the carrier callback as a registered libPorpoise native
thread identity, notify native exit observers before join completes, and
support bounded shutdown of a suspended carrier. Until that contract is
available, the adapter fails closed instead of reaching through a native
`OSThread` or SDL layout.

The complete proposed versioned surface and transition guarantees are recorded
in [LIBPORPOISE_THREAD_CARRIER.md](LIBPORPOISE_THREAD_CARRIER.md).

This containment is deliberate: when the evolving `libPorpoise` address API changes, the adapter should change without rewriting lifted sources or introducing another memory owner.

Trap and system-call callbacks are deliberately explicit. The default adapter does not invent guest exception handling or SDK behavior when the consumer has supplied no matching host service; execution faults at that boundary instead. A callback may leave execution unchanged to continue, set `PORPOISE_EXECUTION_RETURNED` for a normal terminal return, or set a concrete fault. Any other unaccompanied terminal/status transition is converted to an invalid-state fault, including across nested lifted calls. Privileged raw state transfers remain in `PorpoisePpcState`, while MMU, cache, interrupt, and device side effects are reported as approximate or unsupported.

Native-pointer tokens are version-sensitive too. The generic adapter records each distinct token it created, accepts only those tokens on decode, and releases them all during `porpoise_libporpoise_adapter_shutdown`. It permits exactly one live adapter instance, avoiding ambiguous ownership if a host interns token values. Generated entry code performs shutdown on every post-initialization exit. Dedicated adapters that need concurrent instances, a shorter lifetime, or ownership beyond one entry invocation must define it explicitly with the matching `libPorpoise` API.

## ABI boundary

Named calls outside the translated program must be declared as imports. A direct import converts declared GPR/FPR arguments to a typed C call and maps its result back to PPC state. Pointer values always pass through the host adapter. ABI shapes that cannot be represented safely, particularly varargs, require a dedicated `void adapter(PorpoisePpcState *)` implementation.

Native SDK callables that have built-in marshalling adapters are protected
names. A manifest cannot select one as a typed wrapper, use the native name as
an adapter identifier, or attach a different adapter to the exact protected
guest symbol. This prevents configuration from bypassing guest-layout
containment after the runtime has supplied a safe dedicated boundary.

An explicitly skipped input function may be replaced by an import with the same exact symbol or a coalesced duplicate function name at the same entry. Analysis records that guest-address binding so symbolic and indirect calls share the typed bridge. ABI imports that collide with ordinary `.sym` alternate entries are rejected because they are not whole-function replacements. This supports reviewed delegation of bundled SDK code to `libPorpoise` without SDK-prefix guessing or lifting a competing implementation.

Exports perform the inverse mapping and expose selected lifted functions as typed C wrappers. They use an explicitly bound PPC state rather than creating a competing runtime. See [ABI_MANIFEST.md](ABI_MANIFEST.md) for the schema and current single-state/re-entrancy constraint.

## Generated project

A successful output has this conceptual layout:

```text
generated/
|-- meson.build
|-- porpoise-report.json
|-- include/
|   |-- porpoise_lifted.h
|   |-- porpoise_libporpoise_adapter.h
|   |-- porpoise_title_host.h
|   |-- porpoise_generated.h
|   `-- porpoise_exports.h
`-- src/
    |-- generated/...                  # private per-input declarations
    |-- lifted/...                     # nested input structure preserved
    |-- data/porpoise_data_NNNN.c      # assembly-derived initialized bytes
    |-- porpoise_data_private.h        # private generated-data bootstrap
    |-- porpoise_dispatch_private.h    # private raw dispatch contract
    |-- porpoise_generated.c           # public safe-bind facade implementation
    |-- porpoise_imports_private.h     # private state-signature import bridges
    |-- porpoise_lifted.c
    |-- porpoise_libporpoise_adapter.c
    |-- porpoise_libporpoise_ai.c
    |-- porpoise_libporpoise_ar.c
    |-- porpoise_libporpoise_arena.c
    |-- porpoise_libporpoise_card.c
    |-- porpoise_libporpoise_gx.c
    |-- porpoise_libporpoise_gx_objects.c
    |-- porpoise_libporpoise_gx_values.c
    |-- porpoise_libporpoise_guest_os.c
    |-- porpoise_libporpoise_message_queue.c
    |-- porpoise_libporpoise_os_report.c
    |-- porpoise_libporpoise_builtins_private.h # private ABI adapter declarations
    |-- porpoise_libporpoise_private.h # private adapter transaction helpers
    |-- porpoise_function_registry.c       # high-address router
    |-- porpoise_function_registry_XXXX.c  # deterministic shards
    |-- porpoise_data.c
    |-- porpoise_imports.c
    |-- porpoise_exports.c
    `-- porpoise_entry.c               # only when an entry is selected
```

Meson always builds `porpoise_lifted`, a static library, and exposes it as the dependency override `porpoise-generated`. If `--entry` is valid or exactly one unskipped `.fn main, global` exists, Meson also builds `porpoise_title` from a `DolphinMain` adapter and requires the consumer-supplied `porpoise-title-host` dependency. The adapter does not invoke a translated `__start`; host startup remains a `libPorpoise` responsibility, while pre-start host paths, native DVD bootstrap policy, linked startup metadata, and the direct-entry state remain the title-host provider's responsibility.

No entry is a valid library-only result. REL/native-module targets are not generated.

Each lifted translation unit includes only its private per-input declaration,
dispatch, and import contracts. The generated static library compiles
with both `include/` and `src/`, while the `porpoise-generated` dependency
exports only `include/`. Public `porpoise_generated.h` contains the safe bind
facade and no raw address-dispatch or individual lifted-function declarations.
The public libPorpoise adapter header is limited to lifecycle, title
configuration, safe dispatcher binding, serialized guest execution, and
shutdown. Built-in ABI adapter declarations live in
`src/porpoise_libporpoise_builtins_private.h`, which generated imports and
runtime modules can include but `porpoise-generated` consumers cannot.

## Status and reporting model

The opcode registry classifies accepted instructions as:

- `lowered` — C is emitted for the validated operand form;
- `host-equivalent-no-op` — the host model intentionally makes no PPC state change;
- `approximate` — host arithmetic differs from the hardware estimate/behavior;
- `unsupported` — translation must fail.

The report records the classification per instruction, whether the registry entry has a dedicated semantic test, details for non-exact cases, approximations, data objects/fixups/spans with source provenance, diagnostics, functions, files, and summary counts. `--strict` promotes approximations to errors. Neither `lowered` nor `semantic_test: true` is a full ISA-correctness claim.

Unsupported instructions are recorded while lowering the temporary stage, but failure removes that stage before publication. Consequently, a successfully published report ordinarily has zero unsupported instructions.

## Testing and supported hosts

The repository test suite covers runtime endian/address/fault behavior, raw scalar and paired floating-point state, exact raw-bit paired multiply/fused-multiply-add vectors and exception modes, quantized paired loads/stores, system-event callbacks, deferred ARQ and GX callback ordering, GX process ownership, fixed-object endian marshalling, authoritative guest-address copy rejection, exact VI XFB selection, CLI/config and atomic-output contracts, malformed and nested inputs, strict approximations, deterministic generation, title-runtime ordering and one-shot DVD configuration, and generated static-library/executable builds against a stable `libPorpoise` contract stub.

CI targets:

- Ubuntu with GCC, including ASan/UBSan and Cppcheck;
- Windows with MSYS2 MinGW-w64 GCC.

A local Meson option enables two external compatibility gates against a
user-supplied `libPorpoise` checkout. The compile-interface checker builds
temporary strict-C99 consumer probes and reports the currently required GX
enum, canonical-byte, host-array, and versioned GX copy destination contracts
one by one. A second synthetic gate drives the same shared Build/Run core used
by the CLI and GUI, links mixed C/C++ and stable libPorpoise arena symbols,
requires a versioned guest `OK` status, verifies cache reuse, and reruns the
cached executable. Both gates leave dependency source unchanged, and neither
is present in a normal fixture-only test setup. The evolving checkout is opt-in
rather than a blocking default dependency. macOS and MSVC are not currently
supported claims.

## Evolution rules

Changes should preserve these boundaries:

- add opcode support through the registry and report its real status;
- add semantic tests before broadening correctness claims;
- keep guest addresses 32-bit and native pointers behind adapter callbacks;
- add external functions through the ABI manifest, never prefix guessing;
- keep SDK/runtime work in `libPorpoise`;
- keep output deterministic and publication atomic;
- update the adapter, not every lifted file, when `libPorpoise` host APIs evolve.
