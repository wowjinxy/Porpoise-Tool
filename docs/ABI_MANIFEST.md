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
      "symbol": "OSGetArenaLo",
      "header": "dolphin/os.h",
      "return": { "type": "pointer", "register": "r3" },
      "arguments": []
    },
    {
      "kind": "import",
      "symbol": "OSReport",
      "header": "porpoise/os_report_adapter.h",
      "adapter": "PorpoiseOSReportAdapter",
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
  "symbol": "OSSetArenaLo",
  "wrapper": "OSSetArenaLo",
  "header": "dolphin/os.h",
  "return": { "type": "void" },
  "arguments": [
    { "name": "arena_lo", "type": "pointer", "register": "r3" }
  ]
}
```

Direct imports require `kind`, `symbol`, `header`, `return`, and `arguments`.

- `symbol` is the exact assembly branch target. It may contain non-C punctuation but cannot contain whitespace or control characters.
- `wrapper` is the callable C99 identifier. It defaults to `symbol` when `symbol` is already a non-keyword C99 identifier; otherwise it is required.
- `header` is emitted as `#include <header>` and must be a nonempty string without quotes, angle brackets, or control characters.
- `return` and `arguments` declare the PPC register mapping described below.

When lifted code branches to the symbol, the generated bridge reads arguments from `PorpoisePpcState`, calls `wrapper`, and writes the result back to the declared return register. A named target that is neither a translated function nor a declared import fails translation; no signature is guessed.

## Dedicated import adapters

Functions whose ABI cannot be represented as a fixed typed call—especially varargs such as `OSReport`—must use an adapter:

```json
{
  "kind": "import",
  "symbol": "OSReport",
  "adapter": "PorpoiseOSReportAdapter",
  "header": "porpoise/os_report_adapter.h",
  "return": { "type": "void" },
  "arguments": [
    { "name": "format", "type": "pointer", "register": "r3" }
  ]
}
```

An adapter import requires `kind`, `symbol`, `adapter`, `header`, `return`, and `arguments`. It must omit `wrapper`; there is no `variadic` shortcut and Porpoise Tool will not infer additional arguments from a format string. The declared mappings document and validate the guest-facing portion of the call, while the adapter remains responsible for any ABI shape that cannot be expressed as a normal typed call.

The adapter has this contract:

```c
void PorpoiseOSReportAdapter(PorpoisePpcState *state);
```

Generated imports include `header`, which must declare the adapter contract. The implementation must be supplied by `libPorpoise` or another dependency in the consuming project. It reads and writes PPC state directly and should set a state fault when it cannot marshal an argument safely.

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

Generated exports operate on the state most recently passed to `porpoise_bind_export_state`. The generated `DolphinMain` binds its local state before entering lifted code and unbinds it afterward. A wrapper called with no bound state, or after that state has faulted, returns without executing; scalar returns are zero and pointer returns are `NULL`. The binding is a single process-global pointer, so concurrent or re-entrant host use requires additional integration work.

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

## Validation and failure

Unreadable manifest files use exit code `4`. Invalid JSON, unknown/duplicate keys, invalid types or registers, conflicting symbols, and missing required fields use exit code `2`. A well-formed export that does not match translated input is a translation error (`3`), as is an assembly call to an undeclared import.

Manifest validation happens before output publication. A failure therefore leaves an existing destination untouched.
