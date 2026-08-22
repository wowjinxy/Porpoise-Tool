# Recovery workbench architecture

Porpoise uses one recovery pipeline for the command-line tools, the in-process
C API, and the optional desktop workbench:

```text
annotated assembly or DTK import
        -> immutable PorpoiseSession
        -> map and exact-signature evidence
        -> immutable PorpoiseTranslationPlan
        -> validation
        -> staged generation
        -> transactional publication
```

The project file is authoritative configuration. Assembly, ELF files, maps,
catalogs, ABI manifests, and skip lists are inputs. Porpoise reads and hashes
them but never edits them.

## Shared session and plan model

`porpoise_session_open()` parses and validates the assembly tree, skip list,
one or more ABI manifests, zero or more symbol sources, and zero or more SDK
catalogs. A successful `PorpoiseSession` owns an immutable program, ABI,
symbol catalog, and SDK catalog. It stays alive until every plan that borrows
from it has been released.

`porpoise_plan_build()` adds target settings and creates a reviewed function
disposition snapshot. Each read-only function view records the source and
canonical names, translation unit, section, address and size, relocation-aware
signature, map symbol, SDK category and identity, confidence, requested and
resolved actions, binding, origin, evidence flags, override, and any blocking
reason. `porpoise_plan_validate()` rejects structurally incoherent plans. A
plan digest binds these decisions to its session and settings.

The actions are:

- `lift`: translate the function body;
- `import`: dispatch its guest address through a validated ABI contract;
- `omit`: remove the body and, for an explicit or SDK-policy omission, retain
  a shared diagnostic trap at the guest address;
- `data`: preserve the function's exact annotated words as immutable guest
  bytes instead of executable code.

At least one function must remain lifted. An entry point and every ABI export
must resolve to a lifted function. Treat-as-data additionally requires a
complete contiguous byte range that does not overlap existing data.

The session/plan split lets the GUI replan after an override without reparsing
the assembly. The classic CLI is a compatibility wrapper over the same API.

The minimal in-process sequence is:

```c
porpoise_session_open(&session_options, &session, &diagnostics);
porpoise_plan_build(session, &plan_options, &plan, &diagnostics);
porpoise_plan_validate(plan, &diagnostics);
porpoise_project_generate_plan(plan, &project_options, &report, &diagnostics);
```

For multi-target atomic publication, call `porpoise_project_stage_plan()` for
each validated plan and publish the complete array with
`porpoise_project_publish_batch()`. The project-level wrapper
`porpoise_recovery_project_run()` performs that sequence and retains its
sessions/plans for read-only inspection.

## Input stages

A target selects one source kind:

- `assembly`: an existing annotated `.s`/`.S` file or recursive directory;
- `managed_elf`: a trusted local ELF imported by DTK into a managed cache;
- `dtk_prepared_assembly`: an existing DTK tree containing root
  `link_order.txt` and its listed assembly files.

Managed import requires DTK 1.8.0 or newer. Porpoise runs shell-free argument
vectors equivalent to `dtk --version`, `dtk --no-color elf info ELF`, and
`dtk --no-color elf disasm ELF FRESH_STAGE`. It hashes the ELF, DTK binary,
settings, and generated tree; rejects colored or malformed output; validates
every listed path and assembly file; and only then atomically publishes
`.porpoise-cache/<sanitized-target-id>-<hash>/dtk`. A dirty DTK output
directory is never reused as a stage. A cache hit is accepted only after its
dependency metadata and generated content are revalidated.

DTK's canonical `link_order.txt` entries are object paths: `path/to/foo.o`
maps exactly to generated `asm/path/to/foo.s`. Prepared trees may instead list
`.s` or `.S` assembly paths directly, which are used as written. Every entry
must name exactly one generated assembly file, and every assembly file must be
listed. Unsafe paths, duplicates, omissions, extra assembly files, and
case-fold collisions are rejected.

DTK 1.8.3 at commit `e4219e7` has an upstream executable-ELF regression in
which `elf disasm` treats inferred open-ended splits as address zero. Porpoise
keeps a narrowly scoped
[source patch](../tools/patches/dtk-1.8.3-elf-disasm-open-ended-splits.patch).
Apply it only to
that matching DTK source revision, build DTK, and select the resulting binary
with project-mode `--dtk FILE`. The compatibility patch resolves the sentinel
at the next section boundary, preserves last-owner semantics for duplicate
inferred starts, and address-qualifies local symbols referenced from another
translation unit. It does not modify or convert the input ELF.

One reproducible patched-binary workflow is:

```sh
git clone https://github.com/encounter/decomp-toolkit.git
cd decomp-toolkit
git checkout e4219e7
git apply /path/to/Porpoise-Tool/tools/patches/dtk-1.8.3-elf-disasm-open-ended-splits.patch
rustup run 1.90.0 cargo build --release
```

Use `target/release/dtk` (or `target/release/dtk.exe` on Windows) with
`porpoise --project recovery.porpoise.json --dtk FILE`. Keep the patched binary
separate from the stock DTK executable so its provenance remains explicit.

Prepared assembly is validated in place and is never rewritten. See
[Annotated assembly input](INPUT_FORMAT.md) for the accepted dialect.

## Validation and publication

Before source preparation, one batch preflight rejects any selected output or
aggregate report that overlaps the project file, any target input or evidence
file, shared SDK/ABI inputs, the managed cache tree, the runtime directory, or
another selected output. The comparison combines Windows-aware lexical paths
with resolved filesystem identities, including missing children beneath an
existing symlink or junction. This protection is unchanged by `--force`.

All selected targets finish import, session construction, planning, and
validation before generation starts. Generation then creates a separate
sibling stage for every target. Publication starts only after every stage
succeeds.

Batch publication keeps a rollback journal and moves previous destinations to
recoverable siblings before installing new stages. If any publication step
fails or cancellation is observed, Porpoise restores the previous outputs as
a unit. Any failed or cancelled run frees and removes every unpublished stage
before returning while retaining its immutable plans and measured reports for
diagnostic inspection. Existing published destinations remain untouched.
`--force` only authorizes a preflight-approved output replacement; it cannot
overwrite a source, dependency, project, cache, runtime, report, or another
selected output, does not weaken validation, and is never saved in a project.

Generated per-target and aggregate reports use schema version 3. In addition
to the existing file, function, instruction, data, and diagnostic fields, the
recovery report records target identity, canonical SDK identity, requested and
resolved action, binding, provenance, evidence, confidence, override state,
and blocking diagnostics.

## Safety boundaries and current limitations

- Inputs are trusted local files in the first release, but accepted DTK cache
  trees still reject absolute paths, `..` escapes, symlinks/reparse points,
  duplicate paths, and Windows case-fold collisions.
- Map and SDK data are evidence, not permission to guess. Maps may be absent or
  partial, and fuzzy or ambiguous matches never change generated output.
- Ordinary direct-call ABI mappings are manifest data. Stateful adapters stay
  audited built-ins or manifest-backed specialized adapters.
- Porpoise does not convert an ELF through DOL, mutate an assembly tree, embed
  proprietary SDK bodies, or infer classifications from symbol prefixes.
- Generated runtime and adapter internals remain a separate concern from the
  recovery frontend.
- The current generated title still requires at least one lifted function and
  the existing `libPorpoise` host contracts described in
  [Architecture](ARCHITECTURE.md).

## Local OneTri acceptance gate

OneTri is a local integration input, not a redistributable fixture. Keep its
ELF, map, generated assembly, reports, and catalogs outside version control.
For the known local ELF/map pair, the acceptance result is:

- 6 title functions;
- 521 Nintendo/Dolphin or `demo.a` SDK functions;
- 196 CRT/MSL, runtime, MetroTRK, debugger, or stub functions.

The exact local catalog currently yields 533 exact matches. Under `imported`,
52 audited host-bound functions import and 671 functions lift. Under `omit`,
those same 52 functions import, 336 unbound SDK bodies use the shared trap,
and 335 functions lift; seven exact SDK bodies remain lifted because relocation-
backed interior entry points make replacing them structurally unsafe. Mapless
analysis retains the same 533 exact identities;
its category evidence covers 480 automatic SDK, 165 report-only, and 78
uncategorized/title functions because non-exact catalog evidence can still
contribute category provenance.

The local gate exercises `keep`, `imported`, and `omit`, then repeats without a
map using an exact local catalog. Build and Run use the same shared core as the
CLI and GUI, directly from the reviewed schema-v2 project. The selected OneTri
target must explicitly use `imported` and contain its reviewed title-host
profile. The gate rejects faults, unreviewed reached approximations, missing or
out-of-order boot milestones, zero framebuffers, and incomplete frame runs. A
count mismatch is evidence to investigate, not a reason to loosen signatures
or classify by name.

Run the complete gate with a reusable work root outside both repositories:

```sh
python tools/run_onetri_acceptance.py \
  --porpoise /path/to/porpoise \
  --dtk /path/to/patched/dtk \
  --project /path/to/reviewed-recovery.porpoise.json \
  --work /path/to/onetri-acceptance \
  --libporpoise /path/to/libPorpoise \
  --dvd-root /path/to/extracted-disc \
  --meson /path/to/meson \
  --cc /path/to/x64-gcc \
  --cxx /path/to/x64-g++ \
  --trace-frame-limit 3 \
  --frame-limit 300
```

The project is authoritative for the ELF, CodeWarrior map, exact catalog,
generated output, SDK policy, and reviewed host profile. Legacy `--elf`,
`--map`, and `--catalog` options remain accepted as path assertions, but cannot
override it. `--build-catalog` still requires `--catalog-tool` and writes only
to the catalog path already named by the project. `--cc` and `--cxx` are an
optional pair; the shared preflight validates their target, family, version,
runtime, and mixed C/C++ link compatibility. `--build-type` defaults to
`debugoptimized`. `--trace` may select an external JSONL path; otherwise the
unique `onetri-run-*` child owns it. `--trace-frame-limit` defaults to three:
that bounded run verifies ordered milestones, nonempty framebuffers, faults,
and reached approximations without producing a multi-gigabyte trace.
An explicit trace destination must be disjoint from the work root, reviewed
project root, both repositories, and every OneTri input/output path; the gate
checks both containment directions before invoking Build/Run. The work root is
likewise rejected if it contains, or is contained by, any external material.

Each invocation creates and reports a unique evidence directory, while the
managed build cache beside the project remains reusable. The Porpoise
repository, `libPorpoise` checkout, work root, reviewed project, proprietary
inputs, DVD root, and derived artifacts must remain disjoint as documented by
the tool. The final shared `--run` performs generation, dependency binding,
configure, compile, DLL/runtime staging, and execution of the `imported`
target. It first performs the bounded strict traced run, then reuses the managed
build for the untraced sustained run selected by `--frame-limit` (300 by
default). The traced run must reach `PADRead` after `PrintIntro`, proving that
startup entered the controller loop. The second process must complete its frame
limit without a runtime fault. The gate sets the test-only
`PORPOISE_REJECT_APPROXIMATIONS=1` process
environment for both runs, so a dynamically reached non-exact site faults even
without JSONL and the sustained run independently proves that no later
unreviewed approximation was reached. Normal product runs leave this behavior
disabled. All OneTri material remains outside version control.

Related documents:

- [Symbol maps](SYMBOL_MAPS.md)
- [Exact SDK catalogs and policies](SDK_CATALOGS.md)
- [Recovery project files and CLI](RECOVERY_PROJECT.md)
- [Desktop workbench](RECOVERY_GUI.md)
- [Classic CLI migration](MIGRATION.md)
