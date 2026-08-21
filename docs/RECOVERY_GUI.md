# Desktop recovery workbench

`porpoise-gui` is an optional C++17 frontend over the same C99 session, plan,
validation, generation, and publication APIs used by `porpoise`. The core
library and CLI do not depend on GUI libraries.

The desktop target uses Dear ImGui with SDL2's renderer backend and native file
dialogs. Dependency revisions are pinned by the repository wraps. Build it
explicitly:

```sh
meson setup build-gui -Dgui=enabled --wrap-mode=forcefallback
meson compile -C build-gui porpoise-gui
```

Leave `-Dgui=disabled` (the default) for a headless/core-only build.

## Recommended workflow

1. Create or open a `.porpoise.json` project.
2. Configure shared exact catalogs and ABI manifests.
3. Add targets and choose annotated assembly, managed ELF import, or prepared
   DTK assembly. Set output, entry, strictness, SDK policy, maps, and skip list.
4. Run analysis. Parsing, import, signatures, and planning execute on a worker;
   the UI remains responsive and displays phase progress and logs.
5. Filter and sort the function table. Review source/canonical name,
   translation unit, section, address, size, category, confidence, proposed
   action, binding, provenance, conflict state, and override state.
6. Open evidence/disassembly details before applying `Auto`, `Lift`,
   `Import(contract)`, `Omit`, or `Treat as Data`. Multi-selection supports
   bulk changes. Replanning reuses the loaded immutable session.
7. Resolve every blocking diagnostic, then generate. All selected targets are
   staged before the transactional publish begins.
8. Save the project explicitly. Treat an autosave as crash recovery, not as a
   committed project revision.

Other views expose target/project settings, the DTK import flow, data
annotations, diagnostics, progress, logs, schema-v3 reports, and output-folder
controls. Overwrite confirmation maps to the same operational `force` flag as
the CLI and is not persisted.

## Data editor

The editor operates on an immutable copy of bytes already present in the
program. A user may select a complete function, a named ordinary data object,
or any nonempty subrange contained by that data object. It can annotate raw
bytes, verified zero-fill, ASCII, UTF-8, Shift-JIS, UTF-16, signed/unsigned
integer arrays, `f32`/`f64` arrays, and 32-bit guest pointer tables. Endianness
and encoding are explicit where required.

Saving an annotation records both its stable normalized fingerprint and
exact-byte SHA-256. Exact function ranges use the relocation-aware function
fingerprint; data objects and their subranges use an immutable byte
fingerprint. Both identities are recomputed during validation. The editor
cannot patch an ELF, assembly file, DTK cache, or generated source. If the
source changes, a stale fingerprint or byte hash, invalid decoding, overlap,
alignment, or size/count failure blocks generation until the record is
reviewed and explicitly rebound.

## ABI editor

The workbench can inspect any loaded ABI contract and create or edit ordinary
direct-call mappings: import/export kind, symbol, callable/header, editable
result register class/index, and argument register mappings. The editor writes
an ABI manifest and can add its path to the project's shared `abi_contracts`
array. Core ABI validation still rejects register classes or indices that do
not match the selected type or the supported direct-call convention.

Specialized or stateful adapters are intentionally read-only in the generic
editor. They remain audited built-ins or separately authored manifest-backed
contracts. A direct mapping is still validated by the core before an import can
be generated.

## Workers, cancellation, and recovery

Load, import, plan, validate, and generate phases report progress through
`PorpoiseOperationCallbacks`. Cancellation is cooperative. Before publication,
it removes unpublished staging data and leaves existing outputs untouched. If
publication has started, the batch rollback journal restores the prior output
set before cancellation completes.

Project saves are explicit. The workbench maintains separate recovery autosaves
for the project and unsaved direct-ABI drafts and offers either one when newer
than the document. Untitled project and ABI-draft autosaves are discovered at
startup before the last saved project is reopened, so a crash before the first
Save As remains recoverable. Saving atomically replaces the selected local ABI
manifest (or a project-adjacent default), adds that manifest to `abi_contracts`,
and then saves the project. Window layout, recent paths, filters, recovery
drafts, and other machine-only UI state remain outside `.porpoise.json`, so
sharing a project does not share developer-specific state.

The GUI does not bypass validation. Invalid contracts, stale locators, map
conflicts without acknowledgement, overlapping annotations/ranges, an omitted
or data entry point, an invalid export, or a plan with no lifted function stays
blocked even if the UI requested generation.
