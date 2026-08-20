# ABI manifest

The ABI manifest is the only mechanism Porpoise Tool uses to bridge named external C functions and typed host-facing exports. It replaces SDK-prefix guessing and catch-all register casts with explicit, reviewable mappings.

Pass a manifest with `--abi FILE`, or set `"abi"` in an explicit config. A CLI path follows the invoking environment; a config path is resolved relative to that config file.

## Complete example

```json
{
  "schema_version": 1,
  "functions": [
    {
      "kind": "import",
      "symbol": "TitleHostAdd",
      "wrapper": "TitleHostAdd",
      "header": "title_host_api.h",
      "return": { "type": "u32", "register": "r3" },
      "arguments": [
        { "name": "left", "type": "u32", "register": "r3" },
        { "name": "right", "type": "u32", "register": "r4" }
      ]
    },
    {
      "kind": "import",
      "symbol": "OSReport",
      "header": "porpoise_libporpoise_builtins_private.h",
      "adapter": "porpoise_libporpoise_os_report_adapter",
      "return": { "type": "void" },
      "arguments": [
        { "name": "format", "type": "pointer", "register": "r3" }
      ]
    },
    {
      "kind": "export",
      "symbol": "add_one",
      "wrapper": "PorpoiseAddOne",
      "header": "title_exports.h",
      "return": { "type": "u32", "register": "r3" },
      "arguments": [
        { "name": "value", "type": "u32", "register": "r3" }
      ]
    }
  ]
}
```

The root must be one JSON object with exactly these keys:

| Key | Required | Value |
| --- | --- | --- |
| `schema_version` | yes | Numeric `1`. |
| `functions` | yes | Array of function objects; it may be empty. |

Unknown or duplicate keys are errors at every schema level. JSON nesting is limited to 32 levels. Function symbols must be unique across the entire manifest.

## Direct imports

A direct import maps a branch target in lifted assembly to a normal typed C call:

```json
{
  "kind": "import",
  "symbol": "TitleHostSetMode",
  "wrapper": "TitleHostSetMode",
  "header": "title_host_api.h",
  "return": { "type": "u32", "register": "r3" },
  "arguments": [
    { "name": "mode", "type": "u32", "register": "r3" }
  ]
}
```

Direct imports require `kind`, `symbol`, `header`, `return`, and `arguments`.

- `symbol` is the exact assembly branch target. It may contain non-C punctuation but cannot contain whitespace or control characters.
- `wrapper` is the callable C99 identifier. It defaults to `symbol` when `symbol` is already a non-keyword C99 identifier; otherwise it is required.
- `header` is emitted as `#include <header>` and must be a nonempty string without quotes, angle brackets, or control characters.
- `return` and `arguments` declare the PPC register mapping described below.

When lifted code branches to the symbol, the generated bridge reads arguments from `PorpoisePpcState`, calls `wrapper`, and writes the result back to the declared return register. A named target that is neither a translated function nor a declared import fails translation; no signature is guessed.

An import may deliberately name a function that is also present in the assembly input when that exact function (or a coalesced duplicate function name at the same entry) is selected by `--skip-list`. In that case Porpoise Tool does not emit the lifted body. It binds the skipped guest entry address to the typed import bridge instead, so direct symbolic calls, raw-address calls, and `bctr`/`bctrl` dispatch all reach the same host implementation. The function is reported as `imported`. An import that collides with an unskipped input function, two imports bound to the same guest address, or a guessed prefix-wide SDK replacement is an error. An import that names an ordinary `.sym` alternate-entry alias is rejected rather than inconsistently replacing only one entry into its owning function.

This is the intended way to delegate annotated SDK bodies to `libPorpoise`: enumerate reviewed symbols in both the ABI manifest and exact skip list. Porpoise Tool never derives a host signature from the assembly or from a symbol prefix.

## Dedicated import adapters

Functions whose ABI cannot be represented as a fixed typed call—especially varargs such as `OSReport`—must use an adapter:

```json
{
  "kind": "import",
  "symbol": "OSReport",
  "adapter": "porpoise_libporpoise_os_report_adapter",
  "header": "porpoise_libporpoise_builtins_private.h",
  "return": { "type": "void" },
  "arguments": [
    { "name": "format", "type": "pointer", "register": "r3" }
  ]
}
```

An adapter import requires `kind`, `symbol`, `adapter`, `header`, `return`, and `arguments`. It must omit `wrapper`; there is no `variadic` shortcut and Porpoise Tool will not infer additional arguments from a format string. The declared mappings document and validate the guest-facing portion of the call, while the adapter remains responsible for any ABI shape that cannot be expressed as a normal typed call.

The adapter has this contract:

```c
void porpoise_libporpoise_os_report_adapter(PorpoisePpcState *state);
```

Generated imports include `header`, which must declare the adapter contract.
Porpoise Tool supplies the canonical `OSReport` adapter shown above in the
generated private header `src/porpoise_libporpoise_builtins_private.h` and
protects the exact `OSReport` symbol from typed wrappers or replacement
adapters. Built-in manifests must name that private header; the public
`porpoise_libporpoise_adapter.h` is rejected. Other dedicated adapters must be
supplied by `libPorpoise` or another dependency in the consuming project.

The built-in adapter reads the format address from `r3`, integer and pointer
arguments from `r4` through `r10`, floating arguments from `f1` through `f8`,
and overflow arguments from the caller's big-endian stack area at `r1 + 8`.
GPR and FPR cursors are independent; 64-bit integer and stack values follow
the PPC EABI alignment rules. It bounds format, string, field, conversion, and
output sizes and passes only `OSReport("%s", sanitized_output)` to the native
library. Positional arguments, `%n`, wide strings or characters, long double,
NUL-producing `%c`, and unsupported modifiers or specifiers fault explicitly.

## Exports

An export exposes a translated function through a normal typed C wrapper:

```json
{
  "kind": "export",
  "symbol": "title_update",
  "wrapper": "PorpoiseTitleUpdate",
  "header": "title_exports.h",
  "return": { "type": "void" },
  "arguments": [
    { "name": "delta", "type": "f32", "register": "f1" }
  ]
}
```

Exports require `kind`, `symbol`, `wrapper`, `header`, `return`, and `arguments`. `symbol` must identify a translated, unskipped input function. `wrapper` must be a non-keyword C99 identifier. Adapters are forbidden for exports. The generated export source includes `header`, allowing the compiler to check the public declaration against the generated wrapper.

Generated exports operate on the state most recently passed to `porpoise_bind_export_state` on the calling host thread. The generated `DolphinMain` binds its local state before entering lifted code and unbinds it afterward. A wrapper called with no thread-local binding, or after that state has faulted, returns without executing; scalar returns are zero and pointer returns are `NULL`. GCC-compatible thread-local storage keeps bindings on different host threads independent. Re-entrant calls on one host thread deliberately share that thread's current binding and must still enter guest code through the serialized generated dispatch boundary.

## Value objects

Return values and arguments are objects with these keys:

| Key | Required | Rules |
| --- | --- | --- |
| `type` | yes | One of the types below. |
| `register` | yes except `void` return | `r3`–`r10`, `f1`–`f8`, or `none` only for `void`. |
| `name` | optional | Non-keyword C99 identifier. Arguments default to `argument0`, `argument1`, and so on. A return name has no generated effect. |

Supported types and generated C types:

| Manifest type | Generated C type | Register class |
| --- | --- | --- |
| `void` | `void` | return only; omit `register` or use `none` |
| `u8` | `uint8_t` | GPR |
| `u16` | `uint16_t` | GPR |
| `u32` | `uint32_t` | GPR |
| `s8` | `int8_t` | GPR |
| `s16` | `int16_t` | GPR |
| `s32` | `int32_t` | GPR |
| `f32` | `float` | FPR |
| `f64` | `double` | FPR |
| `pointer` | `void *` | GPR plus host pointer conversion |

Mapping constraints:

- integer and pointer returns must use `r3`;
- floating returns must use `f1`;
- integer and pointer arguments may use `r3` through `r10`;
- floating arguments may use `f1` through `f8`;
- within each register class, arguments must be declared in strictly increasing register order;
- registers and argument names cannot be duplicated;
- argument names `state` and `result` are reserved by generated bridges;
- `void` is valid only as a return, has no name, and cannot map to a GPR or FPR.

The schema permits skipped register numbers; it does not synthesize ABI stack arguments.

## Pointer policy

Guest pointers remain 32-bit values in GPRs. Direct import arguments call `porpoise_decode_pointer`; direct import results and export arguments call `porpoise_encode_pointer`. Those operations delegate to the active `PorpoiseHostAdapter` and set a PPC state fault on invalid, unmapped, overflowing, or unsupported MMIO addresses.

Guest pointer value zero maps to native `NULL`, and native `NULL` maps back to zero without a fault.

Some evolving `libPorpoise` versions encode non-console native pointers as finite address tokens. The generated generic adapter treats those values as opaque handles: they may round-trip through pointer conversion, but memory helpers and handle arithmetic reject them. It owns every token it creates until `porpoise_libporpoise_adapter_shutdown`, which generated entry code calls before returning. Integrations that need shorter-lived handles or ownership beyond one entry invocation must express that policy in a dedicated adapter using that `libPorpoise` version's API.

Never model a pointer by casting a `uint32_t` directly to a native pointer, and do not use an integer type in the manifest to bypass pointer conversion. On a 64-bit host that loses both safety and the `libPorpoise` address-token policy.

## Protected native callables

Some SDK functions need more than ordinary pointer decoding because their
pointees have 32-bit, big-endian guest layouts that differ from native host
structures or calling conventions. Porpoise Tool ships dedicated adapters for
the characterized AI initialization, arena, ARQ, CARD probe, DSP task, DVD
file, GX initialization/data/value/frame-buffer, message queue, thread,
OSReport varargs, and VI render-mode boundaries.
The `AIInit` adapter accepts only a null guest callback-stack pointer and calls
native `AIInit(NULL)` without pointer decoding, tokenization, or casts. A
non-null stack faults before native initialization. The current host AI model
is a control-surface approximation and this boundary does not claim audio
output. The arena adapters
mirror configured `uint32_t` guest bounds independently from native pointers.
Getters and successful allocations return only guest addresses; setters and
allocations validate the configured root, crossing, power-of-two alignment,
and 32-bit arithmetic before committing matching native `OSSetArenaLo/Hi`
bounds. The low allocator preserves the guest's two upward-alignment steps,
while the high allocator aligns the old high bound down before subtraction and
aligns the result down again. Exhaustion returns guest `NULL` without mutation;
invalid alignment, arithmetic overflow, native divergence, or an unconfigured
guest mirror faults explicitly. The canonical arena adapters may serve a
reviewed guest alias, including a title's unnamed high-allocation body.

The protected AR allocator family is `ARInit`, `ARAlloc`, `ARFree`,
`ARReset`, and `ARGetSize`. Its adapter owns both the native host-endian table
passed to `libPorpoise` and an independent expected mirror. The guest table is
always a 32-bit address, is validated and write-preflighted across its complete
checked `num_entries * 4` span, and receives only big-endian length words.
Allocation and free remain LIFO operations: capacity/space exhaustion returns
zero. Invalid alignment and inaccessible output memory fault before native
mutation. Table/native divergence or a guest-word rollback failure poisons the
family until the exact owner resets it. A manifest may use the `ARGetSize`
adapter for a reviewed guest alias without importing a stale lifted AR-size
global.

Native AR ownership is exclusive while this boundary is active. The protected
names prevent generated imports from bypassing it. Current `libPorpoise` does
not expose a nonmutating allocator-position or generation query: a direct
out-of-contract native `ARFree` can therefore remain observationally invisible
until the next adapter alloc/free. That operation validates the native result,
rolls back its speculative guest word, and poisons on mismatch. A future native
API can provide immediate detection only through an explicit versioned
snapshot contract.

The protected GX frame-buffer contracts are `GXInit`,
`GXSetDrawDoneCallback`, `GXSetCopyFilter`, `GXSetCopyClear`,
`GXSetDispCopyDst`, `GXSetTexCopyDst`, `GXCopyDisp`, `GXCopyTex`, and
`GXLoadLightObjImm`. They keep native process-global GX ownership, guest
callback dispatch, nullable fixed arrays, exact big-endian object layouts, and
preliminary copy-destination validation inside the dedicated adapter. In particular, a
manifest cannot turn a guest copy destination into a generic decoded pointer:
the copy adapter requires recorded geometry, rejects MMIO/tokens/truncation,
and enters native copy code only when the consumer advertises the matching
versioned guest-address contract. That endpoint is responsible for deriving
the actual span from native GX state and synchronously materializing canonical
guest-memory output; a host-pointer SDK call is not an accepted fallback.
`VISetNextFrameBuffer` is protected for the same reason and requires its
versioned exact-guest-address endpoint; it updates VI selection independently
from GX copy destinations.
`GXSetCopyClear` and channel-color calls
also retain their characterized hidden guest pointer mappings rather than
pretending the PPC aggregate-by-value ABI is a normal host C argument.

All canonical built-in adapter declarations remain private to generated
runtime and import sources. The `porpoise-generated` Meson dependency exports
only lifecycle, configuration, safe binding, serialized execution, and
shutdown APIs; consumers cannot include or invoke built-in ABI adapters
directly through that dependency.

The `CARDProbeEx` adapter
reads signed channel `r3` and nullable guest `s32` output addresses from `r4`
and `r5`. It validates and preloads both complete aligned big-endian spans
before entering native CARD/EXI code, then preflights writes by restoring the
same bytes. This preserves existing values when native code does not write
them, and the signed result is stored in `r3`.
The built-in adapter contract table records both each native callable and its
required register mapping.

`OSGetCurrentThread` is a protected zero-argument import with a guest-pointer
result in `r3`. Its dedicated adapter returns the Tool-owned guest `OSThread`
address bound to the current carrier. It must not use a typed wrapper around
native `OSGetCurrentThread`, because the native return is a host identity and
is neither encodable nor layout-compatible with a guest pointer. An unbound
carrier/main-thread identity faults rather than fabricating a return value.

For those protected names, manifest validation rejects all of the following:

- using the native callable as a typed `wrapper`, including the implicit
  wrapper obtained when `wrapper` is omitted;
- using the native callable itself as an `adapter` identifier;
- attaching a custom or different built-in adapter to the exact protected
  guest symbol.

The canonical built-in adapter may still serve an arbitrary guest alias. A
custom dedicated adapter remains valid for a non-protected symbol, including
an integration-specific alias, because it receives `PorpoisePpcState *` and
owns its complete marshalling policy. These rules prevent a manifest from
silently turning a validated guest structure back into a native structure
cast.

## Validation and failure

Unreadable manifest files use exit code `4`. Invalid JSON, unknown/duplicate keys, invalid types or registers, conflicting symbols, and missing required fields use exit code `2`. A well-formed export that does not match translated input is a translation error (`3`), as is an assembly call to an undeclared import.

Manifest validation happens before output publication. A failure therefore leaves an existing destination untouched.
