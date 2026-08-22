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

On Windows with MinGW, configuration copies the matching GCC C++ runtime DLLs
beside `build-gui/porpoise-gui.exe`, and installation includes the same files.
This makes direct and packaged launches independent of unrelated MinGW DLLs in
the global `PATH`. Do not replace those files with runtimes from another
compiler or architecture.

## Recommended workflow

1. Open **Setup**, create or select a target, and choose annotated assembly,
   managed ELF, or prepared DTK assembly.
2. Select the input and output folder. Managed ELF targets also require a DTK
   executable. The workbench remembers that DTK selection as machine-only
   state for later launches; it is not written into the shared project file.
3. Leave the SDK policy at `keep` and **Strict** off for the first recovery
   pass. New GUI targets default to those settings. Existing projects retain
   their saved strictness.
4. Select the single highlighted **Analyze** action. The workbench saves the
   project first (opening Save As for an untitled project), then runs import,
   parsing, signatures, planning, and validation on a worker.
5. Filter and sort **Functions**. Review source/canonical name,
   translation unit, section, address, size, category, confidence, proposed
   action, binding, provenance, conflict state, and override state.
6. Open evidence/disassembly details before applying `Auto`, `Lift`,
   `Import(contract)`, `Omit`, or `Treat as Data`. Multi-selection supports
   bulk changes. Replanning reuses the loaded immutable session.
7. Resolve every blocking diagnostic. The same primary action advances to
   **Generate**; all selected targets are staged before transactional publish.
8. Review the compact title-host summary, then select **Configure Runtime**.
   Choose a local libPorpoise checkout, Meson 1.2+, a matching x64 C/C++
   compiler pair, and (when requested by the profile) the extracted DVD root.
   The wizard identifies whether that checkout exposes observable GPU-resident
   host-XFB support and queued canonical FIFO v2. Canonical FIFO v1 remains
   compatible, but the wizard warns that its synchronous ingress can be slow
   for lifted code which performs many write-gather stores.
   These absolute paths remain in machine state keyed by the canonical project
   path and are never saved in the shared project.
   Exact register, arena, startup-locator, fingerprint, and initial-memory
   values remain available under **Advanced > Title Host**.
9. The primary action advances to **Build** after configuration passes, then
   to **Run** when the executable and its non-system DLLs are staged. A first
   run writes a JSONL boot trace and stops after three presented frames by
   default, keeping first-boot evidence bounded. The trace path and frame limit
   are available in Runtime configuration. Disable tracing for ordinary play;
   choosing an unlimited traced run is explicit and carries a disk-usage
   warning.
10. Use the **Advanced** tabs only when needed for project details, maps and
   catalogs, data annotations, ABI contracts, reports, or output options.

An unsuccessful operation displays the first error and a corrective hint
inline. Use **Back to Setup** to correct input, DTK, output, strictness, or the
specific highlighted runtime field, or **Show diagnostics** for the complete
error list and live process log. The workbench never changes Strict mode or
another recovery policy silently.

Overwrite confirmation maps to the same operational `force` flag as the CLI
and is not persisted.

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

Build and Run use the shared shell-free process service and the same Cancel
control. The Setup page shows only the active stage; complete Meson/compiler
stdout and stderr remain in Diagnostics. Per-target caches live under
`.porpoise-build` beside the project. Setup can measure or reveal that cache
and removes it only after an explicit confirmation. Dependency-copy fallback
is also opt-in because it can consume substantial disk space.

On POSIX hosts the service launches with `posix_spawnp`: argument arrays,
environment overrides, working-directory changes, pipe wiring, and a dedicated
process group are prepared without running non-async-signal-safe code in a
post-fork GUI child. Cancel first terminates that group, then kills it after a
250 ms grace period. Live log callbacks still receive every byte. The core's
convenience capture is a bounded tail ring: it retains the newest 1 MiB of
stdout and 1 MiB of stderr and marks either stream when older bytes were
dropped, so unattended output cannot grow capture memory without bound.

Analyze and Generate save the current project before starting, so the document
on disk describes the run being performed. The workbench also maintains
separate crash-recovery autosaves for the project and unsaved direct-ABI drafts
and offers either one when newer than the document. Untitled project and
ABI-draft autosaves are discovered at startup before the last saved project is
reopened, so a crash before the first Save As remains recoverable. Saving
atomically replaces the selected local ABI manifest (or a project-adjacent
default), adds that manifest to `abi_contracts`, and then saves the project.
Window layout, the selected DTK executable, recent paths, filters, recovery
drafts, and other machine-only UI state remain outside `.porpoise.json`, so
sharing a project does not share developer-specific state.

The GUI does not bypass validation. Invalid contracts, stale locators, map
conflicts without acknowledgement, overlapping annotations/ranges, an omitted
or data entry point, an invalid export, or a plan with no lifted function stays
blocked even if the UI requested generation.
