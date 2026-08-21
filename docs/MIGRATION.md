# Migrating from classic CLI mode

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
keep `overrides` and `annotations` empty, and set `cache` to `null`. First run:

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
