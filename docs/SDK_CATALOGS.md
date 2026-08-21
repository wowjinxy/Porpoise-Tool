# Exact SDK catalogs and policies

Porpoise can recognize known SDK functions without storing or redistributing
their bodies. A catalog stores a canonical identity, explicit category,
optional audited host contract, an exact SHA-256 signature, structural counts,
and provenance.

## What an exact signature preserves

Signature algorithm version 1 hashes the function size, instruction offsets,
fixed PPC bits, registers and constants, relocation kinds and addends, internal
control-flow targets, instruction layout, and repeated external-target
topology. It masks only fields that a validated linker relocation or external
branch is allowed to change:

| Field | Masked word bits |
| --- | ---: |
| validated `@l`, `@h`, or `@ha` | `0x0000FFFF` |
| validated `@sda21` | `0x001FFFFF` |
| external I-form branch displacement | `0x03FFFFFC` |
| external B-form branch displacement | `0x0000FFFC` |

Internal branch destinations remain part of the identity. A changed constant,
register, control-flow edge, instruction count, relocation kind, or structural
field therefore prevents an exact match.

Automatic action requires all of the following:

- exact canonical SHA-256 and exact structural metadata;
- exactly one catalog identity and, for import, one valid contract;
- no structural signature issue;
- at least eight instructions;
- at least four meaningful fully fixed instruction words; and
- an automatic category: `nintendo_dolphin` or `demo`.

Tiny, malformed, ambiguous, fuzzy, and map-only candidates remain visible in
the report but cannot change output. `crt_msl`, `runtime`, `metrotrk`,
`debugger`, and `stub` are always report-only categories.

## Catalog format and merge rules

Catalog JSON is strict schema version 1 and declares signature algorithm
version 1:

```json
{
  "schema_version": 1,
  "signature_algorithm_version": 1,
  "entries": [
    {
      "canonical_identity": "OS.a/OSReport.o/OSReport",
      "category": "nintendo_dolphin",
      "contract": "OSReport",
      "signature": {
        "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "function_size": 64,
        "instruction_count": 16,
        "fixed_instruction_count": 10,
        "meaningful_fixed_words": 8,
        "relocation_count": 3,
        "internal_branch_count": 2,
        "external_branch_count": 1,
        "external_target_count": 2,
        "issue_flags": 0
      }
    }
  ]
}
```

Sessions call the versioned built-in-catalog entry point and then load additive
local catalogs in order. The current built-in signature catalog is empty;
built-in Nintendo/Dolphin and `demo.a` knowledge is exact archive
classification, not embedded function bodies or signatures. Identical local
entries coalesce. Reusing a canonical identity with any different signature,
category, or contract is a hard configuration error. Catalog entries contain
nonrecoverable digests and metadata, never SDK code.

## Building a local catalog

`porpoise-sdk-catalog` accepts a trusted local ELF and exactly one source of
classification: a CodeWarrior map or an explicit allowlist. It invokes DTK
1.8.0 or newer as `dtk --no-color elf sigs -s SYMBOL -o TEMP ELF`, validates
DTK's output and masks against Porpoise's signature rules, and publishes JSON
atomically.

Map-backed example:

```sh
porpoise-sdk-catalog --elf path/to/sdk.elf --map path/to/sdk.map \
  --output catalogs/local-sdk.json
```

The map route classifies only exact archive ownership. Built-in archive names
cover Nintendo/Dolphin libraries, `demo.a`, CRT/MSL, Runtime, MetroTRK, and
known stubs. Add another archive by exact basename with repeatable
`--library CATEGORY=ARCHIVE`. Same-named local map functions that DTK cannot
select uniquely are skipped with a diagnostic rather than guessed.

For a mapless ELF, provide a strict allowlist:

```sh
porpoise-sdk-catalog --elf path/to/sdk.elf \
  --allowlist docs/examples/sdk-allowlist.example.json \
  --output catalogs/local-sdk.json
```

The allowlist names each ELF selector, canonical identity, category, and
optional contract explicitly. Function-name prefixes are never used for
classification. `--dry-run` prints planned DTK calls without executing them;
`--diagnostics FILE` writes machine-readable skipped-ambiguity information;
`--dtk FILE` selects the DTK executable (otherwise `PORPOISE_DTK` or `PATH` is
used).

For the local OneTri integration gate, use the same explicitly selected DTK
binary for catalog construction and managed ELF import. The DTK 1.8.3
executable-disassembly compatibility patch, exact command, and required local
paths are documented under [Local OneTri acceptance
gate](RECOVERY_WORKBENCH.md#local-onetri-acceptance-gate).

## Policies

`keep` is the default. It reports exact matches and evidence but lifts the body
normally.

`imported` changes only a high-confidence automatic-category match that has a
valid audited host contract. The guest address dispatches through that import.
An exact match without a contract remains lifted.

`omit` removes every high-confidence automatic-category body. A valid contract
is imported; otherwise the guest address resolves to one shared diagnostic
trap. Report-only categories are unaffected by either automatic policy.

Map/signature conflicts warn and keep the body under `keep`; they block
`imported` and `omit`. A stable manual `lift`, `import`, `omit`, or
`treat_as_data` override may choose the disposition only after explicitly
acknowledging the conflict. The evidence remains in schema-v3 reports.

Do not use `omit` as a size optimization until its exact matches and reachable
host contracts have been reviewed. A trapped call is an intentional diagnostic
failure, not a compatible SDK implementation.
