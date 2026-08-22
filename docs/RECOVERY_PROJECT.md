# Recovery project files and CLI

A `.porpoise.json` file is the durable, authoritative description of a recovery
workspace. Schema version 2 adds reviewed title-host bootstrap profiles to the
shared SDK/ABI inputs and named targets introduced by version 1. Version 1
projects remain readable and are migrated to canonical version 2 when saved.
Parsing is strict: unknown or duplicate keys, missing required keys,
wrong JSON types, malformed UTF-8/escapes, noncanonical hashes, and trailing
content are errors.

See the machine-readable [JSON Schema](porpoise-project.schema.json) and the
[redacted multi-target example](examples/recovery-workbench.porpoise.json).
The example contains no proprietary names or bytes; copy it to the intended
workspace directory and replace its illustrative relative paths.

## Root object

All four root keys are required, even when an array is empty:

| Key | Type | Meaning |
| --- | --- | --- |
| `schema_version` | integer `2` | Project format version. Version 1 is accepted only as a legacy input. |
| `sdk_catalogs` | path array | Additive exact SDK catalogs, in load order. |
| `abi_contracts` | path array | Additive ABI manifests, in load order. Exact duplicate contracts coalesce; incompatible duplicates fail. |
| `targets` | target array | Named recovery targets. IDs must be unique. |

## Target object

Every target key is required. Use `null` or an empty array where appropriate.

| Key | Type | Meaning |
| --- | --- | --- |
| `id` | nonempty string | Stable logical target identity and `--target` selector. Unsafe/path-like schema-v1 IDs receive a SHA-256-based managed-cache key; ordinary portable IDs remain readable. |
| `enabled` | boolean | Included by an unqualified project run. An explicit selector may run a disabled target. |
| `source_kind` | enum | `assembly`, `managed_elf`, or `dtk_prepared_assembly`. |
| `input` | path | Source file/tree or ELF. |
| `output` | path | Generated project destination. It must not overlap project inputs or dependencies. |
| `entry` | string or `null` | Lifted entry symbol; `null` permits a library-only result or ordinary single-main selection. |
| `strict` | boolean | Reject approximate lowering. |
| `sdk_policy` | enum | `keep` (default policy), `imported`, or `omit`. |
| `symbol_sources` | array | Optional CodeWarrior or DTK map evidence. |
| `skip_list` | path or `null` | Existing exact-symbol skip list. |
| `overrides` | array | Stable reviewed function decisions. |
| `annotations` | array | Stable read-only interpretations of existing bytes. |
| `title_host` | object or `null` | Reviewed direct-entry bootstrap profile used by Build/Run. |
| `cache` | object or `null` | Compact, non-authoritative dependency/match cache. |

A symbol source requires `kind` and `path`. Its nullable `auxiliary_path`,
`module`, and `permissive` keys are optional on input and canonical saves emit
them; their defaults are no auxiliary file, the empty module, and strict
record loading. `kind` is `codewarrior_map` or `dtk_symbols`. A CodeWarrior
source cannot have a non-null auxiliary path; for DTK it is the optional paired
`splits.txt`. The empty module denotes the main/unnamed module. See [Symbol
maps](SYMBOL_MAPS.md).

## Title-host profile

`title_host` is `null` until a direct-entry bootstrap has been reviewed. A
profile records the entry address, exactly 32 GPR values, optional arena bounds,
up to eight ordered startup-function locators, up to sixteen initial guest
words, native DVD initialization intent, the input SHA-256, and optional
canonical symbol/catalog provenance digests. Startup locators contain module, address,
size, relocation-aware fingerprint, and flags. Flag `1` requests guest-main-
thread binding after that initializer.

Build/Run validation requires aligned nonzero `r1`, nonzero `r2` and `r13`, a
current input digest, a lifted entry at the recorded address, and every startup
locator to retain its fingerprint and `Lift` action. Stale bootstrap metadata
blocks Build/Run but does not block Analyze. Generated title-host code reads the
machine-local DVD directory only from `PORPOISE_DVD_ROOT`; absolute machine
paths are never written into the project.

The inference API recognizes only the standard CodeWarrior evidence set:
`_stack_addr`, `_SDA2_BASE_`, `_SDA_BASE_`, `__ArenaLo`, `__ArenaHi`, a lifted
`__OSThreadInit`, and a lifted `__init_user`. It derives the conventional
eight-byte direct-main frame and its two sentinel words, then leaves native DVD
initialization disabled for review. Missing, ambiguous, imported, or omitted
evidence produces a precise blocker and leaves the prior profile untouched;
the tool never guesses replacement addresses.

## Paths

Relative paths are resolved lexically from the directory containing the
project file, not the process working directory. Loading records both the
spelling and resolved path and does not require references to exist yet.
Saving rebases paths relative to the new project file when both locations have
the same root/volume. Cross-volume, foreign-root, and UNC references remain
absolute.

Paths do not expand `~`, `$VARIABLE`, `${VARIABLE}`, or `%VARIABLE%` and a
Windows drive-relative spelling such as `C:folder` is invalid. Use an explicit
relative path or an absolute drive/UNC/POSIX path. Project paths are lexical;
before importing any source, the runner preflights the complete selected batch.
Every selected output and the aggregate report must be disjoint from the
project file, every target input/skip/map file, shared SDK and ABI inputs, the
managed `.porpoise-cache` tree, the runtime directory, and every other selected
output. Comparisons apply Windows case/separator rules and check both lexical
paths and resolved filesystem identities. For a missing destination, Porpoise
resolves its nearest existing ancestor first, so a symlink or junction parent
cannot hide an overlap. Validated DTK cache trees also reject absolute or
escaping member paths and case-fold collisions.

## Overrides and stale records

An override is bound to a stable locator:

```json
{
  "target": "main-dol",
  "module": "",
  "address": 2147487744,
  "size": 32,
  "fingerprint": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "action": "lift",
  "contract": null,
  "acknowledge_conflict": false
}
```

`fingerprint` is the lowercase relocation-aware SHA-256. Actions are `auto`,
`lift`, `import`, `omit`, and `treat_as_data`. `contract` must be a nonempty
name for `import` and `null` for every other action. An override beats automatic
policy, but not structural validation. A map/signature conflict additionally
requires `acknowledge_conflict: true` for a non-auto decision.

Target, module, address, size, and fingerprint prevent a decision from silently
moving to different code. An unmatched locator is stale and blocks generation.
Re-run analysis, inspect the new evidence and bytes, and explicitly create a
new locator; do not edit only the address or fingerprint to suppress the
diagnostic.

## Data annotations

Annotations reinterpret an immutable byte range; they never edit assembly or
source bytes. They contain the same target/module/address/size/fingerprint
locator plus `exact_bytes_sha256`, `interpretation`, `count`, and `encoding`.
Validation recomputes both identities: an exact code-function range uses its
relocation-aware signature, while a data range or subrange uses the immutable
byte fingerprint. A fingerprint or exact-byte mismatch makes the record stale
and blocks generation. Overlapping annotations, uncovered ranges, invalid
encodings, size/count mismatches, and unaligned multibyte values are rejected.

| Interpretation | Required encoding |
| --- | --- |
| `raw_bytes`, `zero_fill`, `s8_array`, `u8_array` | `null` |
| `ascii` | `ascii` |
| `utf8` | `utf-8` |
| `shift_jis` | `shift-jis` |
| `utf16` | `utf-16be` or `utf-16le` |
| `s16_array`, `u16_array`, `s32_array`, `u32_array`, `f32_array`, `f64_array`, `pointer32_array` | `big-endian` or `little-endian` |

`zero_fill` is accepted only when every existing byte is zero. UTF and
Shift-JIS inputs are decoded and counted. Pointer tables contain 32-bit guest
addresses; interpreting them does not convert them to native pointers.

## Compact cache

`cache` is either `null` or an object containing `input_sha256`,
`settings_sha256`, nullable `dtk_version`, `dependencies`, and exact `matches`.
Dependency entries record path, SHA-256, size, and nanosecond mtime. Match
entries record module, address, size, fingerprint, canonical identity, and an
optional contract.

The cache is an optimization, never evidence authority. It is a hit only when
the source, settings, DTK metadata, dependency set and content still agree.
An absent cache is a normal miss; changed dependencies make it stale;
conflicting exact identities are invalid. The cache-validation API also treats
a programmatically incomplete cache as a miss, while strict project JSON
rejects a partially written cache object. Rebuilding validates the session/plan
pair and retains only exact matches. Setting `cache` to `null` is safe and
forces recomputation.

## Project CLI

Run every enabled target and publish the batch transactionally:

```sh
porpoise --project recovery.porpoise.json
```

Analyze and write a schema-v3 aggregate report without publishing output:

```sh
porpoise --project recovery.porpoise.json --analyze-only \
  --report reports/recovery-plan.json
```

Select targets in explicit order (including a disabled target):

```sh
porpoise --project recovery.porpoise.json \
  --target main-dol --target overlay-rel --dtk tools/dtk --force --verbose
```

No `--target` means all enabled targets. `--dtk FILE` selects the executable
for managed ELF imports instead of `PORPOISE_DTK` or `PATH`; like `--force`,
it is operational and is never saved. Duplicate or unknown selectors and an
empty selection are errors. `--force`, `--quiet`, and `--verbose` are
operational. `--force` is never written to the project. Project mode is
mutually exclusive with positional `INPUT`, `--output`, `--config`, `--abi`,
`--skip-list`, `--map`, `--dtk-symbols`, `--dtk-splits`, `--sdk-catalog`,
`--sdk-policy`, `--module`, `--entry`, and `--strict`.

The C API equivalent is `porpoise_recovery_project_load()` followed by
`porpoise_recovery_project_run()`. A successful run retains each immutable
session and plan for inspection until `porpoise_recovery_run_result_free()`.
