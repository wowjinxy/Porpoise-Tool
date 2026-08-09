# Architecture

Porpoise Tool is split into a build-time frontend and a generated runtime shim. The frontend translates annotated main/DOL-style title assembly; the shim lets that lifted code run inside a consumer-supplied `libPorpoise` host. The tool does not implement a second SDK, console memory system, or application host.

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

`libPorpoise` is intentionally a runtime dependency of generated projects, not of the Porpoise Tool executable. It remains unstable and consumer-supplied. The generated project requests `dependency('libPorpoise', fallback: ['libPorpoise', 'libporpoise_dep'])`; it creates no wrap, checkout, or version pin.

## Frontend pipeline

One explicit context flows through the translation pipeline; there is no cross-input register tracker or configuration auto-discovery.

1. **Options** parse one input, the required output, and optional config/ABI/skip/entry settings. An explicit config is validated before CLI overrides are applied.
2. **Program loading** recursively discovers `.s`/`.S` inputs, sorts their relative paths, parses `.fn` blocks into an in-memory IR, and rejects symbol/output-name collisions.
3. **Selection and ABI loading** mark exact skip-list symbols, validate the complete schema-version 1 ABI manifest, and bind explicitly declared imports to matching skipped guest addresses.
4. **Lowering** looks up each mnemonic in one opcode registry, validates its operand form, emits state-based C, and records a status plus semantic-test flag.
5. **Project generation** writes runtime support, lifted sources, registries, ABI bridges, Meson metadata, and the JSON report into a sibling staging directory.
6. **Publication** moves the completed stage into place. When replacing output, the old directory is temporarily backed up and restored if the new stage cannot be published.

Parsing, unsupported lowering, and strict-mode approximation failures use exit code `3`; I/O and publication failures use `4`. A failed stage is removed and never merged into the requested destination.

The implementation modules follow those responsibilities: options/configuration, program/IR, ABI validation, opcode lowering, report collection, project publication, and shared filesystem/diagnostic utilities. The old monolithic and duplicate project-generation paths are not part of this architecture.

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

State initialization zeroes the register file and attaches the host adapter. It deliberately leaves startup registers, MSR, and HID2 neutral. After host initialization, generated `DolphinMain` asks the separate, versioned `porpoise-title-host` provider for the complete initial GPR image. `porpoise_state_prepare_title_entry` then requires aligned guest `r1` (stack) plus nonzero `r2` (TOC/SDA2) and `r13` (SDA) before enabling MSR[FP], HID2[PSE], and HID2[LSQE]. Missing or invalid bootstrap state fails explicitly—Porpoise Tool does not guess it or execute a lifted `__start`. The first fault is retained, and generated code also stops when a callback marks execution returned or faulted.

Direct calls to another translated function pass the same state pointer. Function starts, address aliases, and labeled instruction entry points share a generated `uint32_t` address switch whose targets all have the same lifted signature. Indirect branches and modeled interrupt return use that registry. An address absent from it produces an unsupported-operation fault instead of calling through a cast.

The registry is emitted as a small deterministic router plus high-16-bit address shards. This keeps very large titles from forcing the C compiler to optimize one enormous switch translation unit. A skipped function that has an exact ABI-import binding appears in the appropriate shard as the corresponding `void import(PorpoisePpcState *)` bridge; a plain skipped function has no dispatch entry.

## Guest memory and pointer model

Guest addresses remain `uint32_t` throughout generated code. The lifted runtime never adds a fake 256 MiB allocation or converts an address with a native pointer cast.

`PorpoiseHostAdapter` provides four memory/pointer operations and three optional system-event operations:

```c
read_bytes(context, guest_address, destination, size)
write_bytes(context, guest_address, source, size)
decode_pointer(context, guest_address, pointer_out)
encode_pointer(context, pointer, guest_address_out)
read_time_base(context, ticks_out)
trap(context, state, instruction_address, trap_options, left, right)
system_call(context, state, instruction_address)
```

Integer and floating loads/stores assemble or disassemble big-endian byte arrays explicitly. This avoids host-endian assumptions and unaligned native dereferences. Access spans are checked for 32-bit overflow before they reach the host.

Annotated four-byte data records are emitted as a deterministic `porpoise_initialize_data` routine that uses these same store helpers. Host memory must already exist; generated code never allocates a competing console-memory buffer.

The current `libPorpoise` adapter is the only generated source that includes unstable host-address interfaces. It:

- calls `OSInit()` once before a lifted entry;
- uses `__OSHostDecodeAddress`, `__OSHostEncodeAddress`, the address-token query, and matching release operation supplied by `libPorpoise`;
- obtains monotonic target ticks through `OSGetTime` for lifted time-base and decrementer instructions;
- treats host-address tokens as opaque handles that pointer conversion may use but guest memory reads, writes, and handle arithmetic may not;
- checks that multi-byte non-token mappings are contiguous;
- rejects null, unmapped, overflowing, and currently unsupported MMIO/EFB spans with explicit host results;
- returns those failures as sticky PPC execution faults.

This containment is deliberate: when the evolving `libPorpoise` address API changes, the adapter should change without rewriting lifted sources or introducing another memory owner.

Trap and system-call callbacks are deliberately explicit. The default adapter does not invent guest exception handling or SDK behavior when the consumer has supplied no matching host service; execution faults at that boundary instead. A callback may leave execution unchanged to continue, set `PORPOISE_EXECUTION_RETURNED` for a normal terminal return, or set a concrete fault. Any other unaccompanied terminal/status transition is converted to an invalid-state fault, including across nested lifted calls. Privileged raw state transfers remain in `PorpoisePpcState`, while MMU, cache, interrupt, and device side effects are reported as approximate or unsupported.

Native-pointer tokens are version-sensitive too. The generic adapter records each distinct token it created, accepts only those tokens on decode, and releases them all during `porpoise_libporpoise_adapter_shutdown`. It permits exactly one live adapter instance, avoiding ambiguous ownership if a host interns token values. Generated entry code performs shutdown on every post-initialization exit. Dedicated adapters that need concurrent instances, a shorter lifetime, or ownership beyond one entry invocation must define it explicitly with the matching `libPorpoise` API.

## ABI boundary

Named calls outside the translated program must be declared as imports. A direct import converts declared GPR/FPR arguments to a typed C call and maps its result back to PPC state. Pointer values always pass through the host adapter. ABI shapes that cannot be represented safely, particularly varargs, require a dedicated `void adapter(PorpoisePpcState *)` implementation.

An explicitly skipped input function may be replaced by an import with the same exact symbol or a coalesced duplicate function name at the same entry. Analysis records that guest-address binding so symbolic and indirect calls share the typed bridge. ABI imports that collide with ordinary `.sym` alternate entries are rejected because they are not whole-function replacements. This supports reviewed delegation of bundled SDK code to `libPorpoise` without SDK-prefix guessing or lifting a competing implementation.

Exports perform the inverse mapping and expose selected lifted functions as typed C wrappers. They use an explicitly bound PPC state rather than creating a competing runtime. See [ABI_MANIFEST.md](ABI_MANIFEST.md) for the schema and current single-state/re-entrancy constraint.

## Generated project

A successful output has this conceptual layout:

```text
generated/
├── meson.build
├── porpoise-report.json
├── include/
│   ├── porpoise_lifted.h
│   ├── porpoise_libporpoise_adapter.h
│   ├── porpoise_title_host.h
│   ├── porpoise_dispatch.h
│   ├── porpoise_generated.h
│   ├── porpoise_data.h
│   ├── porpoise_imports.h
│   ├── porpoise_exports.h
│   └── porpoise/generated/...        # per-input declarations
└── src/
    ├── lifted/...                    # nested input structure preserved
    ├── porpoise_lifted.c
    ├── porpoise_libporpoise_adapter.c
    ├── porpoise_function_registry.c       # high-address router
    ├── porpoise_function_registry_XXXX.c  # deterministic shards
    ├── porpoise_data.c
    ├── porpoise_imports.c
    ├── porpoise_exports.c
    └── porpoise_entry.c              # only when an entry is selected
```

Meson always builds `porpoise_lifted`, a static library, and exposes it as the dependency override `porpoise-generated`. If `--entry` is valid or exactly one unskipped `.fn main, global` exists, Meson also builds `porpoise_title` from a `DolphinMain` adapter and requires the consumer-supplied `porpoise-title-host` dependency. The adapter does not invoke a translated `__start`; host startup remains a `libPorpoise` responsibility, while title-specific initial GPR values remain the title-host provider's responsibility.

No entry is a valid library-only result. REL/native-module targets are not generated.

Each lifted translation unit includes only its per-input declaration header and
the small dispatch contract. The aggregate `porpoise_generated.h` remains a
consumer convenience header; it is not included by every generated source, so
large input trees do not repeatedly parse every lifted declaration.

## Status and reporting model

The opcode registry classifies accepted instructions as:

- `lowered` — C is emitted for the validated operand form;
- `host-equivalent-no-op` — the host model intentionally makes no PPC state change;
- `approximate` — host arithmetic differs from the hardware estimate/behavior;
- `unsupported` — translation must fail.

The report records the classification per instruction, whether the registry entry has a dedicated semantic test, details for non-exact cases, approximations, annotated data words, diagnostics, functions, files, and summary counts. `--strict` promotes approximations to errors. Neither `lowered` nor `semantic_test: true` is a full ISA-correctness claim.

Unsupported instructions are recorded while lowering the temporary stage, but failure removes that stage before publication. Consequently, a successfully published report ordinarily has zero unsupported instructions.

## Testing and supported hosts

The repository test suite covers runtime endian/address/fault behavior, raw scalar and paired floating-point state, quantized paired loads/stores, system-event callbacks, CLI/config and atomic-output contracts, malformed and nested inputs, strict approximations, deterministic generation, and generated static-library/executable builds against a stable `libPorpoise` contract stub.

CI targets:

- Ubuntu with GCC, including ASan/UBSan and Cppcheck;
- Windows with MSYS2 MinGW-w64 GCC.

A local Meson option enables a non-mutating compatibility smoke test against a user-supplied `libPorpoise` checkout. That evolving checkout is opt-in rather than a blocking default dependency. macOS and MSVC are not currently supported claims.

## Evolution rules

Changes should preserve these boundaries:

- add opcode support through the registry and report its real status;
- add semantic tests before broadening correctness claims;
- keep guest addresses 32-bit and native pointers behind adapter callbacks;
- add external functions through the ABI manifest, never prefix guessing;
- keep SDK/runtime work in `libPorpoise`;
- keep output deterministic and publication atomic;
- update the adapter, not every lifted file, when `libPorpoise` host APIs evolve.
