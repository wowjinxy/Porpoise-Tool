# Symbol maps

Symbol maps are optional evidence. A recovery session is valid with no map,
an empty map, a partial map, or several module-scoped sources. Exact SDK
catalogs can identify eligible functions in a mapless session; without either
kind of evidence Porpoise simply lifts functions according to the ordinary
input, ABI, and skip-list rules.

Every loaded record retains its name, section, module, address and optional
size, kind, scope, used/unused state, library and object ownership when known,
and source-file/line provenance. Address aliases remain distinct records.

## CodeWarrior DOL/REL maps

Use a `codewarrior_map` source for the linker's section-layout format. The
loader reads linked virtual addresses and sizes from section layouts. When a
call-tree record is present, it augments the layout record with function versus
object kind and local/global/weak scope. `UNUSED` records are retained as
unused evidence without a linked address. Archive and object columns are kept
separately. The trailing `Linker generated symbols:` table is also loaded as
global, sectionless labels. This retains bootstrap evidence such as
`_stack_addr`, `_SDA_BASE_`, `_SDA2_BASE_`, `__ArenaLo`, and `__ArenaHi`
without guessing a section from the address.

A source's `module` string distinguishes the main DOL from REL or other module
address spaces. The empty string denotes the main/unnamed module. Do not merge
two modules into the empty namespace when their addresses can overlap.

Example project entry:

```json
{
  "kind": "codewarrior_map",
  "path": "maps/main.map",
  "auxiliary_path": null,
  "module": "",
  "permissive": false
}
```

`auxiliary_path` must be `null` for a CodeWarrior map.

## DTK `symbols.txt` and `splits.txt`

Use a `dtk_symbols` source for DTK's section-aware assignments:

```text
RelFunction = .text:0x00000100; // type:function size:0x20 scope:global align:4
```

The stored address is the section/module address from `symbols.txt`; it is not
silently rebased into a DOL address. The configured `module` and parsed section
therefore participate in lookup.

An optional paired `splits.txt` supplies translation-unit ownership ranges:

```text
sdk.a(rel_code.c):
    .text start:0x00000100 end:0x00000200
```

Porpoise assigns a symbol to the containing split and preserves both library
and object names. A split without an archive still supplies object ownership.
Without `splits.txt`, symbols remain useful and their ownership is unknown.
Overlapping ownership ranges are rejected.

Example:

```json
{
  "kind": "dtk_symbols",
  "path": "dtk/symbols.txt",
  "auxiliary_path": "dtk/splits.txt",
  "module": "rel:sample",
  "permissive": true
}
```

## Strict and permissive loading

With `permissive: false`, a malformed individual record is an error and the
catalog append is transactional. With `permissive: true`, malformed records
warn and are skipped. Structural conflicts such as overlapping split ownership
remain unsafe; do not use permissive mode to conceal them.

Multiple sources append in project order. Semantically compatible evidence is
preserved with its provenance. Conflicting exact records are rejected rather
than resolved by load order.

## Relationship to planning

Map evidence can provide names, categories inferred from exact archive
ownership, and useful provenance, but map-only confidence does not qualify an
SDK body for automatic import or omission. Automatic policy changes require an
eligible, unique, exact relocation-aware catalog match.

When map and exact-signature evidence disagree about identity, category, or
size, `keep` emits a warning and leaves the body lifted. `imported` and `omit`
produce a blocked plan. A stable manual override can resolve the action only
when it explicitly acknowledges the conflict; the report retains both the
conflict and override evidence.

Assembly-local symbols are a separate namespace rule. Local functions and
`.sym` aliases are scoped by translation unit, section, and address and receive
deterministic C names. Global collisions remain errors. See
[Annotated assembly input](INPUT_FORMAT.md).
