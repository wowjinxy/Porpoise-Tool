# Migrating from classic CLI mode

## Recovery project schema 1 to schema 2

Recovery project schema version 2 adds the reviewed per-target `title_host`
bootstrap profile. This is separate from the classic flat config format,
which remains schema version 1.

Existing schema-version 1 `.porpoise.json` projects remain valid inputs. They
load with no title-host profile, and a canonical save upgrades the root to
`"schema_version": 2` and emits `"title_host": null` for each target. Loading
alone does not rewrite assembly, ELF, DTK output, or any other source. As with
any project-file migration, keep a backup or review the JSON diff before
sharing the upgraded file.

Schema-version 1 target IDs remain compatible: every nonempty ID, including an
older ID containing punctuation, spaces, or path separators, is preserved
verbatim as the logical selector. Simple portable IDs remain readable cache
directory names; Porpoise derives a stable SHA-256-based component for every
other ID. Loading and saving an older project therefore cannot turn its target
ID into a path traversal or require a reference-rewriting migration.

The safe migration sequence is:

1. Analyze the version 1 project and compare its schema-v3 report with the
   prior result.
2. Save it canonically as version 2. Translation generation may continue with
   `title_host: null`.
3. Analyze the intended executable target, infer the standard CodeWarrior
   profile where evidence permits, and review every register, arena bound,
   startup locator, initial word, and DVD-initialization choice.
4. Save the reviewed profile and reanalyze. Build/Run validates its input and
   relocation-aware fingerprints against the current immutable plan.
5. Configure machine-local dependencies and perform the first managed build.

The profile records portable guest facts: the resolved entry address, exactly
32 GPR values, arena bounds, ordered startup-function locators, bounded initial
guest words, native DVD initialization intent, input identity, and retained
map/catalog provenance. A changed input, stale startup locator, invalid arena,
missing `r1`/`r2`/`r13`, or startup function no longer resolved to `Lift`
blocks Build/Run. These conditions do not block Analyze, so stale state can be
reviewed without weakening validation.

Do not put a libPorpoise checkout, Meson or compiler path, DVD root, trace
destination, working directory, or other host-specific location in
`title_host`. Those values are operational machine state and are deliberately
absent from the shared project schema.

Classic mode remains supported and uses the same immutable session/plan core:

```sh
porpoise INPUT --output DIR [--config FILE] [--abi FILE] \
  [--skip-list FILE] [--map FILE] \
  [--dtk-symbols FILE [--dtk-splits FILE]] \
  [--sdk-catalog FILE] [--sdk-policy keep|imported|omit] \
  [--module NAME] [--entry SYMBOL] [--force] [--strict] \
  [--quiet | --verbose]
```

Existing invocations that do not mention maps, catalogs, or SDK policy retain
their prior behavior. The policy default is `keep`, so adding evidence alone
does not remove a body.

## Classic config additions

The existing strict flat config remains schema version 1. In addition to
`abi`, `skip_list`, `entry`, `strict`, and `verbosity`, it accepts `map`,
`dtk_symbols`, `dtk_splits`, `sdk_catalog`, `sdk_policy`, and `module`.
`dtk_splits` requires `dtk_symbols`. File paths in the config are relative to
that config file; CLI values override config values. `INPUT`, output, and
`force` remain command-line-only.

Example:

```json
{
  "schema_version": 1,
  "abi": "abi/title.json",
  "map": "maps/main.map",
  "sdk_catalog": "catalogs/local-sdk.json",
  "sdk_policy": "keep",
  "module": "main",
  "entry": "main",
  "strict": false,
  "verbosity": "normal"
}
```

## Moving one invocation into a project

Translate the settings as follows:

| Classic input | Project field |
| --- | --- |
| positional `INPUT` | `targets[].input` with `source_kind: "assembly"` |
| `--output` | `targets[].output` |
| `--abi` | root `abi_contracts[]` |
| `--sdk-catalog` | root `sdk_catalogs[]` |
| `--skip-list` | `targets[].skip_list` |
| `--map` | a `codewarrior_map` item in `targets[].symbol_sources` |
| `--dtk-symbols` / `--dtk-splits` | a `dtk_symbols` item and its `auxiliary_path` |
| `--module` | the symbol source `module` (and stable locator module) |
| `--entry` | `targets[].entry` |
| `--strict` | `targets[].strict` |
| `--sdk-policy` | `targets[].sdk_policy` |
| `--force` | remains an operational CLI/UI choice; never save it |

Start from [the redacted example](examples/recovery-workbench.porpoise.json),
keep `overrides` and `annotations` empty, and set both `title_host` and `cache`
to `null`. First run:

```sh
porpoise --project recovery.porpoise.json --analyze-only \
  --report recovery-plan.json
```

Compare the function plan and diagnostics with the classic run. Then run the
project without `--analyze-only` to publish. Projects can load several ABI
manifests and SDK catalogs additively and can add further targets without
changing the first target's settings.

Project mode is intentionally separate from classic flags; do not combine
`--project` with positional input or per-input settings. This prevents two
sources of truth. Use `--target` only to choose among targets already described
by the project.

## Adding managed Build/Run

Once a target has a current reviewed `title_host`, build it without embedding
local dependency paths in the project:

```sh
porpoise --project recovery.porpoise.json --target main-dol --build \
  --libporpoise /path/to/libPorpoise \
  --meson /path/to/meson --cc /path/to/gcc --cxx /path/to/g++ \
  --build-type debugoptimized
```

Run requires exactly one resolved target and implies Build:

```sh
porpoise --project recovery.porpoise.json --target main-dol --run \
  --libporpoise /path/to/libPorpoise \
  --meson /path/to/meson --cc /path/to/gcc --cxx /path/to/g++ \
  --dvd-root /path/to/dvd-root \
  --trace traces/first-boot.jsonl --frame-limit 300
```

The operational project options are:

| Option | Migration use |
| --- | --- |
| `--build` | Generate and build selected targets after profile validation. |
| `--run` | Build and run one target. It cannot be combined with `--analyze-only`. |
| `--libporpoise DIR` | Select a local libPorpoise checkout. |
| `--meson FILE` | Select Meson 1.2 or newer. |
| `--cc FILE`, `--cxx FILE` | Select matching x64 C/C++ compilers. A mixed-language link probe runs before the title configure. |
| `--dvd-root DIR` | Supply `PORPOISE_DVD_ROOT` when the reviewed profile enables native DVD initialization. |
| `--build-type TYPE` | Select `debugoptimized` (default), `debug`, or `release`. |
| `--trace FILE` | Write optional JSONL first-boot evidence. Relative CLI paths use the invocation directory, and Run creates missing parent directories. |
| `--frame-limit N` | Stop a test run after at least one presented frame; useful for deterministic boot gates. |

None of these paths or operational choices is written into
`.porpoise.json`. The GUI keeps reusable selections in per-project machine
state, while CLI values apply only to that invocation. Managed build caches
live beside the project under `.porpoise-build/` and can be removed without
changing the portable recovery project.

The pinned compatibility baseline intentionally has no versioned host-thread
carrier. It is therefore valid for **single-thread compatibility only**. A
normal single-thread boot may proceed, but if the title reaches a
carrier-dependent sleep, wake, suspend, resume, or exit path, execution fails
with a capability diagnostic. Do not work around this by adding a Tool-local
thread carrier or forcing an incompatible carrier API.

## Moving from hand-managed DTK output

Use `dtk_prepared_assembly` to adopt an existing clean DTK tree without
rewriting it. It must contain a root `link_order.txt` that lists every accepted
assembly file exactly once and passes safe-tree validation.

Use `managed_elf` when Porpoise should own the derived cache. The ELF remains
external and unchanged; Porpoise creates and validates a fresh stage before
publishing its cache. Do not point a target output at the ELF, project, map,
ABI/catalog, prepared input, or managed cache tree.

## Introducing SDK policies safely

Migrate in this order:

1. Run without maps/catalogs and preserve the classic output.
2. Add maps and an exact local catalog with `sdk_policy: "keep"`; review the
   schema-v3 report and all conflicts.
3. Add or audit direct host contracts.
4. Try `imported` in analyze-only mode and inspect every changed disposition.
5. Use `omit` only after accepting that unmatched-contract calls deliberately
   reach a diagnostic trap.

Never manufacture a fingerprint, acknowledge a conflict in bulk without
review, or commit proprietary OneTri/SDK inputs as fixtures. See
[Exact SDK catalogs and policies](SDK_CATALOGS.md).
